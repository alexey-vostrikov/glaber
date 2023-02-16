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

#ifdef HAVE_GLB_TESTS

static  zbx_shmem_info_t	*shmtest_mem;
ZBX_SHMEM_FUNC_IMPL(__shmtest, shmtest_mem);

static mem_funcs_t test_memf = {.free_func = __shmtest_shmem_free_func, 
        .malloc_func= __shmtest_shmem_malloc_func,
        .realloc_func = __shmtest_shmem_realloc_func};

#define TEST_MEM_SIZE 1 * ZBX_GIBIBYTE

static void init(void) {
    char *error = NULL;
    if (SUCCEED != zbx_shmem_create(&shmtest_mem, TEST_MEM_SIZE, "State cache size", "TestMemSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	exit(EXIT_FAILURE);
    }

}

void check_create_destroy_leak() {
    u_int64_t was_size = shmtest_mem->free_size;
    index_uint64_t *index = index_uint64_init(&test_memf);
    index_uint64_destroy(index, &test_memf);
    LOG_INF("Was size %ld, free size %ld", was_size, shmtest_mem->free_size );
    assert(shmtest_mem->free_size == was_size && "Create/deastroy shouldn't leak memory");
}
void check_mass_add_remove_keys_leak() {
    u_int64_t was_size = shmtest_mem->free_size;
    index_uint64_t *index = index_uint64_init(&test_memf);
    LOG_INF("Adding 1M key-values");
    for (int i=0; i< 1000000; i++) {
        u_int64_t key = rand(), value = rand();
        index_uint64_add(index, key, value);
    }
    LOG_INF("Added 1M key-values, total keys %d, used mem size is %d", index_get_keys_num(index), was_size -  shmtest_mem->free_size );
    assert( 10000< index_get_keys_num(index) && "There shuld be thousands of keys in the index");
    index_uint64_destroy(index, &test_memf);
    LOG_INF("Was size %ld, free size %ld", was_size, shmtest_mem->free_size );
    assert(shmtest_mem->free_size == was_size && "Create/destroy shouldn't leak memory");
}

void check_store_and_get() {
    u_int64_t was_size = shmtest_mem->free_size;
    index_uint64_t *index = index_uint64_init(&test_memf);

    assert(FAIL == index_uint64_add(index, 12345, 0) && "Zero value insert fails");
    assert(FAIL == index_uint64_add(index, 0, 134210) && "Zero key insert fails");
    
    assert(SUCCEED == index_uint64_add(index, 12345, 72) && "Normal insert SUCCEEDs");
    assert(SUCCEED == index_uint64_add(index, 12345, 172) && "Normal insert SUCCEEDs to the same key");
    
    zbx_vector_uint64_t values;
    zbx_vector_uint64_create(&values);

    assert(FAIL == index_uint64_get(NULL, 123, &values) && "get values with null index object should FAIL");
    assert(FAIL == index_uint64_get(index, 123, NULL) && "get values with null vector object should FAIL");
    assert(FAIL == index_uint64_get(index, 0, NULL) && "get values with zero key should FAIL");

    assert( SUCCEED == index_uint64_get(index, 12345, &values) && "get values should SUCCEED");
    assert(values.values_num == 2 && "make sure got 2 values back");
    zbx_vector_uint64_sort(&values, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);
    assert(values.values[0] == 72 && values.values[1] == 172 && "make sure we've got the exact data we've inserted");

    assert(FAIL == index_uint64_del(NULL, 1,1) && "remove should fail on null index object");
    assert(FAIL == index_uint64_del(index, 2341,1) && "remove should fail on non-existent index");
    assert(FAIL == index_uint64_del(index, 12345, 1345) && "remove should fail on existent index but non-existent value");

    assert(SUCCEED == index_uint64_del(index, 12345, 72) && "remove should SUCCEED on existent index and existent value");
    zbx_vector_uint64_clear(&values);
    assert(SUCCEED == index_uint64_get(index,12345,&values) && "data fetch should succeed, one value should remain");
    assert(values.values_num == 1 && values.values[0] == 172 && "check for exact remaining data : one value == 172");

    assert(SUCCEED == index_uint64_del(index, 12345, 172) && "delete last element should succeed");
    assert(FAIL == index_uint64_get(index,12345,&values) && "data fetch should FAIL, as key muste be deleted on the last element removal");
   
    LOG_INF("Creating index for 10к rand numbers");
    for (int i = 0; i< 10000; i++) {
        assert(SUCCEED == index_uint64_add(index, 5678, rand()) && "Index add should succeed");
    }
    
    LOG_INF("Created index for 10к rand numbers");
    assert(10000 == index_uint64_get_count_by_key(index, 5678) && "there should 10k values in the resulting index");
    assert(FAIL == index_uint64_get_count_by_key(index, 3489539) && "there should be  FAIL on index count for non-existent key");
    assert(SUCCEED == index_uint64_del_key(index, 5678) && "Huge key delete should succeed");
    index_uint64_destroy(index, &test_memf);
    index_uint64_t *index1, *index2;
    index1 = index_uint64_init(&test_memf);
    index2 = index_uint64_init(&test_memf);
    //use index 1 as object index 
    index_uint64_add(index1, 1, 2);
    index_uint64_add(index1, 101, 2);

    //now index 1 poins to both elemenths in the first index
    index_uint64_add(index2, 1, 1);
    index_uint64_add(index2, 1, 101);
    index_uint64_add(index2, 1, 105); //this element should be cleared
    elems_hash_t *index1_ehash =  index_uint64_get_elem_hash(index1);

    index_uint64_sync_objects(index2, index1_ehash);
    assert( index_uint64_get_count_by_key(index2, 1) == 2 && "One value should be removed");

    index_uint64_sync_objects(index2, index1_ehash);
    assert( index_uint64_get_count_by_key(index2, 1) == 2 && "Nothing should change after the second time");

    index_uint64_destroy(index1,&test_memf);
    index_uint64_destroy(index2,&test_memf);
    assert(shmtest_mem->free_size == was_size && "All is deleted, there should be no memory leaks");
}

void check_destroy_is_clean() {
    u_int64_t was_size = shmtest_mem->free_size;
    index_uint64_t *index = index_uint64_init(&test_memf);

    LOG_INF("Adding 10k key-values");
    for (int i=0; i< 100000; i++) {
        u_int64_t key = rand() % 1000 , value = rand();
        index_uint64_add(index, key, value);
    }
    LOG_INF("Creating index for 1к rand numbers");
    for (int i = 0; i< 1000; i++) {
        assert(SUCCEED == index_uint64_add(index, 5678, rand()) && "Index add should succeed");
    }
    
    index_uint64_destroy(index, &test_memf);
    assert(shmtest_mem->free_size == was_size && "All is deleted, there should be no memory leaks"); 
}


void   tests_index_uint64_run(void) {
    LOG_INF("%s Running index TESTS", __func__);
    init();
    check_create_destroy_leak();
    check_mass_add_remove_keys_leak();
    check_store_and_get();
    check_destroy_is_clean();

    LOG_INF("Finished index tests");
}
#endif