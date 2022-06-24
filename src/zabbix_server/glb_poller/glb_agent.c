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
#include "glb_agent.h"
#include "dbcache.h"
#include "preproc.h"
#define GLB_MAX_AGENT_CONNS 8192

extern char *CONFIG_SOURCE_IP;

typedef struct
{
	struct agent_session *sess;/* SNMP session data */
	u_int64_t current_item;  /* itemid of currently processing item */
	int state;				   /* state of the connection */
	int finish_time;		   /* unix time till we wait for data to come */
	zbx_list_t items_list;     /* list of itemids assigned to the session */
	int socket; 	  
	void* conf; 
	int idx;
}  agent_connection_t;

typedef struct {
	zbx_hashset_t items_idx;
	agent_connection_t conns[GLB_MAX_AGENT_CONNS]; 

} agent_conf_t;

static agent_conf_t conf = {0};

typedef struct {
	const char *interface_addr;
	unsigned int interface_port;
	const char *key; //item key value (strpooled)
} agent_item_t;

extern int CONFIG_GLB_AGENT_FORKS;

/******************************************************************************
 * initiates async tcp connection    										  * 
 * ***************************************************************************/
#ifdef HAVE_IPV6
static int glb_async_tcp_connect(int *sock, const char *source_ip, const char *ip, unsigned int port) {
	int		ret = FAIL;
	struct addrinfo	*ai = NULL, hints;
	struct addrinfo	*ai_bind = NULL;
	char		service[8], *error = NULL;
	
	zbx_snprintf(service, sizeof(service), "%hu", port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	if (0 != getaddrinfo(ip, service, &hints, &ai))
	{
		zabbix_log(LOG_LEVEL_DEBUG,"cannot resolve %s", ip);
		goto out;
	}

	if (ZBX_SOCKET_ERROR == (*sock = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol))) {
		zabbix_log(LOG_LEVEL_DEBUG,"cannot create socket [[%s]:%hu]", ip, port);
		goto out;
	}

	if (NULL != source_ip)
	{
		memset(&hints, 0x00, sizeof(struct addrinfo));

		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;

		if (0 != getaddrinfo(source_ip, NULL, &hints, &ai_bind))
		{
			zabbix_log(LOG_LEVEL_WARNING," invalid source IP address '%s'",source_ip);
			close(*sock);
			goto out;
		}

		if (ZBX_PROTO_ERROR == bind(*sock, ai_bind->ai_addr, ai_bind->ai_addrlen))
		{
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't bind ip to the source ip %s",source_ip);
			close(*sock);
			goto out;
		}
	}

	if (ZBX_PROTO_ERROR == connect(*sock, ai->ai_addr, (socklen_t)ai->ai_addrlen)) {

		if (EINPROGRESS == errno) {
			ret = SUCCEED;
		} else {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't start connection on the socket %d",errno);
			close(*sock);
		}
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != ai)
		freeaddrinfo(ai);

	if (NULL != ai_bind)
		freeaddrinfo(ai_bind);

	return ret;
}
#else 
static int glb_async_tcp_connect(int *sock, const char *source_ip, const char *ip, unsigned int port) {

	ZBX_SOCKADDR	servaddr_in;
	struct addrinfo	hints, *ai;
	char		*error = NULL;
	int true = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;


	if (0 != getaddrinfo(ip, NULL, &hints, &ai)) {
		zabbix_log(LOG_LEVEL_DEBUG,"Getaddrinfo failed for name: %s",ip);
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
	servaddr_in.sin_port = htons(port);

	freeaddrinfo(ai);
	
	if (ZBX_SOCKET_ERROR == (*sock = socket(AF_INET, SOCK_STREAM| SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
	{
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't exec gettraddrinfo");
		return FAIL;
	}

	setsockopt(*sock,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));

	if (NULL != source_ip)
	{
		ZBX_SOCKADDR	source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(source_ip);
		source_addr.sin_port = 0;

		if (ZBX_PROTO_ERROR == bind(*sock, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't bind ip to the source ip %s",source_ip);
			close(*sock);
			return FAIL;
		}
	}

	if (ZBX_PROTO_ERROR == connect(*sock, (struct sockaddr *)&servaddr_in, sizeof(servaddr_in))) {
		if (EINPROGRESS == errno) 
			return SUCCEED; 
		
		close(*sock);
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't start connection on the socket %d",errno);
		return FAIL;
	}
	return SUCCEED;
}
#endif

/******************************************************************************
 * starts a connection for the session 										  * 
 * ***************************************************************************/
static int agent_start_connection(agent_connection_t *conn)
{
	poller_item_t *poller_item;

	u_int64_t itemid;

	int ret = SUCCEED;

	zbx_timespec_t timespec;
	char error_str[MAX_STRING_LEN];
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Start, idx is %d", __func__, conn->idx);
	
	zbx_timespec(&timespec);

	if ( POLL_FREE != conn->state )  return FAIL;

	if (SUCCEED != zbx_list_peek(&conn->items_list, (void **)&itemid)) {
		 //DEBUG_ITEM(itemid,"Not starting the connection right now, busy with another item");
		 return FAIL;
	}

	//finding the item, as we use weak linking, an item might be cleaned, then it's ok, we just have to pick a next one
	while (NULL == (poller_item = poller_get_poller_item(itemid)) ) {
		zabbix_log(LOG_LEVEL_WARNING,"Coudln't find item with id %ld in the items hashset", itemid);
	
		//no such item anymore, pop it, and call myself to start the next item
		zbx_list_pop(&conn->items_list, (void **)&itemid);
		zbx_hashset_remove(&conf.items_idx,&itemid);
		DEBUG_ITEM(itemid,"Popped item doesn't exists in the items");
		
		//this will init next item fetch
		agent_start_connection(conn);
		
		return SUCCEED;
	} 
	
	DEBUG_ITEM(itemid, "Fetched item from the list to be polled");

	zbx_list_pop(&conn->items_list, NULL);
	zbx_hashset_remove(&conf.items_idx, &itemid);

	agent_item_t *agent_item = poller_get_item_specific_data(poller_item);	
	
	//starting the new connection
	conn->finish_time = time(NULL) + CONFIG_TIMEOUT + 1; 
	
	DEBUG_ITEM(itemid, "Sending AGENT request");
	

	//cheick if the item is agent type
	zabbix_log(LOG_LEVEL_TRACE, "In %s()  addr:'%s' key:'%s'", __func__,
			   agent_item->interface_addr, agent_item->key);

	//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld connecting ",conf.items[itemid].itemid);
	
	if (SUCCEED != (ret = glb_async_tcp_connect(&conn->socket, CONFIG_SOURCE_IP, agent_item->interface_addr, agent_item->interface_port)))
	{
		DEBUG_ITEM(itemid, "Failed to send syn for item, coudn't connect or create socket");
		close(conn->socket);
		
		poller_preprocess_value(poller_item, NULL,  glb_ms_time(), ITEM_STATE_NOTSUPPORTED, "Couldn't create socket, check if hostname could be resolved by DNS");
		
		return SUCCEED;
	}

	conn->current_item = poller_get_item_id(poller_item);
	conn->state = POLL_CONNECT_SENT;
	conn->finish_time = time(NULL) + CONFIG_TIMEOUT + 1;

	poller_inc_requests();
		
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return SUCCEED;
}



void static glb_agent_handle_timeout(agent_connection_t *conn) {
	
		poller_item_t *poller_item, *glb_next_item;
		u_int64_t mstime = glb_ms_time();
		u_int64_t itemid;
		char error_str[MAX_STRING_LEN];
		
		if ( POLL_FREE == conn->state ||  conn->finish_time > time(NULL)) 
			return;
			

		if (NULL == (poller_item = poller_get_poller_item(conn->current_item))) 
			return;

		itemid = poller_get_item_id(poller_item);
		
		poller_register_item_timeout(poller_item);
		poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Timed out waiting for the responce" );

		//freeing up the connections
		conn->state = POLL_FREE; //this will make it possible to reuse the connection
		close(conn->socket);
		conn->current_item = 0;
		
		poller_return_item_to_queue(poller_item);
				
		zbx_hashset_remove(&conf.items_idx,&itemid);
		
		DEBUG_ITEM(itemid, "Agent connection timeout");

		//handling retries
		if ( SUCCEED == poller_if_host_is_failed(poller_item) ) {
			DEBUG_ITEM(itemid, "Doing local queue cleanup due to too many timed items for host %ld", poller_get_host_id(poller_item));

			while (SUCCEED == zbx_list_peek(&conn->items_list, (void **)&itemid) &&
	   			( NULL != (glb_next_item = poller_get_poller_item(itemid))) &&  
	   			( poller_get_host_id(glb_next_item) ==  poller_get_host_id(poller_item)) ) {

					zbx_list_pop(&conn->items_list, (void **)&itemid);
					poller_return_item_to_queue(glb_next_item);

					zbx_snprintf(error_str,MAX_STRING_LEN,"Skipped from polling due to %d items timed out in a row, last failed item id is %ld", 
										GLB_FAIL_COUNT_CLEAN, poller_get_item_id(poller_item));
					
					zbx_hashset_remove(&conf.items_idx,&itemid);

					LOG_DBG("host %ld item %ld timed out %s", poller_get_host_id(poller_item), itemid, error_str);
					
					DEBUG_ITEM(itemid, "Agent cleaned without polling due to host not answering to 6 requests in the row")
					poller_preprocess_value(glb_next_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, error_str );
		}
	}
		
	zabbix_log(LOG_LEVEL_DEBUG, "%s: Finished", __func__);
	return;
}

/*******************************************************************
 * handles working with socket - waiting for ack, sending request, *
 * waiting for the response										   *
 * *****************************************************************/
void handle_socket_operations(agent_connection_t *conn)
{
    //u_int64_t itemid = conn->current_item;
	int ret, result, count, received_len = 0;
	socklen_t result_len = sizeof(result);
	u_int64_t mstime = glb_ms_time();
	char err_str[MAX_STRING_LEN];
	zbx_socket_t tmp_s;

	poller_item_t *poller_item;
	agent_item_t *agent_item;

	if (NULL == (poller_item = poller_get_poller_item(conn->current_item)))
		return;
	agent_item = poller_get_item_specific_data(poller_item);
	
	bzero(&tmp_s,sizeof(zbx_socket_t));
	
	tmp_s.socket = conn->socket;
	tmp_s.buffer = tmp_s.buf_stat;
	
	switch (conn->state) {
		case POLL_FREE: 
			return;
		case POLL_CONNECT_SENT:
			ret = getsockopt(conn->socket, SOL_SOCKET, SO_ERROR, &result, &result_len);
			if (0 > ret)
				return;

			if (0 != result) {
			
			poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, 
					"Connection to the host failed: check firewall rules and agent is running");
			
			/*note: socket's timeout is system-based and differs from the timeout
			    so fixing finish time to be sure that timeout processing will work */
			conn->finish_time = time(NULL) - 1;
			glb_agent_handle_timeout(conn);
			
			return;
		}
		
		//by this moment we can send the tcp request
		DEBUG_ITEM(poller_get_item_id(poller_item), "Agent item sending request key %s:", agent_item->key);
		
		if (SUCCEED == zbx_tcp_send(&tmp_s, agent_item->key)) {
			//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld sending request completed ",conf.items[itemid].itemid);
			conn->state = POLL_REQ_SENT;
			return;	
		} else 	{ 
			//it might be that the socket still not ready, if so, we just have to wait  a bit
			//on next call of handle op's (probably) the socket will be ready
			if (EAGAIN == zbx_socket_last_error())	{
				DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't send request, socket isn't ready yet");
				//zabbix_log(LOG_LEVEL_TRACE, "Socket isn't ready yet for item %ld", poller_item->itemid);
				return;
			}
			
			conn->state = POLL_FREE;
			close(conn->socket);

			poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Cannot send request to the agent" );
		}

		break;
		
		case POLL_REQ_SENT:
			//checking if there are some data waiting for us in the socket
			ioctl(conn->socket, FIONREAD, &count);
			if (0 == count)
				return;
		
			if (FAIL != (received_len = zbx_tcp_recv_ext(&tmp_s, 0, 0))) {
			
				//zabbix_log(LOG_LEVEL_INFORMATION, "get value from agent result: '%s'", conn->socket->buffer);

				zbx_rtrim(tmp_s.buffer, " \r\n");
				zbx_ltrim(tmp_s.buffer, " ");

				if (0 == strcmp(tmp_s.buffer, ZBX_NOTSUPPORTED)) {
				/* 'ZBX_NOTSUPPORTED\0<error message>' */
				
					if (sizeof(ZBX_NOTSUPPORTED) < tmp_s.read_bytes)
						poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, tmp_s.buffer + sizeof(ZBX_NOTSUPPORTED) );	
					else
						poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Not supported by Zabbix Agent" );

				} else if (0 == strcmp(tmp_s.buffer, ZBX_ERROR)) {
					poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Zabbix Agent non-critical error");
				} else if (0 == received_len) {
					poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, 
							"Received empty response from Zabbix Agent, Assuming that agent dropped connection because of access permissions.");
				} else 	{
					AGENT_RESULT result;
					
					init_result(&result);
    
	   				set_result_type(&result, ITEM_VALUE_TYPE_TEXT, tmp_s.buffer);
	
					poller_preprocess_value(poller_item , &result , mstime, ITEM_STATE_NORMAL, NULL);
				 	poller_register_item_succeed(poller_item);
					
    				free_result(&result);

					zabbix_log(LOG_LEVEL_DEBUG, "Agent item %ld data parsed, type os set, resp is: %s", poller_get_item_id(poller_item), tmp_s.buffer);
					DEBUG_ITEM(poller_get_item_id(poller_item), "Arrived agent response: ts: %ld, val: %s", mstime, tmp_s.buffer);
				}
			} else {
				//	zabbix_log(LOG_LEVEL_DEBUG, "Get value from agent failed: %s", zbx_socket_strerror());
				zbx_snprintf(err_str,MAX_STRING_LEN,"Get value from agent failed: %s", zbx_socket_strerror());
				poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, err_str);
			}

			poller_inc_responces();
			
			zbx_tcp_close(&tmp_s);	
			close(conn->socket);
			conn->socket = 0;
			
			conn->state = POLL_FREE;
			poller_return_item_to_queue(poller_item);

			break;
		
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			zabbix_log(LOG_LEVEL_WARNING,"Called handle socket operations in unexpected state: %d", conn->state);
			
	}
		
}

static int agent_init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
	zbx_timespec_t timespec;
	
	const char *interface_addr;
	unsigned int interface_port;
	const char *key; 
	
	agent_item_t *agent_item;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
	agent_item = zbx_calloc(NULL,0,sizeof(agent_item_t));

	poller_set_item_specific_data(poller_item,agent_item);
	
	agent_item->interface_addr = poller_strpool_add(dc_item->interface.addr);
	agent_item->key = poller_strpool_add(dc_item->key);
	agent_item->interface_port = dc_item->interface.port;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
	return SUCCEED;
}

static void agent_free_item(poller_item_t *poller_item ) {
	
	agent_item_t *agent_item = poller_get_item_specific_data(poller_item);

	poller_strpool_free(agent_item->interface_addr);
	poller_strpool_free(agent_item->key);
	zbx_free(agent_item);

}

/*************************************************************
 * config, connections and lists cleanup		             *
 * ***********************************************************/
static void agent_shutdown(void) {
	
	int i;
	struct list_item *litem, *tmp_litem;
	zbx_timespec_t timespec;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

	zbx_timespec(&timespec);
		
	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		close(conf.conns[i].socket);
		zbx_list_destroy(&conf.conns[i].items_list);
	}
	zbx_hashset_destroy(&conf.items_idx);
	
	//items and hosts has to be freed by poller layer
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/******************************************************************************
 * adds item to the polling list of a conenction							  * 
 * ***************************************************************************/
static void   agent_add_poll_item(poller_item_t *poller_item) {
	
	agent_item_t *agent_item = poller_get_item_specific_data(poller_item);
	u_int64_t itemid = poller_get_item_id(poller_item);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);

	int idx = poller_get_host_id(poller_item) % GLB_MAX_AGENT_CONNS;

	//checking if the item is still in the poller to not put it once again
	if (NULL != zbx_hashset_search(&conf.items_idx, &itemid)) {
		zabbix_log(LOG_LEVEL_DEBUG, "Item %ld is still in the list, not adding to polling again", itemid);
		DEBUG_ITEM(itemid, "Item is already in the poll list, not adding to the list");
		DEBUG_ITEM(itemid, "Current connection state is %d, current item is %ld", conf.conns[idx].state, conf.conns[idx].current_item );
		
		return;
	}

	zbx_list_append(&conf.conns[idx].items_list, (void **)itemid, NULL);
	zbx_hashset_insert(&conf.items_idx,&itemid, sizeof(itemid));

	DEBUG_ITEM(itemid,"Added to list, and items index starting connection");
	agent_start_connection(&conf.conns[idx]);
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/********************************************************
 * general func to handle states, starts, terminates on *
 * timeout or pass control to the socket state handling *
 * ******************************************************/
static void agent_handle_async_io(void) {
	int i;

	LOG_DBG("In %s: Started", __func__);
	
	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		agent_connection_t *conn = &conf.conns[i];
				
		agent_start_connection(conn);
		handle_socket_operations(conn);
		glb_agent_handle_timeout(conn);
	}
	LOG_DBG("In %s: Ended", __func__);
	//not nice solution, maybe it's better to switch to 
	//select and use proper waiting
	usleep(100000);

}

static int forks_count(void) {
	return CONFIG_GLB_AGENT_FORKS;
}

/******************************************
 * config, connections, poll lists init   *
 * ****************************************/
int  glb_agent_init(void) {
		
	int i;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	poller_set_poller_callbacks(agent_init_item, agent_free_item, agent_handle_async_io, agent_add_poll_item, agent_shutdown, forks_count, NULL);
		
	zbx_hashset_create(&conf.items_idx, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		conf.conns[i].state = POLL_FREE;
		conf.conns[i].current_item=0;
		conf.conns[i].idx=i;
	
		zbx_list_create(&conf.conns[i].items_list);
	}
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return SUCCEED;
}

