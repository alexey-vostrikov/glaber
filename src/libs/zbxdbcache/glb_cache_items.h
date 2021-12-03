#ifndef GLB_CACHE_ITEMS_H
#define GLB_CACHE_ITEMS_H

#include "glb_cache.h"

#define GLB_CACHE_MIN_ITEM_VALUES 10
#define GLB_CACHE_ITEM_DEMAND_UPDATE 86400

void *glb_cache_items_init();

int glb_cache_item_create_cb(glb_cache_elem_t *elem, void *items_cfg);
int glb_cache_item_update_meta_cb(glb_cache_elem_t *elem, void *cb_data);
int glb_cache_item_get_meta_cb(glb_cache_elem_t *elem, void *cb_data);
int glb_cache_item_get_state_cb(glb_cache_elem_t *elem, void *cb_data);
int glb_cache_item_get_nextcheck_cb(glb_cache_elem_t *elem, void *cb_data);
int glb_cache_item_values_json_cb(glb_cache_elem_t *elem, void *cb_data);
int glb_cache_items_marshall_item(glb_cache_elem_t *elem, struct zbx_json* json);

int  glb_cache_add_item_values(void *cfg_data, glb_cache_elems_t *elems, ZBX_DC_HISTORY *history, int history_num);

#define ZBX_DC_FLAGS_NOT_FOR_HISTORY	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)
int glb_cache_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);

int glb_cache_get_item_values_by_time(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type,
     zbx_vector_history_record_t *values, int ts_start, int ts_end);

int glb_cache_get_item_values_by_count(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type, 
    zbx_vector_history_record_t *values, int count, int ts_end);

glb_cache_item_meta_t* glb_cache_get_item_meta(u_int64_t itemid);

#endif