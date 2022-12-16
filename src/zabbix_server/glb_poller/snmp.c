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

#include "glb_poller.h"
#include "log.h"
#include "common.h"
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/types.h>
#include "../poller/checks_snmp.h"
#include "preproc.h"
#include "csnmp.h"
#include "poller_async_io.h"
#include "snmp.h"
#include "snmp_get.h"
#include "snmp_walk.h"
#include "poller_sessions.h"
#include "snmp_util.h"

extern int CONFIG_GLB_SNMP_CONTENTION;
extern int CONFIG_GLB_SNMP_FORKS;

typedef struct {
	int socket; //async version uses single socket for the io
	poller_event_t *socket_event;
	zbx_hashset_t hostinfo; 
} async_snmp_conf_t;

static async_snmp_conf_t conf = {0};

static void timeout_event_cb(poller_item_t *poller_item, void *data) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);    
	
	//LOG_INF("Item %ld timed out, cleaning session %u", poller_get_item_id(poller_item), snmp_item->sessid);
	poller_sessions_close_session(snmp_item->sessid);
	snmp_item->sessid = 0;

	DEBUG_ITEM(poller_get_item_id(poller_item), "SNMP: got timeout event,  req_type is %d, get is %d, walk is %d", snmp_item->snmp_req_type, SNMP_CMD_GET,SNMP_CMD_GET_NEXT );


	if (SNMP_CMD_GET_NEXT == snmp_item->snmp_req_type) {
		DEBUG_ITEM(poller_get_item_id(poller_item), "SNMP: got WALK timeout event");
		snmp_walk_timeout(poller_item);
	}
	
	if (SNMP_CMD_GET == snmp_item->snmp_req_type) {
		DEBUG_ITEM(poller_get_item_id(poller_item), "SNMP: got GET timeout event");
		snmp_get_timeout(poller_item);
	}

	poller_register_item_timeout(poller_item);
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
	/* todo: it looks like there is no need to do zbx_snmp_translate - 
	 due to items are naturally cached */
	if (NULL != dc_item->snmp_oid && dc_item->snmp_oid[0] != '\0')
			zbx_snmp_translate(translated_oid, dc_item->snmp_oid, sizeof(translated_oid));
	else
	{
		DEBUG_ITEM(poller_get_item_id(poller_item), "Empty oid %s for item ",translated_oid);
		poller_preprocess_error(poller_item, "Error: empty OID, item will not be polled until updated, fix config to start poll");

		return FAIL;
	}
	snmp_item->oid = poller_strpool_add(translated_oid);
	
	snmp_item->snmp_version = dc_item->snmp_version;
	snmp_item->interface_port = dc_item->interface.port;
	snmp_item->useip = dc_item->interface.useip;
    snmp_item->interface_addr = poller_strpool_add(dc_item->interface.addr);
	snmp_item->community = poller_strpool_add(dc_item->snmp_community);
	snmp_item->ipaddr = NULL;
	snmp_item->sessid = 0;

	snmp_item->tm_event = poller_create_event(poller_item, timeout_event_cb, 0, NULL, 0);

	if (strstr(snmp_item->oid,"discovery[")) 
		snmp_item->snmp_req_type = SNMP_CMD_GET_NEXT;
	else 
		snmp_item->snmp_req_type = SNMP_CMD_GET;
		
	return SUCCEED;
}

static void free_item(poller_item_t *poller_item ) {
	
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	//LOG_INF("Item %ld freeing snmp data", poller_get_item_id(poller_item));
    
	//zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
	if (SNMP_CMD_GET_NEXT == snmp_item->snmp_req_type) {
		snmp_walk_destroy_item(poller_item);
	}

	if (0 == snmp_item->useip) 
		poller_strpool_free(snmp_item->ipaddr);
	
	poller_sessions_close_session(snmp_item->sessid);
	poller_strpool_free(snmp_item->oid);
	poller_strpool_free(snmp_item->interface_addr);
	poller_strpool_free(snmp_item->community);

	poller_destroy_event(snmp_item->tm_event);



	zbx_free(snmp_item);
		
	LOG_DBG( "In %s() Ended", __func__);
}

static int process_result(csnmp_pdu_t *pdu)
{
 	AGENT_RESULT result;
 	poller_item_t *poller_item;
 
	u_int32_t sess_id  = pdu->req_id;
	u_int64_t itemid;
	struct sockaddr_in *saddr = (struct sockaddr_in *)&pdu->addr;
	
	poller_inc_responses();
	if (0 == (itemid = poller_sessions_close_session(pdu->req_id))) {
		char addr_str[20];
		inet_ntop(AF_INET, &(pdu->addr), addr_str, INET_ADDRSTRLEN);
	//	LOG_INF("Arrived responce for session %u that doesn't exists, host %s", sess_id, inet_ntoa(saddr->sin_addr));
		return SUCCEED;
	}
	
//	LOG_INF("Item %ld: got incoming packet", itemid);
	DEBUG_ITEM(itemid, "Arrived SNMP responce");
	
	if (NULL == (poller_item = poller_get_poller_item(itemid))) {
        DEBUG_ITEM(itemid, "Cannot find item for arrived responce id, ignoring");
 		return SUCCEED;
    }
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	poller_disable_event(snmp_item->tm_event);
	snmp_item->retries = 0;
	snmp_item->sessid =0;

 	switch ( snmp_item->snmp_req_type ) {
		case SNMP_CMD_GET_NEXT:
			snmp_walk_process_result(poller_item, pdu);
			break;
		case SNMP_CMD_GET:
			snmp_get_process_result(poller_item, pdu);
			break;
		default:
			LOG_WRN("Unknown snmp request type %d", snmp_item->snmp_req_type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}
	poller_register_item_succeed(poller_item);
}	

void snmp_send_packet(poller_item_t *poller_item, csnmp_pdu_t *pdu) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	//LOG_INF("Sending request for the item %ld", poller_get_item_id(poller_item));
	DEBUG_ITEM(poller_get_item_id(poller_item),"Sending request for the item");
	snmp_item->sessid = poller_sessions_create_session(poller_get_item_id(poller_item), 0);
	pdu->req_id = snmp_item->sessid;

	csnmp_send_pdu(conf.socket, pdu);
	poller_run_timer_event(snmp_item->tm_event, CONFIG_TIMEOUT * 1000);
	poller_inc_requests();

}

static int snmp_send_request(poller_item_t *poller_item) {
 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_item->retries = 0;
	
 	switch ( snmp_item->snmp_req_type ) {
 		case SNMP_CMD_GET_NEXT:
 			snmp_walk_send_first_request(poller_item);
 		break;
 	case SNMP_CMD_GET:	
			snmp_get_send_request(poller_item);
 		break;
 	default:
 			LOG_WRN("Unknown snmp request type %d", snmp_item->snmp_req_type);
 			THIS_SHOULD_NEVER_HAPPEN;
 			exit(-1);
 	}
}

static void resolve_ready_cb(poller_item_t *poller_item,  const char *addr) {
	DEBUG_ITEM(poller_get_item_id(poller_item), "Item resolved to '%s'", addr);
	
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	poller_strpool_free(snmp_item->ipaddr);
	snmp_item->ipaddr = poller_strpool_add(addr);

	snmp_send_request(poller_item);
}

static void   start_poll_item(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    u_int64_t itemid = poller_get_item_id(poller_item);
	
	/* note: queueing on per-host basis should be done here */
	if (1 != snmp_item->useip) {
		if (FAIL == poller_async_resolve(poller_item, snmp_item->interface_addr)) {
			DEBUG_ITEM(poller_get_item_id(poller_item), "Cannot resolve item's interface addr: '%s'", snmp_item->interface_addr);
			poller_preprocess_error(poller_item, "Cannot resolve item's interface hostname");
			poller_return_item_to_queue(poller_item);
		}
		return;
	}
	
	snmp_item->ipaddr = snmp_item->interface_addr;
	
	snmp_send_request(poller_item);
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

void responce_arrived_cb(poller_item_t *null_item, void *null_data) {
	csnmp_pdu_t pdu = {0};
	int i=0;

	while (FAIL != csnmp_recv_pdu(conf.socket, &pdu)) {
		process_result(&pdu);
		csnmp_free_pdu(&pdu);
	}
}

static void  handle_async_io(void) {
	static unsigned int now = 0;

	if (time(NULL) > now + 120) {
		now = time(NULL);

		sigset_t	mask, orig_mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGTERM);
		sigaddset(&mask, SIGUSR2);
		sigaddset(&mask, SIGHUP);
		sigaddset(&mask, SIGQUIT);
		sigprocmask(SIG_BLOCK, &mask, &orig_mask);

		snmp_shutdown(progname);
		
		sigprocmask(SIG_SETMASK, &orig_mask, NULL);
		
		netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_PERSIST_STATE, 1);
		
		snmp_shutdown(progname);
		init_snmp(progname);
	}
}

void snmp_async_shutdown(void) {
	poller_destroy_event(conf.socket_event);
	poller_sessions_destroy();
	close(conf.socket);
}

static int forks_count(void) {
	return CONFIG_GLB_SNMP_FORKS;
}

void snmp_async_init(void) {
	int i;
	mem_funcs_t memf = {.free_func = ZBX_DEFAULT_MEM_FREE_FUNC, .malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, .realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC};

	/*need lib net-snmp for parsing text->digital and getting text oids hints*/
	init_snmp(progname);
	
	poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_poll_item, snmp_async_shutdown, forks_count, resolve_ready_cb);

	if ( (conf.socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        LOG_INF("Couldn't create socket for ASYNC snmp poller");
        exit(EXIT_FAILURE); 
    } 
	
	int flags = fcntl(conf.socket, F_GETFL);
	int size = 100 * ZBX_MEBIBYTE;

	fcntl(conf.socket, F_SETFL, flags | O_NONBLOCK);
	if (FAIL == setsockopt(conf.socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(int))) {
		LOG_INF("Couldn't send max socket buffer size to %d bytes", ZBX_MEBIBYTE);
		exit(-1);
	}
	if (FAIL == setsockopt(conf.socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(int))) {
		LOG_INF("Couldn't send max socket buffer size to %d bytes", ZBX_MEBIBYTE);
		exit(-1);
	}

	LOG_INF("Creating socket event on fd %d", conf.socket);
	conf.socket_event = poller_create_event(NULL, responce_arrived_cb, conf.socket, NULL, 1);

	LOG_INF("Running socket event");
	poller_run_fd_event(conf.socket_event);
	
	poller_sessions_init();
}