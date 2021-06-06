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
#include "module.h"
#include "preproc.h"
#include "zbxjson.h"

typedef struct {
  //  zbx_hashset_t *items;
  	int *requests;
	int *responces;
    zbx_hashset_t workers;
    zbx_hashset_t items_idx; //index to quickly locate itemids for arrived data
    zbx_hashset_t lld_items_reg; //index of already registered lld items
   // zbx_hashset_t lld_reg_data; //lld items registration data
    zbx_hashset_t autoreg_hosts;
 } GLB_SERVER_CONF;

extern char  *CONFIG_WORKERS_DIR;
extern char  **CONFIG_EXT_SERVERS;
extern int	 CONFIG_CONFSYNCER_FREQUENCY;
/*
typedef struct {
    u_int64_t id;
    u_int64_t itemid;
    u_int64_t created;
} LLD_ITEM_CACHE;
*/

typedef struct {
    u_int64_t id;
    const char *host;
    const char *meta;
    const char *interface;
} HOST_AUTOREG;

typedef struct {
    u_int64_t id;
    const char *host;
    const char *key;
    const char *lld_key;
    const char *lld_macro;
} LLD_ITEM_REG;

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
static void add_host_regdata(GLB_SERVER_CONF *conf, char *host, const char *interface, const char *meta1, const char *meta2) {
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

/*******************************************************************
 * saves unique hostname-keys pairs to upload them as a LLD
 * *****************************************************************/
static void add_host_key_regdata(GLB_SERVER_CONF *conf, const char *host, const char *key, const char *lld_key, const char *lld_macro ) {
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

/************************************************************************
 * submits result to the preprocessor                                   *
 * **********************************************************************/
static int glb_server_submit_result(GLB_SERVER_CONF *conf, char *response, GLB_SERVER_T *worker) {
    
    //parsing responce
    struct zbx_json_parse jp_resp;
    zbx_json_type_t type;
    zbx_timespec_t ts;
    char host[HOST_HOST_LEN_MAX],json_key[ITEM_KEY_LEN], full_key[ITEM_KEY_LEN], value[MAX_STRING_LEN], 
        *val = NULL, *log_meta = NULL, *interface = NULL, *key = NULL,
        itemid_s[MAX_ID_LEN], tmp[MAX_STRING_LEN];
    u_int64_t hash;
    int now = time(NULL);


    GLB_SERVER_IDX_T *item_idx = NULL;

    //parsing responce, it should arrive as a json:
    zbx_timespec(&ts);
      
    if (SUCCEED != zbx_json_open(response, &jp_resp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON response '%s' from worker", response);
		return FAIL;
    }

    //mandatory fields that has to present in the response
    if (SUCCEED != zbx_json_value_by_name(&jp_resp, "host", host, MAX_ID_LEN, &type) ||
        SUCCEED != zbx_json_value_by_name(&jp_resp, "key", json_key, MAX_STRING_LEN, &type) ) {
       zabbix_log(LOG_LEVEL_DEBUG,"Cannot parse response from the worker: %s, missing either host,key or value fields",response);
       return FAIL;
    }
    
    //if there is no dedicated value, then the whole response is a value
    if (SUCCEED != zbx_json_value_by_name(&jp_resp, "value", value, MAX_STRING_LEN, &type) ) {
        val = response;
    } else 
        val = value;
 
    //additional field that might be in the response, but not mandatory
    if (SUCCEED == zbx_json_value_by_name(&jp_resp, "time", tmp, MAX_ID_LEN, &type) ) {
        ts.sec = strtol(tmp,NULL,10);
    }

    if (SUCCEED == zbx_json_value_by_name(&jp_resp, "time_ns", tmp, MAX_ID_LEN, &type) ) 
        ts.ns = strtol(tmp,NULL,10);

    //looking for the itemid for the host,key
    if (NULL != worker->item_key) {
        //means the key should be prefixed with key name 
        //and the key value will be the param
        zbx_snprintf(full_key,ITEM_KEY_LEN,"%s[%s]",worker->item_key,json_key);
        key = full_key;
    } else key = json_key;

    
    zbx_snprintf(tmp,MAX_STRING_LEN,"%s,%s",host,key);
    
    hash = (u_int64_t) zbx_heap_strpool_intern(tmp);
    
    item_idx = (GLB_SERVER_IDX_T*)zbx_hashset_search(&conf->items_idx,(void *)&hash);
    
    //checking if it's outdated
    if ( NULL != item_idx && item_idx->expire < now ) {
        zbx_hashset_remove_direct(&conf->items_idx, item_idx);
        item_idx = NULL;
    }
    
    if ( NULL == item_idx ) {
        zabbix_log(LOG_LEVEL_DEBUG,"Item %s,%s is not in index, doing CC lookup",host,key);

        zbx_host_key_t host_key = { .host=(char *) host, .key = (char*) key};
        int errcode;
        DC_ITEM dc_item;
        GLB_SERVER_IDX_T item_idx2;

        DCconfig_get_items_by_keys(&dc_item, &host_key, &errcode, 1);
        
        zabbix_log(LOG_LEVEL_DEBUG,"Finished lookup in CC for item %s,%s, id is %ld",host,key,dc_item.itemid);
        
        if (SUCCEED == errcode ) {
            item_idx2.itemid = dc_item.itemid;
            item_idx2.hostid = dc_item.host.hostid;
            item_idx2.value_type = dc_item.value_type;
            item_idx2.flags = dc_item.flags;

        } else {
            //this will serve as a sort of negative record to not to kill CC under heavy
            //income data rates             
            item_idx2.itemid = 0;
        }

        item_idx2.hash = hash;
        item_idx2.expire = time(NULL) + GLB_SERVER_ITEMID_CACHE_TTL;
        item_idx = zbx_hashset_insert(&conf->items_idx,&item_idx2,sizeof(GLB_SERVER_IDX_T));

        DCconfig_clean_items(&dc_item, &errcode, 1);
    }  
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG,"Item has been found in the index with itemid %ld",&item_idx->itemid);
    }
       
    if ( NULL != item_idx && 0 != item_idx->itemid ) {
        //looking for the glb_item
        // GLB_POLLER_ITEM *glb_item = (GLB_POLLER_ITEM*)zbx_hashset_search(conf->items,&item_idx->itemid);
        
        //if (NULL != glb_item) {
        AGENT_RESULT	result;
        init_result(&result);
        zbx_rtrim(val, ZBX_WHITESPACE);
        set_result_type(&result, ITEM_VALUE_TYPE_TEXT, val);
        zbx_preprocess_item_value(item_idx->hostid, item_idx->itemid, item_idx->value_type, 
                                                item_idx->flags , &result , &ts, ITEM_STATE_NORMAL, NULL);
    
        free_result(&result);
    } 

    //the negative result might mean host doesn't exists in the config, adding the host to reg data
    if (worker->auto_reg_hosts)  {
        char iface[MAX_STRING_LEN];
   
        if (SUCCEED == zbx_json_value_by_name(&jp_resp, "metadata", tmp, MAX_ID_LEN, &type) ) 
            log_meta = tmp;

        if (  NULL != worker->interface_param &&  
              SUCCEED == zbx_json_value_by_name(&jp_resp, worker->interface_param, iface, MAX_STRING_LEN, &type) ) 
            interface = iface;

        zabbix_log(LOG_LEVEL_DEBUG,"Autoregistration is enabled, adding host %s with metadata %s,%s , interface %s to autoreg data",
            host, log_meta, worker->auto_reg_metadata, interface);
        
        add_host_regdata(conf, host, interface, log_meta, worker->auto_reg_metadata);
    }
    
    if ( NULL != worker->lld_key_name ) {
        add_host_key_regdata(conf, host, json_key, worker->lld_key_name, worker->lld_macro_name);
    }

    //autoregistration data is checked and submitted during checking all the worker responces once 
    //each 1/2 time interval of config cache reload 

    
    //TODO: here we should match incoming host name and incoming itemid to
    // existing items and found the itemid belonging to them to submit to
    // the preprocessing
    // also we should form a list of host->keys to periodicaly feed lld data
    // to the preprocessing to automatically create the new items

    //actually, there are two steps 
    //1. if host isn't registered yet and autoregistration is on for the worker - register it!
    //2. if lld is on for the worker , add the item to the registration data, which should be 
    // submitted requlary
    //3. It's very logical to have this options to be on or off based by worker or server
    //so in config it should look like:
    //WorkerServer={"path":"path_to_the_worker","add_hosts":"yes","lld_key":"discovery","metadata":"listener"}
    //DC_HOST dd;
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}



/******************************************************************************
 * item init - from the general dc_item to compact local item        		  * 
 * ***************************************************************************/
unsigned int glb_server_init_item(void *engine, DC_ITEM *dc_item, GLB_SERVER_ITEM *server_item) {
    
    //item and host will be automatically placed to the items hash
    //however to find items arriving from the server workers really quickly
    //we need to keep the reverse index (host,key)->(itemid)
    //and also clean this index upon item removal (this may be done periodicaly)
    GLB_SERVER_CONF *conf = (GLB_SERVER_CONF*)engine;
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
}

/*****************************************************************
 * creates a new worker structure
*****************************************************************/
int  glb_server_create_worker(GLB_SERVER_CONF *conf, char *worker_cfg) {

    GLB_SERVER_T worker, *retworker;
    struct zbx_json_parse jp;
    char full_path[MAX_STRING_LEN], path[MAX_STRING_LEN], params[MAX_STRING_LEN], tmp_str[MAX_STRING_LEN];
    zbx_json_type_t type;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
   
    bzero(&worker, sizeof(GLB_SERVER_T));
     
    if (SUCCEED != zbx_json_open(worker_cfg, &jp)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse worker configuration not a valid JSON: '%s' from worker", worker_cfg);
		exit(-1);
    }

    //path is mandatory
    if (SUCCEED != zbx_json_value_by_name(&jp, "path", path, MAX_STRING_LEN, &type)) {
        zabbix_log(LOG_LEVEL_WARNING,"Couldn't parse 'path' field in the worker configuration: %s", worker_cfg);
        exit(-1);
    }

    if (NULL == CONFIG_WORKERS_DIR) {
        zabbix_log(LOG_LEVEL_WARNING,"To run worker as a server, set WorkerScripts dir location in the configuration file");
        exit(-1);
    }
    zbx_snprintf(full_path,MAX_STRING_LEN,"%s/%s",CONFIG_WORKERS_DIR,path);
/*
    if (-1 != access(full_path, X_OK)) {
		zabbix_log(LOG_LEVEL_DEBUG ,"Found command '%s' - ok for adding to glb_worker",full_path);
	} else {
		zabbix_log(LOG_LEVEL_WARNING ,"Couldn't find command '%s' or it's not executable, stopping",full_path);
        exit(-1);
	}
  */  
    
    //theese are addtional, off by default
    if ( SUCCEED == zbx_json_value_by_name(&jp, "hosts_autoreg",tmp_str , MAX_ID_LEN, &type) &&
         0 == strcmp(tmp_str,"yes")) 
        worker.auto_reg_hosts = 1;
        
    if (SUCCEED == zbx_json_value_by_name(&jp, "metadata", tmp_str, MAX_ID_LEN, &type)) 
        worker.auto_reg_metadata = zbx_heap_strpool_intern(tmp_str);
    
    if (SUCCEED == zbx_json_value_by_name(&jp, "item_key", tmp_str, MAX_ID_LEN, &type)) 
        worker.item_key = zbx_heap_strpool_intern(tmp_str);
    
    if (SUCCEED == zbx_json_value_by_name(&jp, "interface_param", tmp_str, MAX_ID_LEN, &type)) 
        worker.interface_param = zbx_heap_strpool_intern(tmp_str);
    else 
        worker.interface_param = zbx_heap_strpool_intern("interface");

    if (SUCCEED == zbx_json_value_by_name(&jp, "lld_key", tmp_str, MAX_STRING_LEN, &type) )
        worker.lld_key_name = (char *)zbx_heap_strpool_intern(tmp_str);
    
    if ( SUCCEED == zbx_json_value_by_name(&jp, "lld_macro", tmp_str, MAX_STRING_LEN, &type)) 
        worker.lld_macro_name = (char *)zbx_heap_strpool_intern(tmp_str);
    else 
        worker.lld_macro_name = (char *)zbx_heap_strpool_intern(GLB_DEFAULT_SERVER_MACRO_NAME);
    
    zabbix_log(LOG_LEVEL_INFORMATION, "creating a server worker for cmd %s ",full_path);
     
    
    //doing worker json init to parse arguments correcly.
    worker.worker = glb_init_worker(worker_cfg);
    
    if (NULL == worker.worker) {
        zabbix_log(LOG_LEVEL_WARNING,"Cannot init worker, with params %s",worker_cfg);
        exit(-1);
    }
    //worker path has to be interned
    //char *tmp = worker.worker->path;
    //worker.worker->path = (char *)zbx_heap_strpool_intern(tmp);
    //zbx_free(tmp);

    //fine-tuning some params
    worker.worker->async_mode = 1;
    worker.worker->max_calls = GLB_SERVER_MAXCALLS;
    worker.worker->mode_from_worker=GLB_WORKER_MODE_NEWLINE;
    worker.worker->timeout = CONFIG_TIMEOUT;
      
    retworker = (GLB_SERVER_T*)zbx_hashset_insert(&conf->workers,&worker,sizeof(GLB_SERVER_T));

    glb_start_worker(retworker->worker);
    return SUCCEED;
}

/******************************************************************************
 * item deinit - freeing all interned string								  * 
 * ***************************************************************************/
void glb_server_free_item(void *engine, GLB_POLLER_ITEM *glb_poller_item ) {
    
    GLB_SERVER_CONF *conf = (GLB_SERVER_CONF*)engine;
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


static void glb_server_process_results(GLB_SERVER_CONF *conf) {
    
    char *worker_response = NULL;
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
           
    GLB_POLLER_ITEM *glb_poller_item;
    zbx_hashset_iter_t iter;
    GLB_SERVER_T *worker;
    static unsigned int last_reg_send = 0;
    
    //reading responces from all the workers we have
    //reading all the responces we have so far from the worker
    
    //worker iteration loop on the top
    zbx_hashset_iter_reset(&conf->workers,&iter);
    while (NULL != (worker = zbx_hashset_iter_next(&iter))) {
        //we only query alive workers
        zabbix_log(LOG_LEVEL_DEBUG,"Will read data from worker %s pid is %d", worker->worker->path,worker->worker->pid);
        if (SUCCEED == worker_is_alive(worker->worker)) { //only read from alive workers

            zabbix_log(LOG_LEVEL_DEBUG,"Calling async read");
            while (SUCCEED == async_buffered_responce(worker->worker, &worker_response)) {
              
                zabbix_log(LOG_LEVEL_DEBUG,"Parsing line %s from worker %s", worker_response, worker->worker->path);
                glb_server_submit_result(conf, worker_response, worker);
            }
            //checking if registration data could be send by now
            if (time(NULL) - last_reg_send > CONFIG_CONFSYNCER_FREQUENCY/2 ) {
                //glb_submit_hostreg_data(worker);
                //glb_preprocess_lld_data(worker);
            }

        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"Worker %s is not alive, skipping", worker->worker->path);
        }
    }

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}


/*************************************************************
 * autoregisters collected hosts 
 * **********************************************************/
int sync_hosts_autoreg(GLB_SERVER_CONF *conf) {
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
/*************************************************************
 * uploads discovered lld items         
 * ***********************************************************/
int sync_lld_data(GLB_SERVER_CONF *conf) {
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
            /* [{"{#SITENAME}":"/"}] */
            
            zbx_timespec(&ts);

            init_result(&result);
            zbx_snprintf(lld_val, MAX_STRING_LEN, "{ \"data\":[{\"{#%s}\":\"%s\"}] }", lld_reg->lld_macro, lld_reg->key);
            set_result_type(&result, ITEM_VALUE_TYPE_TEXT, lld_val);
            
            zabbix_log(LOG_LEVEL_DEBUG,"Will submit LLD %s",lld_val);
            
            //zbx_lld_process_agent_result(dc_item.itemid, &result, &ts, error);
            zbx_lld_process_value(dc_item.itemid, lld_val, &ts, AR_META,0,time(NULL),NULL);
            
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

/******************************************************************************
 * handles i/o - calls selects/snmp_recieve, 								  * 
 * note: doesn't care about the timeouts - it's done by the poller globbaly   *
 * ***************************************************************************/
void  glb_server_handle_async_io(void *engine) {
    static unsigned int last_sync_time = 0;

	GLB_SERVER_CONF *conf = (GLB_SERVER_CONF*)engine;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);
    
    glb_server_process_results(conf);
      
    //TODO: add periodical worker's check and restart if needed  
    if (time(NULL) - last_sync_time > CONFIG_CONFSYNCER_FREQUENCY/2) {
        zabbix_log(LOG_LEVEL_DEBUG,"It's time to sync autodiscovery hosts and lld data");
        sync_hosts_autoreg(conf);
        sync_lld_data(conf);
        zabbix_log(LOG_LEVEL_DEBUG,"Finished sync");
        last_sync_time = time(NULL);
    }
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}

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

/******************************************************************************
 * init
 * ***************************************************************************/
void* glb_server_init( int *requests, int *responces ) {
	int i, ret;
	static GLB_SERVER_CONF *conf;
    char **worker_cfg;
	
    zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);

	if (NULL == (conf = zbx_malloc(NULL,sizeof(GLB_SERVER_CONF))) )  {
			zabbix_log(LOG_LEVEL_WARNING,"Couldn't allocate memory for async snmp connections data, exititing");
			exit(-1);
		}
        
    conf->requests = requests;
	conf->responces = responces;


    if (NULL == CONFIG_WORKERS_DIR ) {
        zabbix_log(LOG_LEVEL_WARNING, "Warning: trying to run glb_server without 'WorkersScript' set in the config file, not starting");
        exit(-1);
    }
        
    zbx_hashset_create(&conf->workers, 10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_hashset_create(&conf->autoreg_hosts, 10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_hashset_create(&conf->lld_items_reg, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_hashset_create(&conf->items_idx, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC,server_idx_cmp_func);
    
    if ( NULL == *CONFIG_EXT_SERVERS) {
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }
    
    for (worker_cfg = CONFIG_EXT_SERVERS; NULL != *worker_cfg; worker_cfg++)
	{
        if (SUCCEED != (ret = glb_server_create_worker(conf, *worker_cfg)))
			exit(-1);
	}
  
  	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
	return (void *)conf;
}

/******************************************************************************
 * does snmp connections cleanup, not related to snmp shutdown 				  * 
 * ***************************************************************************/
void    glb_server_shutdown(void *engine) {
	
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);	
	//need to deallocate time hashset 
    //stop the glbmap exec
    //dealocate 
	GLB_SERVER_CONF *conf = (GLB_SERVER_CONF*)engine;
    zbx_hashset_destroy(&conf->autoreg_hosts);
    zbx_hashset_destroy(&conf->items_idx);
    zbx_hashset_destroy(&conf->lld_items_reg);
    
    //TODO: shutdown workers and deallocate them
    //this should be copied to pinger, worker either
    //also cleanup items idx
    THIS_SHOULD_NEVER_HAPPEN;
    exit(-1);


	zabbix_log(LOG_LEVEL_DEBUG, "In %s: Ended", __func__);
}
