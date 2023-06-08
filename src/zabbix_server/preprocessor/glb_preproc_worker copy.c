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
//#include "zbxself.h"
#include "log.h"
#include "metric.h"
#include "glb_preproc_ipc.h"
#include "zbxcacheconfig.h"
#include "preproc_history.h"
#include "preproc_worker.h"
#include "item_preproc.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "zbxembed.h"

zbx_es_t	es_engine;

/* c-paste of zbx preprocessing manager data */
typedef struct
{
	zbx_hashset_t			item_config;	/* item configuration L2 cache */
	zbx_hashset_t			history_cache;	/* item value history cache */
	u_int64_t			cfg_revision;	/* the configuration revision */
	u_int64_t			processed_num;	/* processed value counter */
  unsigned char process_type;
  int server_num;
  int  process_num;
} glb_preproc_worker_conf_t;

static glb_preproc_worker_conf_t conf = {0};

static void process_dependent_metrics(const metric_t *metric);

int  prepare_preproc_task_data(const metric_t *metric, zbx_vector_ptr_t **history_in, zbx_vector_ptr_t *history_out, 
             zbx_preproc_op_t **ops, int *ops_num, zbx_preproc_result_t **results) {

    zbx_preproc_item_t *preproc_item_conf;
    zbx_preproc_history_t *hist;

    if (NULL == (preproc_item_conf = zbx_hashset_search(&conf.item_config, &metric->itemid))) {
		DEBUG_ITEM(metric->itemid, "Couldn't found preproc configuration for the item");
	    return FAIL;
	}
	
	if (0 == preproc_item_conf->preproc_ops_num && 0 == preproc_item_conf->dep_itemids_num) {
		DEBUG_ITEM(metric->itemid, "Metric has no either preproc steps nor dependant items not preprocessing");
	    return FAIL;
	}

    *ops = preproc_item_conf->preproc_ops;
    *ops_num = preproc_item_conf->preproc_ops_num;

    if (NULL != (hist = zbx_hashset_search(&conf.history_cache, &metric->itemid))) 
        *history_in = &hist->history;
    else 
        *history_in = NULL;
      
	zbx_vector_ptr_create(history_out);
	
    *results = (zbx_preproc_result_t *)zbx_calloc(NULL, 0, sizeof(zbx_preproc_result_t) * (size_t)*ops_num);
	
	return SUCCEED;
}

static void save_steps_history(u_int64_t itemid, zbx_vector_ptr_t *out_history) {
    zbx_preproc_history_t		*hist_results;
    
    if (NULL != (hist_results = (zbx_preproc_history_t *)zbx_hashset_search(&conf.history_cache, &itemid)))
    	zbx_vector_ptr_clear_ext(&hist_results->history, (zbx_clean_func_t)zbx_preproc_op_history_free);
	
    if ( 0 == out_history->values_num ) {
        if (NULL != hist_results)  {
			zbx_vector_ptr_destroy(&hist_results->history);
			zbx_hashset_remove_direct(&conf.history_cache, hist_results);
		}
        return;
    }

  	if (NULL == hist_results)	{
		zbx_preproc_history_t	history_local;

		history_local.itemid = itemid;
		hist_results = (zbx_preproc_history_t *)zbx_hashset_insert(&conf.history_cache, &history_local,	sizeof(history_local));
		zbx_vector_ptr_create(&hist_results->history);
	}

	zbx_vector_ptr_append_array(&hist_results->history, out_history->values, out_history->values_num);
}

//this is almost worker_preprocess_value, but instead deserializing / serializing data to/from the socket
//it uses local memory storage.
static void preprocess_metric_execute_steps(const metric_t *metric) {

	zbx_variant_t	value_out;
	int			i, steps_num, results_num, ret;
	char			*errmsg = NULL, *error = NULL;
	
	zbx_preproc_op_t	*steps;
	zbx_vector_ptr_t	*history_in = NULL, history_out;
	zbx_preproc_result_t	*results;

	DEBUG_ITEM(metric->itemid,"Metric entered to preprocessing");
	
    if (FAIL == prepare_preproc_task_data(metric, &history_in, &history_out, &steps, &steps_num, &results) || 
        ZBX_VARIANT_NONE == metric->value.type ) {
        DEBUG_ITEM(metric->itemid, "No preprocessing conf found for item sending to processing");
        processing_send_metric(metric);
        return;
    }

	DEBUG_ITEM(metric->itemid, "Will run preprocessing for item %d steps", steps_num);

	if (FAIL == (ret = worker_item_preproc_execute(metric->itemid, NULL, metric->value.type, 
                        (zbx_variant_t *)&metric->value, &value_out, &metric->ts, steps, steps_num, history_in,
			            &history_out, results, &results_num, &errmsg)) && 0 != results_num)
	{
		int action = results[results_num - 1].action;

		if (ZBX_PREPROC_FAIL_SET_ERROR != action && ZBX_PREPROC_FAIL_FORCE_ERROR != action)
		{
			worker_format_error(&metric->value, results, results_num, errmsg, &error);
			zbx_free(errmsg);
		}
		else
			error = errmsg;
	}

	for (i = 0; i < results_num; i++)
		zbx_variant_clear(&results[i].value);
	zbx_free(results);

    DEBUG_ITEM(metric->itemid, "Result of preprocessing of in: '%s', code: '%s', result is '%s'",
        zbx_variant_value_desc(&metric->value),  zbx_result_string(ret), (SUCCEED == ret ? zbx_variant_value_desc(&value_out) : error ));
    
    metric_t new_metric = {.hostid = metric->hostid, .itemid = metric->itemid, .ts = metric->ts, .value = value_out };
    processing_send_metric(&new_metric);
	
	save_steps_history(metric->itemid, &history_out);
	zbx_vector_ptr_destroy(&history_out);

    process_dependent_metrics(&new_metric);
    zbx_variant_clear(&value_out);

	zbx_free(error);
}

static void process_dependent_metrics(const metric_t *metric) {
    zbx_preproc_item_t *preproc_item_conf;
    int i;

    if (NULL == (preproc_item_conf = zbx_hashset_search(&conf.item_config, &metric->itemid))) 
        return;
    
    for (i = 0; i < preproc_item_conf->dep_itemids_num; i++ ) 
        preprocess_metric_execute_steps(metric);

}

IPC_PROCESS_CB(metrics_proc_cb) {
    preprocess_metric_execute_steps((metric_t*)ipc_data);
}

static void	preproc_item_clear(zbx_preproc_item_t *item)
{
	int	i;

	zbx_free(item->dep_itemids);

	for (i = 0; i < item->preproc_ops_num; i++)
	{
		zbx_free(item->preproc_ops[i].params);
		zbx_free(item->preproc_ops[i].error_handler_params);
	}

	zbx_free(item->preproc_ops);
}

void preprocessing_sync_conf() {
  zbx_preproc_item_t	*item;
  u_int64_t old_revision;
  zbx_preproc_history_t	*vault;
  zbx_hashset_iter_t	iter;

  old_revision = conf.cfg_revision;
  DCconfig_get_preprocessable_items(&conf.item_config, &conf.cfg_revision, conf.process_num - 1);
  
  if (old_revision != conf.cfg_revision)
	{
		/* drop items with removed preprocessing steps from preprocessing history cache */
		zbx_hashset_iter_reset(&conf.history_cache, &iter);
		while (NULL != (vault = (zbx_preproc_history_t *)zbx_hashset_iter_next(&iter)))
		{
			if (NULL != zbx_hashset_search(&conf.item_config, &vault->itemid))
				continue;

			zbx_vector_ptr_clear_ext(&vault->history, (zbx_clean_func_t)zbx_preproc_op_history_free);
			zbx_vector_ptr_destroy(&vault->history);
			zbx_hashset_iter_remove(&iter);
		}

		/* reset preprocessing history for an item if its preprocessing step was modified */
		zbx_hashset_iter_reset(&conf.item_config, &iter);
		while (NULL != (item = (zbx_preproc_item_t *)zbx_hashset_iter_next(&iter)))
		{
			if (item->preproc_revision < conf.cfg_revision)
				continue;

			if (NULL == (vault = (zbx_preproc_history_t *)zbx_hashset_search(&conf.history_cache,
					&item->itemid)))
			{
				continue;
			}

			zbx_vector_ptr_clear_ext(&vault->history, (zbx_clean_func_t)zbx_preproc_op_history_free);
			zbx_vector_ptr_destroy(&vault->history);
			zbx_hashset_remove_direct(&conf.history_cache, vault);
		}
	}
}

void preprocessing_worker_init(zbx_thread_args_t *args) {
  
 	conf.process_type = args->info.process_type;
	conf.server_num = args->info.server_num;
	conf.process_num = args->info.process_num;
  
 	zbx_hashset_create_ext(&conf.item_config, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
			(zbx_clean_func_t)preproc_item_clear,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);
  
    preprocessing_sync_conf();
  
    zbx_hashset_create(&conf.history_cache, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	
	zbx_es_init(&es_engine);
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
        usleep(10000);
    } else {
      total_proc +=i;
    }

    if (time(NULL) - 5 > proctitle_update) {
      
      zbx_setproctitle("glb_preproc_worker: processed %d/sec", total_proc/5);
      total_proc = 0;
      proctitle_update = time(NULL);
      preprocessing_sync_conf();
    }    
  }
  
  zbx_es_destroy(&es_engine);

}