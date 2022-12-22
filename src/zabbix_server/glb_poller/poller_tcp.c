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
#include "common.h"
#include "dbcache.h"
#include "preproc.h"

#include "glb_poller.h"
#include "poller_tcp.h"
#include "poller_async_io.h"
#include "poller_sessions.h"

#include "tcp_agent_proto.h"
#include "tcp_simple_http_proto.h"

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

extern char *CONFIG_SOURCE_IP;
extern int CONFIG_GLB_AGENT_FORKS;
struct tcp_item_t {
	const char *interface_addr;
	const char *ipaddr;
	unsigned char	useip;
	unsigned int interface_port;
	unsigned char poll_type;
	struct bufferevent *bev;
	void *proto_ctx;
	u_int32_t sessid;
};
typedef struct tcp_item_t tcp_item_t;

tcp_poll_type_procs_t conf[ASYNC_TCP_POLL_TYPE_COUNT] = {0};

static void async_tcp_destroy_session(poller_item_t * poller_item) {
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	bufferevent_free(tcp_item->bev);
	poller_sessions_close_session(tcp_item->sessid);
	poller_return_item_to_queue(poller_item);
	
	tcp_item->bev = NULL;
	tcp_item->sessid = 0;
}

#define TCP_READ_BUFF_SIZE 8192  //libevent has max chunk size 4096 bytes

static void response_cb(struct bufferevent *bev, void *ctx_ptr) {
	int n, status;
	char buf[TCP_READ_BUFF_SIZE];

	poller_item_t *poller_item = ctx_ptr;
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	
	struct evbuffer *input = bufferevent_get_input(tcp_item->bev);

    n = evbuffer_get_length(input);
    evbuffer_remove(input, buf, n);
		
	status = conf[tcp_item->poll_type].response_cb(poller_item, tcp_item->proto_ctx, buf, n);

	switch (status) {
		case ASYNC_IO_TCP_PROC_FINISH:
			poller_register_item_succeed(poller_item);
			async_tcp_destroy_session(poller_item);		
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
	poller_item_t	*poller_item = ptr;
	tcp_item_t		*tcp_item = poller_get_item_specific_data(poller_item);

 	if (events & BEV_EVENT_CONNECTED)
 	{
 		DEBUG_ITEM(poller_get_item_id(poller_item),"TCP Connected event");
		
		if (NULL != conf[tcp_item->poll_type].connect_cb) {
			unsigned char status = conf[tcp_item->poll_type].connect_cb(poller_item, tcp_item->proto_ctx);
			
			if (ASYNC_IO_TCP_PROC_FINISH == status)
				async_tcp_destroy_session(poller_item);
		}
 		
		return;
 	};
	
 	if (events & BEV_EVENT_ERROR)
 	{
		DEBUG_ITEM(poller_get_item_id(poller_item),"Connection error event");
		conf[tcp_item->poll_type].fail_cb(poller_item, tcp_item->proto_ctx, "Connection error");	
 		async_tcp_destroy_session(poller_item);
	
		return;
 	};

 	if (events & BEV_EVENT_TIMEOUT)
 	{
		DEBUG_ITEM(poller_get_item_id(poller_item),"Timeout event");
	
		conf[tcp_item->poll_type].timeout_cb(poller_item, tcp_item->proto_ctx);
		
		async_tcp_destroy_session(poller_item);
		poller_register_item_timeout(poller_item);

 		return;
 	}
	
 	if (events & BEV_EVENT_EOF) //this might happen when other side sent data and closed connection
 	{
 		struct evbuffer *input = bufferevent_get_input(tcp_item->bev);
		int n = evbuffer_get_length(input);

		//LOG_INF("Connection has been dropped for item %ld, there is %d bytes in the buffer", poller_get_item_id(poller_item), n);
		if (n > 0) { //agent answered and closed connection - this is fine!
			DEBUG_ITEM(poller_get_item_id(poller_item),"Connection closed, has %d byets to process", n);
			response_cb(bev,ptr);
		} else {
			DEBUG_ITEM(poller_get_item_id(poller_item),"Connection is dropped");
			conf[tcp_item->poll_type].fail_cb(poller_item, tcp_item->proto_ctx, "Connection is dropped");	
		}
 		
		async_tcp_destroy_session(poller_item);
 		return;
 	}
 
// 	HALT_HERE("Implement");
}


static void timeout_cb(poller_item_t *poller_item, void *data) {
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	
	conf[tcp_item->poll_type].timeout_cb(poller_item, data);

	poller_register_item_timeout(poller_item);
	async_tcp_destroy_session(poller_item);
}

static void fail_cb(poller_item_t *poller_item, void *data) {

	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	char *reason  = data;
	
	poller_preprocess_error(poller_item, reason);
	async_tcp_destroy_session(poller_item);	
}

extern char *CONFIG_SOURCE_IP;

static int tcp_bev_request(poller_item_t *poller_item, const char *request, int request_size) {
	struct addrinfo	*ai = NULL, *ai_bind = NULL, hints; 
	int		ret = SUCCEED;
#ifdef HAVE_IPV6	
	
	char	service[8];
	int sock;

	tcp_item_t *tcp_item  = poller_get_item_specific_data(poller_item);

	
	zbx_snprintf(service, sizeof(service), "%hu", tcp_item->interface_port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	if (0 != getaddrinfo(tcp_item->ipaddr, service, &hints, &ai))
	{
		LOG_INF("cannot resolve %s", tcp_item->ipaddr);
		
		if (NULL != ai)
			freeaddrinfo(ai);
		
		return FAIL;
	}

	if (-1 == (sock = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol))) {
		LOG_INF("cannot create socket [[%s]:%hu]", tcp_item->ipaddr, tcp_item->interface_port);
		freeaddrinfo(ai);

		return FAIL;
	}
	
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
			if (NULL != ai_bind) freeaddrinfo(ai_bind);
			
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

	ZBX_SOCKADDR	servaddr_in;
	char		*error = NULL;
	int true = 1;
	int sock;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	tcp_item_t *tcp_item  = poller_get_item_specific_data(poller_item);


	if (0 != getaddrinfo(tcp_item->ipaddr, NULL, &hints, &ai)) {
		zabbix_log(LOG_LEVEL_DEBUG,"Getaddrinfo failed for name: %s",tcp_item->ipaddr);
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
	servaddr_in.sin_port = htons(tcp_item->interface_port);
	
	if (ZBX_SOCKET_ERROR == (sock = socket(AF_INET, SOCK_STREAM| SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
	{
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't exec gettraddrinfo");
		freeaddrinfo(ai);
		return FAIL;
	}

	setsockopt(sock,SOL_SOCKET, ,&true,sizeof(int));

	if (NULL != CONFIG_SOURCE_IP)
	{
		ZBX_SOCKADDR	source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(CONFIG_SOURCE_IP);
		source_addr.sin_port = 0;

		if (ZBX_PROTO_ERROR == bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't bind ip to the source ip %s",  CONFIG_SOURCE_IP);
			close(sock);
			freeaddrinfo(ai);
			return FAIL;
		}
	}
#endif

	const struct timeval timeout= {.tv_sec = CONFIG_TIMEOUT, .tv_usec = 0};
	evutil_make_socket_nonblocking(sock);
	
	tcp_item->bev = bufferevent_socket_new(poller_async_get_events_base(), sock, BEV_OPT_CLOSE_ON_FREE);	
	tcp_item->sessid = poller_sessions_create_session(poller_get_item_id(poller_item), 0 );

	bufferevent_setcb(tcp_item->bev, response_cb, NULL, events_cb, poller_item);
	bufferevent_enable(tcp_item->bev, EV_READ|EV_WRITE);
	bufferevent_set_timeouts(tcp_item->bev, &timeout, &timeout);
	
	if (request_size > 0) 
		evbuffer_add(bufferevent_get_output(tcp_item->bev), request, request_size);

#ifdef HAVE_IPV6
	if (-1 == bufferevent_socket_connect(tcp_item->bev,(struct sockaddr *)ai->ai_addr, sizeof(struct sockaddr)) )
#else 
	if (-1 == bufferevent_socket_connect(tcp_item->bev,(struct sockaddr *)&hints, sizeof(hints)) )
#endif
    	{
	   		DEBUG_ITEM(poller_get_item_id(poller_item),"Couldn't start connection");
       		async_tcp_destroy_session(poller_item);
			ret = FAIL;
    	}

	if (NULL != ai) 
		freeaddrinfo(ai);
	if (NULL != ai_bind) 
		freeaddrinfo(ai_bind);

	return ret;
}


static int tcp_send_request(poller_item_t *poller_item) {
	
	DEBUG_ITEM(poller_get_item_id(poller_item),"Starting TCP connection");
	
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	void	*request = NULL;
	size_t		request_size = 0;
	
	
	if (NULL != conf[tcp_item->poll_type].create_request)
			conf[tcp_item->poll_type].create_request(poller_item, tcp_item->proto_ctx, &request, &request_size);
	
	if (FAIL == tcp_bev_request(poller_item, request, request_size)) {
 		poller_preprocess_error(poller_item, "There was an error while connect");
		return  FAIL;
	}

	poller_inc_requests();
	
	return SUCCEED;
}

void resolve_ready_func_cb(poller_item_t *poller_item,  const char *addr) {

	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);
	DEBUG_ITEM(poller_get_item_id(poller_item), "Item ip %s resolved to '%s'", tcp_item->interface_addr, addr);

	poller_strpool_free(tcp_item->ipaddr);
	tcp_item->ipaddr = poller_strpool_add(addr);

	tcp_send_request(poller_item);
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
		
	if (1 != tcp_item->useip) {
	
	   	DEBUG_ITEM(poller_get_item_id(poller_item), "Need to resolve %s", tcp_item->interface_addr);	
		
		if (FAIL == poller_async_resolve(poller_item, tcp_item->interface_addr)) {

			DEBUG_ITEM(poller_get_item_id(poller_item), "Cannot resolve item's interface addr: '%s'", tcp_item->interface_addr);
			poller_preprocess_error(poller_item, "Cannot resolve item's interface hostname");
			poller_return_item_to_queue(poller_item);
		}
		return;
	}
	
	tcp_item->ipaddr = tcp_item->interface_addr;

	tcp_send_request(poller_item);
}	
	
static void tcp_free_item(poller_item_t *poller_item ) {
	
	tcp_item_t *tcp_item = poller_get_item_specific_data(poller_item);

	if (NULL != tcp_item->bev)
		async_tcp_destroy_session(poller_item);

	if ( (NULL != tcp_item->proto_ctx) && 
	     (NULL != conf[tcp_item->poll_type].item_destroy)) {
		
		conf[tcp_item->poll_type].item_destroy(tcp_item->proto_ctx);
	}
	
	poller_strpool_free(tcp_item->interface_addr);
	zbx_free(tcp_item);
}

static int tcp_init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
	zbx_timespec_t timespec;
	
	const char *interface_addr;
	unsigned int interface_port;
	
	tcp_item_t *tcp_item;
	tcp_item = zbx_calloc(NULL,0,sizeof(tcp_item_t));
	
	poller_set_item_specific_data(poller_item,tcp_item);
	
	if ( ITEM_TYPE_AGENT == dc_item->type)
		tcp_item->poll_type = ASYNC_TCP_POLL_TYPE_AGENT;  
	else if ( ITEM_TYPE_SIMPLE == dc_item->type && 0 == (strncmp(dc_item->key, "net.tcp.service[http", 20)))
		tcp_item->poll_type = ASYNC_TCP_POLL_SIMPLE_HTTP_TYPE;
	else 
		return FAIL;
	
	
	if (NULL == (tcp_item->proto_ctx = conf[tcp_item->poll_type].item_init(poller_item, dc_item))) {     //tcp_init_proto_specific_data(dc_item, poller_item, tcp_item))){
		DEBUG_ITEM(poller_get_item_id(poller_item),"Couldn't create item in the poller, it will not be polled");
		poller_preprocess_error(poller_item,"Couldn't create item in the poller, it will not be polled");
	
		tcp_free_item(poller_item);
		return FAIL;
	};
	
	tcp_item->interface_addr = poller_strpool_add(dc_item->interface.addr);
	tcp_item->interface_port = dc_item->interface.port;
	tcp_item->useip = dc_item->interface.useip;
	
	return SUCCEED;
}

static int forks_count(void) {
	return CONFIG_GLB_AGENT_FORKS;
}

static void tcp_shutdown_cb() {
	poller_sessions_destroy();
}

int  glb_tcp_init(void) {
	poller_sessions_init();
	poller_set_poller_callbacks(tcp_init_item, tcp_free_item, NULL, tcp_start_connection, tcp_shutdown_cb, forks_count, resolve_ready_func_cb);
	
	tcp_agent_proto_init(&conf[ASYNC_TCP_POLL_TYPE_AGENT]);
	tcp_simple_http_proto_init(&conf[ASYNC_TCP_POLL_SIMPLE_HTTP_TYPE]);

	return SUCCEED;
}
