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

typedef struct {
    u_int64_t async_delay; //time between async calls to detect and properly handle timeouts
	zbx_hashset_t *items;
    zbx_binary_heap_t events;
	int *requests;
	int *responses;
    zbx_hashset_t workers;
 } 
 GLB_WORKER_CONF;

extern int CONFIG_GLB_WORKER_FORKS;
extern char  *CONFIG_WORKERS_DIR;

/************************************************************************
 * submits result to the preprocessor                                   *
 * **********************************************************************/
static int glb_worker_submit_result(GLB_WORKER_CONF *conf, char *response) {
    struct zbx_json_parse jp_resp;
    char value[MAX_STRING_LEN],itemid_s[MAX_ID_LEN],time_sec_s[MAX_ID_LEN], time_ns_s[MAX_ID_LEN];
    u_int64_t itemid;
    unsigned int time_sec = 0, time_ns = 0;
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Started", __func__);
    GLB_WORKER_ITEM *glb_worker_item; 
    GLB_POLLER_ITEM *glb_item;
    AGENT_RESULT	result;
    zbx_timespec_t ts;
    zbx_json_type_t type;
    
    //parsing responce, it should arrive as a json:
    zbx_timespec(&ts);
    
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
    if (NULL == ( glb_item = (GLB_POLLER_ITEM *)zbx_hashset_search(conf->items, &itemid))) {
        zabbix_log(LOG_LEVEL_WARNING, "Worker returned response with unknown itemid %ld, response is skipped",itemid);
        return FAIL;
    }

    glb_worker_item = (GLB_WORKER_ITEM*)glb_item->itemdata;      
    //it maybe feasible to allow a script to set the precise time 
    //but for now just using the current one 
           
    init_result(&result);
    zbx_rtrim(value, ZBX_WHITESPACE);
    
    set_result_type(&result, ITEM_VALUE_TYPE_TEXT, value);
    //zabbix_log(LOG_LEVEL_INFORMATION,"Send value %s to preprocessing",value);
	zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                                glb_item->flags , &result , &ts, ITEM_STATE_NORMAL, NULL);
    
    free_result(&result);
    
    //marking that polling has finished
    glb_item->state = POLL_QUEUED;
    glb_worker_item->state = POLL_QUEUED;
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: marked item %ld as available for polling ", __func__,glb_item->itemid);
    
    glb_item->lastpolltime = time(NULL);
  
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}


/******************************************************************************
 * item init - from the general dc_item to compact local item        		  * 
 * ***************************************************************************/
unsigned int glb_worker_init_item(DC_ITEM *dc_item, GLB_WORKER_ITEM *worker_item) {
    
    char error[MAX_STRING_LEN];
	AGENT_REQUEST	request;
    char *parsed_key = NULL, *cmd = NULL, *params_dyn = NULL, *key_dyn = NULL, *params_stat = NULL;
    size_t		dyn_alloc = 0, stat_alloc = 0, dyn_offset = 0, stat_offset = 0, cmd_alloc = ZBX_KIBIBYTE, cmd_offset = 0;
    int ret = FAIL, i;
    zabbix_log(LOG_LEVEL_INFORMATION,"Staring %s",__func__);
    zabbix_log(LOG_LEVEL_INFORMATION,"Item key is %s",dc_item->key_orig);
    init_request(&request);

    cmd = (char *)zbx_malloc(cmd, cmd_alloc);
    ZBX_STRDUP(parsed_key, dc_item->key_orig);
    
    //translating dynamic params
    //for worker params we will use parsed parameters, cache them to config cache reload time
    if (SUCCEED != substitute_key_macros(&parsed_key, NULL, dc_item, NULL, NULL, MACRO_TYPE_ITEM_KEY, error,
				sizeof(error))) {
        zabbix_log(LOG_LEVEL_INFORMATION,"Failed to apply macroses to dynamic params %s",parsed_key);
        goto out;
    }
    
    if (SUCCEED != parse_item_key(parsed_key, &request)) {
	    zabbix_log(LOG_LEVEL_INFORMATION,"Failed to parse item key %s",parsed_key);
        goto out;
    }
    
    //itemid is always needed in dynamic params, generating new list only having dynamic data
    zbx_snprintf_alloc(&cmd,&cmd_alloc,&cmd_offset,"%s/%s", CONFIG_WORKERS_DIR, get_rkey(&request));
    worker_item->full_cmd = zbx_heap_strpool_intern(cmd);
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
    zabbix_log(LOG_LEVEL_INFORMATION,"Parsed params: %s", key_dyn);
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
GLB_WORKER_T * glb_worker_create_worker(GLB_WORKER_CONF *conf, GLB_WORKER_ITEM *glb_worker_item) {

    GLB_WORKER_T worker, *retworker;
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s() Started", __func__);
   
    zabbix_log(LOG_LEVEL_INFORMATION, "creating a worker for cmd %s ",glb_worker_item->full_cmd);
    zabbix_log(LOG_LEVEL_INFORMATION, "creating a worker params %s", glb_worker_item->params_dyn);
    bzero(&worker, sizeof(GLB_WORKER_T));
    worker.workerid = glb_worker_item->workerid;
    worker.worker.path = (char *)zbx_heap_strpool_intern(glb_worker_item->full_cmd);
    worker.worker.async_mode = 1;
    worker.worker.max_calls = GLB_WORKER_MAXCALLS;
    worker.worker.mode_from_worker=GLB_WORKER_MODE_NEWLINE;
    worker.worker.mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    worker.worker.timeout = CONFIG_TIMEOUT;
    //worker.worker.params = (char *) zbx_heap_strpool_intern(glb_worker_item->params);    
      
    retworker = (GLB_WORKER_T*)zbx_hashset_insert(&conf->workers,&worker,sizeof(GLB_WORKER_T));
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s() Finished worker id is %ld", __func__, retworker->workerid);
    
    return retworker;
}

/******************************************************************
 * Sends the actual packet (request to worker to send a packet
 * ****************************************************************/
int glb_worker_send_request(void *engine, GLB_POLLER_ITEM *glb_item) {
    zbx_timespec_t ts;

    char request[MAX_STRING_LEN];
    unsigned int now = time(NULL);
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    GLB_WORKER_ITEM *glb_worker_item = (GLB_WORKER_ITEM*)glb_item->itemdata;
    GLB_WORKER_CONF *conf = (GLB_WORKER_CONF*)engine;

    glb_worker_item->lastrequest = glb_ms_time();

    GLB_WORKER_T *worker = (GLB_WORKER_T *)zbx_hashset_search(&conf->workers,&glb_worker_item->workerid);
    
    if (NULL == worker ) {
        //worker not found - creating a new one
        zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't find a worker for item %ld, creating a new one", glb_item->itemid);
        worker = glb_worker_create_worker(conf, glb_worker_item);
    }
    
    zabbix_log(LOG_LEVEL_DEBUG, "Will do request: %s to worker %s",glb_worker_item->params_dyn, glb_worker_item->full_cmd);
    if (SUCCEED != glb_worker_request(&worker->worker, glb_worker_item->params_dyn) ) {
        //sending config error status for the item
        zbx_timespec(&ts);
        zabbix_log(LOG_LEVEL_DEBUG, "Couldn't send request %s",request);
        
        zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                                glb_item->flags , NULL , &ts, ITEM_STATE_NOTSUPPORTED, "Couldn't send request to the script");
        
        return FAIL;
    }

    zabbix_log(LOG_LEVEL_DEBUG, "Request finished");
    
    glb_worker_item->lastrequest = glb_ms_time();

   zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
   return SUCCEED;
}
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
void glb_worker_free_item(GLB_WORKER_ITEM *glb_worker_item ) {
    zbx_free(glb_worker_item->params_dyn);
    zbx_heap_strpool_release(glb_worker_item->full_cmd);
}


/******************************************************************************
 * timeouts handling 
 * check item's timeouts
 * checks worker's timeouts (workers that hasn't been used for some time
 *                          are stopped and freed )
 * ***************************************************************************/
static void glb_worker_handle_timeouts(GLB_WORKER_CONF *conf) {

	zbx_hashset_iter_t iter;
    u_int64_t *itemid;

    static u_int64_t last_check=0;
    u_int64_t glb_time = glb_ms_time();

    GLB_POLLER_ITEM *glb_item;
    zbx_timespec_t ts;
    
    zbx_timespec(&ts);
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //once a second, may be too long if we check a few millions of hosts   
    if (last_check + 1000 > glb_time ) return;
     last_check = glb_time;
    
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() starting worker timouts check", __func__);

    zbx_hashset_iter_reset(conf->items,&iter);
    while (NULL != (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_iter_next(&iter))) {
         GLB_WORKER_ITEM *glb_worker_item = (GLB_WORKER_ITEM*)glb_item->itemdata;
         
         //we've got the item, checking for timeouts:
         if ( POLL_POLLING == glb_worker_item->state ) {
         
   
        }
    }
        
   zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}


static void glb_worker_process_results(GLB_WORKER_CONF *conf) {
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
    zbx_hashset_iter_t iter;
    GLB_WORKER_T *worker;
    //reading responses from all the workers we have
    //reading all the responses we have so far from the worker
    
    //worker iteration loop on the top
    zbx_hashset_iter_reset(&conf->workers,&iter);
    while (NULL != (worker = zbx_hashset_iter_next(&iter))) {
        //we only query alive workers
        zabbix_log(LOG_LEVEL_DEBUG,"Will read data from worker %s", worker->worker.path);
        if (SUCCEED == worker_is_alive(&worker->worker)) { //only read from alive workers
            zabbix_log(LOG_LEVEL_DEBUG,"Calling async read");
            while (SUCCEED == async_buffered_responce(&worker->worker, &worker_response)) {
              
                zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s from worker %s", worker_response, worker->worker.path);
                glb_worker_submit_result(conf, worker_response);
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
void  glb_worker_handle_async_io(void *engine) {
    
    static u_int64_t lastrun=0;
    u_int64_t queue_delay=0;
	GLB_WORKER_CONF *conf = (GLB_WORKER_CONF*)engine;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    //first, calculating async delay
    conf->async_delay=glb_ms_time()-lastrun;
    lastrun=glb_ms_time();

    //parses and submits arrived ICMP responses
    glb_worker_process_results(conf);
  
    //handling timed-out items
    glb_worker_handle_timeouts(engine); //timed out items will be marked as -1 result and next retry will be made
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}



/******************************************************************************
 * inits async structures - static connection pool							  *
 * ***************************************************************************/
void* glb_worker_init(zbx_hashset_t *items, int *requests, int *responses ) {
	int i;
	static GLB_WORKER_CONF *conf;
	char init_string[MAX_STRING_LEN];
    char full_path[MAX_STRING_LEN];
    char add_params[MAX_STRING_LEN];
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
    add_params[0]='\0';

	if (NULL == (conf = zbx_malloc(NULL,sizeof(GLB_WORKER_CONF))) )  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}
    
    //zbx_hashset_create(&conf->pinged_items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    conf->items = items;
    zbx_hashset_create(&conf->workers, 10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    conf->requests = requests;
	conf->responses = responses;

    if (NULL == CONFIG_WORKERS_DIR ) {
        zabbix_log(LOG_LEVEL_WARNING, "Warning: trying to run glb_worker without 'WorkersScript' set in the config file, not starting");
        exit(-1);
    }

	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return (void *)conf;
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
void    glb_worker_shutdown(void *engine) {
	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
	GLB_WORKER_CONF *conf = (GLB_WORKER_CONF*)engine;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
