/*********************************************************
 * this is one more implementation of agent async polling
 *
 * the copyright
 * the fun
 * the result
 * *******************************************************/
#include "common.h"
#include "glb_agent.h"
#include "dbcache.h"
#include "log.h"
#include "preproc.h"

extern char *CONFIG_SOURCE_IP;

/******************************************************************************
 * initiates async tcp connection    										  * 
 * ***************************************************************************/
#ifdef HAVE_IPV6
int glb_async_tcp_connect(int *sock, const char *source_ip, const char *ip, unsigned int port) {
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
int glb_async_tcp_connect(int *sock, const char *source_ip, const char *ip, unsigned int port) {

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
static int glb_agent_start_connection(GLB_ASYNC_AGENT_CONF *conf,  GLB_ASYNC_AGENT_CONNECTION *conn)
{
	GLB_POLLER_ITEM *glb_poller_item;

	//char *tls_arg1, *tls_arg2;
	u_int64_t itemid;

	int ret = SUCCEED;

	zbx_timespec_t timespec;
	char error_str[MAX_STRING_LEN];
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Start, idx is %d", __func__, conn->idx);
	
	zbx_timespec(&timespec);

	if ( POLL_FREE != conn->state )  return FAIL;

	if (SUCCEED != zbx_list_peek(&conn->items_list, (void **)&itemid)) {
		 DEBUG_ITEM(itemid,"Not starting the connection right now, busy with another item");
		 return FAIL;
	}

	//finding the item, as we use weak linking, an item might be cleaned, then it's ok, we just have to pick a next one
	while (NULL == (glb_poller_item =(GLB_POLLER_ITEM *)zbx_hashset_search(conf->items, &itemid)) ) {
		zabbix_log(LOG_LEVEL_WARNING,"Coudln't find item with id %ld in the items hashset", itemid);
	
		//no such item anymore, pop it, and call myself to start the next item
		zbx_list_pop(&conn->items_list, (void **)&itemid);
		zbx_hashset_remove(&conf->lists_idx,&itemid);
		DEBUG_ITEM(itemid,"Popped item doesn't exists in the items");
		
		//this will init next item fetch
		glb_agent_start_connection(conf, conn);
		
		return SUCCEED;
	} 
	
	DEBUG_ITEM(glb_poller_item->itemid,"Fetched item from the list to be polled");

	zbx_list_pop(&conn->items_list, NULL);
	zbx_hashset_remove(&conf->lists_idx,&glb_poller_item->itemid);

	GLB_AGENT_ITEM *glb_agent_item = (GLB_AGENT_ITEM *)glb_poller_item->itemdata;	
	
	//starting the new connection
	conn->finish_time = time(NULL) + CONFIG_TIMEOUT + 1; 
	
	DEBUG_ITEM(glb_poller_item->itemid, "Sending AGENT request");
	

	//cheick if the item is agent type
	zabbix_log(LOG_LEVEL_TRACE, "In %s()  addr:'%s' key:'%s'", __func__,
			   glb_agent_item->interface_addr, glb_agent_item->key);

	//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld connecting ",conf.items[itemid].itemid);
	
	if (SUCCEED != (ret = glb_async_tcp_connect(&conn->socket, CONFIG_SOURCE_IP, glb_agent_item->interface_addr, glb_agent_item->interface_port)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Failed to send syn for item %ld, coudn't connect or create socket",glb_poller_item->itemid);
		close(conn->socket);
		
		zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Couldn't create socket, check if hostname could be resolved by DNS");
		
		return SUCCEED;
	}

	conn->current_item = glb_poller_item->itemid;
	conn->state = POLL_CONNECT_SENT;
	conn->finish_time = time(NULL) + CONFIG_TIMEOUT + 1;

	*conf->requests += 1;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return SUCCEED;
}



void static glb_agent_handle_timeout(GLB_ASYNC_AGENT_CONF *conf, GLB_ASYNC_AGENT_CONNECTION *conn) {
	
		GLB_POLLER_ITEM *glb_poller_item, *glb_next_item;
		zbx_timespec_t timespec;
		u_int64_t itemid;
		char error_str[MAX_STRING_LEN];
		
		if ( POLL_FREE == conn->state ||  conn->finish_time > time(NULL)) 
			return;
			
		if (NULL == (glb_poller_item = (GLB_POLLER_ITEM *)zbx_hashset_search(conf->items,&conn->current_item))) 
			return;
		itemid = glb_poller_item->itemid;
		
		zbx_timespec(&timespec);
		add_host_fail(conf->hosts,glb_poller_item->hostid,timespec.sec);
		
		zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Timed out waiting for the responce" );

		//freeing up the connections
		conn->state = POLL_FREE; //this will make it possible to reuse the connection
		close(conn->socket);
		glb_poller_item->state = POLL_QUEUED;
		zbx_hashset_remove(&conf->lists_idx,&glb_poller_item->itemid);
		
		DEBUG_ITEM(glb_poller_item->itemid, "Agent connection timeout");

		//handling retries
		if ( SUCCEED == host_is_failed(conf->hosts, glb_poller_item->hostid, timespec.sec) ) {
			zabbix_log(LOG_LEVEL_DEBUG, "Doing local queue cleanup due to too many timed items for host %ld", glb_poller_item->hostid);

			while (SUCCEED == zbx_list_peek(&conn->items_list, (void **)&itemid) &&
	   			( NULL != (glb_next_item = zbx_hashset_search(conf->items, &itemid))) &&  
	   			( glb_next_item->hostid == glb_poller_item->hostid) ) {

					zbx_list_pop(&conn->items_list, (void **)&itemid);
					glb_next_item->state = POLL_QUEUED; //indicate we're not processing the item anymore

					zbx_snprintf(error_str,MAX_STRING_LEN,"Skipped from polling due to %d items timed out in a row, last failed item id is %ld", 
										GLB_FAIL_COUNT_CLEAN, glb_poller_item->itemid);
					zbx_hashset_remove(&conf->lists_idx,&glb_next_item->itemid);

					zabbix_log(LOG_LEVEL_DEBUG, "host %ld item %ld timed out %s", glb_poller_item->hostid,  glb_next_item->itemid, error_str);
					DEBUG_ITEM(glb_poller_item->itemid, "Agent cleaned without polling due to host not answering to 6 requests in the row")
					zbx_preprocess_item_value(glb_next_item->hostid, glb_next_item->itemid, glb_next_item->value_type, glb_next_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, error_str );
		}
	}
		
	zabbix_log(LOG_LEVEL_DEBUG, "%s: Finished", __func__);
	return;
}

/*******************************************************************
 * handles working with socket - waiting for ack, sending request, *
 * waiting for the response										   *
 * *****************************************************************/
void handle_socket_operations(GLB_ASYNC_AGENT_CONF *conf, GLB_ASYNC_AGENT_CONNECTION *conn)
{
    //u_int64_t itemid = conn->current_item;
	int ret, result, count, received_len = 0;
	socklen_t result_len = sizeof(result);
	zbx_timespec_t timespec;
	char err_str[MAX_STRING_LEN];
	zbx_socket_t tmp_s;

	GLB_POLLER_ITEM *glb_poller_item;
	GLB_AGENT_ITEM *glb_agent_item;

	if (NULL == (glb_poller_item =(GLB_POLLER_ITEM *)zbx_hashset_search(conf->items, &conn->current_item)))
		return;
	glb_agent_item = (GLB_AGENT_ITEM*)glb_poller_item->itemdata;

	zbx_timespec(&timespec);
	
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
			
			zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Connection to the host failed: check firewall rules and agent is running");

			close(conn->socket);
			conn->state = POLL_FREE;
			conn->current_item = -1;
			return;
		}
		//by this moment we can send the tcp request
		
		zabbix_log(LOG_LEVEL_DEBUG,"Agent item %ld sending request key %s:",
				glb_poller_item->itemid, glb_agent_item->key);
		
		if (SUCCEED == zbx_tcp_send(&tmp_s, glb_agent_item->key)) {
			//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld sending request completed ",conf.items[itemid].itemid);
			conn->state = POLL_REQ_SENT;
			return;	
		} else 	{ 
			//it might be that the socket still not ready, if so, we just have to wait  a bit
			//on next call of handle op's (probably) the socket will be ready
			if (EAGAIN == zbx_socket_last_error())	{
				zabbix_log(LOG_LEVEL_TRACE, "Socket isn't ready yet for item %ld", glb_poller_item->itemid);
				return;
			}
			
			conn->state = POLL_FREE;
			close(conn->socket);

			zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Cannot send request to the agent" );

			//zabbix_log(LOG_LEVEL_INFORMATION, "Agent item %ld data send failed, aborted ", glb_poller_item->itemid);
		}

		break;
		
		case POLL_REQ_SENT:
			//checking if there are some data waiting for us in the socket
			ioctl(conn->socket, FIONREAD, &count);
			if (0 == count)
				return;
		
			if (FAIL != (received_len = zbx_tcp_recv_ext(&tmp_s, 0))) {
			
				//zabbix_log(LOG_LEVEL_INFORMATION, "get value from agent result: '%s'", conn->socket->buffer);

				zbx_rtrim(tmp_s.buffer, " \r\n");
				zbx_ltrim(tmp_s.buffer, " ");

				if (0 == strcmp(tmp_s.buffer, ZBX_NOTSUPPORTED)) {
				/* 'ZBX_NOTSUPPORTED\0<error message>' */
				
					if (sizeof(ZBX_NOTSUPPORTED) < tmp_s.read_bytes)
						zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, tmp_s.buffer + sizeof(ZBX_NOTSUPPORTED) );
						
					else
						zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Not supported by Zabbix Agent" );
				} else if (0 == strcmp(tmp_s.buffer, ZBX_ERROR)) {
					zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Zabbix Agent non-critical error");
				} else if (0 == received_len) {
					zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, "Received empty response from Zabbix Agent, Assuming that agent dropped connection because of access permissions.");
				} else 	{
					AGENT_RESULT result;
					
					init_result(&result);
    
	   				set_result_type(&result, ITEM_VALUE_TYPE_TEXT, tmp_s.buffer);
	
					zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, 
                                                glb_poller_item->flags , &result , &timespec, ITEM_STATE_NORMAL, NULL);
    				free_result(&result);

					zabbix_log(LOG_LEVEL_DEBUG, "Agent item %ld data parsed, type os set, resp is: %s", glb_poller_item->itemid, tmp_s.buffer);
					DEBUG_ITEM(glb_poller_item->itemid,"Arrived agent response");
				}
			} else {
				//	zabbix_log(LOG_LEVEL_DEBUG, "Get value from agent failed: %s", zbx_socket_strerror());
				zbx_snprintf(err_str,MAX_STRING_LEN,"Get value from agent failed: %s", zbx_socket_strerror());
				zbx_preprocess_item_value(glb_poller_item->hostid, glb_poller_item->itemid, glb_poller_item->value_type, glb_poller_item->flags ,
									NULL , &timespec, ITEM_STATE_NOTSUPPORTED, err_str);
			}

<<<<<<< HEAD
			*conf->responses += 1;	
			zbx_tcp_close(&tmp_s);
=======
			*conf->responses += 1;
			zbx_tcp_close(&tmp_s);	
>>>>>>> 5_4_sources
			close(conn->socket);
			conn->socket = 0;
			conn->state = POLL_FREE;
			glb_poller_item->state = POLL_QUEUED;
			
			break;
		
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			zabbix_log(LOG_LEVEL_WARNING,"Called handle socket operations in unexpected state: %d", conn->state);
			
	}
		
}

/*********************************************************
 * init and free item, key and interface addr strpooled  *
**********************************************************/
unsigned int glb_agent_init_item(DC_ITEM *dc_item, GLB_AGENT_ITEM *glb_agent_item) {
  	zbx_timespec_t timespec;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	const char *interface_addr;
	unsigned int interface_port;
	const char *key; //item key value (strpooled)

	glb_agent_item->interface_addr = zbx_heap_strpool_intern(dc_item->interface.addr);
	glb_agent_item->key = zbx_heap_strpool_intern(dc_item->key);
	glb_agent_item->interface_port = dc_item->interface.port;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
	return SUCCEED;
}

void    glb_agent_free_item(GLB_AGENT_ITEM *glb_agent_item ) {
	
	zbx_heap_strpool_release(glb_agent_item->interface_addr);
	zbx_heap_strpool_release(glb_agent_item->key);

}

/*************************************************************
 * config, connections and lists cleanup		             *
 * ***********************************************************/
void    glb_agent_shutdown(void *engine) {
	
	int i;
	struct list_item *litem, *tmp_litem;
	GLB_ASYNC_AGENT_CONF *conf = (GLB_ASYNC_AGENT_CONF*)engine;
	GLB_POLLER_ITEM *glb_poller_item;
	zbx_timespec_t timespec;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

	zbx_timespec(&timespec);
		
	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		close(conf->conns[i].socket);
		zbx_list_destroy(&conf->conns[i].items_list);
	}
	zbx_hashset_destroy(&conf->lists_idx);
	zbx_free(conf);

	//items and hosts has to be freed by poller layer
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/******************************************************************************
 * adds item to the polling list of a conenction							  * 
 * ***************************************************************************/
void   glb_agent_add_poll_item(void *engine, GLB_POLLER_ITEM *glb_poller_item) {
	
	GLB_ASYNC_AGENT_CONF *conf = (GLB_ASYNC_AGENT_CONF*) engine;
	GLB_AGENT_ITEM *glb_agent_item = (GLB_AGENT_ITEM*) glb_poller_item->itemdata;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);

	//checking if the item is still in the poller to not put it once again
	if (NULL != zbx_hashset_search(&conf->lists_idx,&glb_poller_item->itemid)) {
		zabbix_log(LOG_LEVEL_DEBUG, "Item %ld is still in the list, not adding to polling again",glb_poller_item->itemid);
		DEBUG_ITEM(glb_poller_item->itemid, "Item is still waiting to be polled, not adding to the list");
		return;
	}

	int idx=glb_poller_item->hostid % GLB_MAX_AGENT_CONNS;

	zbx_list_append(&conf->conns[idx].items_list, (void **)glb_poller_item->itemid, NULL);
	zbx_hashset_insert(&conf->lists_idx,&glb_poller_item->itemid, sizeof(glb_poller_item->itemid));

	DEBUG_ITEM(glb_poller_item->itemid,"Added to list, and items index starting connection");
	glb_agent_start_connection(conf, &conf->conns[idx]);
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/******************************************
 * config, connections, poll lists init   *
 * ****************************************/
void    *glb_agent_init(zbx_hashset_t *items, zbx_hashset_t *hosts, int *requests, int *responses ) {
		
	int i;
	static GLB_ASYNC_AGENT_CONF *engine;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
	if (NULL == (engine = zbx_malloc(NULL,sizeof(GLB_ASYNC_AGENT_CONF))))  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}
	
	memset(engine, 0, sizeof(GLB_ASYNC_AGENT_CONF));

	engine->items = items;
	engine->hosts = hosts;
	engine->requests = requests;
	engine->responses = responses;
	
	zbx_hashset_create(&engine->lists_idx, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		engine->conns[i].state = POLL_FREE;
		engine->conns[i].current_item=-1;
		engine->conns[i].idx=i;
	
		zbx_list_create(&engine->conns[i].items_list);
	}
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return (void *)engine;
}

/********************************************************
 * general func to handle states, starts, terminates on *
 * timeout or pass control to the socket state handling *
 * ******************************************************/
void    glb_agent_handle_async_io(void *engine) {
	int i;
	GLB_ASYNC_AGENT_CONF *conf = (GLB_ASYNC_AGENT_CONF *)engine;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);
	for (i = 0; i < GLB_MAX_AGENT_CONNS; i++)
	{
		GLB_ASYNC_AGENT_CONNECTION *conn = &conf->conns[i];
				
		glb_agent_start_connection(conf,conn);
		handle_socket_operations(conf, conn);
		glb_agent_handle_timeout(conf, conn);
	}
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);

}

