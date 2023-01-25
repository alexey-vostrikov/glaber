


#include "zbxcommon.h"
#include "zbxalgo.h"

typedef struct {
    u_int64_t orig_itemid;
    u_int64_t conf_itemid;
} ref_item_t;

typedef struct {
    zbx_hashset_t ref_items;
    elems_hash_t items;
    mem_funcs_t *memf;
} config_t;

static config_t *conf = {0};

ELEMS_CREATE(item_create_cb) {

}

ELEMS_FREE(item_free_cb) {

}

void conf_items_init(mem_funcs_t *memf) {
    
    conf = memf->malloc_func(NULL, sizeof(config_t));
    conf->memf = memf;

    zbx_hashset_create_ext(&conf->ref_items, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL, 
                memf->malloc_func, memf->realloc_func, memf->free_func);

    elems_hash_init(memf, item_create_cb, item_free_cb);

}