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

#include "common.h"
#include "zbxalgo.h"
#include "../zbxipcservice/glb_ipc.h"
#include "log.h"
#include "memalloc.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static  zbx_mem_info_t	*apm_ipc;
ZBX_MEM_FUNC_IMPL(_apmipc, apm_ipc);
mem_funcs_t apm_memf = {.free_func = _apmipc_mem_free_func, .malloc_func = _apmipc_mem_malloc_func, .realloc_func = _apmipc_mem_realloc_func };

typedef struct {
    zbx_hashset_t metrics; /* clients will use it */    
    zbx_hashset_t collected_metrics; /* server will use it */
    int flushtime;
    ipc_conf_t *ipc;
} apm_conf_t;

static  apm_conf_t conf={0};

typedef struct {
    void *ptr;
    char *name;
    char *labels;
    unsigned char type;
    int lastchange; 
} apm_metric_t;

enum {
    METRIC_GAUGE = 0,
    METRIC_COUNTER,
    METRIC_DELETE /*used to notify that the metric is deleted */
} metric_type_t;


static void apm_track_metric(void *metric_ptr, unsigned char type, const char *name, const char* labels) {
    apm_metric_t *metric, local_metric;

    if (NULL == name)
        return;
    if (NULL != (metric = zbx_hashset_search(&conf.metrics, &metric_ptr))) 
        return;
    
    local_metric.name = zbx_strdup(NULL, name);
    local_metric.type = METRIC_COUNTER;
    local_metric.ptr = metric_ptr;
    local_metric.type = type;
    
    if (NULL == labels)
        local_metric.labels = NULL;
    else 
        local_metric.labels = zbx_strdup(NULL,labels);

    zbx_hashset_insert(&conf.metrics, &local_metric, sizeof(apm_metric_t));
//    LOG_INF("Finished");
 
}


void apm_track_counter(u_int64_t *counter, const char *name, const char* labels) {
//   LOG_INF("Tracking counter name %s", name);
    apm_track_metric(counter, METRIC_COUNTER, name, labels);
}

void apm_track_gauge(double *gauge, const char *name, const char* labels) {
    apm_track_metric(gauge, METRIC_GAUGE, name, labels); 
}

void apm_add_int_label(void *metric_ptr, const char *key, int value) {
    char buffer[MAX_BUFFER_LEN];
    apm_metric_t *metric = zbx_hashset_search(&conf.metrics, &metric_ptr);
    
    if (NULL == metric) 
        return;
    
    if (NULL == metric->labels) {
        zbx_snprintf(buffer, MAX_BUFFER_LEN, "%s=\"%d\"", key, value);
        metric->labels = zbx_strdup(NULL, buffer);
        return;
    }
    
    zbx_snprintf(buffer, MAX_BUFFER_LEN, "%s,%s=\"%d\"", metric->labels, key, value);
    metric->labels = zbx_strdup(metric->labels, buffer);
}

void apm_add_str_label(void *metric_ptr, const char *key, const char *value) {
    char buffer[MAX_BUFFER_LEN];
    apm_metric_t *metric = zbx_hashset_search(&conf.metrics, &metric_ptr);
    
    if (NULL == metric) 
        return;
    
    if (NULL == metric->labels) {
        zbx_snprintf(buffer, MAX_BUFFER_LEN, "%s=\"%s\"", key, value);
        metric->labels = zbx_strdup(NULL, buffer);
        return;
    }
    
    zbx_snprintf(buffer, MAX_BUFFER_LEN, "%s,%s=\"%s\"", metric->labels, key, value);
    metric->labels = zbx_strdup(metric->labels, buffer);
}

void apm_untrack(void *metric_ptr) {
    apm_metric_t *metric = zbx_hashset_search(&conf.metrics, &metric_ptr);
    if (NULL == metric) 
        return;
    metric->type = METRIC_DELETE;
    
    glb_ipc_send(conf.ipc, 0, metric, 0); /*notify server to delete metric */
    zbx_free(metric->name);
    zbx_free(metric->labels);
    zbx_hashset_remove_direct(&conf.metrics, metric);
}

void apm_flush() {
    static int lastflush = 0;
    
    apm_metric_t *metric;
    
    if (time(NULL) < lastflush + conf.flushtime) 
        return;
//    LOG_INF("APM: flushing metrics");

    lastflush = time(NULL);
    glb_ipc_send(conf.ipc, 0, &conf.metrics, 0);
//    glb_ipc_dump_sender_queues(conf.ipc, "APM sender");
    glb_ipc_flush(conf.ipc);
}

typedef struct {
    u_int64_t pid;
    char *metrics;  
} apm_ipc_metrics_t;

/* the 'server' part. this api is really simple - it doesn't have http server
 but just a method for generating list of all the metrics into a buffer which has
 to be feed to the server code*/
IPC_CREATE_CB(ipc_metric_create_cb) {
    
    apm_ipc_metrics_t *ipc_metric = ipc_data;
    zbx_hashset_t *metrics = local_data;
    zbx_hashset_iter_t iter;
    apm_metric_t *metric;

    static char *buffer = NULL;
    static size_t alloc = 0;
    size_t offset = 0;

    
    zbx_hashset_iter_reset(metrics, &iter);

    while (NULL != (metric = zbx_hashset_iter_next(&iter))) {
        if (NULL == metric->labels)
            zbx_snprintf_alloc(&buffer, &alloc, &offset, "%s", metric->name);
        else 
            zbx_snprintf_alloc(&buffer, &alloc, &offset, "%s{%s}", metric->name, metric->labels);
        
        switch (metric->type) {
        case METRIC_COUNTER:
            zbx_snprintf_alloc(&buffer, &alloc, &offset, " %ld\n", *(u_int64_t*)metric->ptr);
            break;
        case METRIC_GAUGE:
            zbx_snprintf_alloc(&buffer, &alloc, &offset, " %f\n", *(double*)metric->ptr);
            break;
        default:
            HALT_HERE("Unsupported apm metric type for name %s{%s} : %d, this is programming bug", metric->name, metric->labels, metric->type);
        }
    }
//    LOG_INF("APM: flush: created buffer '%s'", buffer);

    if ( 0 == offset || NULL == (ipc_metric->metrics = memf->malloc_func(NULL, offset)))
        return;
    
    zbx_strlcpy(ipc_metric->metrics, buffer, offset + 1);
    ipc_metric->pid =getpid();

    if (alloc > ZBX_MEBIBYTE) {
        zbx_free(buffer);
        buffer = NULL;
        alloc = 0;
    }

    return;
}

IPC_FREE_CB(ipc_metric_free_cb) {
    apm_ipc_metrics_t *ipc_metrics = ipc_data;

    memf->free_func(ipc_metrics->metrics);
    ipc_metrics->metrics = NULL;

    ipc_metrics->pid = 0;
}

/* maybe it's worth of moving to cfg */
#define CONFIG_APM_IPC_SIZE 2 * ZBX_MEBIBYTE

/* must be called from the parent process */
int apm_init() {
    char *error = NULL;
    
    if (SUCCEED != zbx_mem_create(&apm_ipc, CONFIG_APM_IPC_SIZE, "APM ipc cache size", "APMIPCsize ", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    conf.ipc = glb_ipc_init(1000, sizeof(apm_metric_t), 1 , &apm_memf, ipc_metric_create_cb, ipc_metric_free_cb, IPC_LOW_LATENCY);

    zbx_hashset_create(&conf.metrics, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_hashset_create(&conf.collected_metrics, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    
    conf.flushtime = 5;

    return SUCCEED;

}

void apm_destroy() {
}

IPC_PROCESS_CB(new_metrics_cb) {
    apm_ipc_metrics_t *ipc_metrics = ipc_data, *metrics, local_metrics;
    
//    LOG_INF("APM server: recieved metric from pid %ld, value is '%s'", ipc_metrics->pid, ipc_metrics->metrics);

    if (0 == ipc_metrics->pid || NULL == ipc_metrics->metrics)
        return;

    if (NULL != (metrics= zbx_hashset_search(&conf.collected_metrics, &ipc_metrics->pid))) {
//        LOG_INF("APM: Metric from pind %d updated", ipc_metrics->pid);

        metrics->metrics = zbx_strdup(metrics->metrics, ipc_metrics->metrics);
        return;
    }
//    LOG_INF("APM: Metric from pid %d created", ipc_metrics->pid);
    local_metrics.pid = ipc_metrics->pid;
    local_metrics.metrics = zbx_strdup(NULL, ipc_metrics->metrics);
    zbx_hashset_insert(&conf.collected_metrics, &local_metrics, sizeof(local_metrics));
}

void apm_recieve_new_metrics() {
//    glb_ipc_dump_reciever_queues(conf.ipc, "APM queue 0", 0);
//    glb_ipc_dump_reciever_queues(conf.ipc, "APM queue 1", 1);
    glb_ipc_process(conf.ipc, 0, new_metrics_cb, NULL, 0);

}

const char *apm_server_dump_metrics() {
    apm_ipc_metrics_t *metrics;
    zbx_hashset_iter_t iter;
    
    static char *buffer = NULL;
    static size_t alloc = 0;
    
    size_t offset = 0;
//    buffer[0] = '\0';

 //   LOG_INF("APM: Adding metric 0");
    if (alloc > ZBX_MEBIBYTE) {
        zbx_free(buffer);
        buffer = NULL;
        alloc = 0;
    }

    zbx_hashset_iter_reset(&conf.collected_metrics, &iter);

    while (NULL != (metrics = zbx_hashset_iter_next(&iter))) {
        zbx_snprintf_alloc(&buffer, &alloc, &offset, "%s", metrics->metrics);
    
    }

    return buffer;
}

void apm_add_proc_labels(void *metric) {
	apm_add_int_label(metric, "pid", getpid());
    apm_add_int_label(metric, "procnum", process_num);
    apm_add_str_label(metric, "proctype", get_process_type_string(process_type));
}


static u_int64_t heap_usage = 0;
void apm_add_heap_usage() {
    apm_track_counter(&heap_usage, "process_heap_bytes",  NULL);
    apm_add_proc_labels(&heap_usage);
}


static size_t getCurrentRSS( )
{
    long rss = 0L;
    FILE* fp = NULL;
    if ( (fp = fopen( "/proc/self/statm", "r" )) == NULL )
        return (size_t)0L;      /* Can't open? */
    if ( fscanf( fp, "%*s%ld", &rss ) != 1 )
    {
        fclose( fp );
        return (size_t)0L;      /* Can't read? */
    }
    fclose( fp );
    return (size_t)rss * (size_t)sysconf( _SC_PAGESIZE);
}

void apm_update_heap_usage() {
    heap_usage = getCurrentRSS();
}