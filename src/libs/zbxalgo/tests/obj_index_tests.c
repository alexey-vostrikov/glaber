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

#define TEST_MEM_SIZE 1 * ZBX_GIBIBYTE
#define TEST_RECORDS 1000
#define TEST_ELEMS 50

static int test_shm_leak(void) {
    char *error = NULL;
    LOG_INF("Starting shared memory leak test");
    
    if (SUCCEED != zbx_mem_create(&shmtest_mem, TEST_MEM_SIZE, "State cache size", "TestMemSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	exit(EXIT_FAILURE);
    }
    mem_funcs_t memf = {.free_func = __shmtest_mem_free_func, .malloc_func= __shmtest_mem_malloc_func,
        .realloc_func = __shmtest_mem_realloc_func};
    
    unsigned long was_free = shmtest_mem->free_size;
    
    LOG_INF("Done init obj index, cache free space is %ld", was_free);
    
    obj_index_t *idx = obj_index_init(&memf);
   
    LOG_INF("Adding %d records", TEST_RECORDS); 
    
    int i;
    
    for (i = 0; i< TEST_RECORDS; i++) {
        obj_index_add_ref(idx, i, (rand()+1) % (TEST_RECORDS+2) );
    }
   
   LOG_INF("After adding records free space is %ld", shmtest_mem->free_size);
 
   
    obj_index_destroy(idx);
    LOG_INF("After removing records free space is %ld", shmtest_mem->free_size);

    if (was_free !=  shmtest_mem->free_size) {
        LOG_INF("Obj index add/remove leak is detected, %d bytes is lost, TEST FAILED",
            was_free - shmtest_mem->free_size );
        
        exit(EXIT_FAILURE);
    } else 
        LOG_INF("Records create/delete TEST OK");

    idx = obj_index_init(&memf);
   
    LOG_INF("Adding1 %d records", TEST_RECORDS); 
    
    for (i = 0; i< TEST_RECORDS; i++) {
        obj_index_add_ref(idx,i,i);
    }    

    obj_index_t *idx2 = obj_index_init(&memf);
   
    LOG_INF("Adding2 %d records", TEST_RECORDS); 
    
    for (i = TEST_RECORDS; i< TEST_RECORDS*2; i++) {
        obj_index_add_ref(idx2,i,i);
    }    
    obj_index_replace(idx,idx2);
    obj_index_destroy(idx);

    if (was_free !=  shmtest_mem->free_size) {
        
        LOG_INF("Obj replace leak is detected, %d bytes is lost, TEST FAILED",
            was_free - shmtest_mem->free_size );
        
        exit(EXIT_FAILURE);
    } else 
        LOG_INF("Records replace TEST OK");
    LOG_INF("Testing obj_index_update");
    
    obj_index_t *idx_shmem = obj_index_init(&memf);
    obj_index_t *idx_local = obj_index_init(NULL);
    
    for (i = 0; i< TEST_RECORDS; i++) {
        obj_index_add_ref(idx_local,i,i*23);
    }    
    LOG_INF("Updating 1");
    obj_index_update(idx_shmem, idx_local);
    LOG_INF("Updating 1 finished");
    LOG_INF("Updating 1 -same data");

    obj_index_update(idx_shmem, idx_local);
    LOG_INF("Updating 1 -same data - finished");

    obj_index_destroy(idx_local);
    idx_local = obj_index_init(NULL);

    for (i = 0; i< TEST_RECORDS; i++) {
        obj_index_add_ref(idx_local,i,i*2);
    }    
    LOG_INF("Updating 2");
    obj_index_update(idx_shmem, idx_local);
    LOG_INF("Updating 2 - finished");
    
    obj_index_destroy(idx_local);
    obj_index_destroy(idx_shmem);
    
    if (was_free != shmtest_mem->free_size) {
        
        LOG_INF("Obj update leak is detected, %d bytes is lost, TEST FAILED",
            was_free - shmtest_mem->free_size );
        
        exit(EXIT_FAILURE);
    } else 
        LOG_INF("Records update TEST OK");
    
    LOG_INF("Finished testing obj_index_update");
    LOG_INF("Finished shared memory leak test");
}

void tests_obj_index_run() {
    LOG_INF("Running obj index tests");
    test_shm_leak();
    LOG_INF("Finished obj index tests");
}