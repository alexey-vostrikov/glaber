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




#include "glb_common.h"

#ifdef HAVE_GLB_TESTS

#include "zbxshmem.h"
#include "zbxalgo.h"

static  zbx_shmem_info_t	*shmtest_mem;
ZBX_SHMEM_FUNC_IMPL(__shmtest, shmtest_mem);

static u_int64_t allocated_size;

static mem_funcs_t test_memf = {.free_func = __shmtest_shmem_free_func, 
        .malloc_func= __shmtest_shmem_malloc_func,
        .realloc_func = __shmtest_shmem_realloc_func};

mem_funcs_t * tests_mem_allocate_shmem(u_int64_t size) {
    char *error = NULL;
    if (SUCCEED != zbx_shmem_create(&shmtest_mem, size, "State cache size", "TestMemSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	exit(EXIT_FAILURE);
    }
    allocated_size = shmtest_mem->free_size;
    return &test_memf;
}
    
int test_mem_release_shmem() {
    
    if (allocated_size != shmtest_mem->free_size)
        LOG_INF("There was %ld bytes unfreed, total size is %ld", allocated_size -  shmtest_mem->free_size , allocated_size );

    assert (allocated_size == shmtest_mem->free_size && "All mem should be freed prior to release");
  
    zbx_shmem_destroy(shmtest_mem);
}

#endif