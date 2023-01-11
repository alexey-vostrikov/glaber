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
#include <signal.h>
#include <stdio.h>

#include "log.h"
#include "../worker.h"

void test_worker_run() {
    sigset_t	mask, orig_mask;
	
    sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGCHLD);
 
	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		zbx_error("cannot set sigprocmask to block the user signal");

    glb_worker_t *worker;
    char *response;

    assert(NULL == glb_worker_init("/dev/nul1l","",0,0,0,0));
    assert(NULL == glb_worker_init("/etc/localtime","",0,0,0,0));
    assert(NULL != (worker = glb_worker_init("/bin/cat","",0,0,0,0)));
    
    glb_worker_destroy(worker);
    
    //lets try to run cut and read something
    assert(NULL != (worker = glb_worker_init("/bin/cat","",0,0,0,0)));
    assert(SUCCEED == glb_worker_restart(worker));
    sleep(1); //intentional wait
    assert(SUCCEED == glb_worker_is_alive(worker));

    glb_worker_send_request(worker,"hello to you");
    glb_worker_get_sync_response(worker, &response);
    LOG_INF("Got response '%s'", response);
    assert( NULL != response && 0 == strcmp(response, "hello to you"));
    glb_worker_destroy(worker);
    zbx_free(response);

    assert(NULL != (worker = glb_worker_init("/bin/echo","hello world of glaber",0,0,0,0)));
    glb_worker_get_sync_response(worker, &response);
    LOG_INF("Got response '%s'", response);
    glb_worker_destroy(worker);
    zbx_free(response);

    //multiline test
    assert(NULL != (worker = glb_worker_init("/bin/cat","",0,0,0,0)));
    glb_worker_send_request(worker,"line1");
    glb_worker_send_request(worker,"line2");
    
    glb_worker_get_sync_response(worker, &response);
    assert(0 == strcmp(response, "line1"));
    zbx_free(response);

    glb_worker_get_sync_response(worker, &response);
    assert(0 == strcmp(response, "line2"));
    zbx_free(response);
    glb_worker_destroy(worker);

    char * test_data = "line1\nline2\nline3\nline4\nline5\n";
    FILE *fp;
    fp = fopen("/tmp/glaber_exec_test.txt", "w");
    assert(fp != NULL);
    fwrite(test_data,1,strlen(test_data),fp);
    fclose(fp);
    
    //try sync read line by line
    assert(NULL != (worker = glb_worker_init("/bin/cat","/tmp/glaber_exec_test.txt",0,0,0,0)));
    for (int i =0; i< 5; i++) {
        response = NULL;
        glb_worker_get_sync_response(worker, &response);
        LOG_INF("Got line '%s'", response);
        assert(NULL != response);
        zbx_free(response);
    }
    
    //try restart after worker die after the 5th line
    response = NULL;
    //int pid = glb_worker_get_pid(worker);
    //kill(pid,SIGKILL);
    //sleep(1);
    for (int i =0; i < 5; i++) {
        response = NULL;
        glb_worker_get_sync_response(worker, &response);
       // assert(NULL != response);
        LOG_INF("Worker returned '%s'",response);
        zbx_free(response);
        if (i == 4)
            assert(response != NULL);
    }


    LOG_INF("Finished exec tests");
}

void glb_exec_run_tests() {
    LOG_INF("Running execution tests");
    test_worker_run();
    LOG_INF("Finished execution tests");
    HALT_HERE("All exec test successfull");
}

