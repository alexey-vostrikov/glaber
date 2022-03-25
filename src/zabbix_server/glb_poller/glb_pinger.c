/**********************************************************************************
 * So the async glb pinger. How it works?
 * Very well actually.
 * The logic and idea:
 * When time comes, the main glb_poller loop schdules a check
 * this adds the item to be checked into the internal hasmap of items 
 * thats are being checked right now.
 * Then immedaately a task for accesibility check is issued to the glbmap worker
 * on recieving an answer rtt is fixed in the results array and 
 * next poll is scheduled in the internal queue in interpacket delay millisecionds
 * if poll is finished (all results are gathered), then result is calculated
 * So there are two processed that keep an eye on how it all goes
 * High precision (to ms) time queue (binary tree) to start polling a next packet
 * And once a second all polled items are checked if they are not timed out
 * poller works in a loose linking mode: it must be assumed that an item may be removed or changed 
 * between calls and events
 * there is no hosts blacklisting - we ping everything we can as it has to be pinged
 
 * in case is glbmap fails, then all actievly pinged hosts are retried again
 */
#include "log.h"
#include "common.h"
#include "zbxserver.h"
#include "../../libs/zbxexec/worker.h"
#include "glb_pinger.h"
#include "../pinger/pinger.h"
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"

typedef struct {
	//config params
	//const char *key;
	u_int64_t finish_time;
	icmpping_t icmpping; //type of measurement (accesibility check or rtt)
	icmppingsec_type_t	type; //type of rtt check - min,max,avg
	int 	interval; //in ms time between packets
	int 	size; //payload size
	char *addr; //hostname to use
	unsigned int lastresolve; //when it was last resolved, it's a kind of a DNS cache
	char state; //internal item state to distinguish async ops
	char *ip; //ip address of the host
	int count; //how many packets to send
    unsigned int timeout; //timeout in ms - for how long to wait for the packet
	u_int64_t lastpacket_sent; //to control proper intrevals between packets in case is for some reason 
							   //there was a deleay in sending a packet, we need control that next 
							   //packet won't be sent too fast

	//measurement values, zeored each measurement
	double	min;
	double	sum;
	double	max;
	int	rcv;
	int sent;
//	int	cnt;

} pinger_item_t;

typedef struct {
    u_int64_t async_delay; //time between async calls to detect and properly handle timeouts
	//zbx_hashset_t pinged_items;  //hashsets of items and hosts to change their state
    zbx_hashset_t *items;
    zbx_binary_heap_t packet_events;
	int *requests;
	int *responses;
    GLB_EXT_WORKER *worker;
  //  unsigned int pause_till;
} GLB_PINGER_CONF;

extern int CONFIG_GLB_PINGER_FORKS;
extern int CONFIG_PINGER_FORKS;
extern int CONFIG_ICMP_METHOD;
extern char *CONFIG_GLBMAP_LOCATION;
extern char *CONFIG_GLBMAP_OPTIONS;
extern char *CONFIG_SOURCE_IP;


static void glb_pinger_submit_result(GLB_POLLER_ITEM *glb_item, int status, char *error, zbx_timespec_t *ts) {
    u_int64_t value_uint64;
    double value_dbl;
    AGENT_RESULT result;
    pinger_item_t *pinger_item=(pinger_item_t *)glb_item->itemdata;

    init_result(&result);
        
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Starting, itemid is %ld, status is %d", __func__,glb_item->itemid, status);
    
    switch (status) {
        case SUCCEED: {
           
            //value calculaition logic is taken from pinger.c process_value
            switch (pinger_item->icmpping)
			{
				case ICMPPING:
					value_uint64 = (0 != pinger_item->rcv ? 1 : 0);
                    SET_UI64_RESULT(&result, value_uint64);
                    zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting ping value %ld",glb_item->itemid,value_uint64);
                    zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                                glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
					
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
                        zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting  min/max/avg value %f",glb_item->itemid,value_dbl);
                        zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                            glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
				    break;
				case ICMPPINGLOSS:
					value_dbl = (100 * (pinger_item->count - pinger_item->rcv)) / (double)pinger_item->count;
					    SET_DBL_RESULT(&result, value_dbl);
                        zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting loss value %f",glb_item->itemid,value_dbl);
                        zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                            glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
					break;
                default: 
                    zabbix_log(LOG_LEVEL_WARNING,"Inknown ICMP processing type %d for item %ld - this is a programming BUG",
                        pinger_item->icmpping, glb_item->itemid);
                    THIS_SHOULD_NEVER_HAPPEN;
                    exit(-1);
			}

            free_result(&result);
            break;
         }
        default:
            zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 0 ,
                     NULL,ts,ITEM_STATE_NOTSUPPORTED, error);
            break;
    }
    
    //marking that polling has finished
    glb_item->state = POLL_QUEUED;
    pinger_item->state = POLL_QUEUED;
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: marked item %ld as available for polling ", __func__,glb_item->itemid);
    
    glb_item->lastpolltime = time(NULL);
  
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
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
static int init_item(glb_poll_module_t *poll_mod, DC_ITEM *dc_item, GLB_POLLER_ITEM *poller_item) {
	
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
    
    if (NULL == (pinger_item = (pinger_item_t *) zbx_calloc(NULL, 0, sizeof(pinger_item_t)))) {
        LOG_WRN("Cannot allocate mem for pinger item, exiting");
        return FAIL;
    }
    
    poller_item->itemdata = pinger_item;

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
static int glb_pinger_send_ping(GLB_PINGER_CONF *conf, GLB_POLLER_ITEM *glb_item) {
    zbx_timespec_t ts;
    char request[MAX_STRING_LEN];
    char ip_addr[MAX_ID_LEN];
    char *ip;
    unsigned int now = time(NULL);
    LOG_DBG("In %s() Started", __func__);
    pinger_item_t *pinger_item = (pinger_item_t*)glb_item->itemdata;
  
    pinger_item->lastpacket_sent = glb_ms_time();
    zbx_timespec(&ts);
    DEBUG_ITEM(glb_item->itemid,"Sending pings to addr %s, %d seconds to resolve ", pinger_item->addr, pinger_item->lastresolve + GLB_DNS_CACHE_TIME - time(NULL) );

    if (pinger_item->lastresolve < time(NULL) - GLB_DNS_CACHE_TIME) {
        //doing resolving 
        //now additionally we'll resolve the host's address, this is a sort of dns cache
        DEBUG_ITEM(glb_item->itemid,"Doing resolve");
        if (NULL == pinger_item->addr) {
           
            glb_pinger_submit_result(glb_item,CONFIG_ERROR,"Cannot resolve item to IPv4 addr, hostname is not set",&ts);
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
                DEBUG_ITEM(glb_item->itemid, "Cannot resolve");
                DEBUG_ITEM(glb_item->itemid, "Cannot resolve item addr '%s' to IPv4 addr, check hostname or use fping to IPv6", pinger_item->addr);
                glb_pinger_submit_result(glb_item,CONFIG_ERROR,"Cannot resolve item to IPv4 addr, check hostname or use fping to IPv6",&ts);
                
                return FAIL;
            }
        }
        
        pinger_item->ip=zbx_strdup(pinger_item->ip,ip);
        pinger_item->lastresolve = now;
        
        DEBUG_ITEM(glb_item->itemid, "Host resolved ");
        DEBUG_ITEM(glb_item->itemid, "Host %s resolved to %s will cache it for %d seconds",pinger_item->addr, ip, GLB_DNS_CACHE_TIME);
    }
    
    zbx_snprintf(request,MAX_STRING_LEN,"%s %d %ld",pinger_item->ip, pinger_item->size,glb_item->itemid);
    zabbix_log(LOG_LEVEL_DEBUG, "Sending request for ping: %s",request);
    
    
    if (SUCCEED != glb_worker_request(conf->worker, request) ) {
        //sending config error status for the item
        zbx_timespec(&ts);
        zabbix_log(LOG_LEVEL_DEBUG, "Couldn't send request for ping: %s",request);
        glb_pinger_submit_result(glb_item,CONFIG_ERROR,"Couldn't start or pass request to glbmap",&ts);
        return FAIL;
    }
   
    pinger_item->sent++;
    pinger_item->lastpacket_sent = glb_ms_time();

    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
   return SUCCEED;
}
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
static void free_item(glb_poll_module_t *poll_mod,  GLB_POLLER_ITEM *glb_poller_item ) {
    
    //LOG_DBG( "In %s() started", __func__);
    pinger_item_t *pinger_item = (pinger_item_t*)glb_poller_item->itemdata;
	
    zbx_free(pinger_item->ip);
    zbx_free(pinger_item->addr);
    
    pinger_item->ip = NULL;
    zbx_free(pinger_item);
    
    DEBUG_ITEM(glb_poller_item->itemid, "Has been removed from the pinger poller");
  //  LOG_INF( "In %s() Ended", __func__);
}


/******************************************************************************
 * timeouts handling - go through all the items and see if there are 
 * timed out measurements, if so - mark the measurement as timed it
 * and start a next one if the last measurement has finished, then
 * submit the result
 * ***************************************************************************/
//static 
void glb_pinger_handle_timeouts(GLB_PINGER_CONF *conf) {

	zbx_hashset_iter_t iter;
    u_int64_t *itemid;
    static u_int64_t last_check=0;
    u_int64_t glb_time = glb_ms_time();
    GLB_POLLER_ITEM *glb_item;
    zbx_timespec_t ts;
    
    zbx_timespec(&ts);
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //once a second, may be long if we check a few millions of hosts   
    if (last_check + 1000 > glb_time ) return;
     last_check = glb_time;
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() starting timouts check", __func__);

    zbx_hashset_iter_reset(conf->items,&iter);
    while (NULL != (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_iter_next(&iter))) {
         pinger_item_t *pinger_item = (pinger_item_t*)glb_item->itemdata;
         //we've got the item, checking for timeouts:
         if (  POLL_POLLING == pinger_item->state ) {
               if ( pinger_item->sent == pinger_item->count &&  
                   pinger_item->lastpacket_sent < glb_time - pinger_item->timeout - conf->async_delay  ) {
                                
                    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Item %ld set to queued state in the timeout handler",
                            __func__, glb_item->itemid );
                    *conf->responses += 1;
                    //this is timout but overall operation has succeeded
                    glb_pinger_submit_result( glb_item, SUCCEED, NULL, &ts);
                } else {
                    zabbix_log(LOG_LEVEL_DEBUG, "Not marking item %ld as timed out: sent %d, recv %d, count %d, glb_time-lastpacket: %ld, state: %d q_delay: %ld",
                        glb_item->itemid, pinger_item->sent, pinger_item->rcv, pinger_item->count, glb_time - pinger_item->lastpacket_sent, 
                            pinger_item->state,conf->async_delay);
                }
                //one more case: if glmap has failed, we might not send the packets, so we never see them back
                //so accouning delays between async io runs (sometimes DNS queries might impose quite a delay)
                if ( pinger_item->lastpacket_sent == 0 ||  glb_time - pinger_item->lastpacket_sent > CONFIG_TIMEOUT*1000 + conf->async_delay ) {
                    //this is probably some kind of error - we couldn't sent at all or it really took a long time since the last packet
                    zabbix_log(LOG_LEVEL_DEBUG, "State problem:  %ld as timed out: sent %d, recv %d, count %d, glb_time-lastpacket: %ld, state: %d BUG?, async_delay is %ld",
                        glb_item->itemid, pinger_item->sent, pinger_item->rcv, pinger_item->count, glb_time - pinger_item->lastpacket_sent, 
                            pinger_item->state,conf->async_delay);
                                       
                    glb_pinger_submit_result( glb_item, SUCCEED, "Internal pinging error has happened", &ts);
                }
         }
    }
        
   zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}


/******************************************************************************
 * start pinging an item             										  * 
 * ***************************************************************************/
static void start_ping(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item)
{
    GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)poll_mod->poller_data;
    //zbx_timespec_t ts;
    int i;
    u_int64_t glb_time = glb_ms_time();
    LOG_DBG("In %s() Started", __func__);
	    
    pinger_item_t *pinger_item = (pinger_item_t*)glb_item->itemdata;
    char request[MAX_STRING_LEN];
    
    pinger_item->rcv = 0;
    pinger_item->min = 0;
    pinger_item->max = 0;
    pinger_item->sum = 0;
    pinger_item->sent = 0;
  
    *conf->requests += 1;

    pinger_item->state = POLL_POLLING;
    LOG_DBG("In %s() Started1", __func__);

    //sending the first packet immediately
    if (SUCCEED != glb_pinger_send_ping(conf,glb_item)) 
        return;
    
    u_int64_t send_time = glb_time;
    
    DEBUG_ITEM(glb_item->itemid, "Planing %d pings for item %ld to host %s interval is %d, timeout is %d",
                pinger_item->count-1, glb_item->itemid, pinger_item->addr,pinger_item->interval, pinger_item->timeout);
    
    for (i=0; i< pinger_item->count-1; i++) {
        //creating new events
        GLB_PINGER_EVENT *event=zbx_malloc(NULL,sizeof(GLB_PINGER_EVENT));        
        send_time += pinger_item->interval;
        event->time = send_time;
        event->itemid = glb_item->itemid;

        zbx_binary_heap_elem_t elem = {send_time, (const void *)event};
	    zbx_binary_heap_insert(&conf->packet_events, &elem);
    }
   
    LOG_DBG( "In %s() Ended", __func__);
}

#define GLB_MAX_SEND_TIME 10

static void glb_pinger_send_scheduled_packets(GLB_PINGER_CONF *conf) {
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    uint64_t current_glb_time = glb_ms_time();
    uint64_t finish_send_time = current_glb_time + GLB_MAX_SEND_TIME;
    GLB_POLLER_ITEM *glb_item;
    int sent_packets =0 ;

    static int tmp_time=0;
    
    while (FAIL == zbx_binary_heap_empty(&conf->packet_events) && finish_send_time >=glb_ms_time() ) {
	    const zbx_binary_heap_elem_t *min;

		min = zbx_binary_heap_find_min(&conf->packet_events);
			
		GLB_PINGER_EVENT *pinger_event = (GLB_PINGER_EVENT *)min->data;
        u_int64_t etime=pinger_event->time;
        u_int64_t itemid=pinger_event->itemid;
      
        if (tmp_time + 10 < time(NULL)) {
            
            if (glb_ms_time() - etime > 10 * 1000 ) {
                zabbix_log(LOG_LEVEL_WARNING,"Internal pinging queue size is %d",conf->packet_events.elems_num);
                zabbix_log(LOG_LEVEL_WARNING,"Pinging is late %ld milliseconds", glb_ms_time() - etime); 
            }
            tmp_time=time(NULL);
        }

        if (etime > current_glb_time) 
            break;

        zbx_binary_heap_remove_min(&conf->packet_events);
        zbx_free(pinger_event);        

        if (NULL != (glb_item = zbx_hashset_search(conf->items, &itemid))) {
            pinger_item_t *pinger_item = (pinger_item_t*)glb_item->itemdata;
            
            //report state error, but unresloved are ok 
            if (NULL != pinger_item->ip && POLL_POLLING != pinger_item->state ) {
                zabbix_log(LOG_LEVEL_DEBUG,"In %s: sheduled item %ld addr %s in state %d. (DUP!)", 
                                        __func__, glb_item->itemid,pinger_item->ip, pinger_item->state );
                continue;
            } 

            if (pinger_item->lastpacket_sent + pinger_item->interval > current_glb_time ) {
                //this packet is too fast, lets requeue it
                //zabbix_log(LOG_LEVEL_DEBUG,"Rescheduling next packet for item %ld host %s", glb_item->itemid, pinger_item->ip);

                GLB_PINGER_EVENT *event=zbx_malloc(NULL,sizeof(GLB_PINGER_EVENT));        
                event->time = pinger_item->lastpacket_sent + pinger_item->interval;
                event->itemid = glb_item->itemid;

                zbx_binary_heap_elem_t elem = {event->time, (const void *)event};
	            zbx_binary_heap_insert(&conf->packet_events, &elem);
                
                continue;
            }
    
            glb_pinger_send_ping(conf,glb_item);
           
            //updating timings here to compensate delays
            //pinger_item->finish_time = current_glb_time + CONFIG_TIMEOUT *1000;
            
            sent_packets++;
        }
      
    }
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended %d sent", __func__, sent_packets);
}
/*************************************************************************
 * Account the packet, signals by exit status that polling has finished
 * ***********************************************************************/
static int glb_pinger_process_response(GLB_PINGER_CONF *conf, pinger_item_t *pinger_item, int rtt) {

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


static void glb_pinger_process_worker_results(GLB_PINGER_CONF *conf) {
    char *worker_response = NULL;
    zbx_json_type_t type;
    zbx_timespec_t ts;

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
    
    zbx_timespec(&ts);
    
    char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
    u_int64_t itemid_l;
    int rtt_l;
    struct zbx_json_parse jp_resp;
    GLB_POLLER_ITEM *glb_poller_item;

    //reading all the responses we have so far from the worker
    while (SUCCEED == async_buffered_responce(conf->worker, &worker_response)) {
        
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
        
        zabbix_log(LOG_LEVEL_DEBUG,"Parsed itemid %ld",itemid_l);

        if (NULL != (glb_poller_item = zbx_hashset_search(conf->items,&itemid_l)) ) {
            
            pinger_item_t *pinger_item =(pinger_item_t *)glb_poller_item->itemdata;
            
            if (pinger_item->ip != NULL && 0 != strcmp( ip, pinger_item->ip) ) {
                zabbix_log(LOG_LEVEL_WARNING,"Arrived ICMP responce with mismatched ip %s and itemid %ld",ip, itemid_l);
                //line = strtok(NULL, "\n");
                continue;
            }
            
            //checking if the item has been polled right now
            if ( POLL_POLLING != pinger_item->state) {
                zabbix_log(LOG_LEVEL_DEBUG,"Arrived responce for item %ld which is not in polling state (%d) (DUP!)",itemid_l,glb_poller_item->state);
            }
            
            //ok, looks like we can process the data right now
            if (POLL_FINISHED == glb_pinger_process_response(conf, pinger_item, rtt_l)) {
            
                zabbix_log(LOG_LEVEL_DEBUG, "Got the final packet for the item %ld",glb_poller_item->itemid);
                *conf->responses += 1;
                  
                glb_pinger_submit_result(glb_poller_item,SUCCEED,NULL, &ts);
                    
                //theese are two different things, need to set both
                pinger_item->state = POLL_QUEUED; 
                glb_poller_item->state = POLL_QUEUED;
            } 
        } else {
            //probably, for some reason the echo data is broken or maybe item has been deleted
            zabbix_log(LOG_LEVEL_DEBUG, "Couldn't find an item with itemid %ld (response: '%s'",itemid_l,worker_response);
            
            //lets try to find the item by ip address
            //perhaps it's better to create the ip->itemid map to make searches faster
            //but i've only seen a few hosts breaking echo data out of thousands - actually some version of Microtik
            
            zbx_hashset_iter_t iter;
            GLB_POLLER_ITEM *glb_poller_item;
            u_int32_t rtt;

            zbx_hashset_iter_reset(conf->items,&iter);
            
            while (NULL != (glb_poller_item = (GLB_POLLER_ITEM*)zbx_hashset_iter_next(&iter))) {
                //checking if the item has matched ip 
                pinger_item_t *pinger_item = (pinger_item_t*)glb_poller_item->itemdata;

                if ( NULL!= pinger_item->ip && 0 == strcmp(pinger_item->ip,ip)) {

                    rtt = GLB_PINGER_DEFAULT_RTT;
                    zabbix_log(LOG_LEVEL_DEBUG,"Item is matched by ip %s rtt is %d",ip,rtt);
             
                    if ( POLL_POLLING != pinger_item->state) {
                        zabbix_log(LOG_LEVEL_DEBUG,"Arrived responce for item %ld which is not in polling state (%d) (DUP!)",itemid_l,glb_poller_item->state);
                    }

                    //ok, looks like we can process the data right now
                    if (POLL_FINISHED == glb_pinger_process_response(conf, pinger_item, rtt)) {
            
                        zabbix_log(LOG_LEVEL_DEBUG, "Got the final packet for the item with broken echo data %ld",glb_poller_item->itemid);
                        *conf->responses += 1;
                  
                        glb_pinger_submit_result(glb_poller_item,SUCCEED,NULL, &ts);
                    
                        //theese are two different things, need to set both
                        pinger_item->state = POLL_QUEUED; 
                        glb_poller_item->state = POLL_QUEUED;
                    } 
                    //breaking the long cycle
                    break;
                }
            }

        }
    }

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}


/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * note: doesn't care about the timeouts - it's done by the poller globbaly   *
 * ***************************************************************************/
static void  handle_async_io(glb_poll_module_t *poll_mod) {
    
    static u_int64_t lastrun=0;
    u_int64_t queue_delay=0;
	GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)poll_mod->poller_data;
	LOG_DBG("In %s() Started", __func__);
    
    //but we also have to check how much are we late in the queue
    if (FAIL == zbx_binary_heap_empty(&conf->packet_events) ) {
	    const zbx_binary_heap_elem_t *min;
		min = zbx_binary_heap_find_min(&conf->packet_events);
        GLB_PINGER_EVENT *pinger_event = (GLB_PINGER_EVENT *)min->data;
        queue_delay=glb_ms_time()-pinger_event->time;
    }

    //first, calculating async delay
    conf->async_delay=glb_ms_time()-lastrun;
    
    //but if queue delay is bigger then use it for delay
    if ( queue_delay > conf->async_delay ) 
        conf->async_delay=queue_delay;

    lastrun=glb_ms_time();

    //parses and submits arrived ICMP responses
    glb_pinger_process_worker_results(conf);

    //send next packets after waiting for delay
    glb_pinger_send_scheduled_packets(conf);
    
    //handling timed-out items
    glb_pinger_handle_timeouts(conf); //timed out items will be marked as -1 result and next retry will be made
	
	LOG_DBG("In %s: Ended", __func__);
}




/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
static void    async_shutdown(glb_poll_module_t *poll_mod) {
	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
	GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)poll_mod->poller_data;
    LOG_DBG( "In %s() started", __func__);
    LOG_DBG( "In %s() Ended", __func__);
}
static int forks_count(glb_poll_module_t *poll_mod) {
	return CONFIG_GLB_PINGER_FORKS;
}
/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void glb_pinger_init(glb_poll_engine_t *poll) {
	int i;
	static GLB_PINGER_CONF *conf;
	char init_string[MAX_STRING_LEN];
    char full_path[MAX_STRING_LEN];
    char add_params[MAX_STRING_LEN];
	
    LOG_DBG("In %s: starting", __func__);
	
    add_params[0]='\0';

	if (NULL == (conf = zbx_calloc(NULL,0, sizeof(GLB_PINGER_CONF))) )  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}

    poll->poller.poller_data = conf;

    conf->items = &poll->items;
    conf->requests = &poll->poller.requests;
	conf->responses = &poll->poller.responses;
 
    poll->poller.delete_item = free_item;
    poll->poller.handle_async_io = handle_async_io;
    poll->poller.init_item = init_item;
    poll->poller.shutdown = async_shutdown;
    poll->poller.start_poll = start_ping;
    poll->poller.forks_count = forks_count;

    //poll->poller.update_item = NULL;


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

	conf->worker = glb_init_worker(init_string);
    if (NULL == conf->worker) {
        zabbix_log(LOG_LEVEL_WARNING,"Cannot create pinger woker, check the coniguration: '%s'",init_string);
        exit(-1);
    }
    conf->worker->mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    conf->worker->mode_from_worker = GLB_WORKER_MODE_NEWLINE;
    conf->worker->async_mode = 1;

    zbx_binary_heap_create(&conf->packet_events, event_elem_compare, 0);

	LOG_DBG("In %s: Ended", __func__);
}

