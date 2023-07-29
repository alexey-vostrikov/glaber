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

#include "glb_common.h"

#ifdef HAVE_GLB_TESTS

#include "zbxcommon.h"
#include "zbxalgo.h"
#include "zbxsysinfo.h"

#include "log.h"
#include "glb_preproc.h"
#include "../../glb_poller/internal.h"
#include "../../../libs/glb_state/glb_state.h"

IPC_PROCESS_CB(processing_cb) {
}


static int test_no_leaks_run() {
    char *preproc_stat, *preproc_stat_after = NULL;
    char *proc_stat = NULL;
    AGENT_REQUEST request;

    glb_internal_metrics_init();
    glb_state_init();

    LOG_INF("Running leak check testing");
    assert(SUCCEED == preproc_ipc_init() && "Make sure succeeded init run" );
    
    LOG_INF("Saving statistics data");
    
    zbx_init_agent_request(&request);    
    assert(SUCCEED == zbx_parse_item_key("internal[preprocessing]", &request));
    assert(SUCCEED == glb_get_internal_metric("preprocessing",request.nparam, &request, &preproc_stat ) && "Should get internal preproc metrics");
    zbx_free_agent_request(&request);
    assert(NULL != preproc_stat && "Should get preprocessing metric response");

    zbx_init_agent_request(&request);    
    assert(SUCCEED == zbx_parse_item_key("internal[processing]", &request));
    assert(SUCCEED == glb_get_internal_metric("processing",request.nparam, &request, &proc_stat ) && "Should get internal preproc metrics");
    zbx_free_agent_request(&request);

    assert(NULL != proc_stat && "Should get preprocessing metric response");
    LOG_INF("Preproc stat is: %s", preproc_stat);
    LOG_INF("Proc stat is: %s", proc_stat);

    //transfering 1m metrics by batches of 10000k metrics
    for (int i = 0 ; i < 100; i++) {
        
        //LOG_INF("Starting iteration %d", i);
        for (int k = 0; k < 2000; k++) {
            metric_t metric = {.itemid = 1234, .hostid = 0, .value.type = VARIANT_VALUE_UINT64, .value.data.ui64 = k};
            preprocess_send_metric(&metric);
        }
        
        for (int k = 0; k < 2000; k++) {
            metric_t metric = {.itemid = 12356, .hostid = 0, .value.type = VARIANT_VALUE_DBL, .value.data.dbl = 0.245};
            preprocess_send_metric(&metric);
        }

        for (int k = 0; k < 2000; k++) {
            metric_t metric = {.itemid = 1234, .hostid = 0, .value.type = VARIANT_VALUE_ERROR, .value.data.str = "some kind of error goes here"};
            preprocess_send_metric(&metric);
        }

        for (int k = 0; k < 2000; k++) {
            metric_t metric = {.itemid = 1238, .hostid = 0, .value.type = VARIANT_VALUE_STR, .value.data.str = "just some string test"};
            preprocess_send_metric(&metric);
        }
        
        for (int k = 0; k < 2000; k++) {
            metric_t metric = {.itemid = 123348, .hostid = 0, .value.type = VARIANT_VALUE_NONE};
            preprocess_send_metric(&metric);
        }

        preprocessing_force_flush();
        //FINISH METRIC RECIEVING HERER
        //recieving all the metrics
        int cnt = preproc_receive_metrics(1, processing_cb, NULL, 5000);
        
        //cnt += preproc_receive_metrics(1, processing_redirect_cb, NULL, 5000);
        //preprocessing_force_flush();

        cnt += preproc_receive_metrics(1, processing_cb, NULL, 5000);

      //  LOG_INF("Processed %d metrics", cnt);
        assert (10000 == cnt && "Should recieve all the metrics");
        
        //LOG_INF("Iteration %d finished", i);
    }

    
    zbx_init_agent_request(&request);    
    assert(SUCCEED == zbx_parse_item_key("internal[preprocessing]", &request));
    assert(SUCCEED == glb_get_internal_metric("preprocessing",request.nparam, &request, &preproc_stat_after ) && "Should get internal preproc metrics");
    zbx_free_agent_request(&request);

    LOG_INF("After test stat is %s", preproc_stat_after);
    assert(0 == strcmp(preproc_stat_after, preproc_stat) && "Memory usage has to be the same on start and end");

    HALT_HERE("Not finished yet");
}


void run_proc_ipc_tests() {
    
    
    test_no_leaks_run();
    
    HALT_HERE("Processing ipc tests are not implemented");
}

#endif