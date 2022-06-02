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

#define GLB_MAX_SNMP_CONNS 8192
#define CONFIG_SNMP_RETRIES 2

extern int CONFIG_GLB_SNMP_CONTENTION;
extern int CONFIG_GLB_SNMP_FORKS;

/*connection-specific data */
typedef struct {
	struct snmp_session *sess; /* SNMP session data */
	zbx_int64_t current_item;  /* itemid of currently processing item */
	int state;				   
	int finish_time;		   /* unix time till we wait for data to come */
	int retries;			   /* how many retries already passed */
    zbx_list_t items_list;    /* list of itemids assigned to the session */
} snmp_conn_t;

/*engine-specific data */
typedef struct {
	zbx_hashset_t items_idx; //items that are being polled right now
	snmp_conn_t *connections; 
	zbx_vector_ptr_t free_conns;
} async_snmp_conf_t;

typedef struct {
    unsigned char	snmpv3_securitylevel;
	unsigned char	snmpv3_authprotocol;
	unsigned char	snmpv3_privprotocol;

	const char *snmpv3_securityname;
	const char *snmpv3_contextname;
	const char *snmpv3_authpassphrase;
	const char *snmpv3_privpassphrase;
} snmp_v3_conf_t;

typedef struct {
	const char *oid;
	unsigned char snmp_version;
	const char		*interface_addr;
	unsigned char	useip;
	unsigned short	interface_port;
	const char *community;
    snmp_v3_conf_t *v3_conf;
//	char state;

} snmp_item_t;

static async_snmp_conf_t *conf;

/******************************************************************************
 * timeout - connection - cleanup 			   								  * 
 * ***************************************************************************/
static int glb_snmp_handle_timeout(snmp_conn_t *conn) {
	int i;
	zbx_uint64_t item_idx;
	char error_str[MAX_STRING_LEN];
	poller_item_t *poller_item, *poller_next_item;
	u_int64_t mstime= glb_ms_time();
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

	conn->state = POLL_FINISHED;

	if (NULL == (poller_item = poller_get_pollable_item(conn->current_item))) 
	 	return FAIL; 
    
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	poller_register_item_timeout(poller_item);
	
	zbx_snprintf(error_str,MAX_STRING_LEN, "Timed out, no responce for %d seconds %d retries", CONFIG_TIMEOUT, 2 );
	
	poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, error_str );
	
	zbx_hashset_remove(&conf->items_idx, &conn->current_item);		
	poller_return_item_to_queue(poller_item);
		

	if ( SUCCEED == poller_if_host_is_failed(poller_item) ) {
		zabbix_log(LOG_LEVEL_DEBUG, "Doing local queue cleanup due to too many timed items for host %ld", poller_get_host_id(poller_item));

		while (SUCCEED == zbx_list_peek(&conn->items_list, (void **)&item_idx) &&
	   		( NULL != (poller_next_item = poller_get_pollable_item(item_idx))) &&  
	   		(  poller_get_host_id(poller_next_item) == poller_get_host_id(poller_item)) ) {

			zbx_list_pop(&conn->items_list, (void **)&item_idx);

			poller_return_item_to_queue(poller_next_item);
			zbx_snprintf(error_str,MAX_STRING_LEN,"Skipped from polling due to %d items timed out in a row, last failed item id is %ld", 
							GLB_FAIL_COUNT_CLEAN, poller_get_item_id(poller_item));

			zbx_hashset_remove(&conf->items_idx,&item_idx);

			zabbix_log(LOG_LEVEL_DEBUG, "host %ld item %ld timed out %s", poller_get_host_id(poller_item), item_idx, error_str);
			poller_preprocess_value(poller_next_item , NULL , mstime, ITEM_STATE_NOTSUPPORTED, error_str );
		}
	}
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
}

static void register_finished_conn(snmp_conn_t *conn) {
	zbx_vector_ptr_append(&conf->free_conns, conn);
}

/******************************************************************************
 * callback that will be called on async data arrival						  *
 * ***************************************************************************/
static int glb_snmp_callback(int operation, struct snmp_session *sp, int reqid,
					struct snmp_pdu *pdu, void *magic)
{
	AGENT_RESULT result;
	poller_item_t *poller_item;
    u_int64_t mstime=glb_ms_time();

	snmp_conn_t *conn = magic;
	//async_snmp_conf_t *conf = ( async_snmp_conf_t*)conn->conf;

	struct snmp_pdu *req;
	struct variable_list *var;

	unsigned char snmp_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	var = pdu->variables;


	conn->state = POLL_FINISHED;

    if (NULL == (poller_item = poller_get_pollable_item(conn->current_item))) {
        DEBUG_ITEM(conn->current_item,"Responce for aged/deleted item has arrived, ignoring");
		return SUCCEED;
    }

	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

    poller_inc_responces();
	poller_return_item_to_queue(poller_item);

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		
		if (SNMP_ERR_NOERROR == pdu->errstat)
		{  
			while (var)
			{	int ret;

				init_result(&result);
				
				if (SUCCEED == zbx_snmp_set_result(var, &result, &snmp_type)) {
			
					DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP SUCCEED RESULT processing for the item");
					poller_preprocess_value(poller_item, &result ,mstime, ITEM_STATE_NORMAL, NULL);
					poller_register_item_succeed(poller_item);
				} else {
					DEBUG_ITEM(poller_get_item_id(poller_item), "Async SNMP FAILED RESULT processing for the item: %s", result.msg );

					poller_preprocess_value(poller_item, NULL, mstime,ITEM_STATE_NOTSUPPORTED, result.msg);
				}
						
				var = var->next_variable;
				free_result(&result);
			}
			
		} else {
		//	zabbix_log(LOG_LEVEL_INFORMATION,"Connection sconn%d got FAIL responce",conn->idx);
			poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Cannot parse response pdu");
		}
	} else {
			DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP responce TIMEOUT event")	
			glb_snmp_handle_timeout(conn);
	}

	//add connection to the list of finished to restart right after 
	//exit from snmp_read cycle
	register_finished_conn(conn);
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
	return 1;
}	

/******************************************************************************
 * item init - from the general dc_item to compact and specific snmp		  * 
 * ***************************************************************************/
static int snmp_init_item(void *m_conf, DC_ITEM *dc_item, poller_item_t *poller_item) {
	
	char translated_oid[4*MAX_OID_LEN];
	zbx_timespec_t timespec;
	snmp_item_t *snmp_item;
	
//	size_t parsed_oid_len = MAX_OID_LEN;

	char error_str[1024];

	LOG_DBG("In %s: starting", __func__);
	
	snmp_item = zbx_calloc(NULL, 0, sizeof(snmp_item_t));

    poller_set_item_specific_data(poller_item, snmp_item);
	//poller_item->itemdata = snmp_item;
	
	if (NULL != dc_item->snmp_oid && dc_item->snmp_oid[0] != '\0')
	{
		zbx_snmp_translate(translated_oid, dc_item->snmp_oid, sizeof(translated_oid));
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "In %s() cannot translate empty oid %s for item %ld", __func__,
				   translated_oid, dc_item->itemid);
		zbx_preprocess_item_value(dc_item->host.hostid, dc_item->itemid, dc_item->value_type, dc_item->flags , NULL,
					&timespec,ITEM_STATE_NOTSUPPORTED, "Error: empty OID, item will not be polled until updated, fix config to start poll");
		return FAIL;
	}

	snmp_item->snmp_version = dc_item->snmp_version;
	snmp_item->interface_port = dc_item->interface.port;
	snmp_item->useip = dc_item->interface.useip;

    snmp_item->oid = zbx_heap_strpool_intern(translated_oid);
	snmp_item->interface_addr = zbx_heap_strpool_intern(dc_item->interface.addr);
	snmp_item->community = zbx_heap_strpool_intern(dc_item->snmp_community);

    if (SNMP_VERSION_3 == dc_item->snmp_version) {
        snmp_item->v3_conf = zbx_calloc(NULL, 0, sizeof(snmp_v3_conf_t));

	    snmp_item->v3_conf->snmpv3_securitylevel = dc_item->snmpv3_securitylevel;
 	    snmp_item->v3_conf->snmpv3_authprotocol = dc_item->snmpv3_authprotocol;
 	    snmp_item->v3_conf->snmpv3_privprotocol = dc_item->snmpv3_privprotocol;
	
	    snmp_item->v3_conf->snmpv3_securityname = zbx_heap_strpool_intern(dc_item->snmpv3_securityname);
	    snmp_item->v3_conf->snmpv3_contextname = zbx_heap_strpool_intern(dc_item->snmpv3_contextname);
	    snmp_item->v3_conf->snmpv3_authpassphrase = zbx_heap_strpool_intern(dc_item->snmpv3_authpassphrase);
	    snmp_item->v3_conf->snmpv3_privpassphrase = zbx_heap_strpool_intern(dc_item->snmpv3_privpassphrase);
    }

	LOG_DBG("In %s() Ended", __func__);
	return SUCCEED;
}

static void snmp_free_item(void *m_conf,  poller_item_t *poller_item ) {
	
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	zbx_heap_strpool_release(snmp_item->interface_addr);
	zbx_heap_strpool_release(snmp_item->community);
    zbx_heap_strpool_release(snmp_item->oid);
    
    if (SNMP_VERSION_3 == snmp_item->snmp_version) {
        zbx_heap_strpool_release(snmp_item->v3_conf->snmpv3_securityname);
        zbx_heap_strpool_release(snmp_item->v3_conf->snmpv3_contextname);
 	    zbx_heap_strpool_release(snmp_item->v3_conf->snmpv3_authpassphrase);
	    zbx_heap_strpool_release(snmp_item->v3_conf->snmpv3_privpassphrase);
	
        zbx_free(snmp_item->v3_conf);
        snmp_item->v3_conf = NULL;
    }
	
	zbx_free(snmp_item);
		
	LOG_DBG( "In %s() Ended", __func__);
}

/******************************************************************************
 * wrap function to use zbx snmp conn										  * 
 * ***************************************************************************/
static struct snmp_session * glb_snmp_open_conn(poller_item_t *poller_item){
	DC_ITEM dc_item = {0};
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	char error[MAX_STRING_LEN];
	struct snmp_session *sess;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	//theese are	 the necessary dc_item fields to make a connection:
	dc_item.snmp_version = snmp_item->snmp_version;
	dc_item.interface.addr = (char *)snmp_item->interface_addr;
	dc_item.interface.port = snmp_item->interface_port;
	dc_item.interface.useip = snmp_item->useip;
	dc_item.snmp_community = (char *)snmp_item->community;

    if (SNMP_VERSION_3 == snmp_item->snmp_version) {
	    dc_item.snmpv3_securityname =(char *)snmp_item->v3_conf->snmpv3_securityname;
	    dc_item.snmpv3_contextname = (char *)snmp_item->v3_conf->snmpv3_contextname;
	    dc_item.snmpv3_securitylevel = snmp_item->v3_conf->snmpv3_securitylevel;
    	dc_item.snmpv3_authprotocol = snmp_item->v3_conf->snmpv3_authprotocol;
	    dc_item.snmpv3_authpassphrase =(char *)snmp_item->v3_conf->snmpv3_authpassphrase;
	    dc_item.snmpv3_privprotocol = snmp_item->v3_conf->snmpv3_privprotocol;
	    dc_item.snmpv3_privpassphrase = (char *)snmp_item->v3_conf->snmpv3_privpassphrase;
    }
	
	if (NULL == (sess = zbx_snmp_open_session(&dc_item, error, sizeof(error)))) {
		zabbix_log(LOG_LEVEL_DEBUG, "Couldn't open snmp socket to host %s, :%s",snmp_item->interface_addr, error);
		return NULL;
	};
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
	return sess;
}


/******************************************************************************
 * starts a connection 										                  * 
 * ***************************************************************************/
static int glb_snmp_start_connection(snmp_conn_t *conn)
{
	zbx_uint64_t itemid = 0;
	struct snmp_pdu *pdu;
	unsigned char reuse_old_sess = 0;
	poller_item_t *poller_item, *prev_glb_item;
	u_int64_t mstime = glb_ms_time();

	char error_str[MAX_STRING_LEN];

	oid p_oid[MAX_OID_LEN];
	size_t oid_len = MAX_OID_LEN;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);

	//check if connection is free and there are items in the list
	if ( (POLL_FREE != conn->state && POLL_FINISHED != conn->state) || 
		 SUCCEED != zbx_list_peek(&conn->items_list, (void **)&itemid)) {
		 DEBUG_ITEM(itemid,"Not starting the connection right now, it's in busy state");
	
		 return FAIL;
	}

	//finding the item, as we use weak linking, an item might be cleaned, then it's ok, we just have to pick a next one
	if (NULL == (poller_item = poller_get_pollable_item(itemid)) ) {
		zabbix_log(LOG_LEVEL_WARNING,"Coudln't find item with id %ld in the items hashset", itemid);
		//no such item anymore, pop it, and call myself to start the next item
		zbx_list_pop(&conn->items_list, (void **)&itemid);
		zbx_hashset_remove(&conf->items_idx, &itemid);

		DEBUG_ITEM(itemid,"Popped item, doesn't exists in the items");
		glb_snmp_start_connection(conn);
		return SUCCEED;
	} 

	DEBUG_ITEM(poller_get_item_id(poller_item),"Fetched item from the list to be polled");

	if (POLL_FINISHED == conn->state) 
		zbx_snmp_close_session(conn->sess);

	conn->state = POLL_FREE;
	zbx_list_pop(&conn->items_list, (void **)&itemid);
	zbx_hashset_remove(&conf->items_idx,&itemid);
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);	

	if (NULL == snmp_item || NULL == snmp_item->oid) {
		LOG_INF("Got empty snmp item, itemid %ld", poller_get_item_id(poller_item));
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	if (NULL == snmp_parse_oid(snmp_item->oid, p_oid, &oid_len))
		{
			char oid_err[256];
			LOG_DBG("Cannot parse oid %s",snmp_item->oid);
			zbx_snprintf(oid_err,256, "snmp_parse_oid(): cannot parse OID \"%s\"",snmp_item->oid );
			poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, oid_err);
			return FAIL;
		}

	if (NULL == (pdu = snmp_pdu_create(SNMP_MSG_GET)))
	{
		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "snmp_pdu_create(): cannot create PDU object ");
		return FAIL;
	}

	if (NULL == snmp_add_null_var(pdu, p_oid, oid_len))
	{
		poller_preprocess_value(poller_item,  NULL , mstime, ITEM_STATE_NOTSUPPORTED, "snmp_add_null_var(): cannot add null variable.");
		snmp_free_pdu(pdu);
		return FAIL;
	}
	
	if (NULL == (conn->sess = glb_snmp_open_conn(poller_item)))
	{
		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Couldn't open snmp conn");
		snmp_free_pdu(pdu);
		return FAIL;
	}


	conn->sess->callback = glb_snmp_callback;
	conn->sess->timeout = CONFIG_TIMEOUT * 1000 * 1000;
	conn->sess->callback_magic = conn;
	conn->sess->retries = CONFIG_SNMP_RETRIES;
	conn->current_item = poller_get_item_id(poller_item);
	conn->finish_time = time(NULL) + CONFIG_TIMEOUT * CONFIG_SNMP_RETRIES + 1; 
	
	DEBUG_ITEM(poller_get_item_id(poller_item), "Sending SNMP packet");
	
	if (!snmp_send(conn->sess, pdu))
	{
		snmp_perror("snmp_send");
		snmp_free_pdu(pdu);
	
		poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED,  "Couldn't send snmp packet");
		zbx_snmp_close_session(conn->sess);
		conn->state = POLL_FREE;
		poller_return_item_to_queue(poller_item);

		return FAIL;
	}
    poller_inc_requests();
	
    conn->state = POLL_POLLING;
	//snmp_item->state = POLL_POLLING;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return SUCCEED;
}


static void restart_finished_conns() {
	int i;
	
	for (i = 0; i < conf->free_conns.values_num; i++) {
		glb_snmp_start_connection(conf->free_conns.values[i]);
	}
	
	zbx_vector_ptr_clear(&conf->free_conns);
}


/******************************************************************************
 * starts finished connections or a new one that have a data to poll		  * 
 * ***************************************************************************/
static void  snmp_start_new_connections(void *m_conf) {
	
	int i, item_idx;
	static u_int64_t lastrun = 0;

	//only run once a second
	if (glb_ms_time() == lastrun) 
		return;
	
	lastrun = glb_ms_time();

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Started", __func__);
	int now=time(NULL);

	for (i = 0; i < GLB_MAX_SNMP_CONNS; i++) {
		if (POLL_POLLING == conf->connections[i].state && conf->connections[i].finish_time < now &&
				 -1 != conf->connections[i].current_item) {			 
			//zabbix_log(LOG_LEVEL_INFORMATION,"Unhandled by SNMP conn%d timeout event for item %ld", i, conf->connections[i].current_item);
			glb_snmp_handle_timeout(&conf->connections[i]); 	
		}
		
		//starting new cons or closing sockets for free ones
		if ( (POLL_FREE == conf->connections[i].state || POLL_FINISHED == conf->connections[i].state) ) {
		    if ( SUCCEED == zbx_list_peek(&conf->connections[i].items_list, (void **)&item_idx) ) {
				 glb_snmp_start_connection(&conf->connections[i]);
			} else {
				//closing the session
					
				if (POLL_FINISHED == conf->connections[i].state) {
					//zabbix_log(LOG_LEVEL_INFORMATION,"Connetction conn%d CLOSED marked as freee state is %d", i,conf->connections[i].state );
					conf->connections[i].state=POLL_FREE;
					zbx_snmp_close_session(conf->connections[i].sess);
				}
			}
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
/******************************************************************************
 * adds item to the polling list of a conenction							  * 
 * ***************************************************************************/
static void   snmp_add_poll_item(void *m_conf, poller_item_t *poller_item) {
	//async_snmp_conf_t *conf = m_conf;
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    u_int64_t itemid = poller_get_item_id(poller_item);

	//checking if the item is still in the poller to not put it once again
	if (NULL != zbx_hashset_search(&conf->items_idx,&itemid)) {
		zabbix_log(LOG_LEVEL_DEBUG, "Item %ld is still in the list, not adding to polling again", itemid);
		DEBUG_ITEM(itemid, "Item is still waiting to be polled, not adding to the list");
		return;
	}

	int idx = ( poller_get_host_id(poller_item) % (GLB_MAX_SNMP_CONNS/CONFIG_GLB_SNMP_CONTENTION) ) * CONFIG_GLB_SNMP_CONTENTION +
			itemid % CONFIG_GLB_SNMP_CONTENTION;
	//zabbix_log(LOG_LEVEL_INFORMATION, "Calculated index for host id %ld itemid %ld is %d",
	//		poller_item->hostid, poller_item->itemid, idx);
	zbx_list_append(&conf->connections[idx].items_list, (void **)itemid, NULL);
	zbx_hashset_insert(&conf->items_idx,&itemid, sizeof(itemid));

	DEBUG_ITEM(itemid,"Added to list, starting connection");
	glb_snmp_start_connection(&conf->connections[idx]);
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}


/******************************************************************************
 * Once in a wile reinits the snmp to do the mem cleaunup					  * 
 * ***************************************************************************/

static void snmp_reset_snmp(void *m_conf){
	static int roll=0;
	poller_item_t *poller_item;

	//periodical total netsnmp cleanup
	if (roll++ > GLB_ASYNC_POLLING_MAX_ITERATIONS)
	{
		int i, cnt = 0;
		//we'll now drop all the sessions and connections to clean up snmp memory.
		//we'll have to retry all the items then
		for (i = 0; i < GLB_MAX_SNMP_CONNS; i++) {
			if  ( POLL_POLLING == conf->connections[i].state || POLL_FINISHED == conf->connections[i].state)
			{
				conf->connections[i].state = POLL_FREE;
				zbx_list_prepend(&conf->connections[i].items_list, (void **)conf->connections[i].current_item, NULL);
				snmp_close(conf->connections[i].sess);
				
				
                if (NULL != (poller_item = poller_get_pollable_item(conf->connections[i].current_item))) {
                    poller_return_item_to_queue(poller_item);
                }
                
				cnt++;
			}
			
		}
		roll = 0;
	
		zabbix_log(LOG_LEVEL_INFORMATION, "Doing SNMP reset");
		snmp_close_sessions();
		zbx_shutdown_snmp();
		zbx_init_snmp();
		zabbix_log(LOG_LEVEL_INFORMATION, "Finished SNMP reset");
	}
}

/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * ***************************************************************************/
static void  snmp_handle_async_io(void *m_conf) {

	int block = 1, fds = 0, hosts = 0;
	netsnmp_large_fd_set fdset;
	static int last_timeout_time = 0; 

	struct timeval timeout={.tv_sec = 0, .tv_usec = 1000 }; 

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
		
	netsnmp_large_fd_set_init(&fdset, GLB_MAX_SNMP_CONNS);
	snmp_select_info2(&fds, &fdset, &timeout, &block);
	
	hosts = netsnmp_large_fd_set_select(fds, &fdset, NULL, NULL, &timeout);
	
	if (hosts < 0)
		zabbix_log(LOG_LEVEL_WARNING, "End of %s() Something unexpected happened with fds ", __func__);
	else if (hosts > 0) {
		snmp_read2(&fdset); //calling this will call snmp callback function for arrived responses
		restart_finished_conns();//starting new requests on connections that finished
	}
	else 
		snmp_timeout();

	netsnmp_large_fd_set_cleanup(&fdset);
	
	snmp_reset_snmp(conf);
	snmp_start_new_connections(conf); //some connections might fail due to temporarry errors, like network fails, restart them once in a while
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
static void	snmp_async_shutdown(void *m_conf) {
	
	int i;
	struct list_item *litem, *tmp_litem;
	poller_item_t *poller_item;
    u_int64_t mstime = glb_ms_time();
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

	for (i = 0; i < GLB_MAX_SNMP_CONNS; i++)
	{
		zbx_snmp_close_session(conf->connections[i].sess);
		if (conf->connections[i].state == POLL_POLLING && 
		 (NULL != (poller_item = poller_get_pollable_item(conf->connections[i].current_item))))
		{
			poller_preprocess_value(poller_item , NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Couldn't send snmp packet: timeout");
		}
		
		zbx_list_destroy(&conf->connections[i].items_list);
	}
	snmp_close_sessions();
	zbx_free(conf->connections);
	zbx_free(conf);

	zbx_hashset_destroy(&conf->items_idx);
	zbx_shutdown_snmp();
	zbx_vector_ptr_destroy(&conf->free_conns);
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}


static int forks_count(void *m_conf) {
	return CONFIG_GLB_SNMP_FORKS;
}

/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void async_snmp_init(poll_engine_t *poll) {
	int i;

	LOG_INF("In %s: starting", __func__);
	
	zbx_init_snmp();
	
	conf = zbx_malloc(NULL,sizeof(async_snmp_conf_t));
	conf->connections = zbx_malloc(NULL, sizeof(snmp_conn_t)* GLB_MAX_SNMP_CONNS);

    poller_set_poller_module_data(conf);
    poller_set_poller_callbacks(snmp_init_item, snmp_free_item, snmp_handle_async_io, snmp_add_poll_item, snmp_async_shutdown, forks_count);

	for (i = 0; i < GLB_MAX_SNMP_CONNS; i++)
	{
		conf->connections[i].state = POLL_FREE;
		conf->connections[i].current_item=-1;

		zbx_list_create(&conf->connections[i].items_list);
	}
	
	zbx_hashset_create(&conf->items_idx, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	zbx_vector_ptr_create(&conf->free_conns);
}