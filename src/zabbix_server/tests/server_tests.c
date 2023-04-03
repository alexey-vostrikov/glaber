/*
** Copyright (C) 2001-2023 Glaber
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
#include "glb_log.h"
//#include "zbxcommon.h"



#include "../../libs/glb_state/tests/glb_state_tests.h"
#include "../../libs/zbxalgo/tests/algo_tests.h"
#include "../../libs/glb_macro/tests/glb_macro_tests.h"
#include "../../libs/glb_conf/tags/tests/tag_tests.h"

//#include "../preprocessor/worker_tests.c"
//#include "../../libs/zbxexec/tests/exec_tests.c"
int tests_server_run(void) {
   LOG_INF("Running server tests");
   LOG_INF("Running tag tests");
   glb_tags_tests_run();
   
   LOG_INF("Running algo tests");
   tests_algo_run();

//   LOG_INF("Running macro tests");
//   glb_macro_run_tests();

//   LOG_INF("Running state problem tests");
//   state_test_problems();
   
//   LOG_INF("Running glb_state tests");
//   glb_state_run_tests();
   

//     LOG_INF("Running glb_exec tests");
//   //  glb_exec_run_tests();

//     LOG_INF("Running preprocessing worker tests");
// //    test_worker_steps();
 

//     LOG_INF("Server tests finished");

   HALT_HERE("It's not supposed to run server after tests, remove HAVE_GLB_TESTS in zbxcommon.h");
   
}
#endif