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
//#include "preproc.h"
#include "zbxjson.h"

extern int CONFIG_EXT_SERVER_FORKS;

#define WORKER_RESTART_HOLD 10 

typedef struct {
    int last_restart;
    ext_worker_t *worker;
} worker_t;

extern char  *CONFIG_WORKERS_DIR;
extern int	 CONFIG_CONFSYNCER_FREQUENCY;


typedef struct {
    u_int64_t hash;
    u_int64_t itemid;
    u_int64_t hostid;
	unsigned int expire;
    unsigned char flags;
    unsigned char value_type;
} GLB_SERVER_IDX_T;



static void glb_server_submit_fail_result(poller_item_t *poller_item, char *error) {
    
    poller_preprocess_value(poller_item , NULL , glb_ms_time(), ITEM_STATE_NOTSUPPORTED, error);
}

/************************************************************************
 * submits result to the preprocessor                                   *
 * **********************************************************************/
static int glb_server_submit_result(poller_item_t *poller_item, char *response) {
    
    struct zbx_json_parse jp_resp;
    zbx_json_type_t type;
 
    worker_t *worker = poller_get_item_specific_data(poller_item);
    u_int64_t hash;

    GLB_SERVER_IDX_T *item_idx = NULL;
     
    poller_inc_responses();
    
    AGENT_RESULT	result={0};
       
    init_result(&result);
    zbx_rtrim(response, ZBX_WHITESPACE);
    SET_TEXT_RESULT(&result, zbx_strdup(NULL, response));
    poller_preprocess_value(poller_item, &result, glb_ms_time(), ITEM_STATE_NORMAL, NULL);
    free_result(&result);

    zabbix_log(LOG_LEVEL_DEBUG,"In %s: Finished", __func__);
}

static int init_item(DC_ITEM* dcitem, poller_item_t *poller_item) {

    worker_t *worker;
    char *args;
    struct zbx_json_parse jp;
    char full_path[MAX_STRING_LEN],  params[MAX_STRING_LEN], tmp_str[MAX_STRING_LEN];
    zbx_json_type_t type;
    char *path = NULL;
    
    LOG_DBG( "In %s() Started", __func__);

    worker = (worker_t*)zbx_calloc(NULL,0,sizeof(worker_t));
        
    if (NULL == CONFIG_WORKERS_DIR) {
        zabbix_log(LOG_LEVEL_WARNING,"To run worker as a server, set WorkerScripts dir location in the configuration file");
        //TODO: submit item in the error state here
        return FAIL;
    }

    if (NULL == dcitem->params ) {
        LOG_DBG("Cannot run a server worker with an empty path");
        DEBUG_ITEM(dcitem->itemid,"Cannot run a server worker with empty path");
        
        return FAIL;
    }
       
   if ('/' != dcitem->params[0] ) {
        zbx_snprintf(full_path,MAX_STRING_LEN,"%s/%s",CONFIG_WORKERS_DIR,dcitem->params);
        path = full_path;
   } else 
        path = dcitem->params;
        
        //worker->worker.path = zbx_strdup(NULL, path); 

   if (NULL != (args = strchr(path,' '))) {
        args[0] = 0;
        args++;
    } else 
        args = NULL;
        
    worker->worker = worker_init(path, GLB_SERVER_MAXCALLS, 1, GLB_WORKER_MODE_NEWLINE, GLB_WORKER_MODE_NEWLINE, CONFIG_TIMEOUT);

    glb_process_worker_params(worker->worker, args);

    poller_set_item_specific_data(poller_item, worker);
    glb_start_worker(worker->worker);

    DEBUG_ITEM(poller_get_item_id(poller_item), "Finished init of server item, worker %s", worker_get_path(worker->worker));

    return SUCCEED;
};


static void delete_item(poller_item_t *poller_item) {
    worker_t *worker = poller_get_item_specific_data(poller_item);    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Deleting server worker item ");
    
    glb_destroy_worker(worker->worker);
    
    zbx_free(worker);
}

#define MAX_ITERATIONS 10000
ITEMS_ITERATOR(check_workers_data_cb) {
    worker_t *worker =  poller_get_item_specific_data(poller_item);
    
    int iterations = 0;
    
    int last_status = SUCCEED;
    char *worker_responce = NULL;

    if (SUCCEED != worker_is_alive(worker->worker)) {
        int now = time(NULL);

        if (worker->last_restart + WORKER_RESTART_HOLD < now ) {
            worker->last_restart = now;
            glb_start_worker(worker->worker);
        } 
        return POLLER_ITERATOR_CONTINUE;
    }
        
    while ( iterations < MAX_ITERATIONS ) {
        last_status = async_buffered_responce(worker->worker, &worker_responce);
        
        if (FAIL == last_status ) {
            DEBUG_ITEM(poller_get_item_id(poller_item), "Worker is not running, setting UNSUPPORTED value");
            glb_server_submit_fail_result(poller_item, "Couldn't read from the worker - worker isn't running");
        }
         
        if (NULL == worker_responce)
            break;

        iterations++; 
              
        DEBUG_ITEM(poller_get_item_id(poller_item), "Got from worker: %s", worker_responce);
        glb_server_submit_result(poller_item, worker_responce);
              
        zbx_free(worker_responce);
    }



    return POLLER_ITERATOR_CONTINUE;
}

static void	handle_async_io(void) {
    poller_items_iterate(check_workers_data_cb, NULL);
}
static void ws_shutdown(void) {

}

static int forks_count(void) {
	return CONFIG_EXT_SERVER_FORKS;
}

static void start_poll(poller_item_t *poller_item)
{

}

int  glb_worker_server_init(void) {

    if (NULL == CONFIG_WORKERS_DIR ) {
        zabbix_log(LOG_LEVEL_WARNING, "Warning: trying to run glb_worker server without 'WorkersScript' set in the config file, not starting");
        exit(-1);
    }
    
    poller_set_poller_callbacks(init_item, delete_item, handle_async_io, start_poll, ws_shutdown, forks_count, NULL); 
	return SUCCEED;
}
