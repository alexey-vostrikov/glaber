//glb_index is set of methods to manupulate 
// id1->id2 indexes whith back-linking to allow correctly
// id1 ( keys) and id2 ( values )
// it's based on glb_elems implementation to work in shm with as less locking as possible
#include "common.h"
#include "zbxalgo.h"
#include "log.h"

typedef struct {
    zbx_vector_uint64_t refs;
 } ref_id_t;


static int ref_free_callback(elems_hash_elem_t *elem, mem_funcs_t *memf) {
    ref_id_t *ref = (ref_id_t *)elem->data;

    zbx_vector_uint64_destroy(&ref->refs);
    (*memf->free_func)(ref);

    return SUCCEED;
}

static int ref_create_callback(elems_hash_elem_t *elem, mem_funcs_t *memf) {
   
    ref_id_t *ref = (ref_id_t *)(*memf->malloc_func)(NULL,sizeof(ref_id_t));
    elem->data = ref;
    zbx_vector_uint64_create_ext(&ref->refs, memf->malloc_func, memf->realloc_func, memf->free_func);
 
    return SUCCEED;
}

int obj_index_init(obj_index_t *idx, mem_funcs_t *memf) {
        
    if (NULL == idx) 
        return FAIL;
        
    idx->from_to = elems_hash_init(memf, ref_create_callback, ref_free_callback);
    idx->to_from = elems_hash_init(memf, ref_create_callback, ref_free_callback);

    idx->memf = *memf;
    return SUCCEED;
}

void obj_index_destroy(obj_index_t *idx) {

    elems_hash_destroy(idx->from_to);
    elems_hash_destroy(idx->to_from);    
        
    (*idx->memf.free_func)(idx);
}

static int add_ref_callback(elems_hash_elem_t *elem, mem_funcs_t *memf, void *param) {
    
    ref_id_t *ref = (ref_id_t *)elem->data;
    u_int64_t *id_to = (u_int64_t *)param;
  
    zbx_vector_uint64_append(&ref->refs, *id_to);
    zbx_vector_uint64_sort(&ref->refs, ZBX_DEFAULT_UINT64_COMPARE_FUNC);    

}

static int del_ref_callback(elems_hash_elem_t *elem, mem_funcs_t *memf, void *param) {
    int i;
    u_int64_t id_to = *(u_int64_t *)param;
    ref_id_t *ref = (ref_id_t *)elem->data;

    if (FAIL == (i = zbx_vector_uint64_bsearch(&ref->refs, id_to, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
        return SUCCEED;
    
    zbx_vector_uint64_remove(&ref->refs, i);
    
    if (0 == ref->refs.values_num) 
        elem->flags |= ELEM_FLAG_DELETE;
}

static int get_refs_callback(elems_hash_elem_t *elem, mem_funcs_t *memf, void *params) {
    zbx_vector_uint64_t *out_refs = (zbx_vector_uint64_t*) params;
    ref_id_t *ref = (ref_id_t *) elem->data;
    
    zbx_vector_uint64_reserve(out_refs, ref->refs.values_num);
    zbx_vector_uint64_append_array(out_refs, ref->refs.values, ref->refs.values_num);
    
    return SUCCEED;
}

int obj_index_del_id(obj_index_t* idx, u_int64_t id) {
    int i;
    zbx_vector_uint64_t ids;
  
    zbx_vector_uint64_create(&ids);
    
    elems_hash_process(idx->from_to, id, get_refs_callback, &ids, ELEM_FLAG_DO_NOT_CREATE);
     
    for (i = 0; i< ids.values_num; i++) {
        elems_hash_process(idx->to_from, ids.values[i], del_ref_callback, &id, ELEM_FLAG_DO_NOT_CREATE);
    }
 
    zbx_vector_uint64_destroy(&ids);
    elems_hash_delete(idx->from_to, id);
   
    return SUCCEED;
}

int obj_index_add_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to) {
    
    elems_hash_process(idx->from_to, id_from, add_ref_callback, &id_to, 0);
    elems_hash_process(idx->to_from, id_to, add_ref_callback, &id_from, 0);
    
    return SUCCEED;    
}

int obj_index_del_ref(obj_index_t* idx, u_int64_t id_from, u_int64_t id_to) {
    
    elems_hash_process(idx->from_to, id_from, del_ref_callback, &id_to, ELEM_FLAG_DO_NOT_CREATE );
    elems_hash_process(idx->to_from, id_to, del_ref_callback, &id_from, ELEM_FLAG_DO_NOT_CREATE );
    
    return SUCCEED;
}


int obj_index_get_refs_to(obj_index_t *idx, u_int64_t id_from, zbx_vector_uint64_t *out_refs) {
    return  elems_hash_process(idx->from_to, id_from, get_refs_callback, out_refs, ELEM_FLAG_DO_NOT_CREATE);
}

int obj_index_get_refs_from(obj_index_t *idx, u_int64_t id_to, zbx_vector_uint64_t *out_refs) {
    return  elems_hash_process(idx->to_from, id_to, get_refs_callback, out_refs, ELEM_FLAG_DO_NOT_CREATE);
}

int obj_index_clear_index(obj_index_t *idx) {

}

int obj_index_replace(obj_index_t *old_idx, obj_index_t *new_idx) {
   // LOG_INF("Replacing old_idx from to, the new idx memf is %ld", new_idx->memf.free_func);
    elems_hash_replace(old_idx->from_to, new_idx->from_to);
  //  LOG_INF("Replacing old_idx to from");
    elems_hash_replace(old_idx->to_from, new_idx->to_from);
    //after replacenent idx of new will be freed, so we just have to free the struct
    (*new_idx->memf.free_func)(new_idx);
}

static int id_to_vector_dump_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *params) {
    char *str = NULL;
    int i=0;
    
    size_t alloc = 0, offset= 0;
    zbx_vector_uint64_t *vals = (zbx_vector_uint64_t*)elem->data;
    
    zbx_snprintf_alloc(&str,&alloc, &offset,"Key: %ld -> [", elem->id);

    for (i=0; i<vals->values_num; i++) {
        zbx_snprintf_alloc(&str,&alloc, &offset,"%ld, ", vals->values[i]);
    }
    
    if (vals->values_num > 0)  //removing trailing comma and space
        offset = offset - 2;
    
    zbx_snprintf_alloc(&str,&alloc, &offset,"]");
 
    zbx_free(str);
}

void obj_index_dump(obj_index_t *idx) {
    LOG_INF("From -> to dump:");
    elems_hash_iterate(idx->from_to, id_to_vector_dump_cb, NULL);
    LOG_INF("To -> from dump:");
    elems_hash_iterate(idx->to_from, id_to_vector_dump_cb, NULL);
}


/*
static int check_hash_has_ref(zbx_hashset_t *hash, u_int64_t id_from, u_int64_t id_to) {
    int i;
    
    ref_id_t *ref = (ref_id_t *)zbx_hashset_search(hash, &id_from);
    
    if (NULL == ref) 
        return FAIL;
    
    if (FAIL ==(i = zbx_vector_uint64_bsearch(&hash, id_to, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
        return FAIL;
    
    return SUCCEED;
}

int obj_index_check_if_refers(obj_index_t *idx, u_int64_t id_from, u_int64_t id_to) {
    return check_hash_has_ref(idx->from_to, id_from, id_to);
}

int obj_index_check_if_referedby(obj_index_t *idx, u_int64_t id_to, u_int64_t id_from) {
    return check_hash_has_ref(idx->to_from, id_to, id_from);
}

*/