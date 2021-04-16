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
	zbx_hashset_t pinged_items;  //hashsets of items and hosts to change their state
    zbx_hashset_t *items;
    zbx_binary_heap_t packet_events;
	int *requests;
	int *responces;
    GLB_EXT_WORKER *worker;
  //  unsigned int pause_till;
} GLB_PINGER_CONF;

extern int CONFIG_GLB_PINGER_FORKS;
extern int CONFIG_PINGER_FORKS;
extern int CONFIG_ICMP_METHOD;
extern char *CONFIG_GLBMAP_LOCATION;
extern char *CONFIG_GLBMAP_OPTIONS;
extern char *CONFIG_SOURCE_IP;

/********************************************************************
 * returns SUCCEED if pinger item with key "key" could be processed 
 * by glaber poller. Expected to be used on dbconfig.c while 
 * deciding wich queue should be used for the item 
 * andd dc_get_poller_items to fetch items for the polling
 * ******************************************************************/

/*
//#define ZBX_ICMP_KEY_SET "fping]"
#define GLB_ICMP_KEY_SET "glbmap]"

int glb_can_process_glb_pinger(const char *key) {

    //no pollers - cannot process
    if (CONFIG_GLB_PINGER_FORKS == 0) 
        return FAIL;
    
    if (NULL != strstr(key,GLB_ICMP_KEY_SET)) return SUCCEED;
    if (NULL != strstr(key,ZBX_ICMP_KEY_SET)) return FAIL;
    
    //method isn't set per item, looking at default
    if (CONFIG_DEFAULT_ICMP_METHOD == GLB_ICMP) return SUCCEED;
    
    //default method is ZBX, so we will only process if there are no zbx pingers are started
    if ( 1 > CONFIG_PINGER_FORKS ) return SUCCEED;
    
    return FAIL;
}
*/
//static int isIpAddress(char *addr)
//{
//  struct sockaddr_in sa;
//    int result = inet_pton(AF_INET, addr, &(sa.sin_addr));
//    if (result != 0) 
//        return SUCCEED;
    
//    return FAIL;
//}
/*******************************************************************************
 * creates result and send it to the prerpocessing
 * ****************************************************************************/

//  glb_pinger_submit_result( (GLB_PINGER_ITEM*) glb_item->itemdata, GLB_PINGER_RESULT_TIMOUT);

static void glb_pinger_submit_result(GLB_POLLER_ITEM *glb_item, int status, char *error, zbx_timespec_t *ts) {
    u_int64_t value_uint64;
    double value_dbl;
    AGENT_RESULT result;
    GLB_PINGER_ITEM *glb_pinger_item=(GLB_PINGER_ITEM *)glb_item->itemdata;

    init_result(&result);
    
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Starting, itemid is %ld, status is %d", __func__,glb_item->itemid, status);
    
    switch (status) {
        case SUCCEED: {
           
            //value calculaition logic is taken from pinger.c process_value
            switch (glb_pinger_item->icmpping)
			{
				case ICMPPING:
					value_uint64 = (0 != glb_pinger_item->rcv ? 1 : 0);
                    SET_UI64_RESULT(&result, value_uint64);
                    zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting ping value %ld",glb_item->itemid,value_uint64);
                    zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                                glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
					
					break;
				case ICMPPINGSEC:
					switch (glb_pinger_item->type)
					{
						case ICMPPINGSEC_MIN:
							value_dbl = glb_pinger_item->min;
							break;
						case ICMPPINGSEC_MAX:
							value_dbl = glb_pinger_item->max;
							break;
						case ICMPPINGSEC_AVG:
							value_dbl = (0 != glb_pinger_item->rcv ? glb_pinger_item->sum / glb_pinger_item->rcv : 0);
							break;
					}

					if (0 < value_dbl && ZBX_FLOAT_PRECISION > value_dbl)
						value_dbl = ZBX_FLOAT_PRECISION;
                        SET_DBL_RESULT(&result, value_dbl);
                        zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting  min/max/avg value %f",glb_item->itemid,value_dbl);
                        zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                            glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
				    break;
				case ICMPPINGLOSS:
					value_dbl = (100 * (glb_pinger_item->count - glb_pinger_item->rcv)) / (double)glb_pinger_item->count;
					    SET_DBL_RESULT(&result, value_dbl);
                        zabbix_log(LOG_LEVEL_DEBUG,"For item %ld submitting loss value %f",glb_item->itemid,value_dbl);
                        zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                            glb_item->flags , &result , ts, ITEM_STATE_NORMAL, NULL);
					break;
                default: 
                    zabbix_log(LOG_LEVEL_WARNING,"Inknown ICMP processing type %d for item %ld - this is a programming BUG",
                        glb_pinger_item->icmpping, glb_item->itemid);
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
    glb_pinger_item->state = POLL_QUEUED;
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: marked item %ld as available for polling ", __func__,glb_item->itemid);
    
    glb_item->lastpolltime = time(NULL);
  
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}

/******************************************************************************
 * tries to resolve the host ip to ipv4 ip addr fails if it cannot
 * whenever glbmap starts supporting ipv6, this will be obsoleted
 * it's a fixed zbx_host_by_ip func
 * ***************************************************************************/
static  int	zbx_getipv4_by_host(const char *host, char *ip, size_t iplen)
{
	struct addrinfo	hints, *ai = NULL;
    int rc = FAIL;

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
    
    return rc;
}

/******************************************************************************
 * item init - from the general dc_item to compactly init and store a specific pinger		  * 
 * ***************************************************************************/
unsigned int glb_pinger_init_item(DC_ITEM *dc_item, GLB_PINGER_ITEM *pinger_item) {
	
    int	num, count, interval, size, timeout, rc;
    char ip_addr[MAX_ID_LEN], *ip=NULL;
    char *parsed_key = NULL;
    char error[MAX_STRING_LEN];

	zbx_timespec_t timespec;
    icmpping_t icmpping;
    icmppingsec_type_t	type;
       
    //original item preparation
    ZBX_STRDUP(parsed_key, dc_item->key_orig);
    
	if (SUCCEED != substitute_key_macros(&parsed_key, NULL, dc_item, NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error)))
        return FAIL;
	
    if (SUCCEED != parse_key_params(parsed_key, dc_item->interface.addr, &icmpping, &dc_item->interface.addr, &count,
					&interval, &size, &timeout, &type, error, sizeof(error)))
	    return FAIL;
    
    pinger_item->ip = NULL;
    pinger_item->addr = zbx_strdup(NULL, dc_item->interface.addr);
    pinger_item->lastresolve=0;
    pinger_item->type = type;
    pinger_item->count = count;

    if (interval > 20 )
        pinger_item->interval = interval;
    else 
        pinger_item->interval = 20;

    pinger_item->size = size;
    pinger_item->timeout = timeout;
    pinger_item->icmpping = icmpping;
    pinger_item->finish_time = 0;

    pinger_item->min = 0;
	pinger_item->sum = 0;
	pinger_item->max = 0;
	pinger_item->rcv = 0;
	
    zbx_free(parsed_key);
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
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    GLB_PINGER_ITEM *glb_pinger_item = (GLB_PINGER_ITEM*)glb_item->itemdata;

    glb_pinger_item->lastpacket_sent = glb_ms_time();

    if (glb_pinger_item->lastresolve < time(NULL) - GLB_DNS_CACHE_TIME) {
        //doing resolving 
        //now additionally we'll resolve the host's address, this is a sort of dns cache
        
        if (SUCCEED == is_ip4( glb_pinger_item->addr)) {
            ip = glb_pinger_item->addr;
        } else {
            //lets try to resolve the string to ipv4 addr, if we cannot - then dismiss the host as 
            //glbmap doesn't support ipv6 addr space yet
            if (SUCCEED == zbx_getipv4_by_host( glb_pinger_item->addr, ip_addr, MAX_ID_LEN)) {
                ip =ip_addr;
            } else {
                zbx_timespec(&ts);
                glb_pinger_submit_result(glb_item,CONFIG_ERROR,"Cannot resolve item to IPv4 addr, check hostname or use fping to IPv6",&ts);
                return FAIL;
            }
        }
        glb_pinger_item->ip=zbx_strdup(glb_pinger_item->ip,ip);
        glb_pinger_item->lastresolve = now;
        
        zabbix_log(LOG_LEVEL_DEBUG, "Host %s resolved to %s will cache it for %d seconds",glb_pinger_item->addr, ip, GLB_DNS_CACHE_TIME);
    }
    
    zbx_snprintf(request,MAX_STRING_LEN,"%s %d %ld\n",glb_pinger_item->ip, glb_pinger_item->size,glb_item->itemid);
    zabbix_log(LOG_LEVEL_DEBUG, "Sending request for ping: %s",request);
    
    
    if (SUCCEED != glb_worker_request(conf->worker, request) ) {
        //sending config error status for the item
        zbx_timespec(&ts);
        glb_pinger_submit_result(glb_item,CONFIG_ERROR,"Couldn't start or pass request to glbmap",&ts);
        return FAIL;
    }
    glb_pinger_item->sent++;
    glb_pinger_item->lastpacket_sent = glb_ms_time();

  zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
   return SUCCEED;
}
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
void glb_pinger_free_item(GLB_PINGER_ITEM *glb_pinger_item ) {
	
//	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	zbx_free(glb_pinger_item->ip);
    zbx_free(glb_pinger_item->addr);
    glb_pinger_item->ip = NULL;

//	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
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
    
    zbx_hashset_iter_reset(conf->items,&iter);
    while (NULL != (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_iter_next(&iter))) {
         GLB_PINGER_ITEM *glb_pinger_item = (GLB_PINGER_ITEM*)glb_item->itemdata;
         //we've got the item, checking for timeouts:
         if ( glb_pinger_item->sent == glb_pinger_item->count &&  
              glb_pinger_item->lastpacket_sent < glb_time - CONFIG_TIMEOUT * 1000 && 
              POLL_POLLING == glb_pinger_item->state) {
                                
                zabbix_log(LOG_LEVEL_DEBUG,"In %s: Item %ld set to queued state in the timeout handler",
                            __func__, glb_item->itemid );
                *conf->responces += 1;
                //this is timout but overall operation has succeeded
                glb_pinger_submit_result( glb_item, SUCCEED, NULL, &ts);
            }
    }
        
   zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}



/******************************************************************************
 * start pinging an item             										  * 
 * ***************************************************************************/
int glb_pinger_start_ping(void *engine, GLB_POLLER_ITEM *glb_item)
{
    GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)engine;
    //zbx_timespec_t ts;
    int i;
    u_int64_t glb_time = glb_ms_time();
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
	    
    GLB_PINGER_ITEM *glb_pinger_item = (GLB_PINGER_ITEM*)glb_item->itemdata;
    char request[MAX_STRING_LEN];
    
    //not starting new pings for sheduled items or if we're too busy
//    if  ( time(NULL) < conf->pause_till ) {
  //      
    //    zabbix_log(LOG_LEVEL_DEBUG,"Not starting pinging of item %ld",glb_item->itemid);
 //       return FAIL;
 //   }
   
    glb_pinger_item->rcv = 0;
    glb_pinger_item->min = 0;
    glb_pinger_item->max = 0;
    glb_pinger_item->sum = 0;
    glb_pinger_item->sent = 0;
  
 
    *conf->requests += 1;

    glb_pinger_item->state = POLL_POLLING;

    //now we should plan all the packets in the probe, we use milliseconds-key
    u_int64_t send_time = glb_time;

    zabbix_log(LOG_LEVEL_DEBUG, "Planing %d pings for item %ld to host %s interval is %d",
                glb_pinger_item->count, glb_item->itemid, glb_pinger_item->addr,glb_pinger_item->interval);
    
    glb_pinger_item->lastpacket_sent = 0;

    for (i=0; i< glb_pinger_item->count; i++) {
        //creating new events
        GLB_PINGER_EVENT *event=zbx_malloc(NULL,sizeof(GLB_POLLER_EVENT));        
        send_time += glb_pinger_item->interval;
        event->time = send_time;
        event->itemid = glb_item->itemid;

        zbx_binary_heap_elem_t elem = {send_time, (const void *)event};
	    zbx_binary_heap_insert(&conf->packet_events, &elem);
    }
   
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
    return SUCCEED;

}

#define GLB_MAX_SEND_TIME 10

static void glb_pinger_send_scheduled_packets(GLB_PINGER_CONF *conf) {
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    uint64_t current_glb_time = glb_ms_time();
    uint64_t finish_send_time =current_glb_time + GLB_MAX_SEND_TIME;
    GLB_POLLER_ITEM *glb_item;
    int sent_packets =0 ;
    
    while (FAIL == zbx_binary_heap_empty(&conf->packet_events) && finish_send_time >=glb_ms_time() ) {
	    const zbx_binary_heap_elem_t *min;

		min = zbx_binary_heap_find_min(&conf->packet_events);
			
		GLB_PINGER_EVENT *pinger_event = (GLB_PINGER_EVENT *)min->data;
        u_int64_t etime=pinger_event->time;
        u_int64_t itemid=pinger_event->itemid;
      
        
        if (etime > current_glb_time) 
            break;

   //     if (glb_ms_time() - etime > CONFIG_TIMEOUT ) {
   //         zabbix_log(LOG_LEVEL_DEBUG,"In %s : warn: inetrnal queue is %ld mseconds late", __func__, current_glb_time - pinger_event->time );
   //         zabbix_log(LOG_LEVEL_WARNING,"New ping will not be sent for next %d seconds, we're too slow", CONFIG_TIMEOUT);
   //         conf->pause_till = time(NULL) + CONFIG_TIMEOUT;
        
   //     }


        zbx_binary_heap_remove_min(&conf->packet_events);
        zbx_free(pinger_event);        

        if (NULL != (glb_item = zbx_hashset_search(conf->items, &itemid))) {
            GLB_PINGER_ITEM *glb_pinger_item = (GLB_PINGER_ITEM*)glb_item->itemdata;
            
            //report state error, but unresloved are ok 
            if (NULL != glb_pinger_item->ip && POLL_POLLING != glb_pinger_item->state ) {
                zabbix_log(LOG_LEVEL_WARNING,"In %s: sheduled item %ld addr %s in state %d. This is a BUG", 
                                        __func__, glb_item->itemid,glb_pinger_item->ip, glb_pinger_item->state );
                continue;
            } 

            if (glb_pinger_item->lastpacket_sent + glb_pinger_item->interval > current_glb_time ) {
                //this packet is too fast, lets requeue it
                zabbix_log(LOG_LEVEL_DEBUG,"Rescheduling next packet for item %ld host %s", glb_item->itemid, glb_pinger_item->ip);

                GLB_PINGER_EVENT *event=zbx_malloc(NULL,sizeof(GLB_POLLER_EVENT));        
                event->time = glb_pinger_item->lastpacket_sent + glb_pinger_item->interval;
                event->itemid = glb_item->itemid;

                zbx_binary_heap_elem_t elem = {event->time, (const void *)event};
	            zbx_binary_heap_insert(&conf->packet_events, &elem);
                
                continue;
            }
    
            glb_pinger_send_ping(conf,glb_item);
           
            //updating timings here to compensate delays
            //glb_pinger_item->finish_time = current_glb_time + CONFIG_TIMEOUT *1000;
            

            sent_packets++;
        }
      
    }
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended %d sent", __func__, sent_packets);
}
/*************************************************************************
 * Account the packet, signals by exit status that polling has finished
 * ***********************************************************************/
static int glb_pinger_process_response(GLB_PINGER_ITEM *glb_pinger_item, int rtt) {

    if ( glb_pinger_item->timeout > 0 && rtt > glb_pinger_item->timeout ) {
        zabbix_log(LOG_LEVEL_DEBUG,"Ignoring packet from host %s as it came after timeout rtt:%d  timeout: %d)",glb_pinger_item->ip, rtt,  glb_pinger_item->timeout );
        return SUCCEED;
    }

    if (0 == glb_pinger_item->rcv || glb_pinger_item->min > rtt) glb_pinger_item->min = rtt;
	if (0 == glb_pinger_item->rcv || glb_pinger_item->max < rtt) glb_pinger_item->max = rtt;
	
    glb_pinger_item->sum += rtt;
	glb_pinger_item->rcv++;

    if (glb_pinger_item->count == glb_pinger_item->rcv) {
        return POLL_FINISHED;
    
    } else {
       //we prolong the tiemout
      //  glb_pinger_item->finish_time = glb_ms_time() + CONFIG_TIMEOUT * 1000;
      // zabbix_log(LOG_LEVEL_DEBUG,"Host %s got responce %d out of %d %s", glb_pinger_item->ip, glb_pinger_item->rcv, glb_pinger_item->count);
    }


    return SUCCEED;
}


static void glb_pinger_process_worker_results(GLB_PINGER_CONF *conf) {
    char *worker_response = NULL;
    zbx_json_type_t type;
    zbx_timespec_t ts;

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
    
    zbx_timespec(&ts);
    //reading all the responces we have so far from the worker
    while (SUCCEED == glb_worker_responce(conf->worker,&worker_response)) {
        //parsing responce for itemid, rtt, ip fields
        char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
        u_int64_t itemid_l;
        int rtt_l;
        struct zbx_json_parse jp_resp;
        GLB_POLLER_ITEM *glb_poller_item;
  
         char *line = strtok(worker_response, "\n");
        // loop through the string to extract all other tokens
        while ( line != NULL ) {

            zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s", line);
            if (SUCCEED != zbx_json_open(line, &jp_resp)) {
		        zabbix_log(LOG_LEVEL_DEBUG, "Couldn't ropen JSON response from gkbmap %s", line);
		        line = strtok(NULL, "\n");
                
                continue;
	        }

            //if (SUCCEED != zbx_json_parse(jp_resp, *worker_responce)) 
            //zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the glbmap: %s",responce);

         //json paring goes here
            if (SUCCEED != zbx_json_value_by_name(&jp_resp, "saddr", ip, MAX_ID_LEN, &type) ||
                SUCCEED != zbx_json_value_by_name(&jp_resp, "rtt", rtt_s, MAX_ID_LEN, &type) ||
                SUCCEED != zbx_json_value_by_name(&jp_resp, "itemid", itemid_s, MAX_ID_LEN, &type)       
            ) {
                zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the glbmap: %s",worker_response);
                 line = strtok(NULL, "\n");

                continue;
            }
        
            rtt_l = strtol(rtt_s,NULL,10);
            itemid_l = strtol(itemid_s,NULL,10);

            if (NULL != (glb_poller_item = zbx_hashset_search(conf->items,&itemid_l)) ) {
                GLB_PINGER_ITEM *glb_pinger_item =(GLB_PINGER_ITEM *)glb_poller_item->itemdata;
                if (glb_pinger_item->ip != NULL && 0 != strcmp( ip, glb_pinger_item->ip) ) {
                    zabbix_log(LOG_LEVEL_WARNING,"Arrived ICMP responce with mismatched ip %s and itemid %ld",ip, itemid_l);
                     line = strtok(NULL, "\n");
                    continue;
                }
            
                //checking if the item has been polled right now
                if ( POLL_POLLING != glb_pinger_item->state) {
                    zabbix_log(LOG_LEVEL_DEBUG,"Arrived responce for item %ld which is not in polling state (%d) (DUP!)",itemid_l,glb_poller_item->state);
                }
            
                //ok, looks like we can process the data right now
                if (POLL_FINISHED == glb_pinger_process_response(glb_pinger_item, rtt_l)) {
                    *conf->responces += 1;
                  
                    glb_pinger_submit_result(glb_poller_item,SUCCEED,NULL, &ts);
                    
                    //theese are two different things, need to set both
                    glb_pinger_item->state = POLL_QUEUED; 
                    glb_poller_item->state = POLL_QUEUED;
                } 
            } else {
                zabbix_log(LOG_LEVEL_DEBUG, "Couldn't find an item with itemid %ld",itemid_l);
            }
            line = strtok(NULL, "\n");
        }
        zbx_free(worker_response);
    }
   
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}


/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * note: doesn't care about the timeouts - it's done by the poller globbaly   *
 * ***************************************************************************/
void  glb_pinger_handle_async_io(void *engine) {

	GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)engine;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

    //parses and submits arrived ICMP responces
    glb_pinger_process_worker_results(conf);

    //send next packets after waiting for delay
    glb_pinger_send_scheduled_packets(conf);
    
    //handling timed-out items
    glb_pinger_handle_timeouts(engine); //timed out items will be marked as -1 result and next retry will be made
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}



/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void* glb_pinger_init(zbx_hashset_t *items, int *requests, int *responces ) {
	int i;
	static GLB_PINGER_CONF *conf;
	char init_string[MAX_STRING_LEN];
    char full_path[MAX_STRING_LEN];
    char add_params[MAX_STRING_LEN];
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
    add_params[0]='\0';

	if (NULL == (conf = zbx_malloc(NULL,sizeof(GLB_PINGER_CONF))) )  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}
    
    //zbx_hashset_create(&conf->pinged_items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    conf->items = items;
    conf->requests = requests;
	conf->responces = responces;
  //  conf->pause_till = 0;

    //creating internal structs to keep pinged items 
    //and internal events tree
	//creating the glb_map worker
    if (-1 == access(CONFIG_GLBMAP_LOCATION, X_OK))
	{
        zabbix_log(LOG_LEVEL_WARNING,"Couldn't find glb map at the path: %s or it isn't set to be executable: %s", CONFIG_GLBMAP_LOCATION, zbx_strerror(errno));
		exit(-1);
	};

    if ( NULL != CONFIG_SOURCE_IP && SUCCEED == is_ip4(CONFIG_SOURCE_IP) ) {
        zbx_snprintf(add_params,MAX_STRING_LEN,"-S %s",CONFIG_SOURCE_IP);
    } 
    if ( NULL != CONFIG_GLBMAP_OPTIONS ) {
         zbx_snprintf(add_params,MAX_STRING_LEN," %s",CONFIG_GLBMAP_OPTIONS);
    }
    
    zbx_snprintf(init_string,MAX_STRING_LEN,"{ \"path\":\"%s %s -v 0 -q -Z -r 100000 --probe-module=glb_icmp --output-module=json --output-fields=\\\"saddr,rtt,itemid,success\\\"\" }\n",
        CONFIG_GLBMAP_LOCATION, add_params);

	conf->worker = glb_init_worker(init_string);
    conf->worker->mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    conf->worker->mode_from_worker = GLB_WORKER_MODE_NEWLINE;
    conf->worker->async_mode = 1;

    zbx_binary_heap_create(&conf->packet_events, event_elem_compare, 0);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return (void *)conf;
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
void    glb_pinger_shutdown(void *engine) {
	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
	GLB_PINGER_CONF *conf = (GLB_PINGER_CONF*)engine;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
