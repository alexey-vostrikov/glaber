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
#include "zbxcommon.h"
#include "zbxalgo.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../../libs/glb_state/glb_state_items.h"
#include "zbx_item_constants.h"
#include "log.h"
#include "zbxshmem.h"
#include "metric.h"
#include "zbxlld.h"
#include "../glb_poller/internal.h"


u_int64_t DC_config_get_hostid_by_itemid(u_int64_t itemid);
extern int CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

extern size_t  CONFIG_IPC_BUFFER_SIZE;
extern int CONFIG_PREPROC_IPC_METRICS_PER_PREPROCESSOR;  //128 * 1024
extern int CONFIG_PROC_IPC_METRICS_PER_SYNCER; //128 * 1024

//extern int  CONFIG_GLB_PREPROCESSOR_FORKS;

static  zbx_shmem_info_t	*preproc_ipc_mem;
ZBX_SHMEM_FUNC_IMPL(_preprocipc, preproc_ipc_mem);

typedef struct  {
    mem_funcs_t memf;
    ipc_conf_t *preproc_ipc; //ipc poller->preprocessing
    ipc_conf_t *process_ipc; //ipc preprocessing->hist_syncer
} preproc_ipc_conf_t;

static preproc_ipc_conf_t *conf = NULL;

//static 
char *preproc_ipc_allocate_str(const char *str) {
    size_t len = strlen(str) + 1;

    char *new_str = conf->memf.malloc_func(NULL, len);
    if (NULL == new_str ) {
        LOG_INF("Mememory free is %ld", preproc_ipc_mem->free_size);
        HALT_HERE("Out of memory situation!");
        return NULL;
    }
    memcpy(new_str, str, len);
    
    return new_str;
}

static void  preproc_ipc_free_buffer(void *buff) {
    conf->memf.free_func(buff);
}

IPC_CREATE_CB(preproc_ipc_metric_create_cb) {

    metric_t *local_metric = local_data, *ipc_metric = ipc_data;
    
    //LOG_INF("Got dynamic metric id %ld type %ld, str ptr is %ld", local_metric->itemid, local_metric->value.type, local_metric->value.data.str );
	
    DEBUG_ITEM(local_metric->itemid,"Metric sent to preprocessing with type %d, type %s value %s", local_metric->value.type,
					zbx_variant_type_desc(&local_metric->value), zbx_variant_value_desc(&local_metric->value));

    memcpy(ipc_metric, local_metric, sizeof(metric_t));

    if ( SUCCEED == variant_is_dynamic_length(&local_metric->value)) {
        
      //  LOG_INF("Got dynamic metric id %ld type %ld, str ptr is %ld", local_metric->itemid, local_metric->value.type, local_metric->value.data.str );
//        if (NULL !=local_metric->value.data.str )
  //          LOG_INF("Metric value is %s", local_metric->value.data.str);

        ipc_metric->value.data.str = preproc_ipc_allocate_str(local_metric->value.data.str);
    }
    
	DEBUG_ITEM(ipc_metric->itemid,"Metric sent to preprocessing with type %d, type %s value %s", ipc_metric->value.type,
					zbx_variant_type_desc(&ipc_metric->value), zbx_variant_value_desc(&ipc_metric->value));
}

IPC_FREE_CB(preproc_ipc_metric_free_cb) {
    metric_t *ipc_metric = ipc_data;
    
    if (SUCCEED == variant_is_dynamic_length(&ipc_metric->value)) {
      //  LOG_INF("Free mem before free %ld", preproc_ipc_mem->free_size);
        preproc_ipc_free_buffer(ipc_metric->value.data.str);
        
    }

    ipc_metric->value.type = VARIANT_VALUE_NONE;
}



INTERNAL_METRIC_CALLBACK(preprocessing_stat_cb) {
    LOG_INF("Called preproc statistics");
    size_t alloc = 0, offset = 0;
    zbx_snprintf_alloc(result, &alloc, &offset, "{\"queue_size\":\"%ld\", \"free\":\"%0.2f\", \"sent\":\"%ld\"}", 
                glb_ipc_get_queue(conf->preproc_ipc), glb_ipc_get_free_pcnt(conf->preproc_ipc), glb_ipc_get_sent(conf->preproc_ipc) );

    return SUCCEED;
}

INTERNAL_METRIC_CALLBACK(processing_stat_cb) {
    size_t alloc = 0, offset = 0;
    zbx_snprintf_alloc(result, &alloc, &offset, "{\"queue_size\":\"%ld\", \"free\":\"%0.2f\", \"sent\":\"%ld\"}", 
                glb_ipc_get_queue(conf->process_ipc), glb_ipc_get_free_pcnt(conf->process_ipc), glb_ipc_get_sent(conf->process_ipc) );
    
    return SUCCEED;
}

int preproc_ipc_init() {
    char *error = NULL;
    LOG_INF("IPC mem size is %ld", CONFIG_IPC_BUFFER_SIZE);
    if (SUCCEED != zbx_shmem_create(&preproc_ipc_mem, CONFIG_IPC_BUFFER_SIZE, "Metrics IPC buffer size", "MetricsBufferSize ", 0, &error)) {
        LOG_WRN("Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    conf = _preprocipc_shmem_malloc_func(NULL, sizeof(conf));

    //TODO: to reduce congestion on malloc on varaible data, split IPC mem into two separte segments
    conf->memf.free_func = _preprocipc_shmem_free_func;
    conf->memf.malloc_func = _preprocipc_shmem_malloc_func;
    conf->memf.realloc_func = _preprocipc_shmem_realloc_func;

    conf->preproc_ipc = glb_ipc_init_ext(CONFIG_PREPROC_IPC_METRICS_PER_PREPROCESSOR * CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR], sizeof(metric_t), 
        CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR] , &conf->memf, preproc_ipc_metric_create_cb,
         preproc_ipc_metric_free_cb, IPC_HIGH_VOLUME, "poll->preproc");
    
    conf->process_ipc = glb_ipc_init_ext(CONFIG_PROC_IPC_METRICS_PER_SYNCER * CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER], sizeof(metric_t), 
        CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER] , &conf->memf, preproc_ipc_metric_create_cb,
         preproc_ipc_metric_free_cb, IPC_HIGH_VOLUME, "preproc->proc");

    glb_register_internal_metric_handler("preprocessing",   preprocessing_stat_cb);
    glb_register_internal_metric_handler("processing",      processing_stat_cb);

    return SUCCEED;
}

void preproc_ipc_destroy() {
    zbx_shmem_destroy(preproc_ipc_mem);
}

int preprocess_send_metric_ext(const metric_t *metric, int send_wait_mode) {
    glb_state_items_set_poll_result(metric->itemid, metric->ts.sec, ITEM_STATE_NORMAL);
    glb_ipc_send(conf->preproc_ipc, metric->hostid % CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR], (void *)metric, send_wait_mode);
    glb_ipc_flush(conf->preproc_ipc);
}

int preprocess_send_metric(const metric_t *metric) {
    int i;
//
//for (i = 0; i < 10; i++) {
    preprocess_send_metric_ext(metric, IPC_LOCK_TRY_1MS);
//    }
}

void set_item_state(const metric_t *metric) {
     switch (metric->value.type) {
         case ZBX_VARIANT_ERR:
             if (NULL != metric->value.data.err)
                 glb_state_item_set_error(metric->itemid, metric->value.data.err);
             else 
                 glb_state_item_set_error(metric->itemid, "");
             break;
        default:
            glb_state_items_set_poll_result(metric->itemid,metric->ts.sec, ITEM_STATE_NORMAL);
    }
}

int processing_send_metric(const metric_t *metric) {
    set_item_state(metric);
    glb_ipc_send(conf->process_ipc, metric->hostid % CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER], (void *)metric, IPC_LOCK_TRY_ONLY);
    glb_ipc_flush(conf->process_ipc);
}

/*******receiver-side functions *******/
int preproc_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count) {
    int i = glb_ipc_process(conf->preproc_ipc, process_num -1 , proc_func, cb_data, max_count );
    RUN_ONCE_IN_WITH_RET(10, i);
    glb_ipc_dump_reciever_queues(conf->process_ipc, "WAIT PREPROC STAT: Preproc rcv queue", 0);
    LOG_INF("Free mem %ld", preproc_ipc_mem->free_size);
    return i;
};

int process_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count) {
    int i = glb_ipc_process(conf->process_ipc, process_num -1 , proc_func, cb_data, max_count );
    RUN_ONCE_IN_WITH_RET(10, i);
    glb_ipc_dump_reciever_queues(conf->process_ipc, "%p WAIT QUEUE STAT: Processing send queue", process_num -1 );
    LOG_INF("IPC addr is %p", conf->preproc_ipc);
    return i;
};

static int prepare_metric_common(metric_t *metric, u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts) {
    if (NULL == ts) 
      zbx_timespec(&metric->ts);
    else 
        metric->ts = *ts;

    if (0 == hostid && 
        0 == (hostid = DC_config_get_hostid_by_itemid(itemid)))
        return FAIL;
    
    metric->hostid = hostid;
    metric->itemid = itemid;
    metric->flags = flags;
    return SUCCEED;
}

//required to pass metric to processing and avoid false nodata 
int preprocess_empty(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts) {
    metric_t metric={0};

    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    zbx_variant_set_none(&metric.value);
    preprocess_send_metric(&metric);
    return SUCCEED;
}

int preprocess_error(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *error) {
    metric_t metric={0};

    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    if (NULL != error)
        zbx_variant_set_error(&metric.value, (char *)error);
    else 
        zbx_variant_set_error(&metric.value,"");

    preprocess_send_metric(&metric);
    return SUCCEED;
}

int preprocess_str(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *str) {
    metric_t metric={0};

    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    zbx_variant_set_str(&metric.value, (char *)str);
    
    preprocess_send_metric(&metric);
    return SUCCEED;
}

int preprocess_uint64(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, u_int64_t int_val) {
    metric_t metric={0};
    
    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    zbx_variant_set_ui64(&metric.value, int_val);
    preprocess_send_metric(&metric);
    return SUCCEED;
}

int preprocess_dbl(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, double dbl_val) {
    metric_t metric={0};

    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    zbx_variant_set_dbl(&metric.value, dbl_val);
    preprocess_send_metric(&metric);
    return SUCCEED;
}

int preprocess_agent_result(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const AGENT_RESULT *ar, int desired_type) {
    metric_t metric={0};

 //   LOG_INF("Setting itmeid %ld agent result %d", itemid, ar->type);
    DEBUG_ITEM(itemid, "Sending item to preprocessing,  ar type is %d", ar->type);
    if (ar->type & AR_UINT64) {
        if (ITEM_VALUE_TYPE_FLOAT == desired_type) {
            double dbl_val = (double)ar->ui64;
            DEBUG_ITEM(itemid,"Will preprocess uint64 value %ld as double value %f", ar->ui64, dbl_val);
            preprocess_dbl(hostid, itemid, flags, ts, dbl_val);
        } else 
            return preprocess_uint64(hostid, itemid, flags, ts, ar->ui64);
    }
    
    if (ar->type & AR_STRING)
        return preprocess_str(hostid, itemid, flags, ts, ar->str);

    if (ar->type & AR_TEXT)
        return preprocess_str(hostid, itemid, flags, ts, ar->text);
    
    if (ar->type & AR_DOUBLE)
        return preprocess_dbl(hostid, itemid, flags, ts, ar->dbl);
    
    return FAIL;
}

int processing_send_agent_result(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const AGENT_RESULT *ar) {
    metric_t metric={0};
    
    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;

 //   LOG_INF("Setting itmeid %ld agent result %d", itemid, ar->type);
    if (ar->type | AR_UINT64)
        zbx_variant_set_ui64(&metric.value, ar->ui64);

    if (ar->type | AR_STRING)
        zbx_variant_set_str(&metric.value, ar->str);
        
    if (ar->type | AR_TEXT)
        zbx_variant_set_str(&metric.value, ar->text);
        
    if (ar->type | AR_DOUBLE)
        zbx_variant_set_dbl(&metric.value, ar->dbl);
    
    processing_send_metric(&metric);
}

int processing_send_error(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *error) {
    metric_t metric={0};
 
    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    
    if (NULL != error)
        zbx_variant_set_error(&metric.value, (char *)error);
    else 
        zbx_variant_set_error(&metric.value,"");

    processing_send_metric(&metric);
    
    return SUCCEED;
}

int processing_force_flush() {
    glb_ipc_force_flush(conf->process_ipc);
}
int preprocessing_force_flush() {
    glb_ipc_force_flush(conf->preproc_ipc);
}

