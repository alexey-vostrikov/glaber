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
#include "common.h"
#include "zbxserver.h"
#include "../../libs/zbxexec/worker.h"
#include "glb_pinger.h"
#include "../pinger/pinger.h"
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"

#define PINGER_SEND_PACKETS_EVENT 64

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
	u_int64_t lastresolve; //when it was last resolved, it's a kind of a DNS cache
	char state; //internal item state to distinguish async ops
	char *ip; //ip address of the host
	int count; //how many packets to send
    unsigned int timeout; //timeout in ms - for how long to wait for the packet
	u_int64_t lastpacket_sent; //to control proper intrevals between packets in case is for some reason 
							   //there was a deleay in sending a packet, we need control that next 
							   //packet won't be sent too fast

	double	min;
	double	sum;
	double	max;
	int	rcv;
	int sent;
} pinger_item_t;

typedef struct {
    u_int64_t async_delay; //time between async calls to detect and properly handle timeouts
    event_queue_t *events;
    GLB_EXT_WORKER *worker;
    u_int64_t sent_packets;
} pinger_conf_t;

static pinger_conf_t conf;

extern int CONFIG_GLB_PINGER_FORKS;
extern int CONFIG_PINGER_FORKS;
extern int CONFIG_ICMP_METHOD;
extern char *CONFIG_GLBMAP_LOCATION;
extern char *CONFIG_GLBMAP_OPTIONS;
extern char *CONFIG_SOURCE_IP;


static void glb_pinger_submit_result(poller_item_t *poller_item, int status, char *error, u_int64_t mstime) {
    u_int64_t value_uint64;
    double value_dbl;
    AGENT_RESULT result;
    
    pinger_item_t *pinger_item=poller_get_item_specific_data(poller_item);

    init_result(&result);
        
    LOG_DBG("In %s: Starting, itemid is %ld, status is %d, error is '%s', mstime is %ld", __func__,
                poller_get_item_id(poller_item), status, error, mstime);
    
    switch (status) {
        case SUCCEED: {
           
            //value calculaition logic is taken from pinger.c process_value
            switch (pinger_item->icmpping)
			{
				case ICMPPING:
					value_uint64 = (0 != pinger_item->rcv ? 1 : 0);
                    SET_UI64_RESULT(&result, value_uint64);
                
                    poller_preprocess_value(poller_item, &result , mstime, ITEM_STATE_NORMAL, NULL);

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
                    //glbmap returns value in milliseconds
                    //but zabbix expects seconds
                    value_dbl = value_dbl/1000;

					if (0 < value_dbl && ZBX_FLOAT_PRECISION > value_dbl)
						value_dbl = ZBX_FLOAT_PRECISION;
                        SET_DBL_RESULT(&result, value_dbl);
                        //zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting  min/max/avg value %f",poller_item->itemid,value_dbl);
                        poller_preprocess_value(poller_item, &result , mstime, ITEM_STATE_NORMAL, NULL);
				    break;
				case ICMPPINGLOSS:
					value_dbl = (100 * (pinger_item->count - pinger_item->rcv)) / (double)pinger_item->count;
					    SET_DBL_RESULT(&result, value_dbl);
                        //zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting loss value %f",poller_item->itemid,value_dbl);
                        poller_preprocess_value(poller_item, &result , mstime, ITEM_STATE_NORMAL, NULL);
					break;
                default: 
                    zabbix_log(LOG_LEVEL_WARNING,"Inknown ICMP processing type %d for item %ld - this is a programming BUG",
                        pinger_item->icmpping, poller_get_item_id(poller_item));
                    THIS_SHOULD_NEVER_HAPPEN;
                    exit(-1);
			}

            free_result(&result);
            break;
         }
        default:
            poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, error);
            break;
    }
    
    //marking that polling has finished
    poller_return_item_to_queue(poller_item);
    pinger_item->state = POLL_QUEUED;
    
    DEBUG_ITEM(poller_get_item_id(poller_item),"In %s: marked item %ld as available for polling ", __func__);
      
    LOG_DBG("In %s: Finished", __func__);
}

/******************************************************************************
 * tries to resolve the host ip to ipv4 ip addr fails if it cannot
 * whenever glbmap starts supporting ipv6, this will be obsoleted
 * it's a fixed zbx_host_by_ip func
 * ***************************************************************************/
#define MAX_RESOLVE_TIME 3
#define RESOLVE_ACCOUNT_TIME 5

static  int	zbx_getipv4_by_host(const char *host, char *ip, size_t iplen)
{
	struct addrinfo	hints, *ai = NULL;
    int rc = FAIL;

    static u_int64_t resolve_time;
    u_int64_t time_start; //in ms
    static unsigned int last_resolve_reset, resolve_fail_count;


    if (last_resolve_reset < time(NULL) - RESOLVE_ACCOUNT_TIME) {
        resolve_time = 0;
        resolve_fail_count = 0;
        last_resolve_reset = time(NULL);
    }
    
    //if resolving takes significant time, we'll fail
    //to not to interrupt normal polling flow, some items will go to unresolved state
    if (resolve_time > MAX_RESOLVE_TIME * 1000 ) {
        if (resolve_fail_count > 0) {
            zabbix_log(LOG_LEVEL_INFORMATION, "glb_icmp: not resolving %s : limiting DNS due to slow response: there was %d resolve fails during %d seconds",
                    host, resolve_fail_count, RESOLVE_ACCOUNT_TIME);
        }
        resolve_fail_count++;
        return FAIL;
    }

    time_start = glb_ms_time();

	assert(ip);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (0 != getaddrinfo(host, NULL, &hints, &ai))
	{
		ip[0] = '\0';
		goto out;
	}

	switch(ai->ai_addr->sa_family) {
		case AF_INET:
			inet_ntop(AF_INET, &(((struct sockaddr_in *)ai->ai_addr)->sin_addr), ip, iplen);
            rc = SUCCEED;
			break;
		default:
			ip[0] = '\0';
			break;
	}

out:
	if (NULL != ai)
		freeaddrinfo(ai);
    
    resolve_time += glb_ms_time() - time_start;
    LOG_DBG("Resolving of %s took %ld msec", host, glb_ms_time() - time_start);

    return rc;
}

/******************************************************************************
 * item init - from the general dc_item to compactly init and store a specific pinger		  * 
 * ***************************************************************************/
static int init_item(void *m_conf, DC_ITEM *dc_item, poller_item_t *poller_item) {
	
    int	num, count, interval, size, timeout, rc;
    char ip_addr[MAX_ID_LEN], *ip=NULL;
    char *parsed_key = NULL;
    char error[MAX_STRING_LEN];
    char *addr = NULL;
    pinger_item_t *pinger_item;
	zbx_timespec_t timespec;
    icmpping_t icmpping;
    icmppingsec_type_t	type;
    LOG_DBG( "In %s() Started", __func__);
    
    pinger_item = (pinger_item_t *) zbx_calloc(NULL, 0, sizeof(pinger_item_t));
    
    poller_set_item_specific_data(poller_item, pinger_item);
    
    ZBX_STRDUP(parsed_key, dc_item->key_orig);
    
	if (SUCCEED != substitute_key_macros(&parsed_key, NULL, dc_item, NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error)))
        return FAIL;
	
    if (SUCCEED != parse_key_params(parsed_key, dc_item->interface.addr, &icmpping, &addr, &count,
					&interval, &size, &timeout, &type, error, sizeof(error)))
	    return FAIL;
    
    pinger_item->ip = NULL;
    pinger_item->addr = addr;
    pinger_item->lastresolve=0;
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
	
    zbx_free(parsed_key);
    LOG_DBG( "In %s() Ended", __func__);
    return SUCCEED;
}

/******************************************************************
 * Sends the actual packet (request to worker to send a packet
 * ****************************************************************/
static int send_ping(poller_item_t *poller_item) {
    
    char request[MAX_STRING_LEN];
    char ip_addr[MAX_ID_LEN];
    char *ip;
    u_int64_t mstime = glb_ms_time();
    LOG_DBG("In %s() Started", __func__);
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
  
    pinger_item->lastpacket_sent = glb_ms_time();
    
    DEBUG_ITEM(poller_get_item_id(poller_item),"Sending pings to addr %s, %d seconds to resolve ", 
                pinger_item->addr, pinger_item->lastresolve + GLB_DNS_CACHE_TIME - time(NULL) );

    if (pinger_item->lastresolve < time(NULL) - GLB_DNS_CACHE_TIME) {
        //doing resolving 
        //now additionally we'll resolve the host's address, this is a sort of dns cache
        DEBUG_ITEM(poller_get_item_id(poller_item),"Doing resolve");
        if (NULL == pinger_item->addr) {
           
            glb_pinger_submit_result(poller_item,CONFIG_ERROR,"Cannot resolve item to IPv4 addr, hostname is not set", mstime);
            zabbix_log(LOG_LEVEL_DEBUG, "Ping will not be send - empty hostname");
            return FAIL;
        }
   
         if (SUCCEED == is_ip4( pinger_item->addr)) {
            ip = pinger_item->addr;
        } else {
            //lets try to resolve the string to ipv4 addr, if we cannot - then dismiss the host as 
            //glbmap doesn't support ipv6 addr space yet
            if (SUCCEED == zbx_getipv4_by_host( pinger_item->addr, ip_addr, MAX_ID_LEN)) {
                ip = ip_addr;
            } else {
                DEBUG_ITEM(poller_get_item_id(poller_item), "Cannot resolve item addr '%s' to IPv4 addr, check hostname or use fping to IPv6", pinger_item->addr);
                
                glb_pinger_submit_result(poller_item,CONFIG_ERROR,"Cannot resolve item to IPv4 addr, check hostname or use fping to IPv6", mstime);
                
                return FAIL;
            }
        }
        
        pinger_item->ip=zbx_strdup(pinger_item->ip,ip);
        pinger_item->lastresolve = mstime;
        
        DEBUG_ITEM(poller_get_item_id(poller_item), "Host %s resolved to %s will cache it for %d seconds",pinger_item->addr, ip, GLB_DNS_CACHE_TIME);
    }
    
    zbx_snprintf(request,MAX_STRING_LEN,"%s %d %ld",pinger_item->ip, pinger_item->size, poller_get_item_id(poller_item) );
   // LOG_INF("Sending request for ping: %s, worker is %p", request, conf->worker);
    
    
    if (SUCCEED != glb_worker_request(conf.worker, request) ) {
        //sending config error status for the item
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't send request for ping: %s",request);
        glb_pinger_submit_result(poller_item, CONFIG_ERROR ,"Couldn't start or pass request to glbmap", mstime);
        return FAIL;
    }
   
    pinger_item->sent++;
    pinger_item->lastpacket_sent = mstime;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
    return SUCCEED;
}
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
static void free_item(void *m_conf,  poller_item_t *glb_poller_item ) {
    
    //LOG_DBG( "In %s() started", __func__);
    pinger_item_t *pinger_item = poller_get_item_specific_data(glb_poller_item);
	
    zbx_free(pinger_item->ip);
    zbx_free(pinger_item->addr);
    
    pinger_item->ip = NULL;
    zbx_free(pinger_item);
    
    DEBUG_ITEM( poller_get_item_id(glb_poller_item), "Has been removed from the pinger poller");
  //  LOG_INF( "In %s() Ended", __func__);
}

ITEMS_ITERATOR(check_pinger_timeout_cb) {
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    u_int64_t mstime = glb_ms_time();
    pinger_conf_t *conf = poller_data;

    if (POLL_POLLING == pinger_item->state ) {
        if ( pinger_item->sent == pinger_item->count &&  
             pinger_item->lastpacket_sent < mstime - pinger_item->timeout - conf->async_delay  ) {
                                
            DEBUG_ITEM(poller_get_item_id(poller_item),"In %s: Item set to queued state in the timeout handler",__func__);
            
            poller_inc_responces(); 
            //this is timeout but overall operation has succeeded
            glb_pinger_submit_result( poller_item, SUCCEED, NULL, mstime);
        } else {
            DEBUG_ITEM(poller_get_item_id(poller_item), 
                "Not marking item as timed out: sent %d, recv %d, count %d, glb_time-lastpacket: %ld, state: %d q_delay: %ld",
                pinger_item->sent, pinger_item->rcv, pinger_item->count, mstime - pinger_item->lastpacket_sent, 
                pinger_item->state,conf->async_delay);
        }
        
        //one more case: if glmap has failed, we might not send the packets, so we never see them back
        //so accouning delays between async io runs (sometimes DNS queries might impose quite a delay)
        if ( pinger_item->lastpacket_sent == 0 || 
             mstime - pinger_item->lastpacket_sent > CONFIG_TIMEOUT * 1000 + conf->async_delay ) {
            //this is probably some kind of error - we couldn't sent at all or it really took a long time since the last packet
            DEBUG_ITEM(poller_get_item_id(poller_item), 
                "State problem: item is timed out: sent %d, recv %d, count %d, glb_time-lastpacket: %ld, state: %d BUG?, async_delay is %ld",
                pinger_item->sent, pinger_item->rcv, pinger_item->count, mstime - pinger_item->lastpacket_sent, 
                pinger_item->state,conf->async_delay);
                                       
            glb_pinger_submit_result(poller_item, SUCCEED, "Internal pinging error has happened", mstime);
        }
    }
    /* we want to run over all the items */
    /* TODO: with introducing of poller events intrface this func should become a callback
      of the timeout event */
    return POLLER_ITERATOR_CONTINUE;
}


/******************************************************************************
 * timeouts handling - go through all the items and see if there are 
 * timed out measurements, if so - mark the measurement as timed it
 * and start a next one if the last measurement has finished, then
 * submit the result
 * ***************************************************************************/
static void pinger_handle_timeouts(void *m_conf) {
    static u_int64_t last_check=0;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //once a second, may be long if we check a few millions of hosts   
    if (last_check + 1000 > glb_ms_time() ) 
        return;
    
    last_check = glb_ms_time();
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() starting timouts check", __func__);

    poller_items_iterate(check_pinger_timeout_cb, NULL);    

    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/******************************************************************************
 * start pinging an item             										  * 
 * ***************************************************************************/
static void start_ping(void *m_conf, poller_item_t *poller_item)
{
    pinger_conf_t *conf = m_conf;

    int i;
    u_int64_t mstime = glb_ms_time();
    LOG_DBG("In %s() Started", __func__);
	    
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    char request[MAX_STRING_LEN];
    
    pinger_item->rcv = 0;
    pinger_item->min = 0;
    pinger_item->max = 0;
    pinger_item->sum = 0;
    pinger_item->sent = 0;
  
    poller_inc_requests();

    pinger_item->state = POLL_POLLING;

    //sending the first packet immediately
    if (SUCCEED != send_ping(poller_item)) 
        return;
    
    u_int64_t send_time = mstime;
    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Planing %d pings for item to host %s interval is %d, timeout is %d",
                pinger_item->count-1, pinger_item->addr,pinger_item->interval, pinger_item->timeout);
    
    for (i=0; i< pinger_item->count-1; i++) {
        //creating new events
        pinger_event_t *event=zbx_malloc(NULL,sizeof(pinger_event_t));        
        send_time += pinger_item->interval;
        event->time = send_time;
        event->itemid = poller_get_item_id(poller_item);

        event_queue_add_event(conf->events,send_time, PINGER_SEND_PACKETS_EVENT, event);
        
    }
   
    LOG_DBG( "In %s() Ended", __func__);
}

#define GLB_MAX_SEND_TIME 10

EVENT_QUEUE_CALLBACK(send_packets_cb)
{
    static u_int64_t tmp_time = 0;
    u_int64_t mstime = glb_ms_time();
    poller_item_t *poller_item;

    pinger_event_t *pinger_event = (pinger_event_t *)data;
    u_int64_t etime = pinger_event->time;
    u_int64_t itemid = pinger_event->itemid;

    if (tmp_time + 10 * 1000 < mstime)
    {

        if (mstime - etime > 10 * 1000)
        {
            LOG_WRN("Pinging is late %ld milliseconds", mstime - etime);
        }
        tmp_time = mstime;
    }

    if (etime > mstime)
        return FAIL;

    zbx_free(pinger_event);

    if (NULL == (poller_item = poller_get_pollable_item(itemid)))
        return FAIL;
    
        pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);

        // report state error, but unresloved are ok
        if (NULL != pinger_item->ip && POLL_POLLING != pinger_item->state)
        {
            DEBUG_ITEM(poller_get_item_id(poller_item), "In %s: sheduled item addr %s in state %d. (DUP!)",
                       __func__, pinger_item->ip, pinger_item->state);
            return FAIL;
        }

        if (pinger_item->lastpacket_sent + pinger_item->interval > mstime)
        {
            // this packet event has happened too fast, lets requeue it
            // zabbix_log(LOG_LEVEL_DEBUG,"Rescheduling next packet for item %ld host %s", poller_item->itemid, pinger_item->ip);

            pinger_event_t *event = zbx_malloc(NULL, sizeof(pinger_event_t));
            event->time = pinger_item->lastpacket_sent + pinger_item->interval;
            event->itemid = poller_get_item_id(poller_item);
          
           event_queue_add_event(conf.events, event->time, PINGER_SEND_PACKETS_EVENT, event);
           return FAIL;
        }

        send_ping(poller_item);

        // updating timings here to compensate delays
        // pinger_item->finish_time = current_glb_time + CONFIG_TIMEOUT *1000;

        conf.sent_packets++;
        return SUCCEED;
    
}


/*************************************************************************
 * Account the packet, signals by exit status that polling has finished
 * ***********************************************************************/
static int pinger_process_response(pinger_item_t *pinger_item, int rtt) {

    if ( rtt > pinger_item->timeout ) {
        zabbix_log(LOG_LEVEL_DEBUG,"Ignoring packet from host %s as it came after timeout rtt:%d  timeout: %d)",pinger_item->ip, rtt,  pinger_item->timeout );
        return SUCCEED;
    }

    if (0 == pinger_item->rcv || pinger_item->min > rtt ) pinger_item->min = rtt;
	if (0 == pinger_item->rcv || pinger_item->max < rtt) pinger_item->max = rtt;
	
    pinger_item->sum += rtt;
	pinger_item->rcv++;

    if (pinger_item->count == pinger_item->rcv) {
        return POLL_FINISHED;
    } 

    return SUCCEED;
}

/* this might be imporved by creating ip->items index, but 
 so far it looks like maintaining such an index is more expensive than 
 do a full serach of really rare hosts not capable to echo back icmp payload */
ITEMS_ITERATOR(process_item_by_ip_cb) {
    //checking if the item has matched ip 
    char *ip = data;
    pinger_item_t *pinger_item = poller_get_item_specific_data(poller_item);
    u_int32_t rtt;
    pinger_conf_t *conf = poller_data;

    if ( NULL == pinger_item->ip || 0 != strcmp(pinger_item->ip,ip)) 
        return POLLER_ITERATOR_CONTINUE;

    rtt = GLB_PINGER_DEFAULT_RTT;
    DEBUG_ITEM(poller_get_item_id(poller_item), "Item is matched by ip %s rtt is %d", ip, rtt);
             
    if ( POLL_POLLING != pinger_item->state) {
        DEBUG_ITEM(poller_get_item_id(poller_item),
             "Arrived responce for item which is not in polling state (%d) (DUP!)",pinger_item->state);
    }

    if (POLL_FINISHED == pinger_process_response(pinger_item, rtt)) {

        DEBUG_ITEM(poller_get_item_id(poller_item), "Got the final packet for the item with broken echo data");
        poller_inc_responces();
        glb_pinger_submit_result(poller_item, SUCCEED, NULL, glb_ms_time());
                    
        pinger_item->state = POLL_QUEUED; 
        poller_return_item_to_queue(poller_item);
                        
    } 
    return POLLER_ITERATOR_STOP;
}

static void process_worker_results() {
    char *worker_response = NULL;
    zbx_json_type_t type;
    u_int64_t mstime = glb_ms_time();
    
    LOG_DBG("In %s: starting", __func__);
       
    char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
    u_int64_t itemid_l;
    int rtt_l;
    struct zbx_json_parse jp_resp;
    poller_item_t *poller_item;

    //reading all the responses we have so far from the worker
    while (SUCCEED == async_buffered_responce(conf.worker, &worker_response)) {
        
        if ( NULL == worker_response) //read succesifull, no data yet
            break;
        
        zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s", worker_response);
        
        if (SUCCEED != zbx_json_open(worker_response, &jp_resp)) {
		    zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't ropen JSON response from glbmap %s", worker_response);
		    continue;
	    }
            
        if (SUCCEED != zbx_json_value_by_name(&jp_resp, "saddr", ip, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(&jp_resp, "rtt", rtt_s, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(&jp_resp, "itemid", itemid_s, MAX_ID_LEN, &type)       
        ) {
            zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the glbmap: %s",worker_response);
            continue;
        }

        rtt_l = strtol(rtt_s,NULL,10);
        itemid_l = strtol(itemid_s,NULL,10);
        
        DEBUG_ITEM(itemid_l,"Parsed itemid in icmp payload");

        if (NULL != (poller_item = poller_get_pollable_item(itemid_l)) ) {
            
            pinger_item_t *pinger_item =poller_get_item_specific_data(poller_item);
            
            if (pinger_item->ip != NULL && 0 != strcmp( ip, pinger_item->ip) ) {
                zabbix_log(LOG_LEVEL_WARNING,"Arrived ICMP responce with mismatched ip %s and itemid %ld",ip, itemid_l);
                continue;
            }
            
            if ( POLL_POLLING != pinger_item->state) {
                DEBUG_ITEM(poller_get_item_id(poller_item),
                    "Arrived responce for item which is not in polling state (%d) (DUP!)", pinger_item->state);
            }
            
            if (POLL_FINISHED == pinger_process_response(pinger_item, rtt_l)) {
            
                DEBUG_ITEM(poller_get_item_id(poller_item), "Got the final packet for the item");
                poller_inc_responces();
                                  
                glb_pinger_submit_result(poller_item,SUCCEED,NULL, mstime);
                    
                //theese are two different things, need to set both
                pinger_item->state = POLL_QUEUED; 
                poller_return_item_to_queue(poller_item);
            } 
        } else {
            poller_items_iterate(process_item_by_ip_cb, ip);
        }
    }

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}


/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * note: doesn't care about the timeouts - it's done by the poller globbaly   *
 * ***************************************************************************/
static void handle_async_io(void *m_conf)
{

    static u_int64_t lastrun = 0;
    u_int64_t queue_delay = 0;
    u_int64_t mstime = glb_ms_time();

    LOG_DBG("In %s() Started", __func__);

    queue_delay = event_queue_get_delay(conf.events, mstime);

    conf.async_delay = mstime - lastrun;

    if (queue_delay > conf.async_delay)
        conf.async_delay = queue_delay;

    lastrun = mstime;

    process_worker_results();
    /*note - this will send all sheduled packets */
    if (0 == event_queue_process_events(conf.events, 8192)) {
        usleep(10000);
    }
    
    pinger_handle_timeouts(&conf);

    LOG_DBG("In %s: Ended", __func__);
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
static void pings_shutdown(void *m_conf) {
	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
	pinger_conf_t *conf = m_conf;
    LOG_DBG( "In %s() started", __func__);
    LOG_DBG( "In %s() Ended", __func__);
}
static int forks_count(void *m_conf) {
	return CONFIG_GLB_PINGER_FORKS;
}
/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void glb_pinger_init(poll_engine_t *poll_conf) {

	int i;
	char init_string[MAX_STRING_LEN];
    char full_path[MAX_STRING_LEN];
    char add_params[MAX_STRING_LEN];
	
    bzero(&conf, sizeof(pinger_conf_t));
	conf.sent_packets = 0;
    
    poller_set_poller_module_data(&conf);
    poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_ping, pings_shutdown, forks_count);    
 
    add_params[0]='\0';

    if (-1 == access(CONFIG_GLBMAP_LOCATION, X_OK))
	{
        zabbix_log(LOG_LEVEL_WARNING,"Couldn't find glbmap at the path: %s or it isn't set to be executable: %s", CONFIG_GLBMAP_LOCATION, zbx_strerror(errno));
		exit(-1);
	};

    if ( NULL != CONFIG_SOURCE_IP && SUCCEED == is_ip4(CONFIG_SOURCE_IP) ) {
        zbx_snprintf(add_params,MAX_STRING_LEN,"-S %s ",CONFIG_SOURCE_IP);
    } 

    if ( NULL != CONFIG_GLBMAP_OPTIONS ) {
         zbx_snprintf(add_params,MAX_STRING_LEN,"%s ",CONFIG_GLBMAP_OPTIONS);
    }
    
    zbx_snprintf(init_string,MAX_STRING_LEN,"{\"path\":\"%s\", \"params\":\"%s-v 0 -q -Z -r 100000 --probe-module=glb_icmp --output-module=json --output-fields=saddr,rtt,itemid,success\"}\n",
        CONFIG_GLBMAP_LOCATION, add_params);

	conf.worker = glb_init_worker(init_string);
    if (NULL == conf.worker) {
        zabbix_log(LOG_LEVEL_WARNING,"Cannot create pinger woker, check the coniguration: '%s'",init_string);
        exit(-1);
    }

    conf.worker->mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    conf.worker->mode_from_worker = GLB_WORKER_MODE_NEWLINE;
    conf.worker->async_mode = 1;

    conf.events = event_queue_init(NULL);
    event_queue_add_callback(conf.events, PINGER_SEND_PACKETS_EVENT, send_packets_cb );

    LOG_INF("Conf addr is %p ", conf);
	LOG_DBG("In %s: Ended", __func__);
}

