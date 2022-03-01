
/****************** GNU GPL2 copyright goes here ***********************/
#include "zbxalgo.h"
#include "glb_lock.h"
#include "log.h"

//this implementation of low locking hashes for string cached data
//it allows simultanios items change
//items add/removal are still required locking, so the algo should be used only
//for slow-changing structure (however elemnt's data might change really fast)

//it works on top of zabbix hashes with per-element locks
//provides several primitives:

//add,update,delete call-back based functions
//and iterator to process all the elements

elems_hash_t *elems_hash_init(mem_funcs_t *memf, elems_hash_create_cb_t create_func, elems_hash_free_cb_t free_func ) {
    
    elems_hash_t *e_hash = (elems_hash_t *) (*memf->malloc_func)(NULL, sizeof(elems_hash_t));  
    
    zbx_hashset_create_ext(&e_hash->elems,10, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL, 
                        memf->malloc_func, memf->realloc_func, memf->free_func); 
    
    e_hash->elem_create_func = create_func;
    e_hash->elem_free_func = free_func;
    
    glb_rwlock_init(&e_hash->meta_lock);
    e_hash->memf = *memf;
  //  LOG_INF("e_hash init memf free is %ld", e_hash->memf.free_func);

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
    //LOG_INF("About to iter init");
    zbx_hashset_iter_reset(&elems->elems, &iter);
    
  //  LOG_INF("Starting while, total elements to delete %d",elems->elems.num_data);
    while ( NULL != (elem = (elems_hash_elem_t*)zbx_hashset_iter_next(&iter))) {
    //    LOG_INF("Colling delete element for elem id %ld", elem->id);
        delete_element(elems, elem);
    //    LOG_INF("Returned from element for elem id %ld", elem->id);
    }
   // LOG_INF("destroy the hash");
    zbx_hashset_destroy(&elems->elems);
   // LOG_INF("free myself, free func addr is %ld", elems->memf.free_func);
    (*elems->memf.free_func)(elems);
  //  LOG_INF("finished");

}


//elements are created in and inserted in write lock mode
//then the element remains blocked, but overall locked mode is switched to read mode
//this will not allow others to read inconsistent object
elems_hash_elem_t *create_element(elems_hash_t *elems,  uint64_t id ) {
    elems_hash_elem_t *elem, elem_local = {0};

    if (NULL == (elem = zbx_hashset_search(&elems->elems, &id))) {  

        elem_local.id = id;
        glb_lock_init(&elem_local.lock);
        glb_lock_block(&elem_local.lock);

        elem = zbx_hashset_insert(&elems->elems,&elem_local,sizeof(elems_hash_elem_t) );

        (*elems->elem_create_func)(elem, &elems->memf);

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
		//LOG_INF("Creating element");
        if (NULL ==(elem = create_element( elems, id  ))) {
            glb_rwlock_unlock(&elems->meta_lock);
        	return FAIL;
        }

		//LOG_INF("Created, re - rdlocking elems");
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


int elems_hash_delete(elems_hash_t *elems, u_int64_t id) {
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
   
    //LOG_INF("in  : destroying old hset, new elem's memf addr is %ld", new_elems->memf.free_func);
    zbx_hashset_t tmp_hset;
    //LOG_INF("in locking old elems");
    glb_rwlock_wrlock(&old_elems->meta_lock);
    //LOG_INF("in locking new elems");
    glb_rwlock_wrlock(&new_elems->meta_lock);

    //LOG_INF("in  copy old hset -> tmp hset");
    memcpy(&tmp_hset, &old_elems->elems, sizeof(zbx_hashset_t));
   // LOG_INF("in : copy new hset -> old hset");
    memcpy(&old_elems->elems, &new_elems->elems,sizeof(zbx_hashset_t));
   // LOG_INF("in  : destroying old hset, new elem's memf addr is %ld", new_elems->memf.free_func);
   // LOG_INF("in : copy tmp hset -> new hset");
    memcpy(&new_elems->elems, &tmp_hset, sizeof(zbx_hashset_t));
    //LOG_INF("in  : destroying old hset, new elem's memf addr is %ld", new_elems->memf.free_func);
    //LOG_INF("in : unlocking old hset");
    glb_rwlock_unlock(&old_elems->meta_lock);
    
    //LOG_INF("in  : destroying old hset, new elem's memf addr is %ld", new_elems->memf.free_func);
    elems_hash_destroy(new_elems);
    //LOG_INF("finished destroy");
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