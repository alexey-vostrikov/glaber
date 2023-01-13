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

#include "../glb_state_triggers.h"
#include "../glb_state_interfaces.h"

static void state_test_untyped_interfaces() {
    LOG_INF("Starting normal (untyped) interfaces tests");
     mem_funcs_t memf = { .malloc_func = zbx_default_mem_malloc_func, 
            .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};
    
    glb_state_interface_info_t *ifinfo;
    sleep(1);
    assert(FAIL == glb_state_interfaces_register_fail(0, NULL));
    assert(SUCCEED == glb_state_interfaces_register_fail(1, "hi there"));
    assert(NULL != (ifinfo = glb_state_interfaces_get_avail(1)));
    assert(INTERFACE_AVAILABLE_UNKNOWN == ifinfo->avail);    
    assert(SUCCEED == glb_state_interfaces_register_fail(1, "hi there"));
    assert(SUCCEED == glb_state_interfaces_register_fail(1, "hi there"));
    assert(NULL != (ifinfo = glb_state_interfaces_get_avail(1)));
    assert(INTERFACE_AVAILABLE_FALSE == ifinfo->avail);    

    sleep(2);
    HALT_HERE("Intentional halt on iface tests finish- SUCCESS");



}


static void state_test_interfaces() {
    LOG_INF("Starting interfaces tests");
     mem_funcs_t memf = { .malloc_func = zbx_default_mem_malloc_func, 
            .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};
    
    glb_state_interfaces_init(&memf);

    assert(FAIL == glb_state_interfaces_register_ip(NULL, 0));
    assert(SUCCEED == glb_state_interfaces_register_ip("1.4.5.6", 1));
    
    assert(0 == glb_state_interfaces_find_host_by_ip("4.5.6.7"));
    assert(0 == glb_state_interfaces_find_host_by_ip(NULL));
    
    assert(1 == glb_state_interfaces_find_host_by_ip("1.4.5.6"));
    //check several ip->same host
    assert(SUCCEED == glb_state_interfaces_register_ip("127.0.0.1", 1));
    assert(1 == glb_state_interfaces_find_host_by_ip("1.4.5.6"));
    assert(1 == glb_state_interfaces_find_host_by_ip("127.0.0.1"));
    //check ip change to another host
    assert(SUCCEED == glb_state_interfaces_register_ip("127.0.0.1", 5461));
    assert(5461 == glb_state_interfaces_find_host_by_ip("127.0.0.1"));

    //cleanup check
    LOG_INF("Deleting ip addr");
    glb_state_interfaces_release_ip("127.0.0.1");
    LOG_INF("Deleted ip addr");
    assert(0 == glb_state_interfaces_find_host_by_ip("127.0.0.1"));

    //some shit to test stability
    glb_state_interfaces_release_ip(NULL);
    glb_state_interfaces_release_ip("dfcwedfwe");
    glb_state_interfaces_release_ip("127.0.0.1");
    glb_state_interfaces_release_ip("127.0.0.1");

    glb_state_interfaces_destroy();
  //  HALT_HERE("Host tests succeed");
 
}


static void state_test_triggers(){
    LOG_INF("Starting triggers tests");
     mem_funcs_t memf = { .malloc_func = zbx_default_mem_malloc_func, 
            .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};
    state_trigger_info_t info;
    
    LOG_INF("doing trigger init");
    glb_state_triggers_init(&memf);

    LOG_INF("Fail fetch test 1");
    info.id = 0;

    assert(FAIL == glb_state_trigger_get_info(&info));
    LOG_INF("Fail fetch test 1.1");
    assert(FAIL == glb_state_trigger_get_info(NULL));
    LOG_INF("Fail fetch test 1.2");
    assert(FAIL == glb_state_trigger_set_info(NULL));
    LOG_INF("Fail fetch test 1.3");
    assert(FAIL == glb_state_trigger_set_info(&info));
    info.id = 1;
    info.lastcalc = 0;
    //should due to lastcalc is unset
    assert(FAIL == glb_state_trigger_set_info(&info));
    
    //should FAIL due to wrong value
    info.lastcalc = 1;
    info.value = 128;
    assert(FAIL == glb_state_trigger_set_info(&info));
    //still should fail as we didn't succeed setting the state
    assert(FAIL == glb_state_trigger_get_info(&info));
    info.error ="test";
    info.id = 1;
    info.lastcalc = 2;
    info.value = TRIGGER_VALUE_PROBLEM;
    //should fail as error str only possible for UNKNOWN state
    assert(FAIL == glb_state_trigger_set_info(&info));

    info.value = TRIGGER_VALUE_UNKNOWN;

    //now we should succeed
    assert(SUCCEED == glb_state_trigger_set_info(&info));

    bzero(&info, sizeof(info));
    info.id = 2;
    assert(FAIL == glb_state_trigger_get_info(&info));
    
    info.id = 1;
    info.flags = STATE_GET_TRIGGER_ERROR;
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert(info.value == TRIGGER_VALUE_UNKNOWN);

    assert(0 == strcmp(info.error, "test"));        
    zbx_free(info.error);
    
    info.flags = 0;
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert(NULL == info.error);

    info.value = TRIGGER_VALUE_OK;
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    info.value = 128;
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert( TRIGGER_VALUE_OK == info.value);

    info.lastcalc = 12345;
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    info.lastcalc = 0;
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert(12345 == info.lastcalc);

    
    //checking lastchange is changing on value change
    info.id=123423525;
    info.lastcalc=23452345;
    info.value = TRIGGER_VALUE_OK;
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    int old_lastchange = info.lastchange;
    info.value = TRIGGER_VALUE_PROBLEM;
    LOG_INF("Wait 4 seconds timeout for lastchange state change handling");
    sleep(2);
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert((info.lastchange != old_lastchange) && (info.lastchange >=time(NULL)-1) );
    
    //checking lastchange isn't changing if value hasn't changed
    old_lastchange = info.lastchange;
    sleep(2);
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert(old_lastchange == info.lastchange);

    //short functions testing
    assert(TRIGGER_VALUE_NONE == glb_state_trigger_get_value(2435));
    assert(TRIGGER_VALUE_PROBLEM == glb_state_trigger_get_value(123423525));
    
    glb_state_trigger_set_value(123423525,TRIGGER_VALUE_UNKNOWN,0);
    assert(TRIGGER_VALUE_UNKNOWN == glb_state_trigger_get_value(123423525));

    //test housekeeping
    info.id=200;
    info.lastcalc = time(NULL);
    info.value = TRIGGER_VALUE_OK;
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    glb_state_triggers_housekeep(1);
    assert(SUCCEED == glb_state_trigger_get_info(&info));
    assert(200 == info.id);
    
    //now adding outdated item and sleep for 2 seconds to allow housekeeping to run
    //and checkout it has gone
    info.lastcalc = 100;
    assert(SUCCEED == glb_state_trigger_set_info(&info));
    sleep(2);
    assert(SUCCEED == glb_state_trigger_get_info(&info)); 
    assert(100 == info.lastcalc);
    glb_state_triggers_housekeep(1);
    assert(FAIL == glb_state_trigger_get_info(&info)); 

    //now test that housekeep will not run too frequently
    glb_state_trigger_set_value(200, TRIGGER_VALUE_OK, 300);
    assert(SUCCEED == glb_state_trigger_get_info(&info)); 
    glb_state_triggers_housekeep(10);//this shouldn't run and clean the 200 item
    assert(SUCCEED == glb_state_trigger_get_info(&info)); 

    LOG_INF("Doing destroy");
    glb_state_triggers_destroy();
    
    LOG_INF("Trigger tests are finished");
}


#ifdef HAVE_GLB_TESTS
void glb_state_run_tests(void) {
    state_test_untyped_interfaces();
    state_test_interfaces();
    state_test_triggers();

   
}
#endif