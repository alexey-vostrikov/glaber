/*
** Glaber
** Copyright (C) 2018-2042 Glaber
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


#include "../../libs/glb_state/tests/glb_state_tests.h"
#include "../../libs/zbxalgo/tests/algo_tests.h"

#include "../preprocessor/tests/preproc_tests.h"
#include "../glb_poller/tests/test_internal.h"



#include "log.h"
void tests_server_run(void) {
    LOG_INF("Running server tests");    
    sleep(1);
    
    LOG_INF("Running state tests");
    glb_state_run_tests();
    
    LOG_INF("Reunning preprocessing tests");
    run_proc_ipc_tests();
    
    LOG_INF("Running internal metric tests");
    run_internal_metric_tests();
    

    LOG_INF("Running glb_state tests");
//    glb_state_run_tests();

    LOG_INF("Running glb_exec tests");
    //glb_exec_run_tests();

    //LOG_INF("Running preprocessing worker tests");
    //test_worker_steps();
 
    LOG_INF("Running algo tests");
//    tests_algo_run();
    
    LOG_INF("Server tests finished");
 //   HALT_HERE("It's not supposed to run server after tests, remove HAVE_GLB_TESTS in zbxcommon.h")
}
#endif