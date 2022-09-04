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

#include "common.h"
#include "zbxalgo.h"
#include "daemon.h"
//#include "zbxself.h"
#include "log.h"
//#include "preprocessing.h"
//#include "zbxembed.h"
#include "metric.h"
#include "glb_preproc_ipc.h"

#include "../../libs/zbxipcservice/glb_ipc.h"

extern unsigned char process_type, program_type;
extern int server_num, process_num;


//logic of the preprocessing worker:
//as a far as it has tasks, it processes them
//processing implies sending out the result

//so the basic processing loop is formed by receiving metrics ipc callback

//preprocessing steps are NOT cached, instead new low-locked config is used
//dependant items are NOT cached and checked on sending result to processing, and if they are
//the result is reinserted to the local processing queue

//steps cache is kept in the heap, it's maintenance is done hourly 
typedef struct {
    u_int64_t itemid;
    zbx_vector_ptr_t steps_history;
//    preproc_steps_t *steps;
//    zbx_vector_uint64_pair_t *dep_itemids;
} preproc_cache_t;


static void preprocess_metric(const metric_t* metric) {
  //preproc_steps_t *steps = conf_items_preproc_get_steps(metric->itemid);
  //zbx_vector_ptr_t *history_steps = preproc_cache_fetch_history(metric->itemid);
  //zbx_vector_uint64_t dep_itemids = conf_items_preproc_get_depitems(metric->itemid);
  //preproc_cache_t *preproc_data = fetch_preproc_data(metric->itemid);
  //metric_t *new_metric = execute_preproc_steps(metric, preproc_data);
  //processing_send_metric(new_metric);

  //preprocess_dependent_items(preproc_data->dep_itemids, new_metric);
}


IPC_PROCESS_CB(metrics_proc_cb) {
    preprocess_metric((metric_t*)ipc_data);
}

#define BATCH_PROCESS_METRICS 256

ZBX_THREAD_ENTRY(glb_preprocessing_worker_thread, args) {
  int i = 0, total_proc =0, proctitle_update=0;

 	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

  zbx_setproctitle("glb_preproc_worker");
  LOG_INF("glb_preproc_worker started");
 // metric_t metrics[BATCH_PROCESS_METRICS] = {0};

  while (1) {
	
  // LOG_INF("Calling preproc receive metrics");
    if (0 == (i = preproc_receive_metrics(process_num, metrics_proc_cb, NULL, BATCH_PROCESS_METRICS))) {
        usleep(100);
    } else {
      total_proc +=i;
    }

    if (time(NULL) - 5 > proctitle_update) {
      
      zbx_setproctitle("glb_preproc_worker: processed %d/sec", total_proc/5);
      total_proc = 0;
      proctitle_update = time(NULL);
    }    
    //LOG_INF("Processed %d metrics in glb_preprocessing", i);
  }

}