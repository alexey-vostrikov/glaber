/*
** Glaber
** Copyright (C) 2001-2038 Glaber 
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


/* this is async specific implementation calls of snmp functions 
   most of snmp proto-specific calls are used form the original 
   zabbix src/zabbix_server/poller/checks_snmp library */

/* async snmp logic: the module has list of connections
   state machine is build around the connections rather than about 
   items like in glb_poller */

#include "glb_poller.h"
#include "log.h"
#include "common.h"
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/library/large_fd_set.h>
#include "../poller/checks_snmp.h"
#include "preproc.h"
#include "csnmp.h"
#include "poller_async_io.h"

#define GLB_MAX_SNMP_CONNS 8192
#define CONFIG_SNMP_RETRIES 2

extern int CONFIG_GLB_SNMP_CONTENTION;
extern int CONFIG_GLB_SNMP_FORKS;

// /*connection-specific data */
// typedef struct {
// 	struct snmp_session *sess; /* SNMP session data */
// 	zbx_int64_t current_item;  /* itemid of currently processing item */
// 	int state;				   
// 	int finish_time;		   /* unix time till we wait for data to come */
// 	int retries;			   /* how many retries already passed */
//     zbx_list_t items_list;     /* list of itemids assigned to the session */
// 	int fd; 				   //socket file descriptor
// 	poller_event_t *fd_event;
// 	poller_event_t *timeout_event;
// } snmp_conn_t;


// typedef struct {

// } iface_info_t;

// typedef struct  {
// 	u_int64_t hostid;
// 	int sent_requests;
// 	zbx_list_t pending_items;
// 	zbx_hashset_t ifaces;
// 	/*this config is host-specific and likely to change */
// } hostinfo_t;

/*engine-specific data */
typedef struct {
//	zbx_hashset_t items_idx; //items that are being polled right now
//	snmp_conn_t *connections; 
//	zbx_vector_ptr_t free_conns;
	
	int socket; //async version uses single socket for the io
	poller_event_t *socket_event;
	zbx_hashset_t hostinfo; 
} async_snmp_conf_t;

static async_snmp_conf_t conf = {0};

typedef struct {
	const char *oid;
	u_int64_t		lastresolved;
	poller_event_t  *timeout_event;
	//todo: do a bunpool of the config except hostname
	unsigned char	snmp_version;
	const char		*interface_addr;
	unsigned char	useip;
	unsigned short	interface_port;
	const char 		*community;


} snmp_item_t;

/* async snmp logic: 
  - when poller request an item poll, item is added to connection list
  - if connection is free, start_connection is immedately called 

   when connection is started: fd watch event is created as well as timeout timer event
   	- timeout timer might resend a packet if needed and charge itself again
   upon recieving data timeout event is cleared, recieved packet is processed
	and if next request belongs to the same host, new request is send immediately
	or socket is closed and start connection called
   
   upon timeout fd watch event is cleared, host timeout registered. On too many timeouts
   connection's list is cleared from the pending items for the same host.
*/
/*
	A few more thoughts:
	Async implementation and UDP makes it's possible to avoid making bunch of sockets at all.

	Instead, single socket might be used for IO for all devices. So there is no need in 
	onject named "connection".

	However, there might be a need to still have lists of tasks per host to 
	a) keep concurency (pressure)
	b) to process things like walks and keep current state of the request
	
	so, object model shifts a bit towards having hosts objects that hold current 
	host interaction. 

	so, there is no such thing as a 'busy' connection, but we have to keep host's 
	business status. 

	And if host is busy, all new tasks should go to the host's pending list (so pending list goes to the host)
	pending list might also be used to make bulk requests as well as (maybe) remembering max bulk size for
	the host
*/


// poller_item_t *get_next_poll_item(snmp_conn_t *conn) {
	
// 	poller_item_t *poller_item = NULL;
	
// 	while (NULL == poller_item) {
// 		u_int64_t itemid;
	
// 		if (FAIL == zbx_list_pop(&conn->items_list, (void **)&itemid)) {
// 			return NULL;
// 		}

// 		poller_item = poller_get_poller_item(itemid);
// 	}

// 	DEBUG_ITEM(poller_get_item_id(poller_item),"Fetched item from the connection's list");
	
// 	return poller_item;
// }


// void close_connection(snmp_conn_t *conn) {
// 	if (0 != conn->fd) 
// 		close(conn->fd);

// 	poller_destroy_event(conn->fd_event);
// 	poller_destroy_event(conn->timeout_event);
// }

/* accepts translated oid i.e where only digits are present: .1.2.3.4.5.6 */
int str_oid_to_asn(const char *str_oid, asn1_oid_t* coid) {
	char tmp_oid[MAX_OID_LEN];
	char *p;
	int i = 0;
	oid p_oid[MAX_OID_LEN];
	size_t oid_len = MAX_OID_LEN;
	
	if (NULL == snmp_parse_oid(str_oid, p_oid, &oid_len)) {
		return FAIL;
	};

	coid->b = zbx_malloc(NULL, sizeof(int) * oid_len);

	for (i = 0; i< oid_len; i++) {
		coid->b[i] = p_oid[i];
	}

	//LOG_INF("Finished converting oid '%s' length %d", str_oid, i);
	coid->len = oid_len;

	return SUCCEED;
}


int create_pdu(poller_item_t *poller_item, csnmp_pdu_t *pdu, const char* ip_addr) {

	static asn1_oid_t oid;
	struct sockaddr_in server;

	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	bzero(pdu, sizeof(csnmp_pdu_t));
	bzero((char*)&server, sizeof(server));
    
	server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip_addr);
    server.sin_port = htons(snmp_item->interface_port);
		
	memcpy(&pdu->addr, &server, sizeof(struct sockaddr_in));
	pdu->addr_len = sizeof(struct sockaddr_in);
	pdu->req_id = poller_get_item_id(poller_item);
	//oid.b = b;

			
	pdu->command = SNMP_CMD_GET;
	//TODO: use strpooled funcs instead, consider in free
	pdu->community.b = zbx_strdup(NULL, snmp_item->community);
	pdu->community.len = strlen(snmp_item->community);
		
	switch (snmp_item->snmp_version)
	{
		case ZBX_IF_SNMP_VERSION_1:
			pdu->version = SNMP_VERSION_1;
		break;
		case ZBX_IF_SNMP_VERSION_2:
			pdu->version = SNMP_VERSION_2c;
		break;
		default:
			LOG_INF("Unsuppoerted SNMP version in async code");
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}

	if (FAIL == str_oid_to_asn(snmp_item->oid, &oid)) {
	 	poller_preprocess_error(poller_item, glb_ms_time(), "Cannot parse oid");
	 	return FAIL;
	}

	//LOG_INF("Adding null OID to the request");
	return csnmp_add_var(pdu, oid, SNMP_TP_NULL, NULL);
}

//void connection_process_next_item(snmp_conn_t *conn) {
	// LOG_INF("In %s() Starting connection ", __func__);
	// poller_item_t *poller_item;
	// static int old_host = 0;
	// csnmp_pdu_t *pdu;

	// //fetching the next pollable item from the connection's list
	// if (NULL == (poller_item = get_next_poll_item(conn))) {
	// 	close_connection(conn);
	// 	return;
	// }

	// if (old_host != poller_get_host_id(poller_item)) {
	// 	close_connection(conn);
	// 	open_snmp_connection(conn, poller_item);
	// }

	// //preparing packet to send, 
	// pdu = prepare_pdu(conn, poller_item);
	// csnmp_send_pdu(conn->)
	// send_pdu(pdu);

	// poller_run_fd_event( conn->fd_event );
	// poller_run_timer_event (conn ->timer_event, SNMP_TIMEOUT);
//}



/******************************************************************************
 * timeout - connection - cleanup 			   								  * 
 * ***************************************************************************/
// static int glb_snmp_handle_timeout(snmp_conn_t *conn) {
// 	int i;
// 	zbx_uint64_t item_idx;
// 	char error_str[MAX_STRING_LEN];
// 	poller_item_t *poller_item, *poller_next_item;
// 	u_int64_t mstime= glb_ms_time();
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

// 	conn->state = POLL_FINISHED;

// 	if (NULL == (poller_item = poller_get_poller_item(conn->current_item))) 
// 	 	return FAIL; 
    
// 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
// 	poller_register_item_timeout(poller_item);
	
// 	zbx_snprintf(error_str,MAX_STRING_LEN, "Timed out, no responce for %d seconds %d retries", CONFIG_TIMEOUT, 2 );
	
// 	poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, error_str );
	
// 	zbx_hashset_remove(&conf.items_idx, &conn->current_item);		
// 	poller_return_item_to_queue(poller_item);
		

// 	if ( SUCCEED == poller_if_host_is_failed(poller_item) ) {
// 		zabbix_log(LOG_LEVEL_DEBUG, "Doing local queue cleanup due to too many timed items for host %ld", poller_get_host_id(poller_item));

// 		while (SUCCEED == zbx_list_peek(&conn->items_list, (void **)&item_idx) &&
// 	   		( NULL != (poller_next_item = poller_get_poller_item(item_idx))) &&  
// 	   		(  poller_get_host_id(poller_next_item) == poller_get_host_id(poller_item)) ) {

// 			zbx_list_pop(&conn->items_list, (void **)&item_idx);

// 			poller_return_item_to_queue(poller_next_item);
// 			zbx_snprintf(error_str,MAX_STRING_LEN,"Skipped from polling due to %d items timed out in a row, last failed item id is %ld", 
// 							GLB_FAIL_COUNT_CLEAN, poller_get_item_id(poller_item));

// 			zbx_hashset_remove(&conf.items_idx,&item_idx);

// 			zabbix_log(LOG_LEVEL_DEBUG, "host %ld item %ld timed out %s", poller_get_host_id(poller_item), item_idx, error_str);
// 			poller_preprocess_value(poller_next_item , NULL , mstime, ITEM_STATE_NOTSUPPORTED, error_str );
// 		}
// 	}
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
// }

// static void register_finished_conn(snmp_conn_t *conn) {
// 	zbx_vector_ptr_append(&conf.free_conns, conn);
// }

/******************************************************************************
 * callback that will be called on async data arrival						  *
 * ***************************************************************************/
// static int glb_snmp_callback(int operation, struct snmp_session *sp, int reqid,
// 					struct snmp_pdu *pdu, void *magic)
// {
// 	AGENT_RESULT result;
// 	poller_item_t *poller_item;
//     u_int64_t mstime=glb_ms_time();

// 	snmp_conn_t *conn = magic;
// 	//async_snmp_conf_t *conf = ( async_snmp_conf_t*)conn->conf;

// 	struct snmp_pdu *req;
// 	struct variable_list *var;

// 	unsigned char snmp_type;

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

// 	var = pdu->variables;


// 	conn->state = POLL_FINISHED;

//     if (NULL == (poller_item = poller_get_poller_item(conn->current_item))) {
//         DEBUG_ITEM(conn->current_item,"Responce for aged/deleted item has arrived, ignoring");
// 		return SUCCEED;
//     }

// 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

//     poller_inc_responces();
// 	poller_return_item_to_queue(poller_item);

// 	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		
// 		if (SNMP_ERR_NOERROR == pdu->errstat)
// 		{  
// 			while (var)
// 			{	int ret;

// 				init_result(&result);
				
// 				if (SUCCEED == zbx_snmp_set_result(var, &result, &snmp_type)) {
			
// 					DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP SUCCEED RESULT processing for the item");
// 					poller_preprocess_value(poller_item, &result ,mstime, ITEM_STATE_NORMAL, NULL);
// 					poller_register_item_succeed(poller_item);
// 				} else {
// 					DEBUG_ITEM(poller_get_item_id(poller_item), "Async SNMP FAILED RESULT processing for the item: %s", result.msg );

// 					poller_preprocess_value(poller_item, NULL, mstime,ITEM_STATE_NOTSUPPORTED, result.msg);
// 				}
						
// 				var = var->next_variable;
// 				free_result(&result);
// 			}
			
// 		} else {
// 		//	zabbix_log(LOG_LEVEL_INFORMATION,"Connection sconn%d got FAIL responce",conn->idx);
// 			poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Cannot parse response pdu");
// 		}
// 	} else {
// 			DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP responce TIMEOUT event")	
// 			glb_snmp_handle_timeout(conn);
// 	}

// 	//add connection to the list of finished to restart right after 
// 	//exit from snmp_read cycle
// 	register_finished_conn(conn);
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
// 	return 1;
// }	


static void poll_timeout_cb(poller_item_t *poller_item, void* data) {
	
	poller_register_item_timeout(poller_item);
	poller_return_item_to_queue(poller_item);
	poller_preprocess_error(poller_item, glb_ms_time(), "Timeout waiting for the responce");

}


/******************************************************************************
 * item init - from the general dc_item to compact and specific snmp		  * 
 * ***************************************************************************/
static int init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
	
	char translated_oid[4*MAX_OID_LEN];
	u_int64_t now = glb_ms_time();

	zbx_timespec_t timespec;
	snmp_item_t *snmp_item;
	
	char error_str[1024];

	LOG_DBG("In %s: starting", __func__);
	snmp_item = zbx_calloc(NULL, 0, sizeof(snmp_item_t));

    poller_set_item_specific_data(poller_item, snmp_item);
	
	if (NULL != dc_item->snmp_oid && dc_item->snmp_oid[0] != '\0')
			zbx_snmp_translate(translated_oid, dc_item->snmp_oid, sizeof(translated_oid));
	else
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Empty oid %s for item ",translated_oid);
		poller_preprocess_error(poller_item, now, "Error: empty OID, item will not be polled until updated, fix config to start poll");

		return FAIL;
	}
	snmp_item->oid = poller_strpool_add(translated_oid);
	
	snmp_item->snmp_version = dc_item->snmp_version;
	snmp_item->interface_port = dc_item->interface.port;
	snmp_item->useip = dc_item->interface.useip;
    snmp_item->interface_addr = poller_strpool_add(dc_item->interface.addr);
	snmp_item->community = poller_strpool_add(dc_item->snmp_community);

	snmp_item->timeout_event = poller_create_event(poller_item, poll_timeout_cb, 0, NULL);

	LOG_DBG("In %s() Ended", __func__);
	return SUCCEED;
}

static void free_item(poller_item_t *poller_item ) {
	
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	poller_strpool_free(snmp_item->oid);
	
	poller_strpool_free(snmp_item->interface_addr);
	poller_strpool_free(snmp_item->community);
	
	
	poller_destroy_event(snmp_item->timeout_event);

	zbx_free(snmp_item);
		
	LOG_DBG( "In %s() Ended", __func__);
}

/******************************************************************************
 * wrap function to use zbx snmp conn										  * 
 * ***************************************************************************/
// static struct snmp_session * glb_snmp_open_conn(poller_item_t *poller_item){
// 	DC_ITEM dc_item = {0};
// 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
// 	char error[MAX_STRING_LEN];
// 	struct snmp_session *sess;
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

// 	//theese are the necessary dc_item fields to make a connection:
// 	dc_item.snmp_version = snmp_item->snmp_version;
// 	dc_item.interface.addr = (char *)snmp_item->interface_addr;
// 	dc_item.interface.port = snmp_item->interface_port;
// 	dc_item.interface.useip = snmp_item->useip;
// 	dc_item.snmp_community = (char *)snmp_item->community;

//     if (NULL == (sess = zbx_snmp_open_session(&dc_item, error, sizeof(error)))) {
// 		zabbix_log(LOG_LEVEL_DEBUG, "Couldn't open snmp socket to host %s, :%s",snmp_item->interface_addr, error);
// 		return NULL;
// 	};
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
// 	return sess;
// }

// /******************************************************************************
//  * starts a connection 										                  * 
//  * ***************************************************************************/
// static int glb_snmp_start_connection(snmp_conn_t *conn)
// {
// 	zbx_uint64_t itemid = 0;
// 	struct snmp_pdu *pdu;
// 	unsigned char reuse_old_sess = 0;
// 	poller_item_t *poller_item, *prev_glb_item;
// 	u_int64_t mstime = glb_ms_time();

// 	char error_str[MAX_STRING_LEN];

// 	oid p_oid[MAX_OID_LEN];
// 	size_t oid_len = MAX_OID_LEN;
	
// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);

// 	//check if connection is free and there are items in the list
// 	if ( (POLL_FREE != conn->state && POLL_FINISHED != conn->state) || 
// 		 SUCCEED != zbx_list_peek(&conn->items_list, (void **)&itemid)) {
// 		 DEBUG_ITEM(itemid,"Not starting the connection right now, it's in busy state");
	
// 		 return FAIL;
// 	}

// 	//finding the item, as we use weak linking, an item might be cleaned, then it's ok, we just have to pick a next one
// 	if (NULL == (poller_item = poller_get_poller_item(itemid)) ) {
// 		zabbix_log(LOG_LEVEL_WARNING,"Coudln't find item with id %ld in the items hashset", itemid);
// 		//no such item anymore, pop it, and call myself to start the next item
// 		zbx_list_pop(&conn->items_list, (void **)&itemid);
// 		zbx_hashset_remove(&conf.items_idx, &itemid);

// 		DEBUG_ITEM(itemid,"Popped item, doesn't exists in the items");
// 		glb_snmp_start_connection(conn);
// 		return SUCCEED;
// 	} 

// 	DEBUG_ITEM(poller_get_item_id(poller_item),"Fetched item from the list to be polled");

// 	if (POLL_FINISHED == conn->state) 
// 		zbx_snmp_close_session(conn->sess);

// 	conn->state = POLL_FREE;
// 	zbx_list_pop(&conn->items_list, (void **)&itemid);
// 	zbx_hashset_remove(&conf.items_idx,&itemid);
// 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);	

// 	if (NULL == snmp_item || NULL == snmp_item->oid) {
// 		LOG_INF("Got empty snmp item, itemid %ld", poller_get_item_id(poller_item));
// 		THIS_SHOULD_NEVER_HAPPEN;
// 		return FAIL;
// 	}

// 	if (NULL == snmp_parse_oid(snmp_item->oid, p_oid, &oid_len))
// 		{
// 			char oid_err[256];
// 			LOG_DBG("Cannot parse oid %s",snmp_item->oid);
// 			zbx_snprintf(oid_err,256, "snmp_parse_oid(): cannot parse OID \"%s\"",snmp_item->oid );
// 			poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, oid_err);
// 			return FAIL;
// 		}

// 	if (NULL == (pdu = snmp_pdu_create(SNMP_MSG_GET)))
// 	{
// 		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "snmp_pdu_create(): cannot create PDU object ");
// 		return FAIL;
// 	}

// 	if (NULL == snmp_add_null_var(pdu, p_oid, oid_len))
// 	{
// 		poller_preprocess_value(poller_item,  NULL , mstime, ITEM_STATE_NOTSUPPORTED, "snmp_add_null_var(): cannot add null variable.");
// 		snmp_free_pdu(pdu);
// 		return FAIL;
// 	}
	
// 	if (NULL == (conn->sess = glb_snmp_open_conn(poller_item)))
// 	{
// 		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Couldn't open snmp conn");
// 		snmp_free_pdu(pdu);
// 		return FAIL;
// 	}


// 	conn->sess->callback = glb_snmp_callback;
// 	conn->sess->timeout = CONFIG_TIMEOUT * 1000 * 1000;
// 	conn->sess->callback_magic = conn;
// 	conn->sess->retries = CONFIG_SNMP_RETRIES;
// 	conn->current_item = poller_get_item_id(poller_item);
// 	conn->finish_time = time(NULL) + CONFIG_TIMEOUT * CONFIG_SNMP_RETRIES + 1; 
	
// 	DEBUG_ITEM(poller_get_item_id(poller_item), "Sending SNMP packet");
	
// 	if (!snmp_send(conn->sess, pdu))
// 	{
// 		snmp_perror("snmp_send");
// 		snmp_free_pdu(pdu);
	
// 		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED,  "Couldn't send snmp packet");
// 		zbx_snmp_close_session(conn->sess);
// 		conn->state = POLL_FREE;
// 		poller_return_item_to_queue(poller_item);

// 		return FAIL;
// 	}
//     poller_inc_requests();
	
//     conn->state = POLL_POLLING;
// 	//snmp_item->state = POLL_POLLING;

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
// 	return SUCCEED;
// }


// static void restart_finished_conns() {
// 	int i;
	
// 	for (i = 0; i < conf.free_conns.values_num; i++) {
// 		glb_snmp_start_connection(conf.free_conns.values[i]);
// 	}
	
// 	zbx_vector_ptr_clear(&conf.free_conns);
// }


// /******************************************************************************
//  * starts finished connections or a new one that have a data to poll		  * 
//  * ***************************************************************************/
// static void  snmp_start_new_connections(void) {
	
// 	int i, item_idx;
// 	static u_int64_t lastrun = 0;

// 	//only run once a second
// 	if (glb_ms_time() == lastrun) 
// 		return;
	
// 	lastrun = glb_ms_time();

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);
// 	int now=time(NULL);

// 	for (i = 0; i < GLB_MAX_SNMP_CONNS; i++) {
// 		if (POLL_POLLING == conf.connections[i].state && conf.connections[i].finish_time < now &&
// 				 -1 != conf.connections[i].current_item) {			 
// 			//zabbix_log(LOG_LEVEL_INFORMATION,"Unhandled by SNMP conn%d timeout event for item %ld", i, conf.connections[i].current_item);
// 			glb_snmp_handle_timeout(&conf.connections[i]); 	
// 		}
		
// 		//starting new cons or closing sockets for free ones
// 		if ( (POLL_FREE == conf.connections[i].state || POLL_FINISHED == conf.connections[i].state) ) {
// 		    if ( SUCCEED == zbx_list_peek(&conf.connections[i].items_list, (void **)&item_idx) ) {
// 				 glb_snmp_start_connection(&conf.connections[i]);
// 			} else {
// 				//closing the session
					
// 				if (POLL_FINISHED == conf.connections[i].state) {
// 					//zabbix_log(LOG_LEVEL_INFORMATION,"Connetction conn%d CLOSED marked as freee state is %d", i,conf.connections[i].state );
// 					conf.connections[i].state=POLL_FREE;
// 					zbx_snmp_close_session(conf.connections[i].sess);
// 				}
// 			}
// 		}
// 	}

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
// }

static variant_t *convert_snmp_result(csnmp_var_t *var) {
	static variant_t value;

	LOG_INF("Got var of type %d", var->type);
    switch (var->type) {
		case SNMP_TP_BOOL:
   		case SNMP_TP_INT:
   		case SNMP_TP_COUNTER:
   		case SNMP_TP_GAUGE:
       		LOG_INF("Int val: %d", *(int *)var->value);
       	break;
   		case SNMP_TP_COUNTER64:
   		case SNMP_TP_INT64:
   		case SNMP_TP_UINT64:
   		case SNMP_TP_TIMETICKS:
       		LOG_INF( "int 64/ctr val: %lu", *(u_int64_t *)var->value);
       	break;
   		case SNMP_TP_BIT_STR:
   		case SNMP_TP_OCT_STR:
       		LOG_INF("String val: %s", ((asn1_str_t *)var->value)->b);
       	break;
   		case SNMP_TP_IP_ADDR: {
       		asn1_str_t *str = (asn1_str_t *)var->value;
       		for (int j = 0; j < str->len; j++) {
           		if (j != 0) {
               		LOG_INF(".");
           		}
           		LOG_INF( "Ip addr val: %d", str->b[j]);
       		}
       	break;
   		}
   		case SNMP_TP_OID:
		   	LOG_INF("OID val");
       		asn1_dump_oid(*(asn1_oid_t *)var->value);
      	break;
    	case SNMP_TP_NULL:
    	case SNMP_TP_NO_SUCH_OBJ:
    	case SNMP_TP_NO_SUCH_INSTANCE:
    	case SNMP_TP_END_OF_MIB_VIEW:
    	default: {
        	asn1_str_t *str = (asn1_str_t *)var->value;
        	if (str) {
            	LOG_INF( "Type null [%x] (%d)", var->type, str->len);
        	} else {
            	LOG_INF( "Rype null [%x]", var->type);
        	}
        break;
    	}
	}
}

// static int submit_snmp_result(poller_item_t *poller_item, csnmp_var_t *var) {
// 	variant_t *result = convert_snmp_result(var);

// 	if (NULL == result) {
// 		poller_preprocess_error(poller_item, glb_ms_time(), "Couldn't parse returned result");
// 		return FAIL;
// 	}
// 	LOG_INF("Sending ready item to preprocessing should be here");
// 	//poller_preprocess_result(poller_item, result);
// }

/*note: copied from checks_snmp.c */
// static char	*snmp_get_octet_string(csnmp_var_t *var, unsigned char *string_type)
// {
// 	const char	*hint;
// 	char		buffer[MAX_BUFFER_LEN];
// 	char		*strval_dyn = NULL;
// 	struct tree	*subtree;
// 	unsigned char	type;

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

// 	/* find the subtree to get display hint */
// 	subtree = get_tree(var->name, var->name_length, get_tree_head());
// 	hint = (NULL != subtree ? subtree->hint : NULL);

// 	/* we will decide if we want the value from var->val or what snprint_value() returned later */
// 	if (-1 == snprint_value(buffer, sizeof(buffer), var->name, var->name_length, var))
// 		goto end;

// 	zabbix_log(LOG_LEVEL_DEBUG, "%s() full value:'%s' hint:'%s'", __func__, buffer, ZBX_NULL2STR(hint));

// 	if (0 == strncmp(buffer, "Hex-STRING: ", 12))
// 	{
// 		strval_dyn = zbx_strdup(strval_dyn, buffer + 12);
// 		type = ZBX_SNMP_STR_HEX;
// 	}
// 	else if (NULL != hint && 0 == strncmp(buffer, "STRING: ", 8))
// 	{
// 		strval_dyn = zbx_strdup(strval_dyn, buffer + 8);
// 		type = ZBX_SNMP_STR_STRING;
// 	}
// 	else if (0 == strncmp(buffer, "OID: ", 5))
// 	{
// 		strval_dyn = zbx_strdup(strval_dyn, buffer + 5);
// 		type = ZBX_SNMP_STR_OID;
// 	}
// 	else if (0 == strncmp(buffer, "BITS: ", 6))
// 	{
// 		strval_dyn = zbx_strdup(strval_dyn, buffer + 6);
// 		type = ZBX_SNMP_STR_BITS;
// 	}
// 	else
// 	{
// 		/* snprint_value() escapes hintless ASCII strings, so */
// 		/* we are copying the raw unescaped value in this case */

// 		strval_dyn = (char *)zbx_malloc(strval_dyn, var->val_len + 1);
// 		memcpy(strval_dyn, var->val.string, var->val_len);
// 		strval_dyn[var->val_len] = '\0';
// 		type = ZBX_SNMP_STR_ASCII;
// 	}

// 	if (NULL != string_type)
// 		*string_type = type;
// end:
// 	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __func__, ZBX_NULL2STR(strval_dyn));

// 	return strval_dyn;
// }


/*note: copied from checks_snmp.c */
static char	*snmp_err_to_text(unsigned char type)
{
	switch (type)
	{
		case SNMP_TP_NO_SUCH_INSTANCE:
			return zbx_strdup(NULL, "No Such Object available on this agent at this OID");
		case SNMP_TP_NO_SUCH_OBJ:
			return zbx_strdup(NULL, "No Such Instance currently exists at this OID");
		case SNMP_TP_END_OF_MIB_VIEW:
			return zbx_strdup(NULL, "No more variables left in this MIB View"
					" (it is past the end of the MIB tree)");
		default:
			return zbx_dsprintf(NULL, "Value has unknown type 0x%02X", (unsigned int)type);
	}
}
/*note: copied from checks_snmp.c */
int	snmp_set_result(csnmp_var_t *var, AGENT_RESULT *result)
{
	char		*strval_dyn;
	int		ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%d", __func__, (int)var->type);

	int string_type = ZBX_SNMP_STR_UNDEFINED;
 
	if (ASN_OCTET_STR == var->type || ASN_OBJECT_ID == var->type)
	{
		LOG_INF("String val: %s", ((asn1_str_t *)var->value)->b);
		
		
		//if (NULL == (strval_dyn =   zbx_snmp_get_octet_string(var, string_type)))
		if (NULL == var->value)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot receive string value: out of memory."));
			ret = NOTSUPPORTED;
		}
		else
		{
			set_result_type(result, ITEM_VALUE_TYPE_TEXT, ((asn1_str_t *)var->value)->b);
			LOG_INF("Set string value: '%s'", result->text);
			zbx_free(strval_dyn);
		}
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_UINTEGER == var->type || ASN_COUNTER == var->type || ASN_OPAQUE_U64 == var->type ||
			ASN_TIMETICKS == var->type || ASN_GAUGE == var->type)
#else
	else if (ASN_UINTEGER == var->type || ASN_COUNTER == var->type ||
			ASN_TIMETICKS == var->type || ASN_GAUGE == var->type)
#endif
	{
		SET_UI64_RESULT(result, *(unsigned long*)var->value);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_COUNTER64 == var->type || ASN_OPAQUE_COUNTER64 == var->type)
#else
	else if (ASN_COUNTER64 == var->type)
#endif
	{
		SET_UI64_RESULT(result, *(u_int64_t *)var->value);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_INTEGER == var->type || ASN_OPAQUE_I64 == var->type)
#else
	else if (ASN_INTEGER == var->type)
#endif
	{
		char	buffer[21];
		zbx_snprintf(buffer, sizeof(buffer), "%ld", var->value);

		set_result_type(result, ITEM_VALUE_TYPE_TEXT, buffer);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_OPAQUE_FLOAT == var->type)
	{
		SET_DBL_RESULT(result, *(float*)var->value);
	}
	else if (ASN_OPAQUE_DOUBLE == var->type)
	{
		SET_DBL_RESULT(result, *(double*)var->value);
	}
#endif
	else if (ASN_IPADDRESS == var->type)
	{
		asn1_str_t *str = (asn1_str_t *)var->value;
    	
		SET_STR_RESULT(result, zbx_dsprintf(NULL, "%u.%u.%u.%u",
				(unsigned int)str->b[0],
				(unsigned int)str->b[1],
				(unsigned int)str->b[2],
				(unsigned int)str->b[3]));
	}
	else
	{
		SET_MSG_RESULT(result, snmp_err_to_text(var->type));
		ret = NOTSUPPORTED;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}


static int process_result(csnmp_pdu_t *pdu)
{
 	AGENT_RESULT result;
 	poller_item_t *poller_item;
    u_int64_t mstime=glb_ms_time();

	u_int64_t itemid = pdu->req_id;

	/*note: reuest id is only 4 bytes,  while itemid is 8 bytes: fix 
		to also support of bulks and walks */
    if (NULL == (poller_item = poller_get_poller_item(itemid))) {
        DEBUG_ITEM(itemid,"Cannot find item for arrived responce id, ignoring");
 		return SUCCEED;
    }

//	LOG_INF("Found poller item for the request");
 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

 	poller_return_item_to_queue(poller_item);

	if (SNMP_ERR_OK == pdu->error_status) {  
		int i;
		
		for (i = 0; i < pdu->vars_len; i++) {
        
			AGENT_RESULT result;

			init_result(&result);

 			if (SUCCEED == snmp_set_result(&pdu->vars[i], &result)) {
			
				DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP SUCCEED RESULT processing for the item");
				poller_preprocess_value(poller_item, &result ,mstime, ITEM_STATE_NORMAL, NULL);
				poller_register_item_succeed(poller_item);
			} else {
				DEBUG_ITEM(poller_get_item_id(poller_item), "Async SNMP FAILED RESULT processing for the item: %s", result.msg );
				poller_preprocess_value(poller_item, NULL, mstime,ITEM_STATE_NOTSUPPORTED, result.msg);
 			}

			free_result(&result);
		}	
 	} else 
 			poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Got responce PDU with error indication");
}	

/* todo : async walks will start here */
static void send_request(poller_item_t *poller_item, const char* ip_addr) {
	
	csnmp_pdu_t pdu;
	if (FAIL == create_pdu(poller_item, &pdu, ip_addr)) {
		DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't create PDU for the request");
		poller_preprocess_error(poller_item, glb_ms_time(), "Couldn't create PDU for the request");
		return;
	}
	
	csnmp_send_pdu(conf.socket, &pdu);
	csnmp_free_pdu(&pdu);
	poller_inc_requests();
}

void resolve_ready_func_cb(poller_item_t *poller_item,  const char *addr) {
	DEBUG_ITEM(poller_get_item_id(poller_item), "Item resolved to '%s'", addr);
	
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
		
	send_request(poller_item, addr);
	poller_run_timer_event(snmp_item->timeout_event, CONFIG_TIMEOUT);
}

static void   start_poll_item(poller_item_t *poller_item) {

	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    u_int64_t itemid = poller_get_item_id(poller_item);
	
	/* note: quing on per-host basis should be done here */
	if (1 != snmp_item->useip) {
		if (FAIL == poller_async_resolve(poller_item, snmp_item->interface_addr, resolve_ready_func_cb)) {
			DEBUG_ITEM(poller_get_item_id(poller_item), "Cannot resolve item's interface addr: '%s'", snmp_item->interface_addr);
			poller_preprocess_error(poller_item, glb_ms_time(), "Cannot resolve item's interface hostname");
			poller_return_item_to_queue(poller_item);
		}
		return;
	}
	
	send_request(poller_item, snmp_item->interface_addr);
	poller_run_timer_event(snmp_item->timeout_event, CONFIG_TIMEOUT);
	
	poller_run_fd_event(conf.socket_event);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}




void responce_arrived_cb(poller_item_t *null_item, void *null_data) {
	csnmp_pdu_t pdu = {0};
	int i=0;
	while (FAIL != csnmp_recv_pdu(conf.socket, &pdu)) {
		process_result(&pdu);
		csnmp_free_pdu(&pdu);
		bzero(&pdu, sizeof(csnmp_pdu_t));
		poller_inc_responces();
	}
	poller_run_fd_event(conf.socket_event);
}


static void  handle_async_io(void) {
}

static void	snmp_async_shutdown(void) {
	poller_destroy_event(conf.socket_event);
	close(conf.socket);
}

static int forks_count(void) {
	return CONFIG_GLB_SNMP_FORKS;
}

void async_snmp_init(void) {
	int i;

	LOG_INF("In %s: starting", __func__);
	/*need libsnmp for parsing text->digital oids*/
	init_snmp(progname);
	
	poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_poll_item, snmp_async_shutdown, forks_count);

	if ( (conf.socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        LOG_INF("Couldn't create socket for ASYNC snmp poller");
        exit(EXIT_FAILURE); 
    } 
	
	int flags = fcntl(conf.socket, F_GETFL);
	fcntl(conf.socket, F_SETFL, flags | O_NONBLOCK);

	LOG_INF("Creating socket event on fd %d", conf.socket);
	conf.socket_event = poller_create_event(NULL, responce_arrived_cb, conf.socket, NULL);

	LOG_INF("Running socket event");
	poller_run_fd_event(conf.socket_event);
}