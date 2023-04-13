/*
** Glaber
** Copyright (C)  Glaber
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/
#include "log.h"
#include "zbxcommon.h"
#include "../../libs/zbxsysinfo/sysinfo.h"
#include "preproc.h"


#include "glb_poller.h"
#include "poller_tcp.h"
#include "poller_async_io.h"
#include "poller_contention.h"

#include "tcp_agent_proto.h"
#include "tcp_simple_http_proto.h"

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#define DNS_TTL	120

extern int  CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];
extern char *CONFIG_SOURCE_IP;

struct tcp_item_t
{
	const char *interface_addr;
	const char *ipaddr;
	unsigned char useip;
	unsigned int interface_port;
	unsigned char poll_type;
	struct bufferevent *bev;
	void *proto_ctx;
	int next_resolve;
};
typedef struct tcp_item_t tcp_item_t;

tcp_poll_type_procs_t conf[ASYNC_TCP_POLL_TYPE_COUNT] = {0};

static void async_tcp_destroy_session(poller_item_t *poller_item)
{
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	bufferevent_free(tcp_item->bev);
	poller_return_item_to_queue(poller_item);
	tcp_item->bev = NULL;

}

#define TCP_READ_BUFF_SIZE 8192 // libevent has max chunk size 4096 bytes

static void response_cb(struct bufferevent *bev, void *ctx_ptr)
{
	int n, status;
	char buf[TCP_READ_BUFF_SIZE];

	poller_item_t *poller_item = ctx_ptr;
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	struct evbuffer *input = bufferevent_get_input(tcp_item->bev);

	n = evbuffer_get_length(input);
	evbuffer_remove(input, buf, n);

	status = conf[tcp_item->poll_type].response_cb(poller_item, tcp_item->proto_ctx, buf, n);

	switch (status)
	{
	case ASYNC_IO_TCP_PROC_FINISH:
		poller_register_item_succeed(poller_item);
		async_tcp_destroy_session(poller_item);
		poller_contention_remove_session(tcp_item->ipaddr);
		break;

	case ASYNC_IO_TCP_PROC_CONTINUE:
		break;

	default:
		HALT_HERE("TCP Protocol specific function returned unknown processing status %d", status);
	}
}

static void events_cb(struct bufferevent *bev, short events, void *ptr)
{
	char *buffer;
	poller_item_t *poller_item = ptr;
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	if (events & BEV_EVENT_CONNECTED)
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "TCP Connected event");

		if (NULL != conf[tcp_item->poll_type].connect_cb)
		{
			unsigned char status = conf[tcp_item->poll_type].connect_cb(poller_item, tcp_item->proto_ctx);

			if (ASYNC_IO_TCP_PROC_FINISH == status)
				async_tcp_destroy_session(poller_item);
				poller_contention_remove_session(tcp_item->ipaddr);
		}

		return;
	};

	if (events & BEV_EVENT_ERROR)
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Connection error event");
		conf[tcp_item->poll_type].fail_cb(poller_item, tcp_item->proto_ctx, "Connection error");
		async_tcp_destroy_session(poller_item);

		return;
	};

	if (events & BEV_EVENT_TIMEOUT)
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Timeout event");

		conf[tcp_item->poll_type].timeout_cb(poller_item, tcp_item->proto_ctx);

		async_tcp_destroy_session(poller_item);

		return;
	}

	if (events & BEV_EVENT_EOF) // this might happen when other side sent all data and closed connection
	{
		struct evbuffer *input = bufferevent_get_input(tcp_item->bev);
		int n = evbuffer_get_length(input);

		DEBUG_ITEM(poller_get_item_id(poller_item), "Connection has been dropped for the item, there is %d bytes in the buffer",n);
		if (n > 0)
		{ // agent answered and closed connection - this is fine!
			DEBUG_ITEM(poller_get_item_id(poller_item), "Connection closed, has %d byets to process", n);
			response_cb(bev, ptr);
			poller_contention_remove_session(tcp_item->ipaddr);
		}
		else
		{
			DEBUG_ITEM(poller_get_item_id(poller_item), "Connection is dropped");
			conf[tcp_item->poll_type].fail_cb(poller_item, tcp_item->proto_ctx, "Connection is dropped");
		}

		async_tcp_destroy_session(poller_item);
		return;
	}
}


extern char *CONFIG_SOURCE_IP;

static int tcp_bev_request(poller_item_t *poller_item, const char *request, int request_size)
{
	struct addrinfo *ai = NULL, *ai_bind = NULL, hints;
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	int true = 1, sock, ret = SUCCEED;

#ifdef HAVE_IPV6

	char service[8];

	zbx_snprintf(service, sizeof(service), "%hu", tcp_item->interface_port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (0 != getaddrinfo(tcp_item->ipaddr, service, &hints, &ai))
	{
		LOG_INF("cannot resolve %s", tcp_item->interface_addr);

		if (NULL != ai)
			freeaddrinfo(ai);

		return FAIL;
	}

	if (-1 == (sock = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol)))
	{
		LOG_INF("cannot create socket [[%s]:%hu]", tcp_item->ipaddr, tcp_item->interface_port);
		freeaddrinfo(ai);

		return FAIL;
	}

	//	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int));

	if (NULL != CONFIG_SOURCE_IP)
	{
		memset(&hints, 0x00, sizeof(struct addrinfo));

		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;

		if (0 != getaddrinfo(CONFIG_SOURCE_IP, NULL, &hints, &ai_bind))
		{
			LOG_WRN(" invalid source IP address '%s'", CONFIG_SOURCE_IP);

			freeaddrinfo(ai);
			if (NULL != ai_bind)
				freeaddrinfo(ai_bind);

			close(sock);

			return FAIL;
		}

		if (ZBX_PROTO_ERROR == bind(sock, ai_bind->ai_addr, ai_bind->ai_addrlen))
		{
			LOG_WRN("Couldn't bind ip to the source ip %s", CONFIG_SOURCE_IP);

			freeaddrinfo(ai);
			freeaddrinfo(ai_bind);

			close(sock);

			return FAIL;
		}
	}

#else

	ZBX_SOCKADDR servaddr_in;
	char *error = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (0 != getaddrinfo(tcp_item->ipaddr, NULL, &hints, &ai))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Getaddrinfo failed for name: %s", tcp_item->ipaddr);
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
	servaddr_in.sin_port = htons(tcp_item->interface_port);

	if ( -1 == (sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't exec gettraddrinfo");
		freeaddrinfo(ai);
		return FAIL;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		ZBX_SOCKADDR source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(CONFIG_SOURCE_IP);
		source_addr.sin_port = 0;

		if (ZBX_PROTO_ERROR == bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			zabbix_log(LOG_LEVEL_WARNING, "Couldn't bind ip to the source ip %s", CONFIG_SOURCE_IP);
			close(sock);
			freeaddrinfo(ai);
			return FAIL;
		}
	}
#endif

	const struct timeval timeout = {.tv_sec = sysinfo_get_config_timeout(), .tv_usec = 0};
	evutil_make_socket_nonblocking(sock);
	evutil_make_listen_socket_reuseable(sock);
	evutil_make_listen_socket_reuseable_port(sock);

	tcp_item->bev = bufferevent_socket_new(poller_async_get_events_base(), sock, BEV_OPT_CLOSE_ON_FREE);
	
	bufferevent_setcb(tcp_item->bev, response_cb, NULL, events_cb, poller_item);
	bufferevent_enable(tcp_item->bev, EV_READ | EV_WRITE);
	bufferevent_set_timeouts(tcp_item->bev, &timeout, &timeout);

	if (request_size > 0)
		evbuffer_add(bufferevent_get_output(tcp_item->bev), request, request_size);

#ifdef HAVE_IPV6
	if (-1 == bufferevent_socket_connect(tcp_item->bev, (struct sockaddr *)ai->ai_addr, sizeof(struct sockaddr)))
#else
	if (-1 == bufferevent_socket_connect(tcp_item->bev, (struct sockaddr *)&hints, sizeof(struct sockaddr)))
#endif
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't start connection");
		async_tcp_destroy_session(poller_item);
		ret = FAIL;
	}

	if (NULL != ai)
		freeaddrinfo(ai);
	if (NULL != ai_bind)
		freeaddrinfo(ai_bind);

	return ret;
}

static int tcp_send_request(poller_item_t *poller_item)
{
	DEBUG_ITEM(poller_get_item_id(poller_item), "Starting TCP connection");

	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	void *request = NULL;
	size_t request_size = 0;

	if (NULL != conf[tcp_item->poll_type].create_request)
		conf[tcp_item->poll_type].create_request(poller_item, tcp_item->proto_ctx, &request, &request_size);

	if (FAIL == tcp_bev_request(poller_item, request, request_size))
	{
		poller_preprocess_error(poller_item, "There was an error while connect");
		return FAIL;
	}

	poller_contention_add_session(tcp_item->ipaddr);
	poller_inc_requests();
	return SUCCEED;
}

void resolve_ready_func_cb(poller_item_t *poller_item, const char *addr)
{

	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	DEBUG_ITEM(poller_get_item_id(poller_item), "Item ip %s resolved to '%s'", tcp_item->interface_addr, addr);

	poller_strpool_free(tcp_item->ipaddr);
	tcp_item->ipaddr = poller_strpool_add(addr);
	
	tcp_item->next_resolve = time(NULL) + DNS_TTL + rand() % 15;
	tcp_send_request(poller_item);
}

static int ip_str_version(const char *src) {
    char buf[16];
    if (inet_pton(AF_INET, src, buf)) {
        return 4;
    } else if (inet_pton(AF_INET6, src, buf)) {
        return 6;
    }
    return -1;
}

/******************************************************************************
 * starts a connection for the session 										  *
 * ***************************************************************************/
static void tcp_start_connection(poller_item_t *poller_item)
{
	DEBUG_ITEM(poller_get_item_id(poller_item),
			   "starting tcp connection for the item");
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	u_int64_t itemid = poller_get_item_id(poller_item);
	int n;

	if (DEFAULT_TCP_HOST_CONTENTION <= (n = poller_contention_get_sessions(tcp_item->ipaddr)))
	{
	 	DEBUG_ITEM(poller_get_item_id(poller_item), "There are already %d connections for the %s host, delaying poll", n, tcp_item->interface_addr);
	 	poller_return_delayed_item_to_queue(poller_item);
	 	return;
	}
	DEBUG_ITEM(poller_get_item_id(poller_item), "There are already %d connections for the %s host, doing poll", n, tcp_item->interface_addr);

	if (1 == tcp_item->useip) {
		tcp_send_request(poller_item);
		return;
	}

	if ( tcp_item->next_resolve < time(NULL))
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Need to resolve %s", tcp_item->interface_addr);
	
		if (FAIL == poller_async_resolve(poller_item, tcp_item->interface_addr))
		{
			DEBUG_ITEM(poller_get_item_id(poller_item), "Cannot resolve item's interface addr: '%s'", tcp_item->interface_addr);
			poller_preprocess_error(poller_item, "Cannot resolve item's interface hostname");
			poller_return_item_to_queue(poller_item);
		}
		return;
	}
	//LOG_INF("This is dns name %s item, but still cached addres %s",tcp_item->interface_addr, tcp_item->ipaddr);
	tcp_send_request(poller_item);
}

static void tcp_free_item(poller_item_t *poller_item)
{

	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	if (NULL != tcp_item->bev)
		async_tcp_destroy_session(poller_item);

	if ((NULL != tcp_item->proto_ctx) &&
		(NULL != conf[tcp_item->poll_type].item_destroy))
	{

		conf[tcp_item->poll_type].item_destroy(tcp_item->proto_ctx);
	}

	poller_strpool_free(tcp_item->ipaddr);
	
	if (0 == tcp_item->useip)
		poller_strpool_free(tcp_item->interface_addr);

	zbx_free(tcp_item);
}

static int tcp_init_item(DC_ITEM *dc_item, poller_item_t *poller_item)
{
	zbx_timespec_t timespec;

	const char *interface_addr;
	unsigned int interface_port;

	tcp_item_t *tcp_item;
	tcp_item = zbx_calloc(NULL, 0, sizeof(tcp_item_t));
	DEBUG_ITEM(poller_get_item_id(poller_item), "Doing tcp init of the item");
	poller_set_item_specific_data(poller_item, tcp_item);

	if (ITEM_TYPE_AGENT == dc_item->type)
		tcp_item->poll_type = ASYNC_TCP_POLL_TYPE_AGENT;
	else if (ITEM_TYPE_SIMPLE == dc_item->type && 0 == (strncmp(dc_item->key, "net.tcp.service[http", 20)))
		tcp_item->poll_type = ASYNC_TCP_POLL_SIMPLE_HTTP_TYPE;
	else
		return FAIL;

	if (NULL == (tcp_item->proto_ctx = conf[tcp_item->poll_type].item_init(poller_item, dc_item)))
	{ // tcp_init_proto_specific_data(dc_item, poller_item, tcp_item))){
		DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't create item in the poller, it will not be polled");
		poller_preprocess_error(poller_item, "Couldn't create item in the poller, it will not be polled");

		tcp_free_item(poller_item);
		return FAIL;
	};

	tcp_item->useip = dc_item->interface.useip;
	
	if (1 == tcp_item->useip || ip_str_version(dc_item->interface.addr) >0 ) {
		tcp_item->useip = 1;
		tcp_item->ipaddr = poller_strpool_add(dc_item->interface.addr);
		tcp_item->interface_addr = poller_strpool_copy(tcp_item->ipaddr);
		DEBUG_ITEM(dc_item->itemid, "Set new tcp item interface to %s", tcp_item->interface_addr);
	} else {
		tcp_item->interface_addr = poller_strpool_add(dc_item->interface.addr);
		DEBUG_ITEM(dc_item->itemid, "Set new tcp item interface to %s", tcp_item->interface_addr);
	}

	tcp_item->interface_port = dc_item->interface.port;
	

	return SUCCEED;
}

static int forks_count(void)
{
	return CONFIG_FORKS[GLB_PROCESS_TYPE_AGENT];
}

static void async_io(void) {
	poller_contention_housekeep();
}

static void tcp_shutdown_cb()
{
}

int glb_tcp_init(void)
{
	poller_set_poller_callbacks(tcp_init_item, tcp_free_item, async_io, tcp_start_connection, 
		tcp_shutdown_cb, forks_count, resolve_ready_func_cb, NULL);

	tcp_agent_proto_init(&conf[ASYNC_TCP_POLL_TYPE_AGENT]);
	tcp_simple_http_proto_init(&conf[ASYNC_TCP_POLL_SIMPLE_HTTP_TYPE]);

	return SUCCEED;
}
