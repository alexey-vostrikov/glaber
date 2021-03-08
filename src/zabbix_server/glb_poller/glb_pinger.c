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
#include "common.h"
#include "worker.h"
#include "glb_pinger.h"

typedef struct {
	zbx_hashset_t pinged_items;  //hashsets of items and hosts to change their state
    zbx_hashset_t *items;
    zbx_binary_heap_t packet_events;
	int *requests;
	int *responces;
    GLB_EXT_WORKER *worker;
} GLB_PINGER_CONF;

static int isIpAddress(char *addr)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
    if (result != 0) 
        return SUCCEED;
    
    return FAIL;
}

/******************************************************************************
 * item init - from the general dc_item to compactly init and store a specific pinger		  * 
 * ***************************************************************************/
unsigned int glb_pinger_init_item(DC_ITEM *dc_item, GLB_PINGER_ITEM *pinger_item) {
	
    int			num, count, interval, size, timeout, rc;
    char *addr = NULL, *ip = NULL;
    
	zbx_timespec_t timespec;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
    //to do ping succesifully we need to know hostname(actually, ip), interval, ping size and number of packets

    //original item preparation
    ZBX_STRDUP(items[i].key, items[i].key_orig);
	rc = substitute_key_macros(&items[i].key, NULL, &items[i], NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error));

	if (SUCCEED == rc) 
		{
			rc = parse_key_params(items[i].key, items[i].interface.addr, &icmpping, &addr, &count,
					&interval, &size, &timeout, &type, error, sizeof(error));
		}
    //now additionally we'll resolve the host's address, this is a sort of dns cache
    if (SUCCEED == isIpAddress(addr)) {
        ip = addr;
    } else {
        //lets try to resolve the string to ipv4 addr, if we cannot - then dismiss the host as 
        //glbmap doesn't support ipv6 addr space yet
        rc = resolveHostToIPv4(addr, ip);    
    }

    if ( SUCCEED == rc ) {
        //by this time we have ip containing the ipv4 address in string form
        //so it's enough to form th proper pinger_item
        pinger_item->ip = ip;
        pinger_item->type = type;
        pinger_item->count = count;
        pinger_item->interval = interval;
        pinger_item->size = size;
        //index of the current measurment
        pinger_item->curr_idx = 0;
        
        pinger_item->results = zbx_malloc(NULL, sizeof(unsigned int)*count);
        pinger_item->finish_time = 0;
         
    }
	return rc;
}

/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
void glb_pinger_free_item(GLB_PINGER_ITEM *glb_pinger_item ) {
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	zbx_free(glb_pinger_item->ip);
    glb_pinger_item->ip = NULL;

    zbx_free(glb_pinger_item->results);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Ended", __func__);
}


/******************************************************************************
 * timeouts handling - go through all the items and see if there are 
 * timed out measurements, if so - mark the measurement as timed it
 * and start a next one if the last measurement has finished, then
 * submit the result
 * ***************************************************************************/
static void glb_pinger_handle_timeouts(GLB_PINGER_CONF *conf) {

	zbx_hashset_iter_t iter;
    u_int64_t *itemid;
    static int last_check=0;
    unsigned int now = time(NULL);
    GLB_POLLER_ITEM glb_item;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //once a second, may be long if we check a few millions of hosts   
    if (last_check == now ) return;
     last_check = now;
    
    zbx_hashset_iter_reset(conf->items,&iter);
    while (NULL != (glb_item = zbx_hashset_iter_next(&iter))) {
         //we've got the item, checking for timeouts:
         if (glb_item->finish_time < now && GLB_ITEM_STATE_POLLING == glb_item->state) {
                //timeout item, fixing the result
                glb_pinger_save_result( (GLB_PINGER_ITEM*) glb_item->itemdata, GLB_PINGER_RESULT_TIMOUT);
                glb_item->state = GLB_ITEM_STATE_QUEUED;
            }
    }
        
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}



/******************************************************************************
 * start pinging an item             										  * 
 * ***************************************************************************/
static int glb_pinger_start_ping(GLB_PINGER_CONF *conf, GLB_POLLER_ITEM *glb_item)
{
    void *min;
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
	    
    GLB_PINGER_ITEM pinger_item = (GLB_PINGER_ITEM*)glb_item->itemdata;
    char request[MAX_STRING_LEN];
    
    //cleaning the hsitory
    bzero(pinger_item->results, sizeof(int)*pinger->item->count);
    pinger_item->count = 0;

    zbx_snprintf(request,MAX_STRING_LEN,"%s %d %d",pinger_item->ip,pinger_item->size,glb_item->itemid);
    //sending first ping
    if (SUCCEED != glb_worker_request(config->worker, request) ) {
        //sending config error status for the item
        zbx_preproces_item_value();
        return FAIL;
    }

    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
    return SUCCEED;
}

static void glb_pinger_send_ping(conf,glb_item) {
    
}


static void glb_pinger_send_delayed_packets(GLB_PINGER_CONF *conf) {


    while (FAIL == zbx_binary_heap_empty(&conf->packet_events)) {
	    const zbx_binary_heap_elem_t *min;

		min = zbx_binary_heap_find_min(&conf->packet_events);
			
		GLB_PINGER_EVENT *pinger_event = (GLB_PINGER_EVENT *)min->data;
        
        if (pinger_event->time > h_time) 
            break;
        
        zbx_binary_heap_remove_min(&conf->packet_events);
    
        if (NULL != (glb_item = zbx_hashset_search(conf->items, pinger_event->itemid))) {
            //sending a new packet 
            glb_pinger_send_ping(conf,glb_item);
        }
    }
}

static void glb_pinger_process_worker_results(conf) {

    char **worker_response;
    zbx_json_type_t type;
    //reading all the responces we have so far from the worker
    while (<there is more data at worker>) {
        //parsing responce for itemid, rtt, ip fields
        char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
        u_int64_t itemid_l;
        int rtt_l;
        zbx_json_t jp_resp;

        if (SUCEED != zbx_json_parse(jp_resp, responce)) 
            zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the glbmap: %s",responce);
        //json paring goes here
        if (SUCCEED != zbx_json_value_by_name(jp_resp, "saddr", ip, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(jp_resp, "rtt", rtt_s, MAX_ID_LEN, &type) ||
            SUCCEED != zbx_json_value_by_name(jp_resp, "itemid", itemid_s, MAX_ID_LEN, &type)       
        ) {
            zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the glbmap: %s",responce);
            continue;
        }
    
        rtt_l = strtol(rtt_s,NULL,10);
        itemid_l = strtol(itemid,NULL,10);

        if (NULL != (glb_item = zbx_hashset_search(conf->items,&itemid_l)) ) {
            GLB_PINGER_ITEM *pinger_item =(GLB_PINGER_ITEM *)glb_item->itemdata;
            if (0 != strcmp( ip, glb_pinger_item->ip) ) {
                zabbix_log(LOG_LEVEL_WARNING,"Arrived ICMP responce with mismatched ip %s and itemid %ld",ip, itemid_l);
                continue;
            }
            
            //checking if the item has been polled right now
            if ( GLB_ITEM_STATE_POLLING != glb_item->state) {
                zabbix_log(LOG_LEVEL_WARNING,"Arrided responce for item %ld which is not in polling state (%d)",itemid_l,glb_item->state);
            }
            
            //ok, looks like we can process the data right now
            glb_pinger_process_ping_result(conf,GLB_PINGER_RESULT_ARRIVED,rtt_l,glb_item);

        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "Couldn't find an item with itemid %ld",itemid_l);
        }

    }

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
    glb_pinger_send_delayed_packets(conf);

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
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
	if (NULL == (conf = zbx_malloc(NULL,sizeof(GLB_PINGER_CONF))) )  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}
    
    zbx_hashset_create(&conf->pinged_items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    
    conf->requests = requests;
	conf->responces = responces;

    //creating internal structs to keep pinged items 
    //and internal events tree
	//creating the glb_map worker
	conf->worker = glb_init_worker("\"path\":\"/usr/local/sbin/glbmap\"");

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
