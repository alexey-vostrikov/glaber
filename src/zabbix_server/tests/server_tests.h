#ifdef HAVE_GLB_TESTS

#include "zbxcommon.h"
#include "log.h"


//#include "../../libs/glb_state/tests/glb_state_tests.c"
//#include "../../libs/zbxalgo/tests/algo_tests.c"
//#include "../preprocessor/worker_tests.c"
//#include "../../libs/zbxexec/tests/exec_tests.c"


void tests_server_run(void) {
    LOG_INF("Running server tests");
    
    LOG_INF("Running glb_state tests");
  //  glb_state_run_tests();

    LOG_INF("Running glb_exec tests");
  //  glb_exec_run_tests();

    LOG_INF("Running preprocessing worker tests");
//    test_worker_steps();
 
    LOG_INF("Running algo tests");
  //  tests_algo_run();

    LOG_INF("Server tests finished");
    HALT_HERE("It's not supposed to run server after tests, remove HAVE_GLB_TESTS in zbxcommon.h")
}
#endif