/*
** Copyright Glaber
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
#include "../glb_state/load_dump.h"

struct index_uint64_t {
    elems_hash_t *index;
};

ELEMS_CREATE(index_create_cb) {
    zbx_vector_uint64_t *vector = memf->malloc_func(NULL, sizeof(zbx_vector_uint64_t));
    elem->data = vector;
    zbx_vector_uint64_create_ext(vector,memf->malloc_func, memf->realloc_func, memf->free_func);
    return SUCCEED;
}

ELEMS_FREE(index_free_cb) {
    zbx_vector_uint64_t *vector = elem->data;
    zbx_vector_uint64_destroy(vector);
    memf->free_func(vector);
    elem->data = NULL;
    return SUCCEED;
}

index_uint64_t *index_uint64_init(mem_funcs_t *memf) {
    index_uint64_t *index = memf->malloc_func(NULL, sizeof(index_uint64_t));
    index->index = elems_hash_init(memf, index_create_cb, index_free_cb);
    return index;
}

void index_uint64_destroy(index_uint64_t *index, mem_funcs_t *memf) {
    elems_hash_destroy(index->index);
    memf->free_func(index);
}

int index_get_keys_num(index_uint64_t *index) {
    return elems_hash_get_num(index->index);
}

ELEMS_CALLBACK(index_add_key_value_cb) {
    zbx_vector_uint64_t *vector = elem->data;
    zbx_vector_uint64_append(vector, *(u_int64_t*)data);
    zbx_vector_uint64_sort(vector, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    return SUCCEED;    
} 

int  index_uint64_add(index_uint64_t *index, u_int64_t key, u_int64_t value) {
    if ( 0 == value || 0 == key)
        return FAIL;
    return elems_hash_process(index->index, key, index_add_key_value_cb, &value, 0);
}

ELEMS_CALLBACK(index_get_values_cb) {
    zbx_vector_uint64_t *values = data;
    zbx_vector_uint64_t *in_values = elem->data;

    zbx_vector_uint64_append_array(values, in_values->values, in_values->values_num);
    
    return SUCCEED;
}

int index_uint64_get(index_uint64_t *index, u_int64_t key, zbx_vector_uint64_t *values) {
    if (NULL == index || 0 == key || NULL == values)
        return FAIL;
    
    return elems_hash_process(index->index, key, index_get_values_cb, values, ELEM_FLAG_DO_NOT_CREATE);
}

int index_uint64_get_keys_values(index_uint64_t *index, zbx_vector_uint64_t *ids, zbx_vector_uint64_t *values) {
    int i;
    for (i = 0; i < ids->values_num; i++) 
        elems_hash_process(index->index, ids->values[i], index_get_values_cb, values,  ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(index_remove_value_cb) {
    int val_index;
    zbx_vector_uint64_t *values = elem->data;
    
    if (FAIL == (val_index = zbx_vector_uint64_bsearch(values, *(u_int64_t *)data, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
        return FAIL;
    
    zbx_vector_uint64_remove_noorder(values, val_index);
    
    if (0 == values->values_num)
        elem->flags |= ELEM_FLAG_DELETE;

    return SUCCEED;
}

int index_uint64_del(index_uint64_t *index, u_int64_t key, u_int64_t value) {
    if (NULL == index || 0 == key || 0 == value)
        return FAIL;
    
    return elems_hash_process(index->index, key, index_remove_value_cb, &value, ELEM_FLAG_DO_NOT_CREATE);
}

int index_uint64_del_key(index_uint64_t *index, u_int64_t key) {
    return elems_hash_delete(index->index, key);
}
ELEMS_CALLBACK(index_get_keynum_cb) {
    return ((zbx_vector_uint64_t *)elem->data)->values_num;
}

int index_uint64_get_count_by_key(index_uint64_t *index, u_int64_t key) {
    return elems_hash_process(index->index, key, index_get_keynum_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

int index_uint64_get_keys_num(index_uint64_t *index) {
    return elems_hash_get_num(index->index);
}

DUMPER_TO_JSON(index_dump_cb) {
    int i;
    zbx_vector_uint64_t *ids = data;

    zbx_json_addint64(json, "itemid", id);
    zbx_json_addarray(json, "keys");

    for (i =0 ; i < ids->values_num ; i++);
        zbx_json_addint64(json, NULL, ids->values[i]);
    zbx_json_close(json);    
}

int index_uint64_dump(index_uint64_t *index, char *filename) {
    state_dump_objects(index->index, filename, index_dump_cb);
}

ELEMS_CALLBACK(check_values_present_cb) {
    int i = 0;
    zbx_vector_uint64_t *ids = elem->data;
    elems_hash_t *objects = data;

    while  ( i < ids->values_num ) {
        if (FAIL == elems_hash_id_exists(objects, ids->values[i])) {
            LOG_INF("Removing id %ld from the array of %d values", ids->values[i], ids->values_num);
            zbx_vector_uint64_remove(ids, i);
            continue;
        }
        i++;
    }
}

int index_uint64_sync_objects(index_uint64_t *index, elems_hash_t *objects) {
    return elems_hash_iterate(index->index, check_values_present_cb, objects, ELEM_FLAG_ITER_WRLOCK);
}

#ifdef HAVE_GLB_TESTS

elems_hash_t * index_uint64_get_elem_hash(index_uint64_t *index) {
    return index->index;
}
#endif

//int  index_uint64_add_pair_values(index_uint64_t *index, u_int64_t key, zbx_vector_uint64_t *values);
//int  index_uint64_add_keys_value(index_uint64_t *index, zbx_vector_uint64_t *keys, u_int64_t value );
//int index_uint64_check_value_exists(index_uint64_t *index, u_int64_t key, u_int64_t value);

