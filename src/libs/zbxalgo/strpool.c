
#include "zbxalgo.h"
#include "log.h"
#include "glb_lock.h"

// i'll include string pool (strings interning) copy from db cache
// the reason - to be able to use local proces's string and data interning and
// keep it in the heap, not in the shared memory which requires locks

#define REFCOUNT_FIELD_SIZE sizeof(zbx_uint32_t)

static zbx_hash_t __strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int __strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}

static const char *__strpool_intern_n(zbx_hashset_t *strpool, const char *str, size_t len)
{
	void *record;
	zbx_uint32_t *refcount;

	if (NULL == str)
		return NULL;

	record = zbx_hashset_search(strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		record = zbx_hashset_insert_ext(strpool, str - REFCOUNT_FIELD_SIZE,
										REFCOUNT_FIELD_SIZE + len + 1, REFCOUNT_FIELD_SIZE);
		*(zbx_uint32_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	(*refcount)++;

	return (void *)record + REFCOUNT_FIELD_SIZE;
}

static const char *__strpool_intern(zbx_hashset_t *strpool, const char *str)
{
	void *record;
	size_t len;
	zbx_uint32_t *refcount;

	if (NULL == str)
		return NULL;

	len = strlen(str);
	
	return __strpool_intern_n(strpool, str, len);
}


static void __strpool_release(zbx_hashset_t *strpool, const char *str)
{
	zbx_uint32_t *refcount;

	if (NULL == str)
		return;

	refcount = (zbx_uint32_t *)((void *)str - REFCOUNT_FIELD_SIZE);
	if (0 == refcount)
	{
		THIS_SHOULD_NEVER_HAPPEN;
	}

	if (0 == --(*refcount))
	{
		zbx_hashset_remove(strpool, (void *)str - REFCOUNT_FIELD_SIZE);
	}
}

static const char *__strpool_acquire(const char *str)
{
	zbx_uint32_t *refcount;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

// mem-based strpool operations
int strpool_init(strpool_t *strpool, mem_funcs_t *memf)
{	
	mem_funcs_t local_memf = { .malloc_func = zbx_default_mem_malloc_func, 
            .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};
    
    if (NULL == memf)
        memf  = &local_memf;

	zbx_hashset_create_ext(&strpool->strs, 100, __strpool_hash, __strpool_compare, NULL,
						   memf->malloc_func, memf->realloc_func, memf->free_func);

	glb_lock_init(&strpool->lock);

	return SUCCEED;
};

int strpool_destroy(strpool_t *strpool)
{
	glb_lock_block(&strpool->lock);
	zbx_hashset_destroy(&strpool->strs);
	return SUCCEED;
}

const char *strpool_add(strpool_t *strpool, const char *str)
{
	const char *ret;
	glb_lock_block(&strpool->lock);
	ret = __strpool_intern(&strpool->strs, str);
	glb_lock_unlock(&strpool->lock);

	return ret;
}

const char *strpool_add_n(strpool_t *strpool, const char *str, size_t len)
{
	const char *ret;
	glb_lock_block(&strpool->lock);
	ret = __strpool_intern_n(&strpool->strs, str, len);
	glb_lock_unlock(&strpool->lock);

	return ret;
}


void strpool_free(strpool_t *strpool, const char *str)
{
	glb_lock_block(&strpool->lock);
	__strpool_release(&strpool->strs, str);
	glb_lock_unlock(&strpool->lock);
}

const char *strpool_replace(strpool_t *strpool, const char *old_str, const char *new_str)
{
	const char *ret;

	glb_lock_block(&strpool->lock);
	__strpool_release(&strpool->strs, old_str);
	ret = __strpool_intern(&strpool->strs, new_str);

	glb_lock_unlock(&strpool->lock);

	return ret;
}

const char *strpool_copy(const char *str)
{
	return __strpool_acquire(str);
}
