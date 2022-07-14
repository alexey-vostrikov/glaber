/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#include "common.h"

/* generic */

typedef zbx_uint32_t zbx_hash_t;

zbx_hash_t	zbx_hash_lookup2(const void *data, size_t len, zbx_hash_t seed);
zbx_hash_t	zbx_hash_modfnv(const void *data, size_t len, zbx_hash_t seed);
zbx_hash_t	zbx_hash_murmur2(const void *data, size_t len, zbx_hash_t seed);
zbx_hash_t	zbx_hash_sdbm(const void *data, size_t len, zbx_hash_t seed);
zbx_hash_t	zbx_hash_djb2(const void *data, size_t len, zbx_hash_t seed);
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
int	zbx_natural_str_compare_func(const void *d1, const void *d2);
int	zbx_default_ptr_compare_func(const void *d1, const void *d2);
int	zbx_default_uint64_pair_compare_func(const void *d1, const void *d2);

#define ZBX_DEFAULT_INT_COMPARE_FUNC		zbx_default_int_compare_func
#define ZBX_DEFAULT_UINT64_COMPARE_FUNC		zbx_default_uint64_compare_func
#define ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC	zbx_default_uint64_ptr_compare_func
#define ZBX_DEFAULT_STR_COMPARE_FUNC		zbx_default_str_compare_func
#define ZBX_DEFAULT_PTR_COMPARE_FUNC		zbx_default_ptr_compare_func
#define ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC	zbx_default_uint64_pair_compare_func

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

int	is_prime(int n);
int	next_prime(int n);

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
void	*zbx_hashset_search(zbx_hashset_t *hs, const void *data);
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
	/* using zbx_mem_info_t and the associated memory functions then ensure that allow_oom */
	/* is always set to 0.                                                                 */
	zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;
}
zbx_binary_heap_t;

void			zbx_binary_heap_create(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func, int options);
void			zbx_binary_heap_create_ext(zbx_binary_heap_t *heap, zbx_compare_func_t compare_func, int options,
							zbx_mem_malloc_func_t mem_malloc_func,
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

/* vector */

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

/* this function is only for use with zbx_vector_XXX_clear_ext() */
/* and only if the vector does not contain nested allocations */
void	zbx_ptr_free(void *data);
void	zbx_str_free(char *data);

/* 128 bit unsigned integer handling */
#define uset128(base, hi64, lo64)	(base)->hi = hi64; (base)->lo = lo64

void	uinc128_64(zbx_uint128_t *base, zbx_uint64_t value);
void	uinc128_128(zbx_uint128_t *base, const zbx_uint128_t *value);
void	udiv128_64(zbx_uint128_t *result, const zbx_uint128_t *dividend, zbx_uint64_t value);
void	umul64_64(zbx_uint128_t *result, zbx_uint64_t value, zbx_uint64_t factor);

unsigned int	zbx_isqrt32(unsigned int value);

char	*zbx_gen_uuid4(const char *seed);

/* expression evaluation */

#define ZBX_INFINITY	(1.0 / 0.0)	/* "Positive infinity" value used as a fatal error code */
#define ZBX_UNKNOWN	(-1.0 / 0.0)	/* "Negative infinity" value used as a code for "Unknown" */

#define ZBX_UNKNOWN_STR		"ZBX_UNKNOWN"	/* textual representation of ZBX_UNKNOWN */
#define ZBX_UNKNOWN_STR_LEN	ZBX_CONST_STRLEN(ZBX_UNKNOWN_STR)

int	evaluate(double *value, const char *expression, char *error, size_t max_error_len,
		zbx_vector_ptr_t *unknown_msgs);
int	evaluate_unknown(const char *expression, double *value, char *error, size_t max_error_len);
double	evaluate_string_to_double(const char *in);

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
void	zbx_list_create_ext(zbx_list_t *queue, zbx_mem_malloc_func_t mem_malloc_func, zbx_mem_free_func_t mem_free_func);
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


/*elements hash for fast and lockless access */
#define ELEM_FLAG_DO_NOT_CREATE  1
#define ELEM_FLAG_DELETE	2
#define ELEM_FLAG_ITER_WRLOCK	4
//#define ELEM_FLAG_REMAIN_LOCKED  8

typedef struct {
    u_int64_t id;
    pthread_mutex_t lock;
    u_int8_t flags;
	void *data;
} elems_hash_elem_t; 

typedef int	(*elems_hash_create_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf, void *data);
typedef int	(*elems_hash_free_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf);
typedef int	(*elems_hash_process_cb_t)(elems_hash_elem_t *elem, mem_funcs_t *memf, void *params);

typedef struct  {
    zbx_hashset_t elems;
	mem_funcs_t memf;
    pthread_rwlock_t meta_lock; 
	elems_hash_create_cb_t elem_create_func;
	elems_hash_free_cb_t elem_free_func;
} elems_hash_t;

elems_hash_t *elems_hash_init(mem_funcs_t *memf, elems_hash_create_cb_t create_func, elems_hash_free_cb_t elem_free_func );
//elems_hash_t *elems_hash_init_ext(mem_funcs_t *memf, 
//                    elems_hash_create_cb_t create_func, elems_hash_free_cb_t free_func,
//                    zbx_compare_func_t compare_func, zbx_hash_func_t hash_func);
int		elems_hash_process(elems_hash_t *elems, uint64_t id, elems_hash_process_cb_t process_func, void *data, u_int64_t flags);
int		elems_hash_delete(elems_hash_t *elems,  uint64_t id);
void	elems_hash_destroy(elems_hash_t *elems);
void	elems_hash_replace(elems_hash_t *old_elems, elems_hash_t *new_elems);
int 	elems_hash_iterate(elems_hash_t *elems, elems_hash_process_cb_t proc_func, void *params, u_int64_t flags);

typedef struct obj_index_t obj_index_t;
obj_index_t* obj_index_init(mem_funcs_t *memf);

void	obj_index_destroy(obj_index_t *idx);
int		obj_index_add_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to);
int		obj_index_del_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to);
int		obj_index_del_id_from(obj_index_t* idx, u_int64_t id);
int		obj_index_del_id_to(obj_index_t* idx, u_int64_t id);
int		obj_index_get_refs_to(obj_index_t *idx, u_int64_t id_from, zbx_vector_uint64_t *out_refs);
int 	obj_index_get_refs_from(obj_index_t *idx, u_int64_t id_to, zbx_vector_uint64_t *out_refs);
int		obj_index_replace(obj_index_t *old_idx, obj_index_t *new_idx);
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


#endif
