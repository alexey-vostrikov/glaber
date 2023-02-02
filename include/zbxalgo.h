/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#ifndef ZABBIX_ZBXALGO_H
#define ZABBIX_ZBXALGO_H

#include "zbxnum.h"
#include "log.h"

/* generic */

typedef zbx_uint32_t zbx_hash_t;

zbx_hash_t	zbx_hash_modfnv(const void *data, size_t len, zbx_hash_t seed);
zbx_hash_t	zbx_hash_splittable64(const void *data);

#define ZBX_DEFAULT_HASH_ALGO		zbx_hash_modfnv
#define ZBX_DEFAULT_PTR_HASH_ALGO	zbx_hash_modfnv
#define ZBX_DEFAULT_UINT64_HASH_ALGO	zbx_hash_modfnv
#define ZBX_DEFAULT_STRING_HASH_ALGO	zbx_hash_modfnv

typedef zbx_hash_t (*zbx_hash_func_t)(const void *data);

zbx_hash_t	zbx_default_ptr_hash_func(const void *data);
zbx_hash_t	zbx_default_string_hash_func(const void *data);
zbx_hash_t	zbx_default_uint64_pair_hash_func(const void *data);

#define ZBX_DEFAULT_HASH_SEED		0

#define ZBX_DEFAULT_PTR_HASH_FUNC		zbx_default_ptr_hash_func
#define ZBX_DEFAULT_UINT64_HASH_FUNC		zbx_hash_splittable64
#define ZBX_DEFAULT_STRING_HASH_FUNC		zbx_default_string_hash_func
#define ZBX_DEFAULT_UINT64_PAIR_HASH_FUNC	zbx_default_uint64_pair_hash_func

typedef int (*zbx_compare_func_t)(const void *d1, const void *d2);

int	zbx_default_int_compare_func(const void *d1, const void *d2);
int	zbx_default_uint64_compare_func(const void *d1, const void *d2);
int	zbx_default_uint64_ptr_compare_func(const void *d1, const void *d2);
int	zbx_default_str_compare_func(const void *d1, const void *d2);
int	zbx_default_str_ptr_compare_func(const void *d1, const void *d2);
int	zbx_natural_str_compare_func(const void *d1, const void *d2);
int	zbx_default_ptr_compare_func(const void *d1, const void *d2);
int	zbx_default_uint64_pair_compare_func(const void *d1, const void *d2);
int	zbx_default_dbl_compare_func(const void *d1, const void *d2);

#define ZBX_DEFAULT_INT_COMPARE_FUNC		zbx_default_int_compare_func
#define ZBX_DEFAULT_UINT64_COMPARE_FUNC		zbx_default_uint64_compare_func
#define ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC	zbx_default_uint64_ptr_compare_func
#define ZBX_DEFAULT_STR_COMPARE_FUNC		zbx_default_str_compare_func
#define ZBX_DEFAULT_STR_PTR_COMPARE_FUNC	zbx_default_str_ptr_compare_func
#define ZBX_DEFAULT_PTR_COMPARE_FUNC		zbx_default_ptr_compare_func
#define ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC	zbx_default_uint64_pair_compare_func
#define ZBX_DEFAULT_DBL_COMPARE_FUNC		zbx_default_dbl_compare_func

typedef void *(*zbx_mem_malloc_func_t)(void *old, size_t size);
typedef void *(*zbx_mem_realloc_func_t)(void *old, size_t size);
typedef void (*zbx_mem_free_func_t)(void *ptr);

typedef struct {
	zbx_mem_malloc_func_t malloc_func;
	zbx_mem_realloc_func_t realloc_func;
	zbx_mem_free_func_t free_func;
} mem_funcs_t;


void	*zbx_default_mem_malloc_func(void *old, size_t size);
void	*zbx_default_mem_realloc_func(void *old, size_t size);
void	zbx_default_mem_free_func(void *ptr);

#define ZBX_DEFAULT_MEM_MALLOC_FUNC	zbx_default_mem_malloc_func
#define ZBX_DEFAULT_MEM_REALLOC_FUNC	zbx_default_mem_realloc_func
#define ZBX_DEFAULT_MEM_FREE_FUNC	zbx_default_mem_free_func

typedef void (*zbx_clean_func_t)(void *data);

#define ZBX_RETURN_IF_NOT_EQUAL(a, b)	\
					\
	if ((a) < (b))			\
		return -1;		\
	if ((a) > (b))			\
		return +1

#define ZBX_RETURN_IF_DBL_NOT_EQUAL(a, b)	\
						\
	if (FAIL == zbx_double_compare(a, b))	\
	{					\
		if ((a) < (b))			\
			return -1;		\
		else				\
			return +1;		\
	}

/* pair */

typedef struct
{
	void	*first;
	void	*second;
}
zbx_ptr_pair_t;

typedef struct
{
	zbx_uint64_t	first;
	zbx_uint64_t	second;
}
zbx_uint64_pair_t;

/* hashset */

#define ZBX_HASHSET_ENTRY_T	struct zbx_hashset_entry_s

ZBX_HASHSET_ENTRY_T
{
	ZBX_HASHSET_ENTRY_T	*next;
	zbx_hash_t		hash;
#if SIZEOF_VOID_P > 4
	/* the data member must be properly aligned on 64-bit architectures that require aligned memory access */
	char			padding[sizeof(void *) - sizeof(zbx_hash_t)];
#endif
	char			data[1];
};

typedef struct
{
	ZBX_HASHSET_ENTRY_T	**slots;
	int			num_slots;
	int			num_data;
	zbx_hash_func_t		hash_func;
	zbx_compare_func_t	compare_func;
	zbx_clean_func_t	clean_func;
	zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;
}
zbx_hashset_t;

#define ZBX_HASHSET_ENTRY_OFFSET	offsetof(ZBX_HASHSET_ENTRY_T, data)

void	zbx_hashset_create(zbx_hashset_t *hs, size_t init_size,
				zbx_hash_func_t hash_func,
				zbx_compare_func_t compare_func);
void	zbx_hashset_create_ext(zbx_hashset_t *hs, size_t init_size,
				zbx_hash_func_t hash_func,
				zbx_compare_func_t compare_func,
				zbx_clean_func_t clean_func,
				zbx_mem_malloc_func_t mem_malloc_func,
				zbx_mem_realloc_func_t mem_realloc_func,
				zbx_mem_free_func_t mem_free_func);
void	zbx_hashset_destroy(zbx_hashset_t *hs);

int	zbx_hashset_reserve(zbx_hashset_t *hs, int num_slots_req);
void	*zbx_hashset_insert(zbx_hashset_t *hs, const void *data, size_t size);
void	*zbx_hashset_insert_ext(zbx_hashset_t *hs, const void *data, size_t size, size_t offset);
void	*zbx_hashset_search(const zbx_hashset_t *hs, const void *data);
void	zbx_hashset_remove(zbx_hashset_t *hs, const void *data);
void	zbx_hashset_remove_direct(zbx_hashset_t *hs, const void *data);

void	zbx_hashset_clear(zbx_hashset_t *hs);

typedef struct
{
	zbx_hashset_t		*hashset;
	int			slot;
	ZBX_HASHSET_ENTRY_T	*entry;
}
zbx_hashset_iter_t;

void	zbx_hashset_iter_reset(zbx_hashset_t *hs, zbx_hashset_iter_t *iter);
void	*zbx_hashset_iter_next(zbx_hashset_iter_t *iter);
void	zbx_hashset_iter_remove(zbx_hashset_iter_t *iter);
void	zbx_hashset_copy(zbx_hashset_t *dst, const zbx_hashset_t *src, size_t size);

/* hashmap */

/* currently, we only have a very specialized hashmap */
/* that maps zbx_uint64_t keys into non-negative ints */

#define ZBX_HASHMAP_ENTRY_T	struct zbx_hashmap_entry_s
#define ZBX_HASHMAP_SLOT_T	struct zbx_hashmap_slot_s

ZBX_HASHMAP_ENTRY_T
{
	zbx_uint64_t	key;
	int		value;
};

ZBX_HASHMAP_SLOT_T
{
	ZBX_HASHMAP_ENTRY_T	*entries;
	int			entries_num;
	int			entries_alloc;
};

typedef struct
{
	ZBX_HASHMAP_SLOT_T	*slots;
	int			num_slots;
	int			num_data;
	zbx_hash_func_t		hash_func;
	zbx_compare_func_t	compare_func;
	zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;
}
zbx_hashmap_t;

void	zbx_hashmap_create(zbx_hashmap_t *hm, size_t init_size);
void	zbx_hashmap_create_ext(zbx_hashmap_t *hm, size_t init_size,
				zbx_hash_func_t hash_func,
				zbx_compare_func_t compare_func,
				zbx_mem_malloc_func_t mem_malloc_func,
				zbx_mem_realloc_func_t mem_realloc_func,
				zbx_mem_free_func_t mem_free_func);
void	zbx_hashmap_destroy(zbx_hashmap_t *hm);

int	zbx_hashmap_get(zbx_hashmap_t *hm, zbx_uint64_t key);
void	zbx_hashmap_set(zbx_hashmap_t *hm, zbx_uint64_t key, int value);
void	zbx_hashmap_remove(zbx_hashmap_t *hm, zbx_uint64_t key);

void	zbx_hashmap_clear(zbx_hashmap_t *hm);

/* binary heap (min-heap) */

/* currently, we only have a very specialized binary heap that can */
/* store zbx_uint64_t keys with arbitrary auxiliary information */

#define ZBX_BINARY_HEAP_OPTION_EMPTY	0
#define ZBX_BINARY_HEAP_OPTION_DIRECT	(1<<0)	/* support for direct update() and remove() operations */

typedef struct
{
	zbx_uint64_t		key;
	const void		*data;
	u_int64_t	local_data;
}
zbx_binary_heap_elem_t;

typedef struct
{
	zbx_binary_heap_elem_t	*elems;
	int			elems_num;
	int			elems_alloc;
	int			options;
	zbx_compare_func_t	compare_func;
	zbx_hashmap_t		*key_index;

	/* The binary heap is designed to work correctly only with memory allocation functions */
	/* that return pointer to the allocated memory or quit. Functions that can return NULL */
	/* are not supported (process will exit() if NULL return value is encountered). If     */
	/* using zbx_shmem_info_t and the associated memory functions then ensure that         */
	/* allow_oom is always set to 0.                                                       */
	zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;
}
zbx_binary_heap_t;

void			zbx_binary_heap_create(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func, int options);
void			zbx_binary_heap_create_ext(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func,
							int options, zbx_mem_malloc_func_t mem_malloc_func,
							zbx_mem_realloc_func_t mem_realloc_func,
							zbx_mem_free_func_t mem_free_func);
void			zbx_binary_heap_destroy(zbx_binary_heap_t *heap);

int			zbx_binary_heap_empty(zbx_binary_heap_t *heap);
zbx_binary_heap_elem_t	*zbx_binary_heap_find_min(zbx_binary_heap_t *heap);
void			zbx_binary_heap_insert(zbx_binary_heap_t *heap, zbx_binary_heap_elem_t *elem);
void			zbx_binary_heap_update_direct(zbx_binary_heap_t *heap, zbx_binary_heap_elem_t *elem);
void			zbx_binary_heap_remove_min(zbx_binary_heap_t *heap);
void			zbx_binary_heap_remove_direct(zbx_binary_heap_t *heap, zbx_uint64_t key);

void			zbx_binary_heap_clear(zbx_binary_heap_t *heap);

/* vector implementation start */

#define ZBX_VECTOR_DECL(__id, __type)										\
														\
typedef struct													\
{														\
	__type			*values;									\
	int			values_num;									\
	int			values_alloc;									\
	zbx_mem_malloc_func_t	mem_malloc_func;								\
	zbx_mem_realloc_func_t	mem_realloc_func;								\
	zbx_mem_free_func_t	mem_free_func;									\
}														\
zbx_vector_ ## __id ## _t;											\
														\
void	zbx_vector_ ## __id ## _create(zbx_vector_ ## __id ## _t *vector);					\
void	zbx_vector_ ## __id ## _create_ext(zbx_vector_ ## __id ## _t *vector,					\
						zbx_mem_malloc_func_t mem_malloc_func,				\
						zbx_mem_realloc_func_t mem_realloc_func,			\
						zbx_mem_free_func_t mem_free_func);				\
void	zbx_vector_ ## __id ## _destroy(zbx_vector_ ## __id ## _t *vector);					\
														\
void	zbx_vector_ ## __id ## _append(zbx_vector_ ## __id ## _t *vector, __type value);			\
void	zbx_vector_ ## __id ## _append_ptr(zbx_vector_ ## __id ## _t *vector, __type *value);			\
void	zbx_vector_ ## __id ## _append_array(zbx_vector_ ## __id ## _t *vector, __type const *values,		\
									int values_num);			\
void	zbx_vector_ ## __id ## _remove_noorder(zbx_vector_ ## __id ## _t *vector, int index);			\
void	zbx_vector_ ## __id ## _remove(zbx_vector_ ## __id ## _t *vector, int index);				\
														\
void	zbx_vector_ ## __id ## _sort(zbx_vector_ ## __id ## _t *vector, zbx_compare_func_t compare_func);	\
void	zbx_vector_ ## __id ## _uniq(zbx_vector_ ## __id ## _t *vector, zbx_compare_func_t compare_func);	\
														\
int	zbx_vector_ ## __id ## _nearestindex(const zbx_vector_ ## __id ## _t *vector, const __type value,	\
									zbx_compare_func_t compare_func);	\
int	zbx_vector_ ## __id ## _bsearch(const zbx_vector_ ## __id ## _t *vector, const __type value,		\
									zbx_compare_func_t compare_func);	\
int	zbx_vector_ ## __id ## _lsearch(const zbx_vector_ ## __id ## _t *vector, const __type value, int *index,\
									zbx_compare_func_t compare_func);	\
int	zbx_vector_ ## __id ## _search(const zbx_vector_ ## __id ## _t *vector, const __type value,		\
									zbx_compare_func_t compare_func);	\
void	zbx_vector_ ## __id ## _setdiff(zbx_vector_ ## __id ## _t *left, const zbx_vector_ ## __id ## _t *right,\
									zbx_compare_func_t compare_func);	\
														\
void	zbx_vector_ ## __id ## _reserve(zbx_vector_ ## __id ## _t *vector, size_t size);			\
void	zbx_vector_ ## __id ## _clear(zbx_vector_ ## __id ## _t *vector);

#define ZBX_PTR_VECTOR_DECL(__id, __type)									\
														\
ZBX_VECTOR_DECL(__id, __type)											\
														\
typedef void (*zbx_ ## __id ## _free_func_t)(__type data);							\
														\
void	zbx_vector_ ## __id ## _clear_ext(zbx_vector_ ## __id ## _t *vector, zbx_ ## __id ## _free_func_t free_func);

ZBX_VECTOR_DECL(uint64, zbx_uint64_t)
ZBX_PTR_VECTOR_DECL(str, char *)
ZBX_PTR_VECTOR_DECL(ptr, void *)
ZBX_VECTOR_DECL(ptr_pair, zbx_ptr_pair_t)
ZBX_VECTOR_DECL(uint64_pair, zbx_uint64_pair_t)
ZBX_VECTOR_DECL(dbl, double)

ZBX_PTR_VECTOR_DECL(tags, zbx_tag_t*)

#define	ZBX_VECTOR_ARRAY_GROWTH_FACTOR	3/2

#define	ZBX_VECTOR_IMPL(__id, __type)										\
														\
static void	__vector_ ## __id ## _ensure_free_space(zbx_vector_ ## __id ## _t *vector)			\
{														\
	if (NULL == vector->values)										\
	{													\
		vector->values_num = 0;										\
		vector->values_alloc = 32;									\
		vector->values = (__type *)vector->mem_malloc_func(NULL, (size_t)vector->values_alloc *		\
				sizeof(__type));								\
	}													\
	else if (vector->values_num == vector->values_alloc)							\
	{													\
		vector->values_alloc = MAX(vector->values_alloc + 1, vector->values_alloc *			\
				ZBX_VECTOR_ARRAY_GROWTH_FACTOR);						\
		vector->values = (__type *)vector->mem_realloc_func(vector->values,				\
				(size_t)vector->values_alloc * sizeof(__type));					\
	}													\
}														\
														\
void	zbx_vector_ ## __id ## _create(zbx_vector_ ## __id ## _t *vector)					\
{														\
	zbx_vector_ ## __id ## _create_ext(vector,								\
						ZBX_DEFAULT_MEM_MALLOC_FUNC,					\
						ZBX_DEFAULT_MEM_REALLOC_FUNC,					\
						ZBX_DEFAULT_MEM_FREE_FUNC);					\
}														\
														\
void	zbx_vector_ ## __id ## _create_ext(zbx_vector_ ## __id ## _t *vector,					\
						zbx_mem_malloc_func_t mem_malloc_func,				\
						zbx_mem_realloc_func_t mem_realloc_func,			\
						zbx_mem_free_func_t mem_free_func)				\
{														\
	vector->values = NULL;											\
	vector->values_num = 0;											\
	vector->values_alloc = 0;										\
														\
	vector->mem_malloc_func = mem_malloc_func;								\
	vector->mem_realloc_func = mem_realloc_func;								\
	vector->mem_free_func = mem_free_func;									\
}														\
														\
void	zbx_vector_ ## __id ## _destroy(zbx_vector_ ## __id ## _t *vector)					\
{														\
	if (NULL != vector->values)										\
	{													\
		vector->mem_free_func(vector->values);								\
		vector->values = NULL;										\
		vector->values_num = 0;										\
		vector->values_alloc = 0;									\
	}													\
														\
	vector->mem_malloc_func = NULL;										\
	vector->mem_realloc_func = NULL;									\
	vector->mem_free_func = NULL;										\
}														\
														\
void	zbx_vector_ ## __id ## _append(zbx_vector_ ## __id ## _t *vector, __type value)				\
{														\
	__vector_ ## __id ## _ensure_free_space(vector);							\
	vector->values[vector->values_num++] = value;								\
}														\
														\
void	zbx_vector_ ## __id ## _append_ptr(zbx_vector_ ## __id ## _t *vector, __type *value)			\
{														\
	__vector_ ## __id ## _ensure_free_space(vector);							\
	vector->values[vector->values_num++] = *value;								\
}														\
														\
void	zbx_vector_ ## __id ## _append_array(zbx_vector_ ## __id ## _t *vector, __type const *values,		\
									int values_num)				\
{														\
	zbx_vector_ ## __id ## _reserve(vector, (size_t)(vector->values_num + values_num));			\
	memcpy(vector->values + vector->values_num, values, (size_t)values_num * sizeof(__type));		\
	vector->values_num = vector->values_num + values_num;							\
}														\
														\
void	zbx_vector_ ## __id ## _remove_noorder(zbx_vector_ ## __id ## _t *vector, int index)			\
{														\
	if (!(0 <= index && index < vector->values_num))							\
	{													\
		zabbix_log(LOG_LEVEL_CRIT, "removing a non-existent element at index %d", index);		\
		exit(EXIT_FAILURE);										\
	}													\
														\
	vector->values[index] = vector->values[--vector->values_num];						\
}														\
														\
void	zbx_vector_ ## __id ## _remove(zbx_vector_ ## __id ## _t *vector, int index)				\
{														\
	if (!(0 <= index && index < vector->values_num))							\
	{													\
		zabbix_log(LOG_LEVEL_CRIT, "removing a non-existent element at index %d", index);		\
		exit(EXIT_FAILURE);										\
	}													\
														\
	vector->values_num--;											\
	memmove(&vector->values[index], &vector->values[index + 1],						\
			sizeof(__type) * (size_t)(vector->values_num - index));					\
}														\
														\
void	zbx_vector_ ## __id ## _sort(zbx_vector_ ## __id ## _t *vector, zbx_compare_func_t compare_func)	\
{														\
	if (2 <= vector->values_num)										\
		qsort(vector->values, (size_t)vector->values_num, sizeof(__type), compare_func);		\
}														\
														\
void	zbx_vector_ ## __id ## _uniq(zbx_vector_ ## __id ## _t *vector, zbx_compare_func_t compare_func)	\
{														\
	if (2 <= vector->values_num)										\
	{													\
		int	i, j = 1;										\
														\
		for (i = 1; i < vector->values_num; i++)							\
		{												\
			if (0 != compare_func(&vector->values[i - 1], &vector->values[i]))			\
				vector->values[j++] = vector->values[i];					\
		}												\
														\
		vector->values_num = j;										\
	}													\
}														\
														\
int	zbx_vector_ ## __id ## _nearestindex(const zbx_vector_ ## __id ## _t *vector, const __type value,	\
									zbx_compare_func_t compare_func)	\
{														\
	int	lo = 0, hi = vector->values_num, mid, c;							\
														\
	while (1 <= hi - lo)											\
	{													\
		mid = (lo + hi) / 2;										\
														\
		c = compare_func(&vector->values[mid], &value);							\
														\
		if (0 > c)											\
		{												\
			lo = mid + 1;										\
		}												\
		else if (0 == c)										\
		{												\
			return mid;										\
		}												\
		else												\
			hi = mid;										\
	}													\
														\
	return hi;												\
}														\
														\
int	zbx_vector_ ## __id ## _bsearch(const zbx_vector_ ## __id ## _t *vector, const __type value,		\
									zbx_compare_func_t compare_func)	\
{														\
	__type	*ptr;												\
														\
	ptr = (__type *)zbx_bsearch(&value, vector->values, (size_t)vector->values_num, sizeof(__type),		\
			compare_func);										\
														\
	if (NULL != ptr)											\
		return (int)(ptr - vector->values);								\
	else													\
		return FAIL;											\
}														\
														\
int	zbx_vector_ ## __id ## _lsearch(const zbx_vector_ ## __id ## _t *vector, const __type value, int *index,\
									zbx_compare_func_t compare_func)	\
{														\
	while (*index < vector->values_num)									\
	{													\
		int	c = compare_func(&vector->values[*index], &value);					\
														\
		if (0 > c)											\
		{												\
			(*index)++;										\
			continue;										\
		}												\
														\
		if (0 == c)											\
			return SUCCEED;										\
														\
		if (0 < c)											\
			break;											\
	}													\
														\
	return FAIL;												\
}														\
														\
int	zbx_vector_ ## __id ## _search(const zbx_vector_ ## __id ## _t *vector, const __type value,		\
									zbx_compare_func_t compare_func)	\
{														\
	int	index;												\
														\
	for (index = 0; index < vector->values_num; index++)							\
	{													\
		if (0 == compare_func(&vector->values[index], &value))						\
			return index;										\
	}													\
														\
	return FAIL;												\
}														\
														\
														\
void	zbx_vector_ ## __id ## _setdiff(zbx_vector_ ## __id ## _t *left, const zbx_vector_ ## __id ## _t *right,\
									zbx_compare_func_t compare_func)	\
{														\
	int	c, block_start, deleted = 0, left_index = 0, right_index = 0;					\
														\
	while (left_index < left->values_num && right_index < right->values_num)				\
	{													\
		c = compare_func(&left->values[left_index], &right->values[right_index]);			\
														\
		if (0 >= c)											\
			left_index++;										\
														\
		if (0 <= c)											\
			right_index++;										\
														\
		if (0 != c)											\
			continue;										\
														\
		if (0 < deleted++)										\
		{												\
			memmove(&left->values[block_start - deleted + 1], &left->values[block_start],		\
					(size_t)(left_index - 1 - block_start) * sizeof(__type));		\
		}												\
														\
		block_start = left_index;									\
	}													\
														\
	if (0 < deleted)											\
	{													\
		memmove(&left->values[block_start - deleted], &left->values[block_start],			\
				(size_t)(left->values_num - block_start) * sizeof(__type));			\
		left->values_num -= deleted;									\
	}													\
}														\
														\
void	zbx_vector_ ## __id ## _reserve(zbx_vector_ ## __id ## _t *vector, size_t size)				\
{														\
	if ((int)size > vector->values_alloc)									\
	{													\
		vector->values_alloc = (int)size;								\
		vector->values = (__type *)vector->mem_realloc_func(vector->values,				\
				(size_t)vector->values_alloc * sizeof(__type));					\
	}													\
}														\
														\
void	zbx_vector_ ## __id ## _clear(zbx_vector_ ## __id ## _t *vector)					\
{														\
	vector->values_num = 0;											\
}

#define	ZBX_PTR_VECTOR_IMPL(__id, __type)									\
														\
ZBX_VECTOR_IMPL(__id, __type)											\
														\
void	zbx_vector_ ## __id ## _clear_ext(zbx_vector_ ## __id ## _t *vector,					\
		zbx_ ## __id ## _free_func_t free_func)								\
{														\
	if (0 != vector->values_num)										\
	{													\
		int	index;											\
														\
		for (index = 0; index < vector->values_num; index++)						\
			free_func(vector->values[index]);							\
														\
		vector->values_num = 0;										\
	}													\
}
/* vector implementation end */

/* this function is only for use with zbx_vector_XXX_clear_ext() */
/* and only if the vector does not contain nested allocations */
void	zbx_ptr_free(void *data);
void	zbx_str_free(char *data);

/* 128 bit unsigned integer handling */
void	zbx_uinc128_64(zbx_uint128_t *base, zbx_uint64_t value);
void	zbx_uinc128_128(zbx_uint128_t *base, const zbx_uint128_t *value);
void	zbx_udiv128_64(zbx_uint128_t *result, const zbx_uint128_t *dividend, zbx_uint64_t value);
void	zbx_umul64_64(zbx_uint128_t *result, zbx_uint64_t value, zbx_uint64_t factor);

unsigned int	zbx_isqrt32(unsigned int value);

/* forecasting */

#define ZBX_MATH_ERROR	-1.0

typedef enum
{
	FIT_LINEAR,
	FIT_POLYNOMIAL,
	FIT_EXPONENTIAL,
	FIT_LOGARITHMIC,
	FIT_POWER,
	FIT_INVALID
}
zbx_fit_t;

typedef enum
{
	MODE_VALUE,
	MODE_MAX,
	MODE_MIN,
	MODE_DELTA,
	MODE_AVG,
	MODE_INVALID
}
zbx_mode_t;

int	zbx_fit_code(char *fit_str, zbx_fit_t *fit, unsigned *k, char **error);
int	zbx_mode_code(char *mode_str, zbx_mode_t *mode, char **error);
double	zbx_forecast(double *t, double *x, int n, double now, double time, zbx_fit_t fit, unsigned k, zbx_mode_t mode);
double	zbx_timeleft(double *t, double *x, int n, double now, double threshold, zbx_fit_t fit, unsigned k);

/* fifo queue of pointers */

typedef struct
{
	void	**values;
	int	alloc_num;
	int	head_pos;
	int	tail_pos;
}
zbx_queue_ptr_t;

#define zbx_queue_ptr_empty(queue)	((queue)->head_pos == (queue)->tail_pos ? SUCCEED : FAIL)

int	zbx_queue_ptr_values_num(zbx_queue_ptr_t *queue);
void	zbx_queue_ptr_reserve(zbx_queue_ptr_t *queue, int num);
void	zbx_queue_ptr_compact(zbx_queue_ptr_t *queue);
void	zbx_queue_ptr_create(zbx_queue_ptr_t *queue);
void	zbx_queue_ptr_destroy(zbx_queue_ptr_t *queue);
void	zbx_queue_ptr_push(zbx_queue_ptr_t *queue, void *value);
void	*zbx_queue_ptr_pop(zbx_queue_ptr_t *queue);
void	zbx_queue_ptr_remove_value(zbx_queue_ptr_t *queue, const void *value);

/* list item data */
typedef struct list_item
{
	struct list_item	*next;
	void			*data;
}
zbx_list_item_t;

/* list data */
typedef struct
{
	zbx_list_item_t		*head;
	zbx_list_item_t		*tail;
	zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;
}
zbx_list_t;

/* queue item data */
typedef struct
{
	zbx_list_t		*list;
	zbx_list_item_t		*current;
	zbx_list_item_t		*next;
}
zbx_list_iterator_t;

void	zbx_list_create(zbx_list_t *queue);
void	zbx_list_create_ext(zbx_list_t *queue, zbx_mem_malloc_func_t mem_malloc_func,
		zbx_mem_free_func_t mem_free_func);
void	zbx_list_destroy(zbx_list_t *list);
void	zbx_list_append(zbx_list_t *list, void *value, zbx_list_item_t **inserted);
void	zbx_list_insert_after(zbx_list_t *list, zbx_list_item_t *after, void *value, zbx_list_item_t **inserted);
void	zbx_list_prepend(zbx_list_t *list, void *value, zbx_list_item_t **inserted);
int	zbx_list_pop(zbx_list_t *list, void **value);
int	zbx_list_peek(const zbx_list_t *list, void **value);
void	zbx_list_iterator_init(zbx_list_t *list, zbx_list_iterator_t *iterator);
int	zbx_list_iterator_next(zbx_list_iterator_t *iterator);
int	zbx_list_iterator_peek(const zbx_list_iterator_t *iterator, void **value);
void	zbx_list_iterator_clear(zbx_list_iterator_t *iterator);
int	zbx_list_iterator_equal(const zbx_list_iterator_t *iterator1, const zbx_list_iterator_t *iterator2);
int	zbx_list_iterator_isset(const zbx_list_iterator_t *iterator);
void	zbx_list_iterator_update(zbx_list_iterator_t *iterator);
void	*zbx_list_iterator_remove_next(zbx_list_iterator_t *iterator);

/*circular (ring) buffer  */
/* implementation is specific to time-series */
typedef struct {
    int size;
    int count;
	int item_size;
	void *data;
    int head;
	int tail;
} glb_tsbuff_t;

typedef struct {
    unsigned int sec;
    u_int64_t value; 
} glb_tsbuff_value_t;



typedef void (*glb_tsbuff_val_free_func_t)(zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func, void* value);
int 	glb_tsbuff_index(glb_tsbuff_t *tsbuff, int index);
int		glb_tsbuff_init(glb_tsbuff_t *rbuff, unsigned int elem_num, size_t elem_size, zbx_mem_malloc_func_t malloc_func);
void	glb_tsbuff_destroy(glb_tsbuff_t *tsbuff, zbx_mem_free_func_t malloc_func);
int 	glb_tsbuff_resize(glb_tsbuff_t *tsbuff, int new_size, zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func, glb_tsbuff_val_free_func_t val_free_func);

int		glb_tsbuff_get_size(glb_tsbuff_t *tsbuff);
void*	glb_tsbuff_add_to_head(glb_tsbuff_t *tsbuff, int time);
void*	glb_tsbuff_add_to_tail(glb_tsbuff_t *tsbuff, int time);
void*	glb_tsbuff_get_value_head(glb_tsbuff_t *tsbuff);
void*	glb_tsbuff_get_value_tail(glb_tsbuff_t *tsbuff);
int		glb_tsbuff_get_count(glb_tsbuff_t *tsbuff);
void*	glb_tsbuff_get_value_ptr(glb_tsbuff_t *tsbuf, int idx);
int 	glb_tsbuff_get_time_head(glb_tsbuff_t *tsbuff);
int		glb_tsbuff_get_time_tail(glb_tsbuff_t *tsbuff);
int 	glb_tsbuff_free_tail(glb_tsbuff_t *tsbuff);
int		glb_tsbuff_is_full(glb_tsbuff_t *tsbuff);

int		glb_tsbuff_find_time_idx(glb_tsbuff_t *tsbuf, int tm_sec);
void	glb_tsbuff_dump(glb_tsbuff_t *tsbuff);

int    glb_tsbuff_check_has_enough_count_data_time(glb_tsbuff_t *tsbuff, int need_count, int time);
int    glb_tsbuff_check_has_enough_count_data_idx(glb_tsbuff_t *tsbuff, int need_count, int head_idx);



#define ELEMS_CALLBACK(name) \
        static int name(elems_hash_elem_t *elem, mem_funcs_t *memf, void *data) 

#define ELEMS_CREATE(name) \
        	int name(elems_hash_elem_t *elem, mem_funcs_t *memf, void *data) 
			
#define ELEMS_FREE(name) \
        	int name(elems_hash_elem_t *elem, mem_funcs_t *memf) 

#define ELEMS_UPDATE(name) \
        	int name(elems_hash_elem_t *elem, elems_hash_elem_t *elem_new, mem_funcs_t *memf) 

/*elements hash for fast and lockless access */
#define ELEM_FLAG_DO_NOT_CREATE  1
#define ELEM_FLAG_DELETE	2
#define ELEM_FLAG_ITER_WRLOCK	4
#define ELEMS_HASH_READ_ONLY 8
#define ELEMS_HASH_WRITE  16

typedef struct {
    u_int64_t id;
	pthread_rwlock_t rw_lock; 
    u_int8_t flags;
	void *data;
} elems_hash_elem_t; 

typedef int	(*elems_hash_create_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf, void *data);
typedef int	(*elems_hash_free_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf);
typedef int	(*elems_hash_process_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf, void *params);
typedef int	(*elems_hash_update_cb_t)(elems_hash_elem_t *elem, elems_hash_elem_t *elem_new, mem_funcs_t *memf);

//todo: hide elems_hash_t structure
typedef struct  {
    zbx_hashset_t elems;
	mem_funcs_t memf;
    pthread_rwlock_t meta_lock; 
	elems_hash_create_cb_t elem_create_func;
	elems_hash_free_cb_t elem_free_func;
} elems_hash_t;

elems_hash_t *elems_hash_init(mem_funcs_t *memf, elems_hash_create_cb_t create_func, elems_hash_free_cb_t elem_free_func );

int		elems_hash_process(elems_hash_t *elems, uint64_t id, elems_hash_process_cb_t process_func, void *data, u_int64_t flags);
int		elems_hash_delete(elems_hash_t *elems,  uint64_t id);
int 	elems_hash_mass_delete(elems_hash_t *elems, zbx_vector_uint64_t *ids);
void	elems_hash_destroy(elems_hash_t *elems);
void	elems_hash_replace(elems_hash_t *old_elems, elems_hash_t *new_elems);
int 	elems_hash_iterate(elems_hash_t *elems, elems_hash_process_cb_t proc_func, void *params, u_int64_t flags);
int 	elems_hash_get_num(elems_hash_t *elems);
int 	elems_hash_id_exists(elems_hash_t *elems, u_int64_t id);
int 	elems_hash_update(elems_hash_t *elems, elems_hash_t *new_elems, elems_hash_update_cb_t update_func_cb);

typedef struct obj_index_t obj_index_t;
obj_index_t* obj_index_init(mem_funcs_t *memf);

void	obj_index_destroy(obj_index_t *idx);

int		obj_index_add_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to);
int 	obj_index_add_ref_nosort(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to); 

int		obj_index_set_refs(obj_index_t* idx, u_int64_t id_from, zbx_vector_uint64_t *refs);

int		obj_index_del_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to);
int		obj_index_del_id_from(obj_index_t* idx, u_int64_t id);
int		obj_index_del_id_to(obj_index_t* idx, u_int64_t id);

int		obj_index_get_refs_to(obj_index_t *idx, u_int64_t id_from, zbx_vector_uint64_t *out_refs);
int 	obj_index_get_refs_from(obj_index_t *idx, u_int64_t id_to, zbx_vector_uint64_t *out_refs);

int		obj_index_replace(obj_index_t *old_idx, obj_index_t *new_idx);
int 	obj_index_update(obj_index_t *old_idx, obj_index_t *new_idx);
void 	obj_index_dump(obj_index_t *idx);

int 	obj_index_get_numdata(obj_index_t *idx);

//memfunction based strpool funcs with lockings to avoid contention
typedef struct {
	zbx_hashset_t strs;
	pthread_mutex_t lock;
} strpool_t;

int 		strpool_init(strpool_t *strpool, mem_funcs_t *memf);
int 		strpool_destroy(strpool_t *strpool);

const char *strpool_add(strpool_t *strpool, const char *str);
const char *strpool_add_n(strpool_t *strpool, const char *str, size_t len);

void 		strpool_free(strpool_t *strpool, const char *str);
const char *strpool_replace(strpool_t *strpool, const char *old_str, const char *new_str);

const char *strpool_copy(const char *str);


typedef struct binpool_t  binpool_t;

binpool_t *	binpool_init( mem_funcs_t *memf);
int 		binpool_destroy(binpool_t *binpool);

const void *binpool_add(binpool_t *binpool, const void *data, size_t len);
void 		binpool_free(binpool_t *binpool, const void *data);

//event queues - interface to add events with callbacks, usefull for async processing
//and timer-based queues, may use shm and locks for parralel forks


typedef struct event_queue_t event_queue_t;

#define EVENT_QUEUE_CALLBACK(name) \
        	static int name(event_queue_t *eq_conf, u_int64_t event_time, int event_id, void *data, mem_funcs_t *memf)

typedef int	(*event_queue_cb_func_t)(event_queue_t *conf, u_int64_t event_time, int event_id, void *data, mem_funcs_t *memf);

event_queue_t *event_queue_init(mem_funcs_t *s_memf);
void event_queue_destroy(event_queue_t *eq_conf);

int event_queue_process_events(event_queue_t *eq_conf, int max_events);
int event_queue_add_event(event_queue_t *eq_conf, u_int64_t msec_time, unsigned char event_id, void *data);
int event_queue_add_callback(event_queue_t *conf, unsigned char callback_id, event_queue_cb_func_t cb_func);

int event_queue_get_events_count(event_queue_t *conf);
u_int64_t event_queue_get_delay(event_queue_t *conf, u_int64_t now);


typedef struct dedup_store_t dedup_store_t;
typedef void* (*dedup_copy_func_t)(void *dest, void* sorce, mem_funcs_t *memf);
typedef void* (*dedup_clean_func_t)(void *data, mem_funcs_t *memf);


#endif /* ZABBIX_ZBXALGO_H */
