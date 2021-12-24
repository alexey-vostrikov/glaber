/**********************************************************************************
    Glb worker script item - execs scripts assuming they are workers
    supports async and sync flow

 */
#include "log.h"
#include "common.h"
#include "zbxserver.h"
#include "../../libs/zbxexec/worker.h"
#include "../lld/lld_protocol.c"
#include "../trapper/active.h"
#include "glb_server.h"
#include "glb_poller.h"
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"

extern int CONFIG_EXT_SERVER_FORKS;

#define WORKER_RESTART_HOLD 10 

typedef struct {
    zbx_hashset_t *items;
} worker_server_conf_t;

typedef struct {
    int last_restart;
    GLB_EXT_WORKER worker;
} worker_t;

extern char  *CONFIG_WORKERS_DIR;
extern int	 CONFIG_CONFSYNCER_FREQUENCY;

/*
typedef struct {
    u_int64_t id;
    const char *host;
    const char *meta;
    const char *interface;
} HOST_AUTOREG;
*/
/*
typedef struct {
    u_int64_t id;
    const char *host;
    const char *key;
    const char *lld_key;
    const char *lld_macro;
} LLD_ITEM_REG;
*/

typedef struct {
    u_int64_t hash;
    u_int64_t itemid;
    u_int64_t hostid;
	unsigned int expire;
    unsigned char flags;
    unsigned char value_type;
} GLB_SERVER_IDX_T;


/*******************************************************************
 * saves unique hostname-meta pairs to upload them as a host autoreg
 * *****************************************************************/

/*
static void add_host_regdata( worker_server_conf_t *conf, char *host, const char *interface, const char *meta1, const char *meta2) {
    HOST_AUTOREG host_autoreg;
    char tmp[MAX_ID_LEN * 2];
    const char *meta = NULL;

    if (NULL != meta1) {
        if (NULL != meta2)  {
           zbx_snprintf(tmp,MAX_ID_LEN*2,"%s,%s",meta1,meta2);
           meta = tmp;
        } else 
           meta = meta1;
    } else if (NULL != meta2) 
        meta = meta2;

    const char *p_host = zbx_heap_strpool_intern(host);
   
    if (NULL == p_host) 
        return;
    
     if (NULL != zbx_hashset_search(&conf->autoreg_hosts,&p_host)) {
        zabbix_log(LOG_LEVEL_DEBUG,"Host %s already exists in the list, not adding, total hosts %d", host, conf->autoreg_hosts.num_data);

        zbx_heap_strpool_release(p_host);
        return;
    }
    
    host_autoreg.id=(u_int64_t)p_host;
    host_autoreg.host = zbx_heap_strpool_intern(host);
    host_autoreg.meta = zbx_heap_strpool_intern(meta);
    host_autoreg.interface = zbx_heap_strpool_intern(interface);

    zbx_hashset_insert(&conf->autoreg_hosts,&host_autoreg,sizeof(host_autoreg));
}
*/
/*******************************************************************
 * saves unique hostname-keys pairs to upload them as a LLD
 * *****************************************************************/
/*
static void add_host_key_regdata( worker_server_conf_t *conf, const char *host, const char *key, const char *lld_key, const char *lld_macro ) {
    //adding unique host,key to the lld hash
    char tmp[MAX_STRING_LEN];
    LLD_ITEM_REG lld_item_reg;
    const char *p_tmp = NULL;
    zbx_snprintf(tmp,MAX_STRING_LEN,"%s,%s",host,key);

    p_tmp = zbx_heap_strpool_intern(tmp);

    if (NULL != zbx_hashset_search(&conf->lld_items_reg,&p_tmp)) {
        zabbix_log(LOG_LEVEL_DEBUG,"LLD data for %s,%s already exists in the cache not adding, total pairs %d", 
            host, key, conf->lld_items_reg.num_data);    
        zbx_heap_strpool_release(p_tmp);
    }

    lld_item_reg.id = (u_int64_t)p_tmp;
    lld_item_reg.host = zbx_heap_strpool_intern(host);
    lld_item_reg.key = zbx_heap_strpool_intern(key);
    lld_item_reg.lld_key = zbx_heap_strpool_intern(lld_key);
    lld_item_reg.lld_macro = zbx_heap_strpool_intern(lld_macro);

    zbx_hashset_insert(&conf->lld_items_reg,&lld_item_reg,sizeof(lld_item_reg));
}

*/

static void glb_server_submit_fail_result(GLB_POLLER_ITEM *glb_item, char *error) {
    
    worker_t *worker = (worker_t*)glb_item->itemdata;
    zbx_timespec_t ts;

    zbx_timespec(&ts);
    zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                             glb_item->flags , NULL , &ts, ITEM_STATE_NOTSUPPORTED, error);
    
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}



/************************************************************************
 * submits result to the preprocessor                                   *
 * **********************************************************************/
static int glb_server_submit_result(GLB_POLLER_ITEM *glb_item, char *response) {
    
    struct zbx_json_parse jp_resp;
    zbx_json_type_t type;
    zbx_timespec_t ts;
    worker_t *worker = (worker_t*)glb_item->itemdata;
    
//    char json_key[ITEM_KEY_LEN], full_key[ITEM_KEY_LEN], value[MAX_STRING_LEN], 
//        *val = NULL, *log_meta = NULL, *interface = NULL, *key = NULL,
//        itemid_s[MAX_ID_LEN], tmp[MAX_STRING_LEN];
 
    u_int64_t hash;
    int now = time(NULL);

    GLB_SERVER_IDX_T *item_idx = NULL;

    zbx_timespec(&ts);
      
    if (SUCCEED != zbx_json_open(response, &jp_resp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON response '%s' from worker %s", response, worker->worker.path);
		return FAIL;
    }

    //mandatory fields that has to present in the response
    //if (SUCCEED != zbx_json_value_by_name(&jp_resp, "host", host, MAX_ID_LEN, &type) ||
    //    SUCCEED != zbx_json_value_by_name(&jp_resp, "key", json_key, MAX_STRING_LEN, &type) ) {
    //    LOG_WRN("Cannot parse response from the worker: %s, missing either host,key or value fields",response);
    //   return FAIL;
    //}
    
    //if there is no dedicated value, then the whole response is a value
    //if (SUCCEED != zbx_json_value_by_name(&jp_resp, "value", value, MAX_STRING_LEN, &type) ) {
    //    val = response;
    //} else 
    //    val = value;
 
    //additional field that might be in the response, but not mandatory
    //if (SUCCEED == zbx_json_value_by_name(&jp_resp, "time", tmp, MAX_ID_LEN, &type) ) {
    //    ts.sec = strtol(tmp,NULL,10);
    //}

    //if (SUCCEED == zbx_json_value_by_name(&jp_resp, "time_ns", tmp, MAX_ID_LEN, &type) ) 
    //    ts.ns = strtol(tmp,NULL,10);
    /*   
    if (  worker->next_resolve < now ) {
        LOG_INF("Resolving itemid to send data to for server %s", worker->worker->path);

        zbx_host_key_t host_key = { .host = worker->host, .key = worker->key};
        int errcode;
        DC_ITEM dc_item;
        

        
        DCconfig_get_items_by_keys(&dc_item, &host_key, &errcode, 1);
     
        if (SUCCEED == errcode) {
            worker->itemid = dc_item.itemid;
            LOG_INF("Finished lookup in CC for item %s,%s, id is %ld",host,key,dc_item.itemid);
        } else {
            worker->itemid = 0;
            LOG_INF("Failed to find the itemid for worker %s",worker->worker->path);
        }
        
        DCconfig_clean_items(&dc_item, &errcode, 1);
        worker->next_resolve = now + CONFIG_CONFSYNCER_FREQUENCY /2;
    }  
    /*
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG,"Item has been found in the index with itemid %ld",&item_idx->itemid);
    }
    */ 
    //if ( 0 != worker->itemid  ) {
    
    AGENT_RESULT	result;
       
    init_result(&result);
    zbx_rtrim(response, ZBX_WHITESPACE);
    set_result_type(&result, ITEM_VALUE_TYPE_TEXT, response);
    zbx_preprocess_item_value(glb_item->hostid, glb_item->itemid, glb_item->value_type, 
                                             glb_item->flags , &result , &ts, ITEM_STATE_NORMAL, NULL);
    
    free_result(&result);
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}


/******************************************************************************
 * item init - from the general dc_item to compact local item        		  * 
 * ***************************************************************************/

//int glb_server_init_item(DC_ITEM *dc_item, GLB_POLLER_ITEM *poller_item) {
//    LOG_INF("Trying to create worker for itm %ld, host %s, key %s, params %s", dc_item->itemid, 
//			    dc_item->host.host, dc_item->key_orig, dc_item->params);    
    //item and host will be automatically placed to the items hash
    //however to find items arriving from the server workers really quickly
    //we need to keep the reverse index (host,key)->(itemid)
    //and also clean this index upon item removal (this may be done periodicaly)
    /*
     worker_server_conf_t *conf = ( worker_server_conf_t*)engine;
    char tmp[MAX_STRING_LEN];
    GLB_SERVER_IDX_T idx;

    server_item->hostname = zbx_heap_strpool_intern(dc_item->host.host);
    server_item->key      = zbx_heap_strpool_intern(dc_item->key_orig);

    zbx_snprintf(tmp,MAX_STRING_LEN,"%s,%s",server_item->hostname,server_item->key);

    //filling index
    idx.hash = ZBX_DEFAULT_STRING_HASH_FUNC(tmp);
    idx.itemid = dc_item->itemid;

    zbx_hashset_insert(&conf->items_idx,&idx,sizeof(GLB_SERVER_IDX_T));
    
    return SUCCEED;
*/
//  return FAIL;
//}

/*****************************************************************
 * creates a new worker structure
*****************************************************************/
/*
int  glb_server_create_worker( worker_server_conf_t *conf, char *worker_cfg) {

    
*/
/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
/*
void glb_server_free_item(void *engine, GLB_POLLER_ITEM *glb_poller_item ) {
    
     worker_server_conf_t *conf = ( worker_server_conf_t*)engine;
    GLB_SERVER_ITEM *glb_server_item = (GLB_SERVER_ITEM *)glb_poller_item->itemdata;

    char tmp[MAX_STRING_LEN];
    GLB_SERVER_IDX_T idx;

    zbx_snprintf(tmp, MAX_STRING_LEN,"%s,%s", glb_server_item->hostname,glb_server_item->key);
    idx.hash= ZBX_DEFAULT_STRING_HASH_FUNC(tmp);
    idx.itemid = glb_poller_item->itemid;

    zbx_hashset_remove(&conf->items_idx,&idx);
    
    zbx_heap_strpool_release(glb_server_item->key);
    zbx_heap_strpool_release(glb_server_item->hostname);
}
*/

//static void glb_server_process_results( worker_server_conf_t *conf) {
    /*
    char *worker_response = NULL;
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
           
    GLB_POLLER_ITEM *glb_poller_item;
    zbx_hashset_iter_t iter;
    GLB_SERVER_T *worker;
    static unsigned int last_reg_send = 0;
    
    //reading responses from all the workers we have
    //reading all the responses we have so far from the worker
    
    //worker iteration loop on the top
    zbx_hashset_iter_reset(&conf->workers,&iter);
    while (NULL != (worker = zbx_hashset_iter_next(&iter))) {
        //we only query alive workers
        //LOG_INF("Will read data from server worker %s pid is %d, total workers %d", worker->worker->path,worker->worker->pid, conf->workers.num_data);

        if (SUCCEED == worker_is_alive(worker->worker)) { //only read from alive workers

            zabbix_log(LOG_LEVEL_DEBUG,"Calling async read");
            while (SUCCEED == async_buffered_responce(worker->worker, &worker_response)) {
              
                zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s from worker %s", worker_response, worker->worker->path);
                glb_server_submit_result(conf, worker_response, worker);
            }
  
        } else {
            LOG_INF("Server worker %s is not alive, restarting", worker->worker->path);
        }
    }
*/
    //zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
//}


/*************************************************************
 * autoregisters collected hosts 
 * **********************************************************/
/*
int sync_hosts_autoreg( worker_server_conf_t *conf) {
    zbx_hashset_iter_t iter;
    HOST_AUTOREG *h_reg;

    zbx_hashset_iter_reset(&conf->autoreg_hosts,&iter);
    while ( NULL != (h_reg = (HOST_AUTOREG*)zbx_hashset_iter_next(&iter))) {
        zabbix_log(LOG_LEVEL_DEBUG,"Auto registering host %s meta %s",h_reg->host, h_reg->meta);
        
        db_register_host(h_reg->host,NULL,0,ZBX_TCP_SEC_UNENCRYPTED,h_reg->meta,ZBX_CONN_DNS,
            (NULL == h_reg->interface)?h_reg->host:h_reg->interface);
      
        zbx_heap_strpool_release(h_reg->host);
        zbx_heap_strpool_release(h_reg->meta);
        zbx_heap_strpool_release(h_reg->interface);
        zbx_heap_strpool_release((void *)h_reg->id);
        zbx_hashset_iter_remove(&iter);
    }

}
*/
/*************************************************************
 * uploads discovered lld items         
 * ***********************************************************/
/*
int sync_lld_data( worker_server_conf_t *conf) {
    zbx_hashset_iter_t iter;
    LLD_ITEM_REG *lld_reg;
    zbx_host_key_t host_key;
    int errcode;
    DC_ITEM dc_item;

    zbx_hashset_iter_reset(&conf->lld_items_reg,&iter);
    while ( NULL != (lld_reg = (LLD_ITEM_REG*)zbx_hashset_iter_next(&iter))) {
        
        //to submit LLD task first need to figure host,lld_key itemid
        //then - directly submit data as lld result
        host_key.host = (char *)lld_reg->host;
        host_key.key = (char *)lld_reg->lld_key;

        DCconfig_get_items_by_keys(&dc_item, &host_key, &errcode, 1);
        if (SUCCEED == errcode ) {
            AGENT_RESULT result;
            zbx_timespec_t ts;
            char lld_val[MAX_STRING_LEN], error[MAX_STRING_LEN];
            //now generating the LLD data 
  */
            /* [{"{#SITENAME}":"/"}] */
    /*        
            zbx_timespec(&ts);

            init_result(&result);
            zbx_snprintf(lld_val, MAX_STRING_LEN, "{ \"data\":[{\"{#%s}\":\"%s\"}] }", lld_reg->lld_macro, lld_reg->key);
            set_result_type(&result, ITEM_VALUE_TYPE_TEXT, lld_val);
            
            zabbix_log(LOG_LEVEL_DEBUG,"Will submit LLD %s",lld_val);
            
            //zbx_lld_process_agent_result(dc_item.itemid, &result, &ts, error);
            zbx_lld_process_value(dc_item.itemid, dc_item.host.hostid, lld_val, &ts, AR_META,0,time(NULL),NULL);
            
            //, 0, error);
           	
            free_result(&result);
        } else {
            zabbix_log(LOG_LEVEL_INFORMATION,"Failed to find an item id for LLD submission for hots->key '%s'->'%s'",lld_reg->host,lld_reg->lld_key);
        }
        DCconfig_clean_items(&dc_item, &errcode, 1);

        zbx_heap_strpool_release(lld_reg->host);
        zbx_heap_strpool_release(lld_reg->key);
        zbx_heap_strpool_release(lld_reg->lld_key);
        zbx_heap_strpool_release(lld_reg->lld_macro);
        zbx_heap_strpool_release((void *)lld_reg->id);
        
        zbx_hashset_iter_remove(&iter);
    }
}
*/


/*************************************************************
 * compare function for index hash
 * *********************************************************/
static int	server_idx_cmp_func(const void *d1, const void *d2)
{
	const GLB_SERVER_IDX_T	*i1 = (const GLB_SERVER_IDX_T *)d1;
	const GLB_SERVER_IDX_T	*i2 = (const GLB_SERVER_IDX_T *)d2;
    
	ZBX_RETURN_IF_NOT_EQUAL(i1->itemid, i2->itemid);

	return 0;
}

static void init_item(glb_poll_module_t *poll_mod, DC_ITEM* dcitem, GLB_POLLER_ITEM *glb_poller_item) {

    worker_server_conf_t *conf = (worker_server_conf_t *)poll_mod->poller_data;
    worker_t *worker;
    char *args;
    struct zbx_json_parse jp;
    char full_path[MAX_STRING_LEN],  params[MAX_STRING_LEN], tmp_str[MAX_STRING_LEN];
    zbx_json_type_t type;

    LOG_DBG( "In %s() Started", __func__);

    if (NULL == (worker = (worker_t*)zbx_calloc(NULL,0,sizeof(worker_t)))) {
        LOG_WRN("Couldn't allocate heap mem to create a worker, exiting");
        exit(-1);
    }

    if (NULL == CONFIG_WORKERS_DIR) {
        zabbix_log(LOG_LEVEL_WARNING,"To run worker as a server, set WorkerScripts dir location in the configuration file");
        exit(-1);
    }

    if (NULL == dcitem->params ) {
        LOG_DBG("Cannot run a server worker with an empty path");
        DEBUG_ITEM(dcitem->itemid,"Cannot run a server worker with empty path");
        return;
    }
       
    if ( NULL != dcitem->params) {
        char *path;
        
        if ('/' != dcitem->params[0] ) {
            zbx_snprintf(full_path,MAX_STRING_LEN,"%s/%s",CONFIG_WORKERS_DIR,dcitem->params);
            path = full_path;
        } else 
            path = dcitem->params;
        
        worker->worker.path = zbx_strdup(NULL, path); 

        if (NULL != (args = strchr(worker->worker.path,' '))) {
            args[0] = 0;
            args++;
        } else 
            args = NULL;
        
        glb_process_worker_params(&worker->worker, args);
        
       
    }
    
    worker->worker.async_mode = 1;
    worker->worker.max_calls = GLB_SERVER_MAXCALLS;
    worker->worker.mode_from_worker=GLB_WORKER_MODE_NEWLINE;
    
    glb_poller_item->itemdata = worker;
    
    glb_start_worker(&worker->worker);

    LOG_DBG("Finished init of server item %ld, worker %s",glb_poller_item->itemid, worker->worker.path);
};


static void delete_item(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item) {
    LOG_INF("Deleting server worker item %ld", glb_item->itemid);
    
    worker_t *worker = (worker_t*)glb_item->itemdata;

    glb_destroy_worker(&worker->worker);
    LOG_INF("freening he item");
    zbx_free(worker);
    LOG_INF("Finished deleting the item");

}

static void	handle_async_io(glb_poll_module_t *poll_mod) {
    zbx_hashset_iter_t iter;
    GLB_POLLER_ITEM *glb_item;
    char *worker_response = NULL;
    //polling all pollers in cycle if they've got some input
    LOG_DBG("In: %s", __func__);
    worker_server_conf_t *conf = (worker_server_conf_t*)poll_mod->poller_data;
    
    zbx_hashset_iter_reset(conf->items,&iter);
    
    while (NULL != (glb_item =(GLB_POLLER_ITEM *) zbx_hashset_iter_next(&iter))) {
        worker_t *worker = (worker_t*)glb_item->itemdata;
        if (SUCCEED == worker_is_alive(&worker->worker)) { 
            int last_status;
            
            while (SUCCEED == (last_status = async_buffered_responce(&worker->worker, &worker_response))) {
              
                LOG_DBG("Parsing line %s from worker %s", worker_response, &worker->worker.path);
                glb_server_submit_result(glb_item, worker_response);
            }
  
            if (FAIL == last_status ) {
                glb_server_submit_fail_result(glb_item,"Couldn't read from the worker - either filename is wrong or temporary fail");
            }

        } else {
            int now = time(NULL);

            if (worker->last_restart + WORKER_RESTART_HOLD < now ) {
                LOG_DBG("Server worker %s is not alive, restarting", worker->worker.path);
                worker->last_restart = now;
                glb_start_worker(&worker->worker);
            }
        }   
    }
    LOG_DBG("Finished: %s", __func__);
}

static void ws_shutdown(glb_poll_module_t *poll_mod) {

}
static int forks_count(glb_poll_module_t *poll_mod) {
	return CONFIG_EXT_SERVER_FORKS;
}
static void start_poll(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item)
{

}

int  glb_worker_server_init(glb_poll_engine_t *poll ) {
	int i, ret;
	worker_server_conf_t *conf;
    char **worker_cfg;
	
    LOG_DBG("In %s: starting", __func__);

    if (NULL == CONFIG_WORKERS_DIR ) {
        zabbix_log(LOG_LEVEL_WARNING, "Warning: trying to run glb_worker server without 'WorkersScript' set in the config file, not starting");
        exit(-1);
    }
    
    if (NULL == (conf = (worker_server_conf_t *)zbx_malloc(NULL,sizeof(worker_server_conf_t))) )  {
		LOG_WRN("Couldn't allocate memory for server workers module, exiting");
		return FAIL;
	}

    poll->poller.poller_data = conf;
    bzero(conf, sizeof(worker_server_conf_t));   

   // zbx_hashset_create(&conf->workers, 10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    conf->items = &poll->items; 

    poll->poller.init_item = init_item;
    poll->poller.delete_item = delete_item;
    poll->poller.handle_async_io = handle_async_io;
    poll->poller.start_poll = start_poll;
    poll->poller.shutdown = ws_shutdown;
    poll->poller.forks_count = forks_count;
	
	return SUCCEED;
}
