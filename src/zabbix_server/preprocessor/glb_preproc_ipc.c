/*
** Copyright Glaber
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

/* this is simple monitoring engine to export internal metrics
via prometheus protocol via standart monitoring url

now it's NOT follows standards as it doesn't support HELP and TYPE keywords
*/

//TODO idea for improvement - implement a kind of a buffer pool to avoid alloc cluttering

#include "common.h"
#include "zbxalgo.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "log.h"
#include "memalloc.h"
#include "metric.h"

#define CONFIG_PREPROC_IPC_SIZE     128 * ZBX_MEBIBYTE
#define PREPROC_IPC_METRICS_BUFFER  2048

extern int  CONFIG_GLB_PREPROCESSOR_FORKS;

static  zbx_mem_info_t	*preproc_ipc_mem;
ZBX_MEM_FUNC_IMPL(_preprocipc, preproc_ipc_mem);

typedef struct  {
    mem_funcs_t memf;
    ipc_conf_t *ipc;
} preproc_ipc_conf_t;

// typedef struct {
//     metric_t metric;
//     char *local_buff;
//     int buff_size;
// } ipc_metric_t;

static preproc_ipc_conf_t conf = {0};

static char *preproc_ipc_allocate_str(const char *str) {
    size_t len = strlen(str) + 1;

    char *new_str = conf.memf.malloc_func(NULL, len);
    memcpy(new_str, str, len);
    
    return new_str;
}

static void  preproc_ipc_free_buffer(void *buff) {
    conf.memf.free_func(buff);
}

IPC_CREATE_CB(preproc_ipc_metric_create_cb) {

    metric_t *local_metric = local_data, *ipc_metric = ipc_data;
    char *ipc_str = NULL; 

    memcpy(ipc_metric, local_metric, sizeof(metric_t));

    if ( SUCCEED == variant_is_dynamic_length(&local_metric->value)) 
        ipc_metric->value.data.str = preproc_ipc_allocate_str(local_metric->value.data.str);


}

IPC_FREE_CB(preproc_ipc_metric_free_cb) {
    metric_t *ipc_metric = ipc_data;
    
    if (SUCCEED == variant_is_dynamic_length(&ipc_metric->value)) {
        preproc_ipc_free_buffer(ipc_metric->value.data.str);
    }

    ipc_metric->value.type = VARIANT_VALUE_NONE;
}

int preproc_ipc_init(size_t ipc_size) {
    char *error = NULL;
  //  LOG_INF("doing preproc ipc init");
    if (SUCCEED != zbx_mem_create(&preproc_ipc_mem, ipc_size, "Preproc IPC buffer size", "PreprocBufferSize ", 1, &error)) {
        LOG_WRN("Shared memory create failed: %s", error);
    	return FAIL;
    }
      // LOG_INF("doing preproc ipc init2");

    conf.memf.free_func = _preprocipc_mem_free_func;
    conf.memf.malloc_func = _preprocipc_mem_malloc_func;
    conf.memf.realloc_func = _preprocipc_mem_realloc_func;

    conf.ipc = glb_ipc_init(PREPROC_IPC_METRICS_BUFFER * CONFIG_GLB_PREPROCESSOR_FORKS, sizeof(metric_t), 
    CONFIG_GLB_PREPROCESSOR_FORKS , &conf.memf, preproc_ipc_metric_create_cb,
         preproc_ipc_metric_free_cb, IPC_HIGH_VOLUME);
      // LOG_INF("doing preproc ipc init3");


    //LOG_INF("Preprocs shm initi successifull");
    return SUCCEED;
}

void preproc_ipc_destroy() {

}


int preprocess_send_metric(metric_t *metric) {
    glb_ipc_send(conf.ipc, metric->hostid % CONFIG_GLB_PREPROCESSOR_FORKS, metric, IPC_LOCK_WAIT);
    glb_ipc_flush(conf.ipc);
}

/*******receiver-side functions *******/
int preproc_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count) {
   return glb_ipc_process(conf.ipc, process_num -1 , proc_func, cb_data, max_count );
};


