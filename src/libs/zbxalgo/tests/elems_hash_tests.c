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

#include "log.h"
#include "memalloc.h"
#include "zbxalgo.h"

static  zbx_mem_info_t	*shmtest_mem;
ZBX_MEM_FUNC_IMPL(__shmtest, shmtest_mem);

mem_funcs_t test_memf = {.free_func = __shmtest_mem_free_func, .malloc_func= __shmtest_mem_malloc_func,
        .realloc_func = __shmtest_mem_realloc_func};

#define TEST_MEM_SIZE 1 * ZBX_GIBIBYTE
#define TEST_RECORDS 20
void init(void) {
    char *error = NULL;
    if (SUCCEED != zbx_mem_create(&shmtest_mem, TEST_MEM_SIZE, "State cache size", "TestMemSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	exit(EXIT_FAILURE);
    }

}
ELEMS_CREATE(create_cb) {
    elem->data = memf->malloc_func(NULL, 256);

}
ELEMS_FREE(free_cb) {
    memf->free_func(elem->data);
    elem->data = NULL;
}

ELEMS_CALLBACK(test_cb) {
    
}

void test_create_delete_leak() {
    LOG_INF("running test %s", __func__);
    u_int64_t was_size = shmtest_mem->free_size;
    elems_hash_t *ehash =  elems_hash_init(&test_memf,create_cb, free_cb); 
    elems_hash_destroy(ehash);
//    for(int i=1; i< TEST_RECORDS; i++) {
//        elems_hash_process(ehash, i,  test_cb, NULL, 0);
//    }
    if (was_size != shmtest_mem->free_size) {
        HALT_HERE("Empty elems_hash create/remove test FAILED");
    }
    LOG_INF("test %s SUCCEDED", __func__);
}

void test_records_create_del_leak() {
    LOG_INF("running test %s", __func__);
    
    u_int64_t was_size = shmtest_mem->free_size;
    elems_hash_t *ehash =  elems_hash_init(&test_memf,create_cb, free_cb); 
    

    for(int i=0; i< TEST_RECORDS; i++) {
        elems_hash_process(ehash, i,  test_cb, NULL, 0);
    }

    elems_hash_destroy(ehash);
    sleep(1);

    if (was_size != shmtest_mem->free_size) {
        HALT_HERE("%s test FAILED: memory difference is %d", __func__, was_size - shmtest_mem->free_size);
    }
    LOG_INF("test %s SUCCEDED", __func__);
}


void   tests_elems_hash_run(void) {
    LOG_INF("%s Running TESTS", __func__);
    init();
    test_create_delete_leak();
    test_records_create_del_leak();
    LOG_INF("Finished elems hash tests");
}