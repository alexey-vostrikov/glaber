/*
** Glaber
** Copyright (C) 2001-2028 Glaber JSC
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
#include "zbxserver.h"
#include "zbx_item_constants.h"
#include "../../libs/zbxexec/worker.h"
#include "glb_pinger.h"
#include "../pinger/pinger.h"
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"
#include "poller_async_io.h"
#include "zbxsysinfo.h"
#include "zbxip.h"

extern int  CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

#define PINGER_FLOAT_PRECISION	0.0001

typedef struct
{
	u_int64_t time;
	u_int64_t itemid;
} pinger_event_t;

typedef struct {
	u_int64_t finish_time;
	icmpping_t icmpping; //type of measurement (accesibility check or rtt)
	icmppingsec_type_t	type; //type of rtt check - min,max,avg
	int 	interval; //in ms time between packets
	int 	size; //payload size
	char *addr; //hostname to use
	//u_int64_t lastresolve; //when it was last resolved, it's a kind of a DNS cache
	char state; //internal item state to distinguish async ops
	char *ip; //ip address of the host
	int count; //how many packets to send
    unsigned int timeout; 

	double	min;
	double	sum;
	double	max;
	int	rcv;
	int sent;
    poller_event_t *packet_event;
    poller_event_t *timeout_event;
} pinger_item_t;

typedef struct {
    u_int64_t async_delay; //time between async calls to detect and properly handle timeouts
    glb_worker_t *worker;
    u_int64_t sent_packets;
    poller_event_t *worker_event;

} pinger_conf_t;

static pinger_conf_t conf;

extern int CONFIG_ICMP_METHOD;
extern char *CONFIG_GLBMAP_LOCATION;
extern char *CONFIG_GLBMAP_OPTIONS;
extern char *CONFIG_SOURCE_IP;
extern int CONFIG_ICMP_NA_ON_RESOLVE_FAIL;

static int pinger_process_response(poller_item_t *poller_item, int rtt) {
    
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    
    DEBUG_ITEM( poller_get_item_id(poller_item),"Processing ping echo, packet %d out of %d", pinger_item->rcv + 1, pinger_item->count );
    
    if ( rtt > pinger_item->timeout ) {
        DEBUG_ITEM( poller_get_item_id(poller_item),"Ignoring packet from host %s as it came after timeout rtt:%d  timeout: %d)",pinger_item->ip, rtt,  pinger_item->timeout );
        return SUCCEED;
    }

    if (0 == pinger_item->rcv || pinger_item->min > rtt ) pinger_item->min = rtt;
	if (0 == pinger_item->rcv || pinger_item->max < rtt) pinger_item->max = rtt;
	
    pinger_item->sum += rtt;
	pinger_item->rcv++;

    if (pinger_item->count == pinger_item->rcv) {
        return POLL_FINISHED;
    } 
    poller_register_item_succeed(poller_item);
    return SUCCEED;
}


static void finish_icmp_poll(poller_item_t *poller_item, int status, char *error) {
    u_int64_t value_uint64;
    double value_dbl;
    //AGENT_RESULT result;
    
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    poller_disable_event(pinger_item->timeout_event);
    
  //  zbx_init_agent_result(&result);
        
    LOG_DBG("In %s: Starting, itemid is %ld, status is %d, error is '%s'", __func__,
                poller_get_item_id(poller_item), status, error);
      
    switch (status) {
        case SUCCEED: {
           
            //value calculaition logic is taken from pinger.c process_value
            switch (pinger_item->icmpping)
			{
				case ICMPPING:
					value_uint64 = (0 != pinger_item->rcv ? 1 : 0);
                    poller_preprocess_uint64(poller_item, NULL, value_uint64);
                    break;
				case ICMPPINGSEC:
					switch (pinger_item->type)
					{
						case ICMPPINGSEC_MIN:
							value_dbl = pinger_item->min;
							break;
						case ICMPPINGSEC_MAX:
							value_dbl = pinger_item->max;
							break;
						case ICMPPINGSEC_AVG:
							value_dbl = (0 != pinger_item->rcv ? pinger_item->sum / pinger_item->rcv : 0);
							break;
					}
                    value_dbl = value_dbl/1000;

					if (0 < value_dbl && PINGER_FLOAT_PRECISION > value_dbl)
                        value_dbl = PINGER_FLOAT_PRECISION;
                    poller_preprocess_dbl(poller_item, NULL, value_dbl);
				    break;
				case ICMPPINGLOSS:
					value_dbl = (100 * (pinger_item->count - pinger_item->rcv)) / (double)pinger_item->count;
					poller_preprocess_dbl(poller_item, NULL, value_dbl);
					break;
                default: 
                    zabbix_log(LOG_LEVEL_WARNING,"Inknown ICMP processing type %d for item %ld - this is a programming BUG",
                        pinger_item->icmpping, poller_get_item_id(poller_item));
                    THIS_SHOULD_NEVER_HAPPEN;
                    exit(-1);
			}

      //      zbx_free_agent_result(&result);
            break;
         }
        default:
            poller_preprocess_error(poller_item, error);
            break;
    }
    
    //marking that polling has finished
    poller_return_item_to_queue(poller_item);
    
    pinger_item->state = POLL_QUEUED;
    
    DEBUG_ITEM(poller_get_item_id(poller_item),"In %s: marked item as available for polling ", __func__);
      
    LOG_DBG("In %s: Finished", __func__);
}
/* this might be imporved by creating ip->items index, but 
 so far it looks like maintaining such an index is more expensive than 
 do a full serach of really rare hosts not capable to echo back icmp payload */
ITEMS_ITERATOR(process_item_by_ip_cb) {
    //checking if the item has matched ip 
    char *ip = data;
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
//    zbx_timespec_t ts;
    u_int32_t rtt;


    if ( NULL == pinger_item->ip || 0 != strcmp(pinger_item->ip,ip)) 
        return POLLER_ITERATOR_CONTINUE;

    rtt = GLB_PINGER_DEFAULT_RTT;
    DEBUG_ITEM(poller_get_item_id(poller_item), "Item is matched by ip %s rtt is %d", ip, rtt);
             
    if ( POLL_POLLING != pinger_item->state) {
        DEBUG_ITEM(poller_get_item_id(poller_item),
             "Arrived responce for item which is not in polling state (%d) (DUP!)",pinger_item->state);
    }

    if (POLL_FINISHED == pinger_process_response(poller_item, rtt)) {

        DEBUG_ITEM(poller_get_item_id(poller_item), "Got the final packet for the item with broken echo data");
        poller_inc_responses();
        finish_icmp_poll(poller_item, SUCCEED, NULL);
                    
        pinger_item->state = POLL_QUEUED; 
        poller_return_item_to_queue(poller_item);
                        
    } 
    return POLLER_ITERATOR_STOP;
}

static void process_worker_results_cb(poller_item_t *garbage, void *data) {
    char *worker_response = NULL;
    zbx_json_type_t type;
    //u_int64_t mstime = glb_ms_time();
    
    //LOG_INF("In %s: start", __func__);
       
    char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
    u_int64_t itemid_l;
    int rtt_l;
    struct zbx_json_parse jp_resp;
    poller_item_t *poller_item;

    //reading all the responses we have so far from the worker
    while (SUCCEED == glb_worker_get_async_buffered_responce(conf.worker, &worker_response)) {
        
        if ( NULL == worker_response) //read succesifull, no data yet
            break;
        
        zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s", worker_response);
        
        if (SUCCEED != zbx_json_open(worker_response, &jp_resp)) {
		    zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON response from glbmap %s", worker_response);
            zbx_free(worker_response);
            continue;
	    }
            
        if (SUCCEED != zbx_json_value_by_name(&jp_resp, "saddr", ip, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(&jp_resp, "rtt", rtt_s, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(&jp_resp, "itemid", itemid_s, MAX_ID_LEN, &type)       
        ) {
            zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response all fields (saddr, rtt, itemid) from the glbmap: %s",worker_response);
            zbx_free(worker_response);
            continue;
        }
        
        zbx_free(worker_response);

        rtt_l = strtol(rtt_s,NULL,10);
        itemid_l = strtol(itemid_s,NULL,10);
        
        DEBUG_ITEM(itemid_l,"Parsed itemid in icmp payload");

        if (NULL == (poller_item = poller_get_poller_item(itemid_l)) )  {
            poller_items_iterate(process_item_by_ip_cb, ip);
            continue;
        }
        
        pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
            
        if (pinger_item->ip != NULL && 0 != strcmp( ip, pinger_item->ip) ) {
            DEBUG_ITEM(poller_get_item_id(poller_item), "Arrived ICMP responce with mismatched ip %s",ip);
            continue;
        }
            
        if ( POLL_POLLING != pinger_item->state) {
            DEBUG_ITEM(poller_get_item_id(poller_item),
                "Arrived responce for item which is not in polling state (%d) (DUP or late arrival)", pinger_item->state);
            continue;
        }
            
        if (POLL_FINISHED == pinger_process_response(poller_item, rtt_l)) {
            DEBUG_ITEM(poller_get_item_id(poller_item), "Got the final packet for the item");
 
            poller_inc_responses();
            finish_icmp_poll(poller_item, SUCCEED, NULL);
                    
           //theese are two different things, need to set both
            pinger_item->state = POLL_QUEUED; 
            poller_return_item_to_queue(poller_item);
        } 

    }
    
    poller_run_fd_event(conf.worker_event);//, worker_get_fd_from_worker(conf.worker));
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}

int subscribe_worker_fd() {
    
    if (NULL != conf.worker_event) {
      //  LOG_INF("Clearing the old event data %p", conf.worker_event);
        poller_destroy_event(conf.worker_event);
    }

    conf.worker_event = poller_create_event(NULL, process_worker_results_cb,  worker_get_fd_from_worker(conf.worker),  NULL, 0);
    poller_run_fd_event(conf.worker_event);
}

/******************************************************************
 * Sends the actual packet (request to worker to send a packet
 * ****************************************************************/
static int send_icmp_packet(poller_item_t *poller_item) {
    
    char request[MAX_STRING_LEN];
    static int last_worker_pid = 0;

    //u_int64_t now = glb_ms_time();
    LOG_DBG("In %s() Started", __func__);
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
  
    DEBUG_ITEM(poller_get_item_id(poller_item),"Sending ping %d out %d to addr %s(%s)", 
                pinger_item->sent + 1, pinger_item->count, pinger_item->addr, pinger_item->ip);

    zbx_snprintf(request,MAX_STRING_LEN,"%s %d %ld",pinger_item->ip, pinger_item->size, poller_get_item_id(poller_item) );
   // LOG_INF("Sending request for ping: %s, worker is %p", request, conf->worker);
    
    if (SUCCEED != glb_worker_send_request(conf.worker, request) ) {
        //sending config error status for the item
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't send request for ping: %s",request);
        finish_icmp_poll(poller_item, CONFIG_ERROR ,"Couldn't start or pass request to glbmap");
        return FAIL;
    }
   
    pinger_item->sent++;
 //   pinger_item->lastpacket_sent = now;

    if (last_worker_pid != glb_worker_get_pid(conf.worker)) {
        LOG_INF("glbmap worker's PID (%d) is changed, subscibing the new FD (%d)", last_worker_pid, glb_worker_get_pid(conf.worker));
        last_worker_pid = glb_worker_get_pid(conf.worker);
        subscribe_worker_fd(worker_get_fd_from_worker(conf.worker));
    }
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
    return SUCCEED;
}

void send_timeout_cb(poller_item_t *poller_item, void *data) {
    DEBUG_ITEM(poller_get_item_id(poller_item), "In item timeout handler, submitting result");
    finish_icmp_poll(poller_item, SUCCEED, NULL);
    poller_register_item_timeout(poller_item);
}


//sends the packet, if all packets are sent, reset packet sent callback and sets timeout callback
void send_packet_cb(poller_item_t *poller_item, void *data) {
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
       
    send_icmp_packet(poller_item);

    if (pinger_item->sent < pinger_item->count) {
        DEBUG_ITEM(poller_get_item_id(poller_item), "Planing next packet send in %d milliseconds", pinger_item->interval);
        poller_run_timer_event(pinger_item->packet_event, pinger_item->interval);
    } else {
        DEBUG_ITEM(poller_get_item_id(poller_item), "Planing timeout in %d milliseconds", pinger_item->interval);
        //poller_destroy_event(pinger_item->time_event);
        //pinger_item->time_event = poller_create_event(poller_item, send_timeout_cb, 0,  NULL, 0);
        poller_run_timer_event(pinger_item->timeout_event, pinger_item->timeout);
    }
}

/******************************************************************************
 * item init - from the general dc_item to compactly init and store a specific pinger		  * 
 * ***************************************************************************/
static int init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
	
    int	num, count, interval, size, timeout, rc;
    char ip_addr[MAX_ID_LEN], *ip=NULL;
    char *parsed_key = NULL;
    char error[MAX_STRING_LEN];
    char *addr = NULL;
    pinger_item_t *pinger_item;
	zbx_timespec_t timespec;
    icmpping_t icmpping;
    icmppingsec_type_t	type;
    
    pinger_item = (pinger_item_t *) zbx_calloc(NULL, 0, sizeof(pinger_item_t));
    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Doing item init in icmp async poller");
    
    poller_set_item_specific_data(poller_item, pinger_item);
    
    ZBX_STRDUP(parsed_key, dc_item->key_orig);
    
	if (SUCCEED != zbx_substitute_key_macros(&parsed_key, NULL, dc_item, NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error))) {
        poller_preprocess_error(poller_item, error);
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't init in icmp poller: %s", error);
        return FAIL;
    }

    if (dc_item->interface.useip) 
        addr = dc_item->interface.ip_orig;
    else 
        addr = dc_item->interface.dns_orig;

    DEBUG_ITEM(poller_get_item_id(poller_item),"Interface host addr is %s ip %s dns %s addr %s", addr, dc_item->interface.ip_orig, dc_item->interface.dns_orig,
            dc_item->interface.addr);

    if (SUCCEED != zbx_parse_key_params(parsed_key, addr, &icmpping, &addr, &count,
					&interval, &size, &timeout, &type, error, sizeof(error))) {
        poller_preprocess_error(poller_item, error);
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't parse icmp parameters: %s", error);
        return FAIL;
    }
    
    pinger_item->ip = NULL;
    pinger_item->addr = addr;
    pinger_item->type = type;
    pinger_item->count = count;

    if (0 != interval )
        pinger_item->interval = interval;
    else 
        pinger_item->interval = GLB_DEFAULT_ICMP_INTERVAL;
        
    if (0 != timeout )
        pinger_item->timeout = timeout;
    else 
        pinger_item->timeout = GLB_DEFAULT_ICMP_TIMEOUT;
    
    if (0 != size )
        pinger_item->size = size;
    else 
        pinger_item->size = GLB_DEFAULT_ICMP_SIZE;
    
    pinger_item->icmpping = icmpping;
    pinger_item->finish_time = 0;
    pinger_item->min = 0;
	pinger_item->sum = 0;
	pinger_item->max = 0;
	pinger_item->rcv = 0;
    
    pinger_item->packet_event = poller_create_event(poller_item, send_packet_cb, 0, NULL, 0);
    pinger_item->timeout_event = poller_create_event(poller_item, send_timeout_cb, 0, NULL, 0);

    zbx_free(parsed_key);
    DEBUG_ITEM(poller_get_item_id(poller_item), "Succesifully finished item init in icmp async poller");
    return SUCCEED;
}

/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
static void free_item(poller_item_t *glb_poller_item ) {
    pinger_item_t *pinger_item = poller_get_item_specific_data(glb_poller_item);
	
    zbx_free(pinger_item->ip);
    zbx_free(pinger_item->addr);
    
    poller_destroy_event(pinger_item->packet_event);
    poller_destroy_event(pinger_item->timeout_event);

    zbx_free(pinger_item);
  
    DEBUG_ITEM( poller_get_item_id(glb_poller_item), "Has been removed from the pinger poller");
}

int needs_resolve(pinger_item_t *pinger_item) {
    u_int64_t now = glb_ms_time();

  //  if (pinger_item->lastresolve + GLB_DNS_CACHE_TIME > now) 
  //      return FAIL;
    
    if (SUCCEED == zbx_is_ip4(pinger_item->addr)) {
        //    pinger_item->lastresolve = now;
            pinger_item->ip = zbx_strdup(pinger_item->ip, pinger_item->addr);
            return FAIL;
    }

    return SUCCEED;    
}

void schedule_item_poll(poller_item_t *poller_item) {

    LOG_DBG("Start pinging item %ld", poller_get_item_id(poller_item));
    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Starting item ping");
    
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
     
    pinger_item->rcv = 0;
    pinger_item->min = 0;
    pinger_item->max = 0;
    pinger_item->sum = 0;
    pinger_item->sent = 0;
    pinger_item->state = POLL_POLLING;
   // pinger_item->time_event = poller_create_event(poller_item, send_packet_cb, 0,  NULL, 0);

    poller_inc_requests();
    send_packet_cb(poller_item, NULL);
}

static void resolved_callback(poller_item_t *poller_item, const char *addr) {
    char buf[128];
    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Item's host name has been resolved to %s", addr);

    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
  //  pinger_item->lastresolve = glb_ms_time();
    pinger_item->ip = zbx_strdup(pinger_item->ip, addr);

    schedule_item_poll(poller_item);
}

static void resolve_fail_callback(poller_item_t *poller_item) {
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    
    if (CONFIG_ICMP_NA_ON_RESOLVE_FAIL) {
        
        pinger_item->sent = pinger_item->count;
        pinger_item->rcv = 0;

        finish_icmp_poll(poller_item, SUCCEED, NULL);
        return;
    } 
    
    finish_icmp_poll(poller_item, FAIL, "Failed to resolve host name");
}

/******************************************************************************
 * start pinging an item             										  * 
 * ***************************************************************************/
static void start_ping(poller_item_t *poller_item)
{
    int i;
    u_int64_t mstime = glb_ms_time();
    LOG_DBG("In %s() Started", __func__);

    DEBUG_ITEM(poller_get_item_id(poller_item), "Start pinging time:");
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    
    if (POLL_POLLING == pinger_item->state) {
        DEBUG_ITEM(poller_get_item_id(poller_item), "Pinging is not possible right now: already pinging, skipping");
        return;
    }

    if (SUCCEED == needs_resolve(pinger_item)) {
       // LOG_INF("Item %ld needs resolving of name %s", poller_get_item_id(poller_item), pinger_item->addr );
        
        DEBUG_ITEM(poller_get_item_id(poller_item), "Sending item's hostname to async resolve");
        DEBUG_ITEM(poller_get_item_id(poller_item), "Sending item's hostname %s to async resolve", pinger_item->addr);

        poller_async_resolve(poller_item, pinger_item->addr);
        return;
    } else { 
        DEBUG_ITEM(poller_get_item_id(poller_item), "Item doesn't need resolving");
        schedule_item_poll(poller_item);
    }
    return;

}

static void handle_async_io(void)
{ 
    //in true async pollers this proc should always be empty!
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
static void pings_shutdown(void) {
	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
    poller_destroy_event(conf.worker_event);

}

static int forks_count(void) {
	return CONFIG_FORKS[GLB_PROCESS_TYPE_PINGER];
}
/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void glb_pinger_init(void) {

	char args[MAX_STRING_LEN], add_params[MAX_STRING_LEN];
	
    bzero(&conf, sizeof(pinger_conf_t));
	conf.sent_packets = 0;

    poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_ping, pings_shutdown, 
        forks_count,  resolved_callback, resolve_fail_callback);    
 
    add_params[0]='\0';

    if (-1 == access(CONFIG_GLBMAP_LOCATION, X_OK) )
	{
        zabbix_log(LOG_LEVEL_WARNING,"Couldn't find glbmap at the path: %s or it isn't set to be executable: %s", CONFIG_GLBMAP_LOCATION, zbx_strerror(errno));
		exit(-1);
	};

    if ( NULL != CONFIG_SOURCE_IP && SUCCEED == zbx_is_ip4(CONFIG_SOURCE_IP) ) {
        zbx_snprintf(add_params,MAX_STRING_LEN,"-S %s ",CONFIG_SOURCE_IP);
    } 

    if ( NULL != CONFIG_GLBMAP_OPTIONS ) {
         zbx_snprintf(add_params,MAX_STRING_LEN,"%s ",CONFIG_GLBMAP_OPTIONS);
    }
    
    zbx_snprintf(args, MAX_STRING_LEN, "%s-v 0 -q -Z -r 100000 --probe-module=glb_icmp --output-module=json --output-fields=saddr,rtt,itemid,success",
         add_params);

	conf.worker = glb_worker_init(CONFIG_GLBMAP_LOCATION, args, 60, 0, 0, 0);
    
    if (NULL == conf.worker) {
        zabbix_log(LOG_LEVEL_WARNING,"Cannot create pinger woker, check the coniguration: path '%s', args %s",CONFIG_GLBMAP_LOCATION, args);
        exit(-1);
    }

    worker_set_mode_to_worker(conf.worker, GLB_WORKER_MODE_NEWLINE);
    worker_set_mode_from_worker(conf.worker, GLB_WORKER_MODE_NEWLINE);
    //worker_set_async_mode(conf.worker, 1);
    
    conf.worker_event = NULL; //poller_create_event(NULL, process_worker_results_cb, NULL);

    LOG_DBG("In %s: Ended", __func__);
}

