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

#include "../tags.h"
#include "zbxalgo.h"

static void test_create_delete() {
    u_int64_t was_size = shmtest_mem->free_size;
    tags_t *t_set = tags_create_ext(&test_memf);
  //  LOG_INF("Diff is %lld %lld", was_size,shmtest_mem->free_size);
    assert( shmtest_mem->free_size != was_size && "Assert some shmem has been used");
    tag_t tag = {.tag = "tag", .value = "value"};
    tag_t tag2 = {.tag = "tag1", .value = "value2"};
    tag_t tag3 = {.tag = "tag1", .value = "value3"};
//    LOG_INF("ewrgwrtgwt1, t_set is %p", t_set);
 //   LOG_INF("Diff is %lld %lld", was_size,shmtest_mem->free_size);
    assert(FAIL == tags_add_tag_ext(t_set, NULL, &test_memf) && "Assert FAILS on null tag add");
//    LOG_INF("ewrgwrtgwt2, t_set is %p", t_set);
//    LOG_INF("Diff is %lld %lld", was_size,shmtest_mem->free_size);
    assert(SUCCEED  == tags_add_tag_ext(t_set, &tag, &test_memf) && "Assert SUCCEED on real tag add");
//    LOG_INF("ewrgwrtgwt3");
 //   LOG_INF("Diff is %lld %lld", was_size,shmtest_mem->free_size);
    assert(FAIL  == tags_add_tag_ext(t_set, &tag, &test_memf) && "Assert FAILS on existing tag add");
 //   LOG_INF("After fail Diff is %lld %lld", was_size,shmtest_mem->free_size);
//    tags_add_tag(tags, &tag);
  //  LOG_INF("Adding tag2");
    assert(SUCCEED == tags_add_tag_ext(t_set, &tag2, &test_memf) && "Assert SUCCEED on new tag add");
    assert(SUCCEED == tags_add_tag_ext(t_set, &tag3, &test_memf) && "Assert SUCCEED on existing tag with new value add");
    
    assert(SUCCEED == tags_del_tags_by_tag_ext(t_set, "tag", &test_memf) && "Removal existing tag SUCCEED");
    assert(SUCCEED == tags_del_tags_by_tag_ext(t_set, "tag1", &test_memf) && "Removal existing tags should SUCCEED");
    assert(FAIL == tags_del_tags_by_tag_ext(t_set, "tag4", &test_memf) && "Removal nonexisting tags should FAIL");
    
    //testing removal by tag and value
    assert(SUCCEED  == tags_add_tag_ext(t_set, &tag, &test_memf) && "Assert SUCCEED on real tag add");
    assert(SUCCEED  == tags_add_tag_ext(t_set, &tag2, &test_memf) && "Assert SUCCEED on real tag add");
    assert(SUCCEED  == tags_add_tag_ext(t_set, &tag3, &test_memf) && "Assert SUCCEED on real tag add");

    assert(SUCCEED == tags_del_tags_ext(t_set, &tag2, &test_memf) && "Removal existing tags should SUCCEED");
    assert(SUCCEED  == tags_add_tag_ext(t_set, &tag2, &test_memf) && "Assert SUCCEED on deleted tag re-add");
    assert(FAIL == tags_add_tag_ext(t_set, &tag2, &test_memf) && "Assert FAIL on already added tag");
    assert(FAIL  == tags_add_tag_ext(t_set, &tag3, &test_memf) && "Assert FAIL on already added tag");

    //some search tests
    assert(FAIL == tags_search_by_tag(t_set, "non-existing-tag") && "Assert FAIL search non existing tag");
    assert(SUCCEED == tags_search_by_tag(t_set, "tag1") && "Assert SUCCEED search for existing tag");
    assert(SUCCEED == tags_search(t_set, &tag3) && "Assert SUCCEED search for existing tag");
    tag3.value = "non-existent value";
    assert(FAIL == tags_search(t_set, &tag3) && "Assert FAIL search for existing tag, but not existing value");

    tags_destroy_ext(t_set, &test_memf);
    assert( shmtest_mem->free_size == was_size && "Assert all memory released");
}

void glb_tags_tests_run() {
    sleep(1);
    init();
    LOG_INF("Doing tag sets tests");
    test_create_delete();
    
    tags_t *tags = tags_create_ext(&test_memf);

    LOG_INF("Finished tag sets tests");
    HALT_HERE("Intentional halt");
}

#endif