/************************************************************************
    Glb worker script item - execs scripts assuming they are workers
    supports async and sync flow
 ***********************************************************************/
#include "log.h"
#include "common.h"
#include "zbxserver.h"
#include "../../libs/zbxexec/worker.h"
#include "glb_worker.h"
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"

extern int CONFIG_GLB_WORKER_FORKS;
typedef struct {
    u_int64_t async_delay; //time between async calls to detect and properly handle timeouts
    zbx_binary_heap_t events;
    zbx_hashset_t workers;
 } 
 worker_conf_t;

static worker_conf_t conf = {0};

typedef struct {
	u_int64_t workerid;
//	char *path; //fill path to the worker
	u_int64_t lastrequest; //to shutdown and cleanup idle workers
	GLB_EXT_WORKER worker;
} worker_t;

typedef struct {
	//config params
	//const char *key;
	u_int64_t finish_time;
	char state; //internal item state for async ops
	unsigned int timeout; //timeout in ms - for how long to wait for the response before consider worker dead
	u_int64_t lastrequest; //to control proper intrevals between packets in case is for some reason 
							   //there was a deleay in sending a packet, we need control that next 
							   //packet won't be sent too fast
	u_int64_t workerid; //id of a worker to process the data
	char *params_dyn; //dynamic translated params
	const char *full_cmd; //fill path and unparsed params - command to start the worker 

} worker_item_t;


extern int CONFIG_GLB_WORKER_FORKS;
extern char  *CONFIG_WORKERS_DIR;

/************************************************************************
 * submits result to the preprocessor                                   *
 * **********************************************************************/
static int worker_submit_result(char *response) {
    struct zbx_json_parse jp_resp;
    char value[MAX_STRING_LEN],itemid_s[MAX_ID_LEN],time_sec_s[MAX_ID_LEN], time_ns_s[MAX_ID_LEN];
    u_int64_t itemid;
    unsigned int time_sec = 0, time_ns = 0;
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Started", __func__);
    worker_item_t *worker_item; 
    poller_item_t *poller_item;
    AGENT_RESULT	result;
    zbx_json_type_t type;
    
    //error is unset - sending the value
    if (SUCCEED != zbx_json_open(response, &jp_resp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON response '%s' from worker", response);
		return FAIL;
    }
            
    if (SUCCEED != zbx_json_value_by_name(&jp_resp, "itemid", itemid_s, MAX_ID_LEN, &type) ||
        SUCCEED != zbx_json_value_by_name(&jp_resp, "value", value, MAX_STRING_LEN, &type) 
       ) {
       zabbix_log(LOG_LEVEL_WARNING,"Cannot parse response from the worker: %s",response);
        return FAIL;
    }

    itemid = strtol(itemid_s,NULL,10); 
    //looking for the item
    if (NULL == ( poller_item = poller_get_poller_item(itemid))) {
        zabbix_log(LOG_LEVEL_WARNING, "Worker returned response with unknown itemid %ld, response is skipped", itemid);
        return FAIL;
    }

    worker_item = poller_get_item_specific_data(poller_item);      
           
    init_result(&result);
    zbx_rtrim(value, ZBX_WHITESPACE);
    
    set_result_type(&result, ITEM_VALUE_TYPE_TEXT, value);
    
	poller_preprocess_value(poller_item, &result , glb_ms_time(), ITEM_STATE_NORMAL, NULL);
    
    free_result(&result);
    
     
    poller_return_item_to_queue(poller_item);
    worker_item->state = POLL_QUEUED;
    
    DEBUG_ITEM(poller_get_item_id(poller_item),"In %s: marked item as available for polling ", __func__);
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}


/******************************************************************************
 * item init - from the general dc_item to compact local item        		  * 
 * ***************************************************************************/
static int init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
    
    char error[MAX_STRING_LEN];
	AGENT_REQUEST	request;
    char *parsed_key = NULL, *cmd = NULL, *params_dyn = NULL, *key_dyn = NULL, *params_stat = NULL;
    size_t		dyn_alloc = 0, stat_alloc = 0, dyn_offset = 0, stat_offset = 0, cmd_alloc = ZBX_KIBIBYTE, cmd_offset = 0;
    int ret, i;
    zabbix_log(LOG_LEVEL_DEBUG,"Staring %s",__func__);
    zabbix_log(LOG_LEVEL_DEBUG,"Item key is %s",dc_item->key_orig);
   
    worker_item_t *worker_item;
    
    worker_item = (worker_item_t*)zbx_calloc(NULL, 0, sizeof(worker_item_t));
        
    poller_set_item_specific_data(poller_item, worker_item);
    
    init_request(&request);

    cmd = (char *)zbx_malloc(cmd, cmd_alloc);
    ZBX_STRDUP(parsed_key, dc_item->key_orig);
    
    //translating dynamic params
    //for worker params we will use parsed parameters, cache them to config cache reload time
    if (SUCCEED != substitute_key_macros(&parsed_key, NULL, dc_item, NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error))) {
        zabbix_log(LOG_LEVEL_INFORMATION,"Failed to apply macroses to dynamic params %s",parsed_key);
        ret = FAIL;
        goto out;
    }
    
    if (SUCCEED != parse_item_key(parsed_key, &request)) {
	    zabbix_log(LOG_LEVEL_INFORMATION,"Failed to parse item key %s",parsed_key);
        ret = FAIL;
        goto out;
    }
    
    //itemid is always needed in dynamic params, generating new list only having dynamic data
    zbx_snprintf_alloc(&cmd,&cmd_alloc,&cmd_offset,"%s/%s", CONFIG_WORKERS_DIR, get_rkey(&request));
    poller_strpool_free(worker_item->full_cmd);
    
    worker_item->full_cmd = poller_strpool_add(cmd);
    worker_item->workerid = (u_int64_t)worker_item->full_cmd;
    
    zbx_snprintf_alloc(&key_dyn, &dyn_alloc, &dyn_offset, "'%ld'", dc_item->itemid);
    
    for (i = 0; i < get_rparams_num(&request); i++)
	{
		const char	*param;
		char		*param_esc;

		param = get_rparam(&request, i);
		param_esc = zbx_dyn_escape_shell_single_quote(param);
          
        zbx_snprintf_alloc(&key_dyn, &dyn_alloc, &dyn_offset, " '%s'", param_esc);
		zbx_free(param_esc);
	}
    
    zbx_snprintf_alloc(&key_dyn, &dyn_alloc, &dyn_offset, "\n");
    zabbix_log(LOG_LEVEL_DEBUG,"Parsed params: %s", key_dyn);
    
    if (NULL != worker_item->params_dyn) 
        zbx_free(key_dyn);
    
    worker_item->params_dyn = key_dyn;
    free_request(&request);

    ret = SUCCEED;
out:
    zbx_free(parsed_key);
    zbx_free(params_stat);
    zbx_free(cmd);
    free_request(&request);
    return ret;
}
/*****************************************************************
 * creates a new worker structure
*****************************************************************/
static worker_t * worker_create_worker(worker_item_t *worker_item) {

    worker_t worker, *retworker;
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s() Started", __func__);
   
    zabbix_log(LOG_LEVEL_INFORMATION, "creating a worker for cmd %s ",worker_item->full_cmd);
    zabbix_log(LOG_LEVEL_INFORMATION, "creating a worker params %s", worker_item->params_dyn);
    bzero(&worker, sizeof(worker_t));

    
    worker.workerid = worker_item->workerid;
    worker.worker.path = (char *)poller_strpool_add(worker_item->full_cmd);
    worker.worker.async_mode = 1;
    worker.worker.max_calls = GLB_WORKER_MAXCALLS;
    worker.worker.mode_from_worker=GLB_WORKER_MODE_NEWLINE;
    worker.worker.mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    worker.worker.timeout = CONFIG_TIMEOUT;
   
      
    retworker = (worker_t*)zbx_hashset_insert(&conf.workers,&worker,sizeof(worker_t));
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s() Finished worker id is %ld", __func__, retworker->workerid);
    
    return retworker;
}

/******************************************************************
 * Sends the actual packet (request to worker to send a packet
 * ****************************************************************/
static void send_request(poller_item_t *poller_item) {
    u_int64_t mstime = glb_ms_time();

    char request[MAX_STRING_LEN];
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

    worker_item_t *worker_item = poller_get_item_specific_data(poller_item);

    worker_item->lastrequest = glb_ms_time();

    worker_t *worker = (worker_t *)zbx_hashset_search(&conf.workers,&worker_item->workerid);
    
    if (NULL == worker ) {
        //worker not found - creating a new one
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't find a worker for item, creating a new one");
        worker = worker_create_worker(worker_item);
    }
    
    zabbix_log(LOG_LEVEL_DEBUG, "Will do request: %s to worker %s",worker_item->params_dyn, worker_item->full_cmd);
    if (NULL == worker_item->params_dyn ||  SUCCEED != glb_worker_request(&worker->worker, worker_item->params_dyn) ) {
        //sending config error status for the item
        zabbix_log(LOG_LEVEL_DEBUG, "Couldn't send request %s",request);
        
        poller_preprocess_value(poller_item, NULL , mstime, ITEM_STATE_NOTSUPPORTED, "Couldn't send request to the script");
        
        return;
    }

    zabbix_log(LOG_LEVEL_DEBUG, "Request finished");
    
    worker_item->lastrequest = mstime;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
static void free_item(poller_item_t *poller_item ) {
    
    worker_item_t *worker_item = poller_get_item_specific_data(poller_item);

    zbx_free(worker_item->params_dyn);
    poller_strpool_free(worker_item->full_cmd);
    zbx_free(worker_item);
}


/******************************************************************************
 * timeouts handling 
 * check item's timeouts
 * checks worker's timeouts (workers that hasn't been used for some time
 *                          are stopped and freed )
 * ***************************************************************************/
static void worker_handle_timeouts() {

	zbx_hashset_iter_t iter;
    u_int64_t *itemid;

    static u_int64_t last_check=0;
    u_int64_t glb_time = glb_ms_time();

    poller_item_t *glb_item;
    zbx_timespec_t ts;
    
    zbx_timespec(&ts);
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //once a second, may be too long if we check a few millions of hosts   
    if (last_check + 1000 > glb_time ) return;
     last_check = glb_time;
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() starting worker timeouts check", __func__);

    // zbx_hashset_iter_reset(conf->items,&iter);
    // while (NULL != (glb_item = (poller_item_t *)zbx_hashset_iter_next(&iter))) {
    //      worker_item_t *worker_item = (worker_item_t*)glb_item->itemdata;
         
    //      //we've got the item, checking for timeouts:
    //      if ( POLL_POLLING == worker_item->state ) {
         
   
    //     }
    // }
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}


static void worker_process_results() {
    char *worker_response = NULL;
    zbx_json_type_t type;
    zbx_timespec_t ts;

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
    
    zbx_timespec(&ts);
    
    char itemid_s[MAX_ID_LEN], ip[MAX_ID_LEN], rtt_s[MAX_ID_LEN];
    u_int64_t itemid_l;
    int rtt_l;
    struct zbx_json_parse jp_resp;
    poller_item_t *glb_poller_item;
    zbx_hashset_iter_t iter;
    worker_t *worker;
    //reading responses from all the workers we have
    //reading all the responses we have so far from the worker
    
    //worker iteration loop on the top
    zbx_hashset_iter_reset(&conf.workers,&iter);
    while (NULL != (worker = zbx_hashset_iter_next(&iter))) {
        //we only query alive workers
        zabbix_log(LOG_LEVEL_DEBUG,"Will read data from worker %s", worker->worker.path);
        if (SUCCEED == worker_is_alive(&worker->worker)) { //only read from alive workers
            zabbix_log(LOG_LEVEL_DEBUG,"Calling async read");
            while (SUCCEED == async_buffered_responce(&worker->worker, &worker_response)) {
              
                LOG_DBG("Parsing line %s from worker %s", worker_response, worker->worker.path);
                worker_submit_result(worker_response);
            }
        } else {
         zabbix_log(LOG_LEVEL_DEBUG,"Will %s is not alive, skipping", worker->worker.path);
        }
    }

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}


/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * note: doesn't care about the timeouts - it's done by the poller globbaly   *
 * ***************************************************************************/
static void  handle_async_io(void) {
    
    static u_int64_t lastrun=0;
    u_int64_t queue_delay=0;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //first, calculating async delay
    conf.async_delay=glb_ms_time()-lastrun;
    lastrun=glb_ms_time();

    worker_process_results();
    worker_handle_timeouts(); //timed out items will be marked as -1 result and next retry will be made
	//todo: upon completing the worker interface, finish
    //the proper wait timeout handling
    usleep(10000);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
static void  worker_shutdown(void) {
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
	THIS_SHOULD_NEVER_HAPPEN;
    LOG_INF("Not implemented worker shutdown yet");
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
static int forks_count(void) {
	return CONFIG_GLB_WORKER_FORKS;
}

/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void glb_worker_init(void) {
	
    zbx_hashset_create(&conf.workers, 10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    poller_set_poller_callbacks(init_item, free_item, handle_async_io, send_request, worker_shutdown, forks_count);

    if (NULL == CONFIG_WORKERS_DIR ) {
        zabbix_log(LOG_LEVEL_WARNING, "Warning: trying to run glb_worker without 'WorkersScript' set in the config file, not starting");
        exit(-1);
    }
	
}
