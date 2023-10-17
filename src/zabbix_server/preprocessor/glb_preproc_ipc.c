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

#include "zbxcommon.h"
#include "zbxalgo.h"
#include "../../libs/zbxipcservice/glb_serial_buffer.h"
#include "../../libs/zbxipcservice/glb_ipc2.h"
#include "../../libs/glb_state/glb_state_items.h"
#include "zbx_item_constants.h"
#include "log.h"
#include "zbxshmem.h"
#include "metric.h"
#include "zbxlld.h"
#include "../glb_poller/internal.h"
#include "glb_preproc.h"


u_int64_t DC_config_get_hostid_by_itemid(u_int64_t itemid);
extern int CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

extern size_t  CONFIG_IPC_BUFFER_SIZE;
extern int CONFIG_PREPROC_IPC_METRICS_PER_PREPROCESSOR;  //128 * 1024
extern int CONFIG_PROC_IPC_METRICS_PER_SYNCER; //128 * 1024

static  zbx_shmem_info_t	*preproc_ipc_mem;
static  zbx_shmem_info_t	*proc_ipc_mem;

ZBX_SHMEM_FUNC_IMPL(_preprocipc, preproc_ipc_mem);
ZBX_SHMEM_FUNC_IMPL(_procipc, proc_ipc_mem);

typedef struct  {
    mem_funcs_t preproc_memf;
    mem_funcs_t proc_memf;
    mem_funcs_t fwd_preproc_memf;

    ipc2_conf_t *preproc_ipc; //ipc poller->preprocessing
    ipc2_conf_t *fwd_preproc_ipc; //preproc->fwd preproc, need separate for priority
    ipc2_conf_t *process_ipc; //ipc preprocessing->hist_syncer

    serial_buffer_t **buff_preproc;
    serial_buffer_t **buff_fwd_preproc;
    serial_buffer_t **buff_proc;

} metrics_ipc_conf_t;

static metrics_ipc_conf_t *conf = NULL;

INTERNAL_METRIC_CALLBACK(preprocessing_stat_cb) {
    size_t alloc = 0, offset = 0;
    
     zbx_snprintf_alloc(result, &alloc, &offset, 
             "{\"queue_size\":\"%d\", \"sent\":\"%d\","
             "\"mem_total\":\"%ld\", \"mem_used\":\"%ld\",\"mem_free_pcnt\":\"%0.2f\"}",
             ipc2_get_queue_size(conf->preproc_ipc), ipc2_get_sent_items(conf->preproc_ipc),
             preproc_ipc_mem->total_size,  preproc_ipc_mem->used_size,  
             ((double)preproc_ipc_mem->free_size * 100.0)/((double)preproc_ipc_mem->total_size));

    return SUCCEED;
}

INTERNAL_METRIC_CALLBACK(processing_stat_cb) {
    size_t alloc = 0, offset = 0;
    
     zbx_snprintf_alloc(result, &alloc, &offset, 
         "{\"queue_size\":\"%d\", \"sent\":\"%d\","
         "\"mem_total\":\"%ld\", \"mem_used\":\"%ld\",\"mem_free_pcnt\":\"%0.2f\"}",
        ipc2_get_queue_size(conf->preproc_ipc), ipc2_get_sent_items(conf->preproc_ipc),
        proc_ipc_mem->total_size,  proc_ipc_mem->used_size,  ((double)proc_ipc_mem->free_size * 100.0)/((double)proc_ipc_mem->total_size));
        
    return SUCCEED;
}

#define METRICS_IPC_DEFAULT_BUFFER_SIZE 256

static serial_buffer_t ** init_buffers(int count) {
    int i;
    
    serial_buffer_t **sbuffs = zbx_malloc(NULL, sizeof(serial_buffer_t *) * count);
    
    for (i = 0; i < count; i++) {
        sbuffs[i] =  serial_buffer_init(METRICS_IPC_DEFAULT_BUFFER_SIZE);
    }

    return sbuffs;
}

int isMetricDynamicSize(const metric_t *metric) {
    if (VARIANT_VALUE_STR == metric->value.type || 
        VARIANT_VALUE_ERROR == metric->value.type && 
        NULL != metric->value.data.str)  {
        return SUCCEED;
    }
    return FAIL;
}

static void buffer_add_metric(serial_buffer_t *sbuff, const metric_t * metric) {
    void *str_offset = NULL;

    if ( SUCCEED == isMetricDynamicSize(metric) ) {
        str_offset = serial_buffer_add_data(sbuff, metric->value.data.str, strlen(metric->value.data.str) + 1);
    }
    
    metric_t *m = serial_buffer_add_item(sbuff, metric, sizeof(metric_t));
    
    if (NULL != str_offset) 
        m->value.data.str = str_offset;
 }

#define METRICS_IPC_FLUSH_PERIOD 1
static void buffers_flush_ext(ipc2_conf_t *ipc, serial_buffer_t **sbuff, int flush_period, int buffer_limit, int priority) {
    int i;
    int flush_time = time(NULL) - flush_period;

    for (i = 0; i < ipc2_get_consumers(ipc); i++) {
 
         if (serial_buffer_get_time_created(sbuff[i]) < flush_time || 
            (buffer_limit > 0 && buffer_limit <= serial_buffer_get_items_count(sbuff[i]) ) ) {  
            
            ipc2_send_chunk(ipc, i, serial_buffer_get_items_count(sbuff[i]), serial_buffer_get_buffer(sbuff[i]), 
                serial_buffer_get_used(sbuff[i]), priority); 

            serial_buffer_clean(sbuff[i]);
        }
    }
}

static void buffers_flush(ipc2_conf_t *ipc, serial_buffer_t **sbuff, int priority) {
    buffers_flush_ext(ipc, sbuff, METRICS_IPC_FLUSH_PERIOD, 0, priority);
}

static void buffers_force_flush(ipc2_conf_t *ipc, serial_buffer_t **sbuff) {
    buffers_flush_ext(ipc, sbuff, 0, 0, ALLOC_PRIORITY_NORMAL );
}

static void processing_buffers_flush(ipc2_conf_t *ipc, serial_buffer_t **sbuff) {
    buffers_flush_ext(ipc, sbuff, 2, 1000, ALLOC_PRIORITY_NORMAL );
}

int metrics_ipc_init() {
    char *error = NULL;
    
    if (SUCCEED != zbx_shmem_create(&preproc_ipc_mem, CONFIG_IPC_BUFFER_SIZE, "Preproc metrics IPC buffer size", "IPCBufferSize", 1, &error)) {
        LOG_WRN("Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    if (SUCCEED != zbx_shmem_create(&proc_ipc_mem, CONFIG_IPC_BUFFER_SIZE, "Processing IPC buffer size", "IPCBufferSize", 1, &error)) {
        LOG_WRN("Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    conf = _preprocipc_shmem_malloc_func(NULL, sizeof(metrics_ipc_conf_t));
   
    conf->preproc_memf.free_func = _preprocipc_shmem_free_func;
    conf->preproc_memf.malloc_func = _preprocipc_shmem_malloc_func;
    conf->preproc_memf.realloc_func = _preprocipc_shmem_realloc_func;
    
    conf->proc_memf.free_func = _procipc_shmem_free_func;
    conf->proc_memf.malloc_func = _procipc_shmem_malloc_func;
    conf->proc_memf.realloc_func = _procipc_shmem_realloc_func;

    if (NULL == (conf->preproc_ipc = ipc2_init(CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR], &conf->preproc_memf, "->preproc", preproc_ipc_mem )))
        return FAIL;
    
    if (NULL == (conf->process_ipc = ipc2_init(CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER], &conf->proc_memf, "->processing", proc_ipc_mem )))
        return FAIL;

    conf->buff_preproc = init_buffers(CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR]);
    conf->buff_proc = init_buffers(CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER]);

    glb_register_internal_metric_handler("preprocessing",   preprocessing_stat_cb);
    glb_register_internal_metric_handler("processing",      processing_stat_cb);
    
    return SUCCEED;
}

void metrics_ipc_destroy() {
    zbx_shmem_destroy(preproc_ipc_mem);
    zbx_shmem_destroy(proc_ipc_mem);
}

int preprocess_send_metric_hi_priority(const metric_t *metric) {
    int queue_num = metric->hostid % CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR];
    
    glb_state_items_set_poll_result(metric->itemid, metric->ts.sec, ITEM_STATE_NORMAL);
    DEBUG_ITEM(metric->itemid, "Sending metric to preprocessing");

    buffer_add_metric(conf->buff_preproc[queue_num], metric);
    buffers_flush(conf->preproc_ipc, conf->buff_preproc, ALLOC_PRIORITY_HIGH);
}

int preprocess_send_metric(const metric_t *metric) {
    int queue_num = metric->hostid % CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR];
    
    glb_state_items_set_poll_result(metric->itemid, metric->ts.sec, ITEM_STATE_NORMAL);
    DEBUG_ITEM(metric->itemid, "Sending metric to preprocessing");
   
    buffer_add_metric(conf->buff_preproc[queue_num], metric);
    buffers_flush(conf->preproc_ipc, conf->buff_preproc, ALLOC_PRIORITY_NORMAL);
}

int processing_send_metric(const metric_t *metric) {
    int queue_num = metric->hostid % CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTSYNCER];
    DEBUG_ITEM(metric->itemid, "Sending metric to processing");

    buffer_add_metric(conf->buff_proc[queue_num], metric);
    processing_buffers_flush(conf->process_ipc, conf->buff_proc);
    
}

static void preproc_metric_parse_cb(void *buffer, void *item, void *ctx_func, int i) {
    metric_t *metric = item;
    metric_preprocess_func_t metric_preproc_func = ctx_func;

    if (SUCCEED == isMetricDynamicSize(metric) ) {
          metric->value.data.str = serial_buffer_get_real_addr(buffer, (u_int64_t)metric->value.data.str);
    }
    
    metric_preproc_func(metric);
}

static void metrics_chunk_process_cb(void *buffer, void *ctx_data) {
    serial_buffer_proc_func_t proc_func = ctx_data;
    serial_buffer_process(buffer, preproc_metric_parse_cb, proc_func);
}
/*******receiver-side functions *******/
int preproc_receive_metrics(int process_num, metric_preprocess_func_t proc_func, void *cb_data, int max_count) {

    int i = ipc2_receive_one_chunk(conf->preproc_ipc, NULL, process_num - 1, metrics_chunk_process_cb, proc_func);
    return i;
};
typedef struct {
    void *ctx_data;
    metric_process_func_t proc_func;
} metrics_proc_data_t;


static void proc_metric_parse_cb(void *buffer, void *item, void *ctx_func, int i) {
    metric_t *metric = item;
    metrics_proc_data_t *proc_data = ctx_func;

    if (SUCCEED == isMetricDynamicSize(metric) ) 
        metric->value.data.str = serial_buffer_get_real_addr(buffer,  (u_int64_t)metric->value.data.str);
    
    proc_data->proc_func(metric, i, proc_data->ctx_data);
}

static void proc_metrics_chunk_process_cb(void *buffer, void *ctx_data) {
    serial_buffer_proc_func_t proc_func = ctx_data;

    serial_buffer_process(buffer, proc_metric_parse_cb, proc_func);
}

int process_receive_metrics(int process_num, metric_process_func_t proc_func, void *cb_data, int max_count) {
    metrics_proc_data_t proc_data = {.ctx_data = cb_data, .proc_func = proc_func};

    int i = ipc2_receive_one_chunk(conf->process_ipc, NULL, process_num - 1, proc_metrics_chunk_process_cb, &proc_data);

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

    DEBUG_ITEM(itemid, "Sending item to preprocessing,  ar type is %d", ar->type);
    
    if (ar->type & AR_META) {
        glb_state_items_set_lastlogsize(itemid, ar->lastlogsize);
    }
    
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

int processing_send_agent_result_from_proxy(u_int64_t hostid, u_int64_t itemid, 
        u_int64_t flags, const zbx_timespec_t *ts, const AGENT_RESULT *ar) {
    metric_t metric={0};
    DEBUG_ITEM(itemid, "Sending item to preocessing, ar type is %d", ar->type);
    
    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;

    if (ar->type & AR_META) {
        glb_state_items_set_lastlogsize(itemid, ar->lastlogsize);
    }
    
    if (ar->type & AR_UINT64) {
        zbx_variant_set_ui64(&metric.value, ar->ui64);
        DEBUG_ITEM(itemid, "Sending to processing UINT64 value %lld", metric.value.data.ui64);
    }

    if (ar->type & AR_STRING) {
        zbx_variant_set_str(&metric.value, ar->str);
        DEBUG_ITEM(itemid, "Sending to processing STR value %s", metric.value.data.str);
    }
        
    if (ar->type & AR_TEXT) {
        zbx_variant_set_str(&metric.value, ar->text);
         DEBUG_ITEM(itemid, "Sending to processing STR value %s", metric.value.data.str);
    }
        
    if (ar->type & AR_DOUBLE) {
        zbx_variant_set_dbl(&metric.value, ar->dbl);
         DEBUG_ITEM(itemid, "Sending to processing DBL value %f", metric.value.data.dbl);
    }
    
    processing_send_metric(&metric);
    glb_state_item_set_lastdata_by_metric(&metric);
}

int processing_send_error_from_proxy(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *error) {
    metric_t metric={0};
    DEBUG_ITEM(itemid, "Sending error to processing");
    if (FAIL == prepare_metric_common(&metric, hostid, itemid, flags, ts)) 
        return FAIL;
    
    if (NULL != error)
        zbx_variant_set_error(&metric.value, (char *)error);
    else 
        zbx_variant_set_error(&metric.value,"");

    processing_send_metric(&metric);
    glb_state_item_set_lastdata_by_metric(&metric);
    return SUCCEED;
}

int processing_force_flush() {
    buffers_force_flush(conf->process_ipc, conf->buff_proc);
}

int preprocessing_force_flush() {
    buffers_force_flush(conf->preproc_ipc, conf->buff_preproc);
}

int preprocessing_flush(int priority) {
    buffers_flush(conf->preproc_ipc, conf->buff_preproc, priority);
}

int processing_flush() {
    buffers_flush(conf->process_ipc, conf->buff_proc, ALLOC_PRIORITY_NORMAL);
}

