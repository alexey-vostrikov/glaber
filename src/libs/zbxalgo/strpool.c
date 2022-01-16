
#include "zbxalgo.h"
#include "log.h"

//i'll include string pool (strings interning) copy from db cache
//the reason - to be able to use local proces's string and data interning and 
//keep it in the heap, not in the shared memory which requires locks

#define	REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

static zbx_hashset_t strpool;

static zbx_hash_t	__strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	__strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}


void zbx_heap_strpool_init(){
	zbx_hashset_create(&strpool, 100, __strpool_hash, __strpool_compare);
}

void zbx_heap_strpool_destroy(){
	zbx_hashset_destroy(&strpool);
}


const char	*zbx_heap_strpool_intern(const char *str)
{
	void		*record;
	zbx_uint32_t	*refcount;

	if (NULL == str) 
		return NULL;

	record = zbx_hashset_search(&strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		record = zbx_hashset_insert_ext(&strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);
		*(zbx_uint32_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	(*refcount)++;

	return (void *)record + REFCOUNT_FIELD_SIZE;
}

void	zbx_heap_strpool_release(const char *str)
{
	zbx_uint32_t	*refcount;

	if ( NULL == str ) return;
	//LOG_INF("in heap_strpool_release");
	refcount = (zbx_uint32_t *)((void *)str - REFCOUNT_FIELD_SIZE);
	if ( 0 == refcount) {
	//	LOG_INF("Requested removal of string which has refcount set to 0 already");
		THIS_SHOULD_NEVER_HAPPEN;
	}
	if (0 == --(*refcount)) {
	//	LOG_INF("%s: removing from the strpool", str);
		zbx_hashset_remove(&strpool, (void *)str - REFCOUNT_FIELD_SIZE);
	}
}

const char	*zbx_heap_strpool_acquire(const char *str)
{
	zbx_uint32_t	*refcount;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}