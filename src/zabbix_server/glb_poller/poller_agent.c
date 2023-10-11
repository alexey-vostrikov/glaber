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

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
#include "../../zbxcomms/tls.h"
#endif



#include "glb_poller.h"
#include "poller_tcp.h"
#include "poller_async_io.h"
#include "poller_contention.h"

#include "tcp_agent_proto.h"
//#include "tcp_simple_http_proto.h"

#include <event2/event.h>
// #include <event2/bufferevent.h>
// #include <event2/buffer.h>

#define DNS_TTL	120

extern int  CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];
extern char *CONFIG_SOURCE_IP;

#define AGENT_MAX_HOST_CONTENTION  4
#define AGENT_POLLER_MAX_CONNECTIONS  4096

//TODO: fix this by passing timeout via thread args
int CONFIG_TIMEOUT = 10;

typedef struct 
{
	const char *addr;
	unsigned char useip;
	unsigned int port;
	unsigned char tls_connect;
	int socket;
	
	const char *tls_arg1;
	const char *tls_arg2;
	
	poller_event_t *timeout_event;
	poller_event_t *network_event;
	zbx_tls_context_t *tls_ctx;
	//void *proto_ctx;

} agent_item_t;


//static tcp_poll_type_procs_t conf[ASYNC_TCP_POLL_TYPE_COUNT] = {0};

// static void async_tcp_destroy_session(poller_item_t *poller_item)
// {
// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);

// 	bufferevent_free(tcp_item->bev);
// 	poller_return_item_to_queue(poller_item);
// 	tcp_item->bev = NULL;

// }

#define TCP_READ_BUFF_SIZE 8192 // libevent has max chunk size 4096 bytes

// static void response_cb(struct bufferevent *bev, void *ctx_ptr)
// {
// 	int n, status;
// 	char buf[TCP_READ_BUFF_SIZE];

// 	poller_item_t *poller_item = ctx_ptr;
// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);

// 	struct evbuffer *input = bufferevent_get_input(tcp_item->bev);

// 	n = evbuffer_get_length(input);
// 	evbuffer_remove(input, buf, n);

// 	status = conf[tcp_item->poll_type].response_cb(poller_item, tcp_item->proto_ctx, buf, n);

// 	switch (status)
// 	{
// 	case ASYNC_IO_TCP_PROC_FINISH:
// 		poller_iface_register_succeed(poller_item);
// 		async_tcp_destroy_session(poller_item);
// 		poller_contention_remove_session(tcp_item->ipaddr);
// 		break;

// 	case ASYNC_IO_TCP_PROC_CONTINUE:
// 		break;

// 	default:
// 		HALT_HERE("TCP Protocol specific function returned unknown processing status %d", status);
// 	}
// }

// static void events_cb(struct bufferevent *bev, short events, void *ptr)
// {
// 	char *buffer;
// 	poller_item_t *poller_item = ptr;
// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);

// 	if (events & BEV_EVENT_CONNECTED)
// 	{
// 		DEBUG_ITEM(poller_item_get_id(poller_item), "TCP Connected event");

// 		if (NULL != conf[tcp_item->poll_type].connect_cb)
// 		{
// 			unsigned char status = conf[tcp_item->poll_type].connect_cb(poller_item, tcp_item->proto_ctx);

// 			if (ASYNC_IO_TCP_PROC_FINISH == status)
// 				async_tcp_destroy_session(poller_item);
// 				poller_contention_remove_session(tcp_item->ipaddr);
// 		}

// 		return;
// 	};

// 	if (events & BEV_EVENT_ERROR)
// 	{
// 		DEBUG_ITEM(poller_item_get_id(poller_item), "Connection error event");
// 		conf[tcp_item->poll_type].conn_fail_cb(poller_item, tcp_item->proto_ctx, "Connection error");
// 		async_tcp_destroy_session(poller_item);

// 		return;
// 	};

// 	if (events & BEV_EVENT_TIMEOUT)
// 	{
// 		DEBUG_ITEM(poller_item_get_id(poller_item), "Timeout event");

// 		conf[tcp_item->poll_type].timeout_cb(poller_item, tcp_item->proto_ctx);

// 		async_tcp_destroy_session(poller_item);

// 		return;
// 	}

// 	if (events & BEV_EVENT_EOF) // this might happen when other side sent all data and closed connection
// 	{
// 		struct evbuffer *input = bufferevent_get_input(tcp_item->bev);
// 		int n = evbuffer_get_length(input);

// 		DEBUG_ITEM(poller_item_get_id(poller_item), "Connection has been dropped for the item, there is %d bytes in the buffer",n);
// 		if (n > 0)
// 		{ // agent answered and closed connection - this is fine!
// 			DEBUG_ITEM(poller_item_get_id(poller_item), "Connection closed, has %d byets to process", n);
// 			response_cb(bev, ptr);
// 			poller_contention_remove_session(tcp_item->ipaddr);
// 		}
// 		else
// 		{
// 			DEBUG_ITEM(poller_item_get_id(poller_item), "Connection is dropped");
// 			conf[tcp_item->poll_type].conn_fail_cb(poller_item, tcp_item->proto_ctx, "Connection is dropped");
// 		}

// 		async_tcp_destroy_session(poller_item);
// 		return;
// 	}
// }


extern char *CONFIG_SOURCE_IP;


// static int tcp_send_request(poller_item_t *poller_item)
// {
// 	DEBUG_ITEM(poller_item_get_id(poller_item), "Starting TCP connection");

// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);

// 	void *request = NULL;
// 	size_t request_size = 0;

// 	if (NULL != conf[tcp_item->poll_type].create_request)
// 		conf[tcp_item->poll_type].create_request(poller_item, tcp_item->proto_ctx, &request, &request_size);

// 	if (FAIL == tcp_bev_request(poller_item, request, request_size))
// 	{
// 		poller_preprocess_error(poller_item, "There was an error while connect");
// 		return FAIL;
// 	}

// 	poller_contention_add_session(tcp_item->ipaddr);
// 	poller_inc_requests();
// 	return SUCCEED;
// }

// void resolve_ready_func_cb(poller_item_t *poller_item, const char *addr)
// {

// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);
// 	DEBUG_ITEM(poller_item_get_id(poller_item), "Item ip %s resolved to '%s'", tcp_item->interface_addr, addr);

// 	poller_strpool_free(tcp_item->ipaddr);
// 	tcp_item->ipaddr = poller_strpool_add(addr);
	
// 	tcp_item->next_resolve = time(NULL) + DNS_TTL + rand() % 15;
// 	tcp_send_request(poller_item);
// }

// static int ip_str_version(const char *src) {
//     char buf[16];
//     if (inet_pton(AF_INET, src, buf)) {
//         return 4;
//     } else if (inet_pton(AF_INET6, src, buf)) {
//         return 6;
//     }
//     return -1;
// }
void agent_socket_connected_cb(poller_item_t *poller_item, void *data) {
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	//ok, for unencrypted ones, we writing the request to the socket

	//for encrypted, starting tls negitiation
	if ( ZBX_TCP_SEC_UNENCRYPTED !=  agent_item->tls_connect) {
		LOG_INF("Doing TLS connection init");



	} else {
		LOG_INF("Doing unencrypted connection init");
	}
	



	HALT_HERE("So, we're connected, don't know what to do next ^)");
}

int agent_create_socket(poller_item_t *poller_item, const char *ip, char **error) {
	char		service[8];
	struct addrinfo	*ai = NULL, hints;
	struct addrinfo	*ai_bind = NULL;
	size_t alloc = 0, size = 0;

	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);

	
	zbx_snprintf(service, sizeof(service), "%hu", agent_item->port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (0 != getaddrinfo(ip, service, &hints, &ai))
	{
		zbx_snprintf_alloc(error, &alloc, &size,"Invalid IP address [%s]", ip);
		return FAIL;
	}

	if (-1 == (agent_item->socket = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol)))
	{
		zbx_snprintf_alloc(error,  &alloc, &size, "cannot create socket [[%s]:%hu]: %s",
				ip, agent_item->port, strerror_from_system(zbx_socket_last_error()));
		freeaddrinfo(ai);
		return FAIL;
	}

	if (-1 == fcntl(agent_item->socket, F_SETFD, FD_CLOEXEC))
	{
		zbx_snprintf_alloc(error, &alloc, &size, "failed to set the FD_CLOEXEC file descriptor flag on socket [[%s]:%hu]: %s",
				ip, agent_item->port, strerror_from_system(zbx_socket_last_error()));
		freeaddrinfo(ai);
		return FAIL;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		memset(&hints, 0x00, sizeof(struct addrinfo));

		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype =  SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;

		if (0 != getaddrinfo(CONFIG_SOURCE_IP, NULL, &hints, &ai_bind))
		{
			zbx_snprintf_alloc(error, &alloc, &size,"invalid source IP address [%s]", CONFIG_SOURCE_IP);

			freeaddrinfo(ai);
			close(agent_item->socket);

			return FAIL;
		}

		if ( -1 == bind(agent_item->socket, ai_bind->ai_addr,(int) ai_bind->ai_addrlen))
		{
		
			zbx_snprintf_alloc(error, &alloc, &size,"bind() failed: %s", strerror_from_system(zbx_socket_last_error()));
			freeaddrinfo(ai);
			freeaddrinfo(ai_bind);
			close(agent_item->socket);
			return FAIL;
		}
	}

	if (NULL != ai_bind)
 		freeaddrinfo(ai_bind);
	LOG_INF("Starrting connection, socket fd is %d", agent_item->socket);
	int conn_ret = connect(agent_item->socket, ai->ai_addr, (socklen_t)ai->ai_addrlen);
	
	if ( 0 == conn_ret ) {
			//fine, we-ve already conncted, call 
			agent_socket_connected_cb(poller_item, NULL);
	} else 
	if ( -1 == conn_ret && EINPROGRESS == errno) {
		agent_item->network_event = poller_create_event(poller_item, agent_socket_connected_cb, agent_item->socket, NULL, 0);
		poller_run_fd_event(agent_item->network_event);
		freeaddrinfo(ai);
		return SUCCEED;
	}
	
	LOG_INF("Connect returned %d status, errno is %d", conn_ret, errno);
	*error = zbx_strdup(*error, strerror_from_system(errno));
	
	freeaddrinfo(ai);
	return FAIL;
}

void agent_clear_text_arrived_response() {
	
}

/*
void agent_tls_response_arrived_cb(poller_item_t *poller_item) {
	//while (hasn_t_got_full_responce) {
		do_async_tls_read()
		add_buffer()
		if (not_all_data_arrived) {
			continue_tls_read;
			return;
		}

		cleanup_and_close_tls();
		process_the_response();
	//}
}


void agent_tls_completed_handshake_cb(polller_item_t * poller_item) {
	//check for error and send encrypted request
	if (there_is_a_tls_error(poller_item)) {
		do_tls_cleanup(poller_item);
		set_error;
	}
	
	//most likely, we can write in sync mode, it's quite safe as the request 
	//is rather small
	start_tls_write(poller_item);


}


void agent_socket_connect_event_cb(poller_item) {
	//here we read what socket_connect syscall returned and 
	//then starting the 
	if (agent->tls_connect != TLS_NONE) {
		agent_prepare_tls_cts(poller_item);
		agent_init_tls_handshake(poller_item);
	} else {
		agent_send_cleartext_request(poller_item);
	}
	
}
*/

static void agent_finish_item_poll(poller_item_t *poller_item, int result, const char *value) {
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	
	poller_contention_remove_session(agent_item->addr);
	poller_return_item_to_queue(poller_item);
	
	HALT_HERE("Got result %s, finish isn't impemented yet");
}


static void agent_item_timeout_cb(poller_item_t* poller_item, void *data) {
	//
	//agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	
	agent_finish_item_poll(poller_item, TIMEOUT_ERROR, "Timeout waiting for response");
	//HALT_HERE("Timeout handler not implemented yet");
}

void agent_start_timeout_watch(poller_item_t *poller_item) {
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	agent_item->timeout_event = poller_create_event(poller_item, agent_item_timeout_cb, 0, NULL, 0);
	poller_run_timer_event(agent_item->timeout_event, CONFIG_TIMEOUT * 1000);
}

void agent_send_request(poller_item_t *poller_item, const char *addr) {
	char *error = NULL;
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	DEBUG_ITEM(poller_item_get_id(poller_item), "Will send request to '%s'", addr);

	if (FAIL == agent_create_socket(poller_item, addr, &error))  {
		LOG_INF("Failed to create a socket");
		agent_finish_item_poll(poller_item, CONFIG_ERROR, error);
		zbx_free(error);
		return;
	}

//	agent_item->conn_state = TCP_CONNECT;
	agent_start_timeout_watch(poller_item);
}

static int agent_start_connection(poller_item_t *poller_item) {
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);
	int n;

	if ( ZBX_TCP_SEC_UNENCRYPTED == agent_item->tls_connect) {
		DEBUG_ITEM(poller_item_get_id(poller_item), "Item has no TLS set, skipping for test purposes");
		poller_return_delayed_item_to_queue(poller_item);
		return POLL_STARTED_OK;
	}

	DEBUG_ITEM(poller_item_get_id(poller_item), "starting agent connection for the item");

	if (AGENT_MAX_HOST_CONTENTION <= (n = poller_contention_get_sessions(agent_item->addr))) {
		DEBUG_ITEM(poller_item_get_id(poller_item), 
			"There are already %d connections to the host already, delaying the item", n);
		return POLL_NEED_DELAY;
	}

	// if (AGENT_POLLER_MAX_CONNECTIONS <= (n =  poller_contention_sessions_count() )) {
	// 	DEBUG_ITEM(poller_item_get_id(poller_item), 
	// 		"There are already %d total connections, delaying the item", n);
	// 	poller_return_delayed_item_to_queue(poller_item);
	// 	return POLL_ME;
	// }
	
	LOG_INF("Starting a new connection to `%s`, total connections %d", 
			agent_item->addr, poller_contention_sessions_count());

	poller_contention_add_session(agent_item->addr);

	if (0 == agent_item->useip) {
		DEBUG_ITEM(poller_item_get_id(poller_item), "Doing async item resolve of addr '%s'", agent_item->addr);
		poller_async_resolve(poller_item, agent_item->addr);
		return POLL_STARTED_OK;
	}
	
	DEBUG_ITEM(poller_item_get_id(poller_item), "Item doesn't need resolving");
	agent_send_request(poller_item, agent_item->addr);
	
	return POLL_STARTED_OK;
}
 static void agent_resolve_fail_cb(poller_item_t *poller_item) {
	DEBUG_ITEM(poller_item_get_id(poller_item),"Failed to reolve item's DNS address");
	LOG_INF("Failed to resolve an address");
	agent_finish_item_poll(poller_item, CONFIG_ERROR, "Failed to resolve item's DNS name");
 }

/******************************************************************************
 * starts a connection for the session 										  *
 * ***************************************************************************/
// static void tcp_start_connection(poller_item_t *poller_item)
// {

// 	DEBUG_ITEM(poller_item_get_id(poller_item),
// 			   "starting tcp connection for the item");
// 	agent_item_t *tcp_item = poller_item_get_specific_data(poller_item);
// 	u_int64_t itemid = poller_item_get_id(poller_item);
// 	int n;

// 	if (DEFAULT_TCP_HOST_CONTENTION <= (n = poller_contention_get_sessions(tcp_item->addr)))
// 	{
// 	 	DEBUG_ITEM(poller_item_get_id(poller_item), "There are already %d connections for the %s host, delaying poll", n, tcp_item->interface_addr);
// 	 	poller_return_delayed_item_to_queue(poller_item);
// 	 	return;
// 	}
// 	DEBUG_ITEM(poller_item_get_id(poller_item), "There are already %d connections for the %s host, doing poll", n, tcp_item->interface_addr);

// 	if (1 == tcp_item->useip) {
// 		tcp_send_request(poller_item);
// 		return;
// 	}

// 	if ( tcp_item->next_resolve < time(NULL))
// 	{
// 		DEBUG_ITEM(poller_item_get_id(poller_item), "Need to resolve %s", tcp_item->interface_addr);
	
// 		if (FAIL == poller_async_resolve(poller_item, tcp_item->interface_addr))
// 		{
// 			DEBUG_ITEM(poller_item_get_id(poller_item), "Cannot resolve item's interface addr: '%s'", tcp_item->interface_addr);
// 			poller_preprocess_error(poller_item, "Cannot resolve item's interface hostname");
// 			poller_return_item_to_queue(poller_item);
// 		}
// 		return;
// 	}
// 	//LOG_INF("This is dns name %s item, but still cached addres %s",tcp_item->interface_addr, tcp_item->ipaddr);
// 	tcp_send_request(poller_item);
// }


static int init_tls_params(agent_item_t *agent_item, DC_ITEM *dc_item, char **error) {

	if (ZBX_TCP_SEC_UNENCRYPTED != agent_item->tls_connect && 
		ZBX_TCP_SEC_TLS_CERT != agent_item->tls_connect &&
		ZBX_TCP_SEC_TLS_PSK != agent_item->tls_connect)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	if (ZBX_TCP_SEC_UNENCRYPTED != agent_item->tls_connect)
		return SUCCEED;

	#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_TLS_PSK == agent_item->tls_connect && '\0' == *agent_item->tls_arg1)
	{
		*error = zbx_strdup(NULL, "cannot connect with PSK: PSK not available");
		return FAIL;
	}
#else
	if (ZBX_TCP_SEC_TLS_CERT == agent_item->tls_connect || 
		ZBX_TCP_SEC_TLS_PSK == agent_item->tls_connect)
	{
		*error = zbx_strdup( NULL, "support for TLS was not compiled in");
		return FAIL;
	}
#endif

	switch (dc_item->host.tls_connect)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			agent_item->tls_arg1 = NULL;
			agent_item->tls_arg2 = NULL;
			break;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			agent_item->tls_arg1 = poller_strpool_add(dc_item->host.tls_issuer);
			agent_item->tls_arg2 =  poller_strpool_add(dc_item->host.tls_subject);
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			agent_item->tls_arg1 =  poller_strpool_add(dc_item->host.tls_psk_identity);
			agent_item->tls_arg2 =  poller_strpool_add(dc_item->host.tls_psk);
			break;
#else
		case ZBX_TCP_SEC_TLS_CERT:
		case ZBX_TCP_SEC_TLS_PSK:
			DEBUG_ITEM(dc_item->itemid, "A TLS connection is configured to be used with agent"
					" but support for TLS was not compiled into %s.",
					get_program_type_string(program_type)));
			*error = zbx_strdup(NULL,"A TLS connection is configured to be used with agent"
					" but support for TLS was not compiled in");
			return FAIL;
#endif
		default:
			//LOG_INF("Invalid TLS connection parameters.");
			*error = zbx_strdup(NULL,"Invalid TLS connection parameters.");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
	return SUCCEED;
}

static void free_tls_params(agent_item_t * agent_item) {
	poller_strpool_free(agent_item->tls_arg1);
	poller_strpool_free(agent_item->tls_arg2);
}

static void agent_free_item(poller_item_t *poller_item)
{
	agent_item_t *agent_item = poller_item_get_specific_data(poller_item);

	poller_strpool_free(agent_item->addr);
	free_tls_params(agent_item);
	zbx_free(agent_item);
}

static int agent_init_item(DC_ITEM *dc_item, poller_item_t *poller_item)
{
	zbx_timespec_t timespec;

	const char *interface_addr;
	unsigned int port;
	char *error = NULL;

	agent_item_t *agent_item;
	agent_item = zbx_calloc(NULL, 0, sizeof(agent_item_t));
	DEBUG_ITEM(poller_item_get_id(poller_item), "Doing agent init of the item");

	poller_set_item_specific_data(poller_item, agent_item);
	
	agent_item->useip = dc_item->interface.useip;
	agent_item->addr = poller_strpool_add(dc_item->interface.addr);
	agent_item->port = dc_item->interface.port;
	agent_item->tls_connect = dc_item->host.tls_connect;
	DEBUG_ITEM(dc_item->itemid, "Item init: has %d as TLS", agent_item->tls_connect);
	
	if (FAIL == init_tls_params(agent_item, dc_item, &error)) {
		DEBUG_ITEM(dc_item->itemid, "Failed to init TLS params, item will not be polled until reconfigured");
		poller_preprocess_error(poller_item, error);
		zbx_free(error);
		return FAIL;
	}
	
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

int glb_poller_agent_init(void)
{
	
	poller_set_poller_callbacks(agent_init_item, agent_free_item, async_io, agent_start_connection, 
		tcp_shutdown_cb, forks_count, agent_send_request, agent_resolve_fail_cb, "agent", 0, 1);

	
	return SUCCEED;
}
