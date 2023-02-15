
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
#include "log.h"

#ifdef HAVE_GLB_TESTS

#include "../glb_state_ids.h"
#include "../glb_state_problems.h"

void test_problems() {
    
    mem_funcs_t memf = { .malloc_func = zbx_default_mem_malloc_func, 
            .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};
    
    glb_state_problems_init(&memf);
    glb_state_ids_init(1);

    
    u_int64_t problemid = glb_state_problem_create(0, GLB_PROBLEM_SOURCE_TRIGGER, 123, "test trigger", 3, NULL);
    assert (problemid != 0 && "Should succeed to create a problem");

    //wrong source
    problemid = glb_state_problem_create(0, 4, 123, "test trigger", 3, NULL);
    assert(problemid == 0);
    
    //ALL OK
    problemid = glb_state_problem_create(123, GLB_PROBLEM_SOURCE_TRIGGER, 123, "test trigger", 3, NULL);
    assert(problemid == 123);
    
    //should FAIL as the problem already exists
    problemid = glb_state_problem_create(123, GLB_PROBLEM_SOURCE_TRIGGER, 123, "test trigger", 3, NULL);
    assert(problemid == 0);

    //shuold FAIL due to no name passed
    problemid = glb_state_problem_create(122, GLB_PROBLEM_SOURCE_TRIGGER, 123, NULL, 3, NULL);
    assert(problemid == 0);

    //should FAIL as wrong severity
    problemid = glb_state_problem_create(122, GLB_PROBLEM_SOURCE_TRIGGER, 123, "test trigger", 6, NULL);
    assert(problemid == 0);
    
    //all attempts except should be failed by now
    assert(glb_state_problems_get_count() == 2);

    //recover something unexistant should fail
    assert(FAIL == glb_state_problem_recover(0,0) && "nonexistent problem recover should fail");
    
    assert(SUCCEED == glb_state_problem_recover(123,0) && "existent problem recover should succeed");
    assert(FAIL == glb_state_problem_recover(123,0) && "Second problem recover should fail");

    /****Test problem fetching ******/
    assert(FAIL == glb_state_problems_get_by_triggerid(7385, NULL) && "Fetch of unknown trigger's problem should fail");
    assert(FAIL == glb_state_problems_get_by_triggerid(123, NULL) && "Fetch of known trigger's problem  but null vector should fail");
    
    zbx_vector_ptr_t problems;
    zbx_vector_ptr_create(&problems);
    LOG_INF("Problems is %p", &problems);
    assert(SUCCEED == glb_state_problems_get_by_triggerid(123, &problems) && "Fetch of known trigger's problem should succeed");
    assert(2 == problems.values_num && "There should be two problems for trigger 123");
    LOG_INF("Cleaning problems");
    glb_state_problems_clean(&problems);
    LOG_INF("Destroying vector");
    zbx_vector_ptr_clear(&problems);


    //trying hosts operations. Host info is external to problems
    zbx_vector_uint64_t hosts;
    zbx_vector_uint64_create(&hosts);
    zbx_vector_uint64_append(&hosts, 45);
    zbx_vector_uint64_append(&hosts, 4545);
    
    LOG_INF("Creating problem with two hosts");
    assert(0 != glb_state_problem_create(0, GLB_PROBLEM_SOURCE_TRIGGER, 145, "test trigger", 3, &hosts));
    
    zbx_vector_uint64_append(&hosts, 14545);
    assert(0 != glb_state_problem_create(0, GLB_PROBLEM_SOURCE_TRIGGER, 2145, "test trigger", 3, &hosts));

    LOG_INF("Checking fetching problems by host");
    assert(FAIL == glb_state_problems_get_by_hostid(2454, &problems) && "Fetch of unknown host problems should fail");
    assert(SUCCEED == glb_state_problems_get_by_hostid(45, &problems) && "Fetch of known host problems should SUCCEED");
    assert(2 == problems.values_num && "There should be two problems for host 45");
    glb_state_problems_clean(&problems);
    zbx_vector_ptr_clear(&problems);

///check host 14545
    assert(SUCCEED == glb_state_problems_get_by_hostid(14545, &problems) && "Fetch of known host problems should SUCCEED");
    assert(1 == problems.values_num && "There should be one problem for host 45");

    glb_state_problems_clean(&problems);
    zbx_vector_ptr_clear(&problems);
    
    LOG_INF("Finished problems creating");
    glb_state_problems_destroy();

}
void test_ids() {
   glb_state_ids_init(152);
   for(int i=0; i< 10000; i++) {
        u_int64_t a = glb_state_id_gen_new(1), b = glb_state_id_gen_new(1);
        if (a >= b) {
            LOG_INF(" sequence id wrong at iter %d %lld %lld",i , a, b);
            assert(0);
        }
    }
    
    u_int64_t a,b;
    
    
    a = glb_state_id_gen_new(1);
    glb_state_ids_init(38);
    b = glb_state_id_gen_new(2);
    LOG_INF("Got ids: %lld %lld", a, b);
    assert( (a & (u_int64_t)0xff) != (b & (u_int64_t)0xff) );
    a = glb_state_id_gen_new(2);
    assert( (a & (u_int64_t)0xff) == (b & (u_int64_t)0xff) );
    
}


void state_test_problems() {
    LOG_INF("Starting problems test");
    test_problems();
    test_ids();
    HALT_HERE("Finished");
}

#endif