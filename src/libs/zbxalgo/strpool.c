
#include "zbxalgo.h"
#include "log.h"
#include "glb_lock.h"

//i'll include string pool (strings interning) copy from db cache
//the reason - to be able to use local proces's string and data interning and 
//keep it in the heap, not in the shared memory which requires locks

#define	REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

static zbx_hashset_t strpool_local;

static zbx_hash_t	__strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	__strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}


void zbx_heap_strpool_init(){
	zbx_hashset_create(&strpool_local, 100, __strpool_hash, __strpool_compare);
}

void zbx_heap_strpool_destroy(){
	zbx_hashset_destroy(&strpool_local);
}


static const char	*__strpool_intern(zbx_hashset_t *strpool, const char *str)
{
	void		*record;
	zbx_uint32_t	*refcount;

	if (NULL == str) 
		return NULL;

	record = zbx_hashset_search(strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		record = zbx_hashset_insert_ext(strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);
		*(zbx_uint32_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	(*refcount)++;

	return (void *)record + REFCOUNT_FIELD_SIZE;
}

const char	*zbx_heap_strpool_intern(const char *str)  {
	return __strpool_intern(&strpool_local,str);
}

static void	__strpool_release(zbx_hashset_t *strpool, const char *str)
{
	zbx_uint32_t	*refcount;

	if ( NULL == str ) return;
	
	refcount = (zbx_uint32_t *)((void *)str - REFCOUNT_FIELD_SIZE);
	if ( 0 == refcount) {
		THIS_SHOULD_NEVER_HAPPEN;
	}
	
	if (0 == --(*refcount)) {
		zbx_hashset_remove(strpool, (void *)str - REFCOUNT_FIELD_SIZE);
	}
}

void	zbx_heap_strpool_release(const char *str) {
	__strpool_release(&strpool_local, str);
}

static const char	*__strpool_acquire(const char *str)
{
	zbx_uint32_t	*refcount;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

const char	*zbx_heap_strpool_acquire(const char *str) {
	return __strpool_acquire(str);
}

//mem-based strpool operations
int strpool_init(strpool_t *strpool, mem_funcs_t *memf) {
	
	zbx_hashset_create_ext(&strpool->strs, 100, __strpool_hash, __strpool_compare, NULL, 
		memf->malloc_func, memf->realloc_func, memf->free_func);
	
	glb_lock_init(&strpool->lock);
	
	return SUCCEED;
};

int  strpool_destroy(strpool_t *strpool) {
	glb_lock_block(&strpool->lock);
	zbx_hashset_destroy(&strpool->strs);	
	return SUCCEED;
}

const char *strpool_add(strpool_t *strpool, const char *str) {
	const char *ret;
	glb_lock_block(&strpool->lock);
	ret = __strpool_intern(&strpool->strs,str);
	glb_lock_unlock(&strpool->lock);
	
	return ret;
}	

void  strpool_free(strpool_t *strpool, const char *str) {
	glb_lock_block(&strpool->lock);
	__strpool_release(&strpool->strs, str);
	glb_lock_unlock(&strpool->lock);
}

const char *strpool_copy(char *str) {
	return __strpool_acquire(str);
}


