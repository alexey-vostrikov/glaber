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

#include "log.h"

#include "zbxalgo.h"
#include "glb_common.h"
#include "../conf.h"
#include "zbxshmem.h"
#include "zbxjson.h"

static  zbx_shmem_info_t	*shmtest_mem;
ZBX_SHMEM_FUNC_IMPL(__shmtest, shmtest_mem);
#define TEST_MEM_SIZE  10 * ZBX_MEBIBYTE

static mem_funcs_t test_memf = {.free_func = __shmtest_shmem_free_func, 
        .malloc_func= __shmtest_shmem_malloc_func,
        .realloc_func = __shmtest_shmem_realloc_func};

#ifdef HAVE_GLB_TESTS

static void init(void) {
    char *error = NULL;
    if (SUCCEED != zbx_shmem_create(&shmtest_mem, TEST_MEM_SIZE, "State cache size", "TestMemSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	exit(EXIT_FAILURE);
    }
}

static char *test_json = "{ \"some_key\":\"some_val\", \
    \"test_array\":\
    [   {\"pair1\":\"value\", \"pair2\":\"efefdee\"},\
        {\"test3\":\"dfsfswfw\", \"test4\":\"sfefwefwe\"}\
    ]}";

//typedef void (*glb_conf_array_free_func_cb_t)(void *data, mem_funcs_t *memf, strpool_t *strpool);
//typedef int (*glb_conf_array_create_func_cb_t)(void *data, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool);

typedef struct {
    char *pair1;
    char *pair2;
} test_pairs_t;

CONF_ARRAY_CREATE_FROM_JSON_CB(record_create_cb){
    test_pairs_t *pairs = data;


    if ( SUCCEED == glb_conf_add_json_param_memf(jp, memf, "pair1", &pairs->pair1) && 
         SUCCEED == glb_conf_add_json_param_memf(jp, memf, "pair2", &pairs->pair2) )
        return SUCCEED;

    LOG_INF("Failed to parse object");
    pairs->pair1 = NULL;
    pairs->pair2 = NULL;

    return FAIL;
}

CONF_ARRAY_FREE_CB(record_free_cb) {
    test_pairs_t *pairs = data;

    if ( NULL !=pairs->pair1)
         memf->free_func(pairs->pair1);
    
    if ( NULL !=pairs->pair2)
        memf->free_func(pairs->pair2);
}


void test_conf_read_free() {
    LOG_INF("Testing iteration: %s", test_json);
    struct zbx_json_parse jp;
    void *array_ptr;
    int count = 0;
    strpool_t *strpool;
    
    u_int64_t was_size = shmtest_mem->free_size;

    assert(SUCCEED == zbx_json_open(test_json, &jp) && "test json open should open ok");
    count = glb_conf_create_array_from_json(&array_ptr, "test_array", &jp, 23, &test_memf, strpool, record_create_cb);
    assert(2 == count && "two test objects are parsed");
    glb_conf_free_json_array(array_ptr, count, 23, &test_memf, strpool, record_free_cb);
    assert(was_size == shmtest_mem->free_size && "All memory has been released");

}

void conf_tests_run() {
    init();
    test_conf_read_free();
    HALT_HERE("glb_conf_tests completed");
}

#endif