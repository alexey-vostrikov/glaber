/*
** Copyright Glaber 2018-2023
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


#include "zbxcommon.h"
#include "zbxalgo.h"
#include "conf_items.h"

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



/*Deprecated at-writing funcs: temp interface to config cache for the objects not in Glaber Config cache yet*/
int DC_conf_items_get_valuemap_info(u_int64_t itemid, glb_conf_item_valuemap_info_t *vm_info);
int DC_conf_items_get_valuetype(u_int64_t itemid);
u_int64_t DC_conf_item_get_hostid(u_int64_t itemid);
char *DC_conf_item_get_name(u_int64_t itemid);
char *DC_conf_item_get_key(u_int64_t itemid);
int DC_conf_item_get_triggerids(u_int64_t itemid, zbx_vector_uint64_t *triggerids);


int glb_conf_items_get_valuemap_info(u_int64_t itemid, glb_conf_item_valuemap_info_t *vm_info) {
    return DC_conf_items_get_valuemap_info(itemid, vm_info);
}

int glb_conf_items_get_valuetype(u_int64_t itemid) {
    return DC_conf_items_get_valuetype(itemid);
}

 u_int64_t glb_conf_item_get_hostid(u_int64_t itemid) {
    return DC_conf_item_get_hostid(itemid);
 }

 char* glb_conf_item_get_key(u_int64_t itemid) {
    return DC_conf_item_get_key(itemid);
 }
 
 char* glb_conf_item_get_name(u_int64_t itemid) {
    return DC_conf_item_get_name(itemid);
 }

int glb_conf_item_get_triggerids(u_int64_t itemid, zbx_vector_uint64_t *triggerids) {
    return DC_conf_item_get_triggerids(itemid, triggerids);
}