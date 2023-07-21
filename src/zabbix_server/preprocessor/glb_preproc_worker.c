/*
** Glaber
** Copyright (C) 2001-2022 Glaber SIA
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
#include "zbxthreads.h"
#include "log.h"
#include "metric.h"
#include "glb_preproc_ipc.h"
#include "zbxpreproc.h"
#include "zbxcacheconfig.h"
#include "../../libs/zbxpreproc/pp_history.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../../libs/zbxpreproc/pp_execute.h"
#include "../../libs/zbxpreproc/preproc_snmp.h"
#include "../glb_poller/poller_async_io.h"
#include "zbxembed.h"
#include "zbxlld.h"

#define MAX_DEPENDENCY_LEVEL 16
#define PREPROC_CONFIG_SYNC_INTERVAL 5
#define PROCTITLE_UPDATE_INTERVAL 5 
#define FLUSH_INTERVAL  1


/* c-paste of zbx preprocessing manager data */
typedef struct
{
    zbx_hashset_t			items;	/* item config && history */
	u_int64_t			cfg_revision;	/* the configuration revision */
	u_int64_t			processed_num;	/* processed value counter */
    unsigned char process_type;
    int server_num;
    int  process_num;
    int total_proc; 
    zbx_pp_context_t ctx;
    poller_event_t *new_config_check;
    poller_event_t *proctitle_update;
    poller_event_t *periodic_flush;
    poller_event_t *processing;
} glb_preproc_worker_conf_t;

static glb_preproc_worker_conf_t conf = {0};

static void process_dependent_metrics(metric_t *metric, zbx_pp_item_preproc_t *preproc, int max_dep);

zbx_pp_item_t *get_prperoc_item(u_int64_t itemid) {
    return zbx_hashset_search(&conf.items, &itemid);
}

void send_preprocessed_metric(const metric_t *metric, const zbx_pp_item_t *preproc_conf) {
    
    if ( NULL != preproc_conf && ( preproc_conf->preproc->flags & ZBX_FLAG_DISCOVERY_RULE ) ) {
        
        if (ZBX_VARIANT_STR != metric->value.type || NULL == metric->value.data.str )
            return;

        DEBUG_ITEM(metric->itemid, "Items is discovery, sending to LLD manager");
        zbx_lld_process_value(metric->itemid, metric->hostid, metric->value.data.str, &metric->ts, 0, 0, 0, NULL);
        
        return;
    }
    
    processing_send_metric(metric);
}

static void preprocess_metric_execute_steps(metric_t *metric, zbx_pp_cache_t *cache, int dep_level) {

	zbx_variant_t	value_out;
	int			i, steps_num, results_num, ret;
    
    zbx_pp_item_t *preproc_conf;

	DEBUG_ITEM(metric->itemid,"Metric entered to preprocessing with type %d, type %s value %s", metric->value.type,
					zbx_variant_type_desc(&metric->value), zbx_variant_value_desc(&metric->value));
	
    if (ZBX_VARIANT_NONE == metric->value.type || NULL == ( preproc_conf = get_prperoc_item(metric->itemid))) {
        DEBUG_ITEM(metric->itemid, "No preprocessing conf found for item, sending to processing");
        send_preprocessed_metric(metric, NULL);
        return;
    }
	
	zbx_variant_set_none(&value_out);

	DEBUG_ITEM(metric->itemid, "Runing preprocessing for item, %d steps",   preproc_conf->preproc->steps_num);

	pp_execute(&conf.ctx, preproc_conf->preproc, cache, metric, &value_out, NULL, NULL);

    DEBUG_ITEM(metric->itemid, "Result of preprocessing of in: '%s', result is '%s'",
        zbx_variant_type_desc(&value_out), zbx_variant_value_desc(&value_out));
    
    metric_t new_metric = {.hostid = metric->hostid, .itemid = metric->itemid, .ts = metric->ts, .value = value_out };
    
    send_preprocessed_metric(&new_metric, preproc_conf);
    process_dependent_metrics(&new_metric, preproc_conf->preproc, dep_level - 1);
 	zbx_variant_clear(&value_out);
}

static void process_dependent_metrics(metric_t * metric, zbx_pp_item_preproc_t *preproc_conf, int level) {
    metric_t dep_metric = *metric;
    zbx_pp_cache_t*		cache;
	int i;
	
	if (level < 0) //recursion protection
		return;
	
    if (NULL == preproc_conf) 
        return;
    
    DEBUG_ITEM(metric->itemid, "Processing dependat metrics");

    

    cache = pp_cache_create(preproc_conf, &metric->value);
	
    for (i = 0; i < preproc_conf->dep_itemids_num; i++ ) {
        DEBUG_ITEM(metric->itemid, "Processing dependat metric %ld", preproc_conf->dep_itemids[i]);
		dep_metric.itemid = preproc_conf->dep_itemids[i];
        preprocess_metric_execute_steps(&dep_metric, cache, level);
        DEBUG_ITEM(metric->itemid, "Finished processing dependat metric %ld", preproc_conf->dep_itemids[i]);
	}

    pp_cache_release(cache);
}

IPC_PROCESS_CB(metrics_proc_cb) {
    preprocess_metric_execute_steps((metric_t*)ipc_data, NULL, MAX_DEPENDENCY_LEVEL);
}

void preprocessing_sync_conf(poller_item_t *poller_item, void *data) {
  glb_preproc_worker_conf_t *conf = data;
  DCconfig_get_preprocessable_items(&conf->items, &conf->cfg_revision, conf->process_num );
}

void proctitle_update(poller_item_t *poller_item, void *data) {
    glb_preproc_worker_conf_t *conf = data;
 
    zbx_setproctitle("glb_preproc_worker #%d: processed %d/sec", conf->process_num,
                                         conf->total_proc/PROCTITLE_UPDATE_INTERVAL);
    conf->total_proc = 0;
}

#define BATCH_PROCESS_METRICS 32768

void process_incoming_metrics(poller_item_t *poller_item, void *data) {
    glb_preproc_worker_conf_t *conf = data;
    int next_run = 0, i;
    
    if (0 == (i = preproc_receive_metrics(conf->process_num, metrics_proc_cb, NULL, BATCH_PROCESS_METRICS))) 
        next_run = 1; //sleeping 1msec if no items
    
    conf->total_proc += i;
    poller_run_timer_event(conf->processing, next_run);

}

void ipc_flush(poller_item_t *poller_item, void *data) {
    preprocessing_flush();
}

void preprocessing_worker_init(zbx_thread_args_t *args, glb_preproc_worker_conf_t *conf) {
  
 	conf->process_type = args->info.process_type;
	conf->server_num = args->info.server_num;
	conf->process_num = args->info.process_num;
    
    bzero(&conf->ctx, sizeof(conf->ctx));
  
 	zbx_hashset_create_ext(&conf->items, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
			(zbx_clean_func_t)pp_item_clear,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);
  
    preprocessing_sync_conf(NULL, conf);
	
#ifdef HAVE_LIBXML2
	xmlInitParser();
#endif

#ifdef HAVE_LIBCURL
	curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

#ifdef HAVE_NETSNMP
	preproc_init_snmp();
#endif

  poller_async_loop_init();
    
  conf->new_config_check = poller_create_event(NULL, preprocessing_sync_conf, 0, conf, 1);
  poller_run_timer_event(conf->new_config_check, PREPROC_CONFIG_SYNC_INTERVAL * 1000 );

  conf->proctitle_update = poller_create_event(NULL, proctitle_update , 0, conf, 1);
  poller_run_timer_event(conf->proctitle_update, PROCTITLE_UPDATE_INTERVAL * 1000 );

  conf->periodic_flush = poller_create_event(NULL, ipc_flush , 0, conf, 1);
  poller_run_timer_event(conf->periodic_flush, FLUSH_INTERVAL * 1000 );

  conf->processing = poller_create_event(NULL, process_incoming_metrics , 0, conf, 0);
  poller_run_timer_event(conf->processing, 1 );

}

ZBX_THREAD_ENTRY(glb_preprocessing_worker_thread, args) {

  preprocessing_worker_init((zbx_thread_args_t *)args, &conf);
  
  poller_async_loop_run();

  if (conf.ctx.es_initialized)
    zbx_es_destroy(&conf.ctx.es_engine);
}