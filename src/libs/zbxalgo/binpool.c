/*
** Glaber
** Copyright (C) 2001-2100
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

#include "zbxalgo.h"
#include "log.h"
#include "glb_lock.h"

#define HEADER_SIZE 2 * sizeof(zbx_uint32_t)
#define SIZE_OFFSET sizeof(zbx_uint32_t)

struct binpool_t{
	zbx_hashset_t data;
	pthread_mutex_t lock;
};

static zbx_hash_t __binpool_hash(const void *data)
{	u_int32_t *size = (u_int32_t*) data + SIZE_OFFSET;
	
	return ZBX_DEFAULT_STRING_HASH_ALGO(data + HEADER_SIZE, *size , ZBX_DEFAULT_HASH_SEED);
}

static int __binpool_compare(const void *d1, const void *d2)
{
	u_int32_t *size1 = (u_int32_t*) d1 + SIZE_OFFSET, *size2 = (u_int32_t*)d2 + SIZE_OFFSET;

	if (*size1 != *size2) 
		return (ZBX_DEFAULT_INT_COMPARE_FUNC(size1, size2));
	
	return memcmp((char *)d1 + HEADER_SIZE, (char *)d2 + HEADER_SIZE, *size1 );
}

static void *__binpool_intern_n(zbx_hashset_t *binpool, const char *str, size_t len)
{
	void *record;
	zbx_uint32_t *refcount;
	zbx_uint32_t *size;

	if (NULL == str)
		return NULL;

	record = zbx_hashset_search(binpool, str - HEADER_SIZE);

	if (NULL == record)
	{
		record = zbx_hashset_insert_ext(binpool, str - HEADER_SIZE,
										HEADER_SIZE + len, HEADER_SIZE);
		*(zbx_uint64_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	size = (zbx_uint32_t*)record + SIZE_OFFSET;

	(*refcount)++;
	*size = len;

	return (void *)record + HEADER_SIZE;
}

static void __binpool_release(zbx_hashset_t *binpool, const void *str)
{
	zbx_uint32_t *refcount;

	if (NULL == str)
		return;

	refcount = (zbx_uint32_t *)((void *)str - HEADER_SIZE);
	if (0 == refcount)
	{
		THIS_SHOULD_NEVER_HAPPEN;
	}

	if (0 == --(*refcount))
	{
		zbx_hashset_remove(binpool, (void *)str - HEADER_SIZE);
	}
}

static const char *__binpool_acquire(const char *str)
{
	zbx_uint32_t *refcount;

	refcount = (zbx_uint32_t *)(str - HEADER_SIZE);
	(*refcount)++;

	return str;
}

binpool_t *binpool_init( mem_funcs_t *memf)
{
	binpool_t *binpool = memf->malloc_func(NULL, sizeof(binpool_t));

	zbx_hashset_create_ext(&binpool->data, 100, __binpool_hash, __binpool_compare, NULL,
						   memf->malloc_func, memf->realloc_func, memf->free_func);

	glb_lock_init(&binpool->lock);

	return SUCCEED;
};

int binpool_destroy(binpool_t *binpool)
{
	glb_lock_block(&binpool->lock);
	zbx_hashset_destroy(&binpool->data);
	return SUCCEED;
}

const void * binpool_add(binpool_t *binpool, const void *data, size_t len)
{
	const char *ret;
	glb_lock_block(&binpool->lock);
	ret = __binpool_intern_n(&binpool->data, data, len);
	glb_lock_unlock(&binpool->lock);

	return ret;
}

void binpool_free(binpool_t *binpool, const void* data)
{
	glb_lock_block(&binpool->lock);
	__binpool_release(&binpool->data, data);
	glb_lock_unlock(&binpool->lock);
}

const char *binpool_copy(const char *str)
{
	return __binpool_acquire(str);
}
