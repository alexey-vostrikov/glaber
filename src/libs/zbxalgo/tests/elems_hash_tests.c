/*
** Glaber
** Copyright (C) 2018-2023 Glaber
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
#include "zbxshmem.h"
#include "zbxalgo.h"

static  zbx_shmem_info_t	*shmtest_mem;
ZBX_SHMEM_FUNC_IMPL(__shmtest, shmtest_mem);

static mem_funcs_t test_memf = {.free_func = __shmtest_shmem_free_func, 
        .malloc_func= __shmtest_shmem_malloc_func,
        .realloc_func = __shmtest_shmem_realloc_func};

#define TEST_MEM_SIZE 1 * ZBX_GIBIBYTE
#define TEST_RECORDS 20
static void init(void) {
    char *error = NULL;
    if (SUCCEED != zbx_shmem_create(&shmtest_mem, TEST_MEM_SIZE, "State cache size", "TestMemSize", 0, &error)) {
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
    LOG_INF("test %s SUCCEED", __func__);
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
    LOG_INF("test %s SUCCEED", __func__);
}

//#define TEST_RECORDS 1000000
void test_elem_hash_remove_unexistant() {
    u_int64_t was_size = shmtest_mem->free_size;
    zbx_vector_uint64_t ids;
    zbx_vector_uint64_create(&ids);
    zbx_vector_uint64_reserve(&ids, TEST_RECORDS);

    elems_hash_t *ehash =  elems_hash_init(&test_memf,create_cb, free_cb); 
    LOG_INF("Creating 1M hashset and array");

    for(int i=0; i < TEST_RECORDS; i++) {
        elems_hash_process(ehash, i,  test_cb, NULL, 0);
        zbx_vector_uint64_append(&ids, i);
    }
    
    LOG_INF("There are %d records in the hash, %d records in the vector", ehash->elems.num_data, ids.values_num);
    LOG_INF("Mem used: %ld", was_size - shmtest_mem->free_size);
    LOG_INF("Removing unexistent");

    elems_hash_remove_absent_in_vector(ehash, &ids);
    LOG_INF("There are %d records in the hash, %d records in the vector", ehash->elems.num_data, ids.values_num);
    assert(ehash->elems.num_data == TEST_RECORDS && "All records should remain");

    //now lets remove half of the records
    zbx_vector_uint64_clear(&ids);
    
    for(int i=0; i < TEST_RECORDS / 2 ; i++)
            zbx_vector_uint64_append(&ids, i);
    LOG_INF("There are %d records in the hash, %d records in the vector", ehash->elems.num_data, ids.values_num);
    
    elems_hash_remove_absent_in_vector(ehash, &ids);
    assert(ehash->elems.num_data == TEST_RECORDS / 2 && "All records should remain");
    LOG_INF("There are %d records in the hash, %d records in the vector", ehash->elems.num_data, ids.values_num);
    elems_hash_destroy(ehash);
    assert(was_size == shmtest_mem->free_size && "Check no leak happened");
}

void   tests_elems_hash_run(void) {
    LOG_INF("%s Running TESTS", __func__);
    init();
    test_elem_hash_remove_unexistant();
    test_create_delete_leak();
    test_records_create_del_leak();

    LOG_INF("Finished elems hash tests");
}