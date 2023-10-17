/*
** Copyright Glaber 2018-2023
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
#include "log.h"
#include "zbxshmem.h"
#include "glb_lock.h"
#include <threads.h>
#include <stdatomic.h>

#include "metric.h"
#include "../glb_ipc2.h"
#include "../glb_serial_buffer.h"
#include "../../../zabbix_server/tests/test_utils.h"

#define TEST_MEM_SIZE (u_int64_t)256 * ZBX_MEBIBYTE

char *test_line = "This is a test line used for testing";

static void test_receive_cb(void *rcv_data, void *ctx_data) {
    char *rcv_line = rcv_data;
    char *sent_line = ctx_data;

  

   
   assert( 0 == strcmp(rcv_line, sent_line) && "should get just what has sent");
}

static void run_tests() {
    mem_funcs_t *memf = NULL;
    zbx_shmem_info_t	*shm_mem = NULL;
    ipc2_conf_t *ipc;
    ipc2_rcv_conf_t *rcv_ipc;
    int i = 0;

    assert(SUCCEED == tests_mem_allocate_shmem(TEST_MEM_SIZE, &memf, &shm_mem) && 
                        "Test shmem should succeed on create");
    
    LOG_INF("Test shmem created, memf is %p", memf);

    ipc = ipc2_init(5, memf, "Some name", shm_mem );
    assert(NULL != ipc && "IPC should be created");
    rcv_ipc = ipc2_init_receiver();
    
    for (int i = 0; i < 10; i++) {
        ipc2_send_chunk(ipc, 0, 13, test_line, strlen(test_line) + 1);
        ipc2_receive_one_chunk(ipc, rcv_ipc, 0, test_receive_cb, test_line);
    }
    
    LOG_INF("Destroying IPC");

    ipc2_destroy(ipc);
    ipc2_deinit_receiver(rcv_ipc);

    test_mem_release_shmem();

}

#define IPC_CONGESTION_TEST_COUNT 10000000

int ipc_slow_reader(void* thr_data) {
    ipc2_conf_t *ipc = thr_data;
    int i = 0;
  //  LOG_INF("Slow reader started, sleeping 2 sec");
  //  sleep(2);
    ipc2_rcv_conf_t *ipc_rcv = ipc2_init_receiver();
    //ipc2_dump_queues(ipc);
    
    while ( i < IPC_CONGESTION_TEST_COUNT) {
      //  LOG_INF("Getting chunks");
         ipc2_receive_one_chunk(ipc, ipc_rcv, 0, test_receive_cb, test_line);
         i++;
      //  LOG_INF("Received %d chunks", i);
    }
    
    ipc2_deinit_receiver(ipc_rcv);
    LOG_INF("Reader has received %d chunks, exiting", i);
    return 0;
}

void run_congestion_test(){
    mem_funcs_t *memf = NULL;
    zbx_shmem_info_t	*shm_mem = NULL;
    ipc2_conf_t *ipc;
    int i = 0;
    thrd_t thr[1];

    assert(SUCCEED == tests_mem_allocate_shmem(TEST_MEM_SIZE, &memf, &shm_mem) && 
                        "Test shmem should succeed on create");
    
    LOG_INF("Test shmem created, memf is %p", memf);

    ipc = ipc2_init(5, memf, "Some name", shm_mem );
    assert(NULL != ipc && "IPC should be created");

    LOG_INF("Starting reader thread");    
    thrd_create(&thr[0], ipc_slow_reader, ipc);

    for (int i = 0; i < IPC_CONGESTION_TEST_COUNT; i++) 
        ipc2_send_chunk(ipc, 0, 13, test_line, strlen(test_line) + 1);

    LOG_INF("Waiting for the reader finish");
    thrd_join(thr[0], NULL);
   
    LOG_INF("Reader finished, destroying IPC");
    ipc2_destroy(ipc);

    test_mem_release_shmem();

}

atomic_int sent_messages = 0;
atomic_int rcvd_messages = 0;


#define TEST_MEM_SIZE (u_int64_t)256 * ZBX_MEBIBYTE  //IPC SIZE
#define TEST_MESSAGES 100000000  //HOW MANY MESSAGES TO SEND
#define TEST_SENDERS 1500 // HOW MANY POLLERS
#define TEST_RECEIVERS 40 //HOW MANY PREPROCESSORS
#define MSG_SIZE    65536 //AVG MESSAGE SIZE (this is about 1500 numeric metrics)
#define RESEND_PROBABILITY 40 //in percent
#define RESEND_MULTIPLICATOR 5 //how many new message we want to send upon reveiving a message


int ipc_sender(void* thr_data) {
    ipc2_conf_t *ipc = thr_data;
    char *buffer = zbx_malloc(NULL, MSG_SIZE);
    zbx_snprintf(buffer, MSG_SIZE, "%s", test_line);

    
    while (sent_messages < TEST_MESSAGES) {
    
        ipc2_send_chunk(ipc, rand() % TEST_RECEIVERS, MSG_SIZE/100, buffer, MSG_SIZE);
        sent_messages++;
    
    }
   // LOG_INF("Sender finished");
   zbx_free(buffer);
}

void ipc_read(void *data, void *ctx_data) {
    char *rcv_line = data;
   // LOG_INF("ipc_read got '%s'", rcv_line);
    
    if (0 != strcmp(rcv_line, test_line)) {
        LOG_INF("Received '%s'", rcv_line);
    }
    assert( 0 == strcmp(rcv_line, test_line) && "should get just what has sent");

    


}

int ipc_receiver(void* thr_data) {
    ipc2_conf_t *ipc = thr_data;
    ipc2_rcv_conf_t *ipc_rcv = ipc2_init_receiver();

    while (rcvd_messages < sent_messages) {
    
        if ( 0 < ipc2_receive_one_chunk(ipc, ipc_rcv, rand() % TEST_RECEIVERS, ipc_read, NULL)) {
            rcvd_messages ++;
        } else 
            usleep(1);
    }
    
    ipc2_deinit_receiver(ipc_rcv);
    //LOG_INF("Receiver finished");
}

int reporter(void *thr_data) {
    ipc2_conf_t *ipc = thr_data;

    while (rcvd_messages < TEST_MESSAGES) {
        sleep(1) ;
        LOG_INF("Rcvd %ld messages, sent %ld messages, plan %ld", rcvd_messages, sent_messages, TEST_MESSAGES);
        ipc2_dump_queues(ipc);
    }
}

void run_many_senders_test(){
    mem_funcs_t *memf = NULL;
    zbx_shmem_info_t	*shm_mem = NULL;
    ipc2_conf_t *ipc;
    int i = 0;

    thrd_t thr_senders[TEST_SENDERS];
    thrd_t thr_receivers[TEST_RECEIVERS];
    thrd_t thr_reporter;

    assert(SUCCEED == tests_mem_allocate_shmem(TEST_MEM_SIZE, &memf, &shm_mem) && 
                        "Test shmem should succeed on create");
    
    LOG_INF("Test shmem created, memf is %p", memf);

    ipc = ipc2_init(TEST_RECEIVERS, memf, "Many_senders", shm_mem );
    assert(NULL != ipc && "IPC should be created");

    thrd_create(&thr_reporter, reporter, ipc);

    LOG_INF("Starting senders threads");    

    for (int i = 0; i< TEST_SENDERS; i++)
        thrd_create(&thr_senders[i], ipc_sender, ipc);
        
    LOG_INF("Starting receivers threads");
    
    for (int i = 0; i< TEST_RECEIVERS; i++)    
        thrd_create(&thr_receivers[i], ipc_receiver, ipc);

    LOG_INF("Waiting for senders finish");
    
    for (int i = 0; i< TEST_SENDERS; i++)
        thrd_join(thr_senders[i], NULL);
    
    LOG_INF("Waiting for receivers to finish");
    
    for (int i = 0; i < TEST_RECEIVERS; i++) {
     //   LOG_INF("Waiting for receiver %d", i);
        thrd_join(thr_receivers[i], NULL);
    //    LOG_INF("Receiver %d is finished", i);
    }

    thrd_join(thr_reporter, NULL);

    LOG_INF("ALL finished, got %d messages destroying IPC", rcvd_messages);
    ipc2_destroy(ipc);

    test_mem_release_shmem();

}

 void check_buffer_metric_cb(void *buff, void *item, void *ctx_data, int i) {
    metric_t *metric = item;
    
   // LOG_INF("Metrics's addr is %lld", item);

    LOG_INF("Metric host id is %lld, itemid %lld", metric->hostid, metric->itemid);
    assert(metric->hostid == metric->itemid *34 && "should get metric with matching hostid and itemid");

    if (VARIANT_VALUE_STR == metric->value.type || 
        VARIANT_VALUE_ERROR == metric->value.type) {
        metric->value.data.str = serial_buffer_get_real_addr(buff, (u_int64_t)metric->value.data.str);
    }
    
    LOG_INF("Received metric type '%s'", zbx_variant_type_desc(&metric->value));
    LOG_INF("Received metric value '%s'", zbx_variant_value_desc(&metric->value));

 }

void run_buffer_serialisation_test() {

    serial_buffer_t *sbuff = serial_buffer_init(2048);
    assert( NULL != sbuff && "Serial buffer should be created" );
    serial_buffer_destroy(sbuff);

    assert (NULL == serial_buffer_init((u_int64_t)2 * ZBX_GIBIBYTE));

    sbuff = serial_buffer_init(2048);
    assert(serial_buffer_get_size(sbuff) == 2048 && "sbuf must be of init size");
    assert(serial_buffer_get_used(sbuff) == 0 && "sbuf must be empty"); 
   
    metric_t metric = {.hostid = 890 * 34, .itemid = 890, .ts = time(NULL) };
    zbx_variant_set_dbl(&metric.value,35634.234);
    
    for (int i = 0; i< 10; i++) {
        LOG_INF("Not written some data, buffer size is %d, used %d", serial_buffer_get_size(sbuff), serial_buffer_get_used(sbuff));
        serial_buffer_add_item(sbuff, &metric, sizeof(metric_t));

        //adding string metric
        metric.itemid = rand();
        metric.hostid = metric.itemid * 34;
        metric.value.type =VARIANT_VALUE_STR;
        metric.value.data.str = serial_buffer_add_data(sbuff, test_line, strlen(test_line) + 1);
        
        serial_buffer_add_item(sbuff, &metric, sizeof(metric_t));
        LOG_INF("Written some data, buffer size is %d, used %d", serial_buffer_get_size(sbuff), serial_buffer_get_used(sbuff));
    }

    //try to read the metric
    serial_buffer_process(serial_buffer_get_buffer(sbuff), check_buffer_metric_cb, NULL);
    
    serial_buffer_clean(sbuff);
    assert(serial_buffer_get_size(sbuff) > 0 && "Buffer still should large");
    assert(serial_buffer_get_used(sbuff) == 0 && "Buffer should be unused");
    serial_buffer_clean(sbuff);

    HALT_HERE("Not implemented");
}

#define SERIAL_BUFF_ITEMS   2048
#define STRING_MAX_LEN  1024

void integrity_check_cb(void *buff, void *item, void *ctx_data, int i) {
    LOG_INF("Checking metrics %d", i);
    metric_t *m = item;
    LOG_INF("Metric itemid is %ld ", m->itemid);
    m->value.data.str = serial_buffer_get_real_addr(buff, (u_int64_t)m->value.data.str);
    LOG_INF("Got %d, got item %lld, length %d", i, m->itemid, strlen(m->value.data.str));
}

void test_serial_buffer_integrity() {
    serial_buffer_t *sbuff = serial_buffer_init(2048);
    assert( NULL != sbuff && "Serial buffer should be created" );

    for (int i=0; i < SERIAL_BUFF_ITEMS; i++) {
        metric_t *m;
        void *str_offset;
        char buffer[STRING_MAX_LEN];
        metric_t str_metric = {.itemid = rand()%STRING_MAX_LEN, .hostid=45623};
        memset(buffer, 'A', STRING_MAX_LEN);
        buffer[str_metric.itemid] = 0;
        zbx_variant_set_str(&str_metric.value, buffer);
        
        ///IMPORTANT!!!! add first all dynamic fields and only after - the item
        ///item's ptr should be considred invalid after any censecutive _add_ call
        
        str_offset = serial_buffer_add_data(sbuff, buffer, strlen(buffer));
        m = serial_buffer_add_item(sbuff, &str_metric, sizeof(metric_t));
        m->value.data.str = str_offset;
             
        
        LOG_INF("Added %d metric string size %d, buffer size is %d, used is %d", i, strlen(buffer) + 1, 
                        serial_buffer_get_size(sbuff), serial_buffer_get_used(sbuff));
        LOG_INF("Buffer is %p",serial_buffer_get_buffer(sbuff));
    }
    LOG_INF("Doing integrity check");
    //checking for integrity
    serial_buffer_process(serial_buffer_get_buffer(sbuff), integrity_check_cb, NULL);
    
    
    
    serial_buffer_destroy(sbuff);

    HALT_HERE("Stop");
}




void glb_ipc2_run_serial_tests() {
  //  run_tests();
    test_serial_buffer_integrity();
    run_buffer_serialisation_test();
    run_many_senders_test(); //this has to be tuned according to situation to emulate the real load
   // run_congestion_test();
    HALT_HERE("Congestion tests finished");
    

}

#endif