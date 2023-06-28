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
#include "zbxembed.h"

#define MAX_DEPENDENCY_LEVEL 16

/* c-paste of zbx preprocessing manager data */
typedef struct
{
    zbx_hashset_t			items;	/* item config && history */
	u_int64_t			cfg_revision;	/* the configuration revision */
	u_int64_t			processed_num;	/* processed value counter */
    unsigned char process_type;
    int server_num;
    int  process_num;
    zbx_pp_context_t ctx;
} glb_preproc_worker_conf_t;

static glb_preproc_worker_conf_t conf = {0};

static void process_dependent_metrics(metric_t *metric, int max_dep);

zbx_pp_item_preproc_t *get_prperoc_item(u_int64_t itemid) {
    return zbx_hashset_search(&conf.items, &itemid);
}

static void preprocess_metric_execute_steps(const metric_t *metric, zbx_pp_cache_t	*cache, int dep_level) {

	zbx_variant_t	value_out;
	int			i, steps_num, results_num, ret;
    zbx_pp_item_preproc_t *item;

	DEBUG_ITEM(metric->itemid,"Metric entered to preprocessing with type %d, type %s value %s", 
					zbx_variant_type_desc(&metric->value), zbx_variant_value_desc(&metric->value));
	
    if (ZBX_VARIANT_NONE == metric->value.type || NULL == ( item = get_prperoc_item(metric->itemid))) {
        DEBUG_ITEM(metric->itemid, "No preprocessing conf found for item, sending to processing");
        processing_send_metric(metric);
        return;
    }

	DEBUG_ITEM(metric->itemid, "Runing preprocessing for item %d steps", steps_num);

	pp_execute(&conf.ctx, item, cache, (zbx_variant_t*)&metric->value, metric->ts, &value_out, NULL, NULL);

    DEBUG_ITEM(metric->itemid, "Result of preprocessing of in: '%s', result is '%s'",
        zbx_variant_value_desc(&metric->value),  zbx_variant_value_desc(&value_out));
    
    metric_t new_metric = {.hostid = metric->hostid, .itemid = metric->itemid, .ts = metric->ts, .value = value_out };
    processing_send_metric(&new_metric);

    process_dependent_metrics(&new_metric, dep_level);
 	zbx_variant_clear(&value_out);
}


//note this should be preprocessed via normal preprocessing
//or at least, using "local" preprocessing only for "local" data
int preprocess_metric(const metric_t *metric) {
	preprocess_metric_execute_steps(metric, NULL, MAX_DEPENDENCY_LEVEL);	
}

static void process_dependent_metrics(metric_t * metric, int level) {
    zbx_pp_item_preproc_t *item_conf;
    zbx_pp_cache_t*		cache;
	int i;
	
	if (level < 0) //recursion protection
		return;
	
    if (NULL == (item_conf = zbx_hashset_search(&conf.items, &metric->itemid))) 
        return;
    
    cache = pp_cache_create(item_conf, &metric->value);
	
    for (i = 0; i < item_conf->dep_itemids_num; i++ ) {
		metric->itemid = item_conf->dep_itemids[i];
        preprocess_metric_execute_steps(metric, cache, level);
	}

    pp_cache_release(cache);
}

IPC_PROCESS_CB(metrics_proc_cb) {
    preprocess_metric_execute_steps((metric_t*)ipc_data, NULL, MAX_DEPENDENCY_LEVEL);
}

void preprocessing_sync_conf() {
  DCconfig_get_preprocessable_items(&conf.items, &conf.cfg_revision, conf.process_num );
}

void preprocessing_worker_init(zbx_thread_args_t *args) {
  
 	conf.process_type = args->info.process_type;
	conf.server_num = args->info.server_num;
	conf.process_num = args->info.process_num;
    
    bzero(&conf.ctx, sizeof(conf.ctx));
  
 	zbx_hashset_create_ext(&conf.items, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
			(zbx_clean_func_t)pp_item_clear,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);
  
    preprocessing_sync_conf();
	
#ifdef HAVE_LIBXML2
	xmlInitParser();
#endif

#ifdef HAVE_LIBCURL
	curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

#ifdef HAVE_NETSNMP
	preproc_init_snmp();
#endif

}

#define BATCH_PROCESS_METRICS 256

ZBX_THREAD_ENTRY(glb_preprocessing_worker_thread, args) {
  int i = 0, total_proc =0, proctitle_update=0;

  zbx_setproctitle("glb_preproc_worker");
  LOG_INF("glb_preproc_worker started");
  
  preprocessing_worker_init((zbx_thread_args_t *)args);

  //TODO: event-based loop to avoid all the clutter 
  while (1) {

    if (0 == (i = preproc_receive_metrics(conf.process_num, metrics_proc_cb, NULL, BATCH_PROCESS_METRICS))) {
        usleep(10011);
    } else {
      total_proc +=i;
    }

    if (time(NULL) - 5 > proctitle_update) {
      zbx_setproctitle("glb_preproc_worker: processed %d/sec", total_proc/5);
      total_proc = 0;
      proctitle_update = time(NULL);
      preprocessing_sync_conf();
	  preprocessing_force_flush(); //if there are redirected items, flush them
    }    
  }
  
  if (conf.ctx.es_initialized)
    zbx_es_destroy(&conf.ctx.es_engine);
}