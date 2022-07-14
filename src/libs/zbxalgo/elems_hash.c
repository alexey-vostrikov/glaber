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
    
    LOG_INF("Starting while, total elements to delete %d",elems->elems.num_data);
 
    while ( NULL != (elem = (elems_hash_elem_t*)zbx_hashset_iter_next(&iter))) 
        (*elems->elem_free_func)(elem, &elems->memf);
   
    
    zbx_hashset_destroy(&elems->elems);
   
    (*elems->memf.free_func)(elems);
}


//elements are created in and inserted in write lock mode
//then the element remains blocked, but overall locked mode is switched to read mode
//this will not allow others to read inconsistent object
elems_hash_elem_t *create_element(elems_hash_t *elems,  uint64_t id, void *data ) {
    elems_hash_elem_t *elem, elem_local = {.id = id, .data = data };
    
    if (NULL == (elem = zbx_hashset_search(&elems->elems, &elem_local))) {  
        //note - for id - based objects elems' id will hold the meaningfull id
        //however for other items it will hold first eight bytes of the object 
        //intrepreted as id, also  such an objects might redefine the element's id
        //however it will not be searchable by elems_process,  if search by id 
        //is required, then the id shoudl be pregenerated and put into the first 
        //eight bytes or pointer to it should be passed directly
        //elem_local.id = *(u_int64_t*)id;
        glb_lock_init(&elem_local.lock);
        glb_lock_block(&elem_local.lock);
   //     LOG_INF("Calling callback");
        (*elems->elem_create_func)(&elem_local, &elems->memf, data);
    //    LOG_INF("Return from create callback");
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
   // LOG_INF("Locking");
	glb_rwlock_rdlock(&elems->meta_lock);
    //LOG_INF("searhing");
    if (NULL == (elem = zbx_hashset_search(&elems->elems, &id))) {
      //LOG_INF("relock in write mode");
        glb_rwlock_unlock(&elems->meta_lock); //need to relock in write mode 
		
        if ( 1 == (flags & ELEM_FLAG_DO_NOT_CREATE))
            return FAIL;
        glb_rwlock_wrlock(&elems->meta_lock);
        
        //note: create_elem will leave element in blocked state as it needs to be initialized by the user proc first
        //before becoming accessible to other threads
	//	LOG_INF("Creating element");
        if (NULL ==(elem = create_element( elems, id, params ))) {
            glb_rwlock_unlock(&elems->meta_lock);
        	return FAIL;
        }

	//	LOG_INF("Created, re - rdlocking elems");
        glb_rwlock_unlock(&elems->meta_lock);
        glb_rwlock_rdlock(&elems->meta_lock);
	}  else  {
        glb_lock_block(&elem->lock);
    }

	ret = process_func(elem, &elems->memf, params);
        
    if ( 1 == (elem->flags & ELEM_FLAG_DELETE)) {
        //processing func marked the element for deletion
        //we leave element blocked, but reloacking in write mode 
        glb_rwlock_unlock(&elems->meta_lock);
        
        glb_rwlock_wrlock(&elems->meta_lock);
        delete_element(elems, elem);
        glb_rwlock_unlock(&elems->meta_lock);
    
        return ret;
    }
    //this is pretty wrong thing, but for long processing and the processing
    //if (0 == (flags & ELEM_FLAG_REMAIN_LOCKED))
    glb_lock_unlock(&elem->lock);

    glb_rwlock_unlock(&elems->meta_lock);

	return ret;
}


int elems_hash_delete(elems_hash_t *elems, uint64_t id) {
    elems_hash_elem_t *elem;
    int ret = SUCCEED;

    glb_rwlock_wrlock(&elems->meta_lock);
    
    elem = zbx_hashset_search(&elems->elems, &id);
    if (NULL != elem) {
      ret = delete_element(elems, elem);
    } else {
      ret = FAIL;
    }
    
    glb_rwlock_unlock(&elems->meta_lock);
    return ret;

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

//iterator will continue till all data or till proc_func returns SUCCEED
//TODO: implement readlocked version
//this one might be quite expensive

int elems_hash_iterate(elems_hash_t *elems, elems_hash_process_cb_t proc_func, void *params, u_int64_t flags) {
   
    elems_hash_elem_t *elem;
    int last_ret = SUCCEED;
    zbx_hashset_iter_t iter;

    glb_rwlock_rdlock(&elems->meta_lock);

    zbx_hashset_iter_reset(&elems->elems, &iter);
    
    while ( (NULL !=(elem = (elems_hash_elem_t*) zbx_hashset_iter_next(&iter))) &&
            SUCCEED == last_ret ) {
        glb_lock_block(&elem->lock);
        (*proc_func)(elem, &elems->memf, params);
        glb_lock_unlock(&elem->lock);
    }
    
    glb_rwlock_unlock(&elems->meta_lock);
    
}