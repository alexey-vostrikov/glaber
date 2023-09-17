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
#ifdef GLB_HAVE_TESTS

#include "log.h"
#include "../glb_state_hosts.h"
#include "../../../zabbix_server/tests/test_utils.h"



#define TEST_MEM_SIZE 16 * ZBX_MEBIBYTE
extern int CONFIG_UNREACHABLE_DELAY;

static void test_hosts_heartbeat_process(void){
    mem_funcs_t *test_memf;

    test_memf = tests_mem_allocate_shmem(TEST_MEM_SIZE);
    glb_state_hosts_init(test_memf);

    //should just survive
    glb_state_hosts_process_heartbeat(1245,5);
    glb_state_hosts_process_heartbeat(1001,1);
    glb_state_hosts_process_heartbeat(1002,1);
    glb_state_hosts_process_heartbeat(1004,1);


    assert(FAIL == glb_state_hosts_get_heartbeat_alive_status(384) && "Absent host should FIAL");
    assert( HOST_HEARTBEAT_ALIVE== glb_state_hosts_get_heartbeat_alive_status(1245) && "Host hb should be ALIVE");
    assert( HOST_HEARTBEAT_ALIVE == glb_state_hosts_get_heartbeat_alive_status(1002) && "Host hb should be ALIVE");
    LOG_INF("Sleeping 2 sec for heartbeats to expire");
    sleep(2);
    assert( HOST_HEARTBEAT_DOWN == glb_state_hosts_get_heartbeat_alive_status(1002) && "Host hb should be DOWN now");
    glb_state_hosts_reset_heartbeat(384);
    assert( HOST_HEARTBEAT_UNKNOWN == glb_state_hosts_get_heartbeat_alive_status(384) && "Reset absent host should be UNKNOWN");
    glb_state_hosts_reset_heartbeat(1245);
    assert( HOST_HEARTBEAT_UNKNOWN == glb_state_hosts_get_heartbeat_alive_status(1245) && "Reset ALIVE host should be UNKNOWN");
    glb_state_hosts_delete(1245);
    assert( FAIL == glb_state_hosts_get_heartbeat_alive_status(1245) && "Deleted host should return FAIL");

    LOG_INF("doing deinit");
    glb_state_hosts_destroy(&test_memf);
    
    test_mem_release_shmem();
    LOG_INF("Finished");
}

void test_hosts_interface_process() {
    LOG_INF("%s started", __func__);
    mem_funcs_t *test_memf;
    test_memf = tests_mem_allocate_shmem(TEST_MEM_SIZE);

    glb_state_hosts_init(test_memf);
    LOG_INF("Calling set avail by name");
    
    glb_state_hosts_set_name_interface_avail(123,"testif",INTERFACE_AVAILABLE_UNKNOWN,"");
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_TRUE,"");
    
    LOG_INF("Fetching avail data");
    assert(INTERFACE_AVAILABLE_TRUE == glb_state_hosts_get_id_interface_avail(1234,1245,NULL) &&
            "Iface should be AVAILABLE");
            
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string");

    //LOG_INF("Interface state is %d", glb_state_hosts_get_id_interface_avail(1234,1245,NULL));

    assert(INTERFACE_AVAILABLE_TRUE == glb_state_hosts_get_id_interface_avail(1234,1245,NULL) &&
            "Iface should be AVAIL after first FAIL");
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string");
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string12");
  //  glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string123");
    assert(INTERFACE_AVAILABLE_FALSE == glb_state_hosts_get_id_interface_avail(1234,1245,NULL) &&
            "Iface should be FALSE after third FAIL");
    
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string123");
    LOG_INF("Checking for FAIL delay prolongation, sleep 5 sec");
    int disabled_till, state;
    sleep(5);

    state = glb_state_hosts_get_id_interface_avail(1234,1245, &disabled_till);

    assert(INTERFACE_AVAILABLE_FALSE == state && 
            disabled_till - time(NULL) && 
            disabled_till - CONFIG_UNREACHABLE_DELAY + 4 <time(NULL) &&
            "disabled till time should be more then now and should be 5 seconds less" );
    glb_state_hosts_set_id_interface_avail(1234, 1245, INTERFACE_AVAILABLE_FALSE,"some error string123");
    state = glb_state_hosts_get_id_interface_avail(1234,1245, &disabled_till);
    assert(disabled_till - time(NULL) - CONFIG_UNREACHABLE_DELAY <= 1 && "disabled till should be updated to full length ");

    LOG_INF("Doing host mixed interfaces set/get host with 5 interfaces, 1M iterations");

    glb_state_hosts_set_id_interface_avail(10000, 2, INTERFACE_AVAILABLE_UNKNOWN,"some error string");
    glb_state_hosts_set_id_interface_avail(10000, 3, INTERFACE_AVAILABLE_FALSE,"some error string"); 
    glb_state_hosts_set_id_interface_avail(10000, 3, INTERFACE_AVAILABLE_FALSE,"some error string"); 
    glb_state_hosts_set_name_interface_avail(10000,"NAH",INTERFACE_AVAILABLE_UNKNOWN,"");
    
    int i;
    for (int i =0 ; i< 1000000; i++ ) {
        glb_state_hosts_set_id_interface_avail(10000, 1, INTERFACE_AVAILABLE_UNKNOWN,"some error string");
        glb_state_hosts_set_id_interface_avail(10000, 2, INTERFACE_AVAILABLE_UNKNOWN,"some error string");
        glb_state_hosts_set_id_interface_avail(10000, 3, INTERFACE_AVAILABLE_FALSE,"some error string"); 
        glb_state_hosts_set_name_interface_avail(10000,"NAH",INTERFACE_AVAILABLE_UNKNOWN,"");
        glb_state_hosts_set_name_interface_avail(10000,"BAH",INTERFACE_AVAILABLE_TRUE,"wcewe");

        assert(INTERFACE_AVAILABLE_TRUE == glb_state_hosts_get_name_interface_avail(10000, "BAH", NULL) && "Should be avail");
        assert(INTERFACE_AVAILABLE_UNKNOWN == glb_state_hosts_get_name_interface_avail(10000, "NAH", NULL) && "Should be unknown");
        assert(INTERFACE_AVAILABLE_FALSE == glb_state_hosts_get_id_interface_avail(10000,3,NULL) && "should be false");
    }
    
    glb_state_hosts_reset(10000);
    
    LOG_INF("Checking reset result");
    
    assert(INTERFACE_AVAILABLE_UNKNOWN == glb_state_hosts_get_name_interface_avail(10000, "BAH", NULL) && "Should be unknown");
    assert(INTERFACE_AVAILABLE_UNKNOWN == glb_state_hosts_get_id_interface_avail(10000,3,NULL) && "should be unknown");

    struct zbx_json j;
    zbx_json_init(&j, 4096);
    glb_state_hosts_get_ifaces_json_avail_state(10000, &j);
    LOG_INF("Got the 10000 host ifaces data: '%s'", j.buffer);

    glb_state_hosts_destroy();
    test_mem_release_shmem();
    LOG_INF("%s Finished", __func__);
}

void glb_state_hosts_interfaces_run_tests(void) {
    test_hosts_interface_process();
    test_hosts_heartbeat_process();
    HALT_HERE("Host state interfaces are not impelmented");
}

#endif