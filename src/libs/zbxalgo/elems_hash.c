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
#include "zbxalgo.h"
#include "glb_lock.h"
#include "log.h"

#define elem_lock(elem, flags) \
     if ((flags) & ELEMS_HASH_READ_ONLY) glb_rwlock_rdlock(&(elem)->rw_lock); else glb_rwlock_wrlock(&(elem)->rw_lock);

#define elem_unlock(elem) \
      glb_rwlock_unlock(&(elem)->rw_lock); 

elems_hash_t *elems_hash_init(mem_funcs_t *memf, elems_hash_create_cb_t create_func, elems_hash_free_cb_t free_func ) {
    
    elems_hash_t *e_hash = (elems_hash_t *) (*memf->malloc_func)(NULL, sizeof(elems_hash_t));  
    
    zbx_hashset_create_ext(&e_hash->elems, 0, ZBX_DEFAULT_UINT64_HASH_FUNC,
            ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL, 
                            memf->malloc_func, memf->realloc_func, memf->free_func); 
  
    e_hash->elem_create_func = create_func;
    e_hash->elem_free_func = free_func;
    
    glb_rwlock_init(&e_hash->meta_lock);
    e_hash->memf = *memf;
     return e_hash;
}

static int delete_element(elems_hash_t *elems, elems_hash_elem_t *elem) {
    
    int ret = (*elems->elem_free_func)(elem, &elems->memf);
    zbx_hashset_remove_direct(&elems->elems, elem);
    
    return ret;
}

void elems_hash_destroy(elems_hash_t *elems) {
    
    zbx_hashset_iter_t iter;
    elems_hash_elem_t *elem;
   
    zbx_hashset_iter_reset(&elems->elems, &iter);
    
    while ( NULL != (elem = (elems_hash_elem_t*)zbx_hashset_iter_next(&iter))) 
        (*elems->elem_free_func)(elem, &elems->memf);
   
    
    zbx_hashset_destroy(&elems->elems);
   
    (*elems->memf.free_func)(elems);
}

elems_hash_elem_t *create_element(elems_hash_t *elems,  uint64_t id, void *data ) {
    elems_hash_elem_t *elem, elem_local = {.id = id, .data = data };
    
    if (NULL == (elem = zbx_hashset_search(&elems->elems, &elem_local))) {  

        glb_rwlock_init(&elem_local.rw_lock);
        glb_rwlock_wrlock(&elem_local.rw_lock);

        (*elems->elem_create_func)(&elem_local, &elems->memf, data);

        elem = zbx_hashset_insert(&elems->elems,&elem_local,sizeof(elems_hash_elem_t) );
    
    } else {
        elem = NULL;
    }

    return elem;
}

int elems_hash_process(elems_hash_t *elems, uint64_t id, elems_hash_process_cb_t process_func, void *params, u_int64_t flags) {
    
    int ret;
    elems_hash_elem_t *elem;
    
	if (NULL == process_func)
		return FAIL;

	glb_rwlock_rdlock(&elems->meta_lock);

    if (NULL == (elem = zbx_hashset_search(&elems->elems, &id))) {

        glb_rwlock_unlock(&elems->meta_lock);
		
        if ( 1 == (flags & ELEM_FLAG_DO_NOT_CREATE))
            return FAIL;

        glb_rwlock_wrlock(&elems->meta_lock);

        if (NULL == ( elem = create_element( elems, id, params ))) {
            glb_rwlock_unlock(&elems->meta_lock);
        	return FAIL;
        }
	
        glb_rwlock_unlock(&elems->meta_lock);
        glb_rwlock_rdlock(&elems->meta_lock);

	}  else  
        elem_lock(elem, flags);

	ret = process_func(elem, &elems->memf, params);
        
    if ( 0 < (elem->flags & ELEM_FLAG_DELETE)) {

        glb_rwlock_unlock(&elems->meta_lock);
        glb_rwlock_wrlock(&elems->meta_lock);
        
        delete_element(elems, elem);
        
        glb_rwlock_unlock(&elems->meta_lock);
        return ret;
    }

    elem_unlock(elem);
    glb_rwlock_unlock(&elems->meta_lock);

	return ret;
}

int elems_hash_delete(elems_hash_t *elems, uint64_t id) {
    elems_hash_elem_t *elem;
    int ret = SUCCEED;

    glb_rwlock_wrlock(&elems->meta_lock);
    
    elem = zbx_hashset_search(&elems->elems, &id);
    if (NULL != elem) 
      ret = delete_element(elems, elem);
    else 
      ret = FAIL;
    
    glb_rwlock_unlock(&elems->meta_lock);
    return ret;
}

int elems_hash_mass_delete(elems_hash_t *elems, zbx_vector_uint64_t *ids) {
    elems_hash_elem_t *elem;
    int i, count = 0;

    if (0 == ids->values_num)
        return 0;

    glb_rwlock_wrlock(&elems->meta_lock);

    for (i = 0; i<ids->values_num; i++) {

        if (NULL != (elem = zbx_hashset_search(&elems->elems, &ids->values[i])));
        {
            LOG_INF("Deleting element id %ld", elem->id);
            delete_element(elems, elem);
            count++;
        }
    }

    glb_rwlock_unlock(&elems->meta_lock);
    return count;
}

ELEMS_CALLBACK(update_process_func_cb) {
    zbx_ptr_pair_t *params = data;
    elems_hash_update_cb_t cb_func = params->second;

    cb_func(params->first, elem, memf);
}

ELEMS_CALLBACK(addnew_func_cb) {
    zbx_ptr_pair_t *params = data;
    elems_hash_update_cb_t cb_func = params->second;

    cb_func(elem, params->first, memf);
}

ELEMS_CALLBACK(update_delete_func_cb) {
    zbx_ptr_pair_t *params = data, 
                    proc_params = {.first = elem, .second = params->second};
    elems_hash_t *new_elems = params->first;

    if (FAIL == elems_hash_id_exists(new_elems, elem->id)) {
        elem->flags = ELEM_FLAG_DELETE;
        return SUCCEED;
    }

    elems_hash_process(new_elems, elem->id, update_process_func_cb, &proc_params, 0);
}

ELEMS_CALLBACK(add_new_func_cb) {
    zbx_ptr_pair_t *params = data;
    elems_hash_t *elems = params->first;
    zbx_ptr_pair_t proc_params = {.first = elem, .second = params->second};

    if (FAIL == elems_hash_id_exists(elems, elem->id)) {
        elems_hash_process(elems, elem->id, addnew_func_cb, &proc_params, 0);
    }
}

int elems_hash_update(elems_hash_t *elems, elems_hash_t *new_elems, elems_hash_update_cb_t update_func_cb) {
    zbx_ptr_pair_t params = {.first = new_elems, .second = update_func_cb};
    elems_hash_iterate(elems, update_delete_func_cb, &params, ELEMS_HASH_WRITE);
    params.first = elems;
    elems_hash_iterate(new_elems, add_new_func_cb, &params, ELEMS_HASH_WRITE);
}

void elems_hash_replace(elems_hash_t *old_elems, elems_hash_t *new_elems) {
   
    zbx_hashset_t tmp_hset;
    
    glb_rwlock_wrlock(&old_elems->meta_lock);
    glb_rwlock_wrlock(&new_elems->meta_lock);
    memcpy(&tmp_hset, &old_elems->elems, sizeof(zbx_hashset_t));
    memcpy(&old_elems->elems, &new_elems->elems,sizeof(zbx_hashset_t));
    memcpy(&new_elems->elems, &tmp_hset, sizeof(zbx_hashset_t));
    glb_rwlock_unlock(&old_elems->meta_lock);
    elems_hash_destroy(new_elems);
  
}   

int elems_hash_iterate(elems_hash_t *elems, elems_hash_process_cb_t proc_func, void *params,
     u_int64_t element_lock_flags ) {
   
    elems_hash_elem_t *elem;
    int last_ret = SUCCEED;
    zbx_hashset_iter_t iter;

    glb_rwlock_rdlock(&elems->meta_lock);

    zbx_hashset_iter_reset(&elems->elems, &iter);
    
    while ( (NULL !=(elem = (elems_hash_elem_t*) zbx_hashset_iter_next(&iter))) &&
            SUCCEED == last_ret ) {
        elem_lock(elem,  element_lock_flags);
        (*proc_func)(elem, &elems->memf, params);
        elem_unlock(elem);

        if ( 0 < (elem->flags & ELEM_FLAG_DELETE)) {

            glb_rwlock_unlock(&elems->meta_lock);
            glb_rwlock_wrlock(&elems->meta_lock);
        
            delete_element(elems, elem);
        
            glb_rwlock_unlock(&elems->meta_lock);
            glb_rwlock_rdlock(&elems->meta_lock);
        }
    }
    
    glb_rwlock_unlock(&elems->meta_lock);
    
}

int  elems_hash_get_num(elems_hash_t *elems) {
    int num_data = 0;
    glb_rwlock_rdlock(&elems->meta_lock);
    num_data = elems->elems.num_data;
    glb_rwlock_unlock(&elems->meta_lock);

    return num_data;
}

int elems_hash_id_exists(elems_hash_t *elems, u_int64_t id) {
    int ret = FAIL;

    glb_rwlock_rdlock(&elems->meta_lock);
    
    if (NULL != zbx_hashset_search(&elems->elems, &id))
        ret = SUCCEED;

    glb_rwlock_unlock(&elems->meta_lock);
    return ret;
}