#ifndef GLB_CACHE_ITEMS_H
#define GLB_CACHE_ITEMS_H

#include "glb_cache.h"



//TODO: avoid flags, they are puzzling, do clear function calls instead
#define  GLB_CACHE_ITEM_UPDATE_LASTDATA     0x01
#define  GLB_CACHE_ITEM_UPDATE_NEXTCHECK    0x02
#define  GLB_CACHE_ITEM_UPDATE_STATE        0x04
#define  GLB_CACHE_ITEM_UPDATE_ERRORMSG     0x08
#define  GLB_CACHE_ITEM_UPDATE_ERRORCODE    0x10

#define ZBX_DC_FLAGS_NOT_FOR_HISTORY (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)

typedef struct {
    int state;
    int lastdata;
    int nextcheck;
    const char *error;
    int errcode;
} glb_cache_item_meta_t;

int glb_cache_items_init(mem_funcs_t *memf);

int glb_ic_add_values(ZBX_DC_HISTORY *history, int history_num);
int glb_ic_get_values(u_int64_t itemid, int value_type, zbx_vector_history_record_t *values, int ts_start, int count, int ts_end);
int glb_cache_item_update_meta(u_int64_t itemid, glb_cache_item_meta_t *meta, unsigned int flags, int value_type);
int glb_cache_item_get_nextcheck(u_int64_t itemid);
int glb_cache_item_update_nextcheck(u_int64_t itemid, int nextcheck);
int glb_cache_item_get_state(u_int64_t itemid);
int glb_cache_items_get_state_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);

int glb_vc_load_items_cache();
int glb_vc_dump_cache();

int glb_cache_get_items_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);
int glb_cache_get_items_status_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);
void glb_cache_get_item_stats(zbx_vector_ptr_t *stats);
int glb_cache_get_item_state(u_int64_t itemid);
int glb_cache_get_values_by_count(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int count, int ts_end);
int glb_cache_get_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds, int ts_end);
//int glb_cache_add_item_values(void *cfg_data, glb_cache_elems_t *elems, ZBX_DC_HISTORY *history, int history_num);
int glb_cache_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);

//int glb_cache_get_item_values_by_time(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type,
//                                      zbx_vector_history_record_t *values, int ts_start, int ts_end);
//int glb_cache_get_item_values_by_count(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type,
//                                       zbx_vector_history_record_t *values, int count, int ts_end);
glb_cache_item_meta_t *glb_cache_get_item_meta(u_int64_t itemid);



// functions to emulate old valuecache interface
int zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
                      int count, const zbx_timespec_t *ts);
int zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value);

// int glb_cache_item_create_cb(glb_cache_elem_t *elem, void *param);
// int glb_cache_item_update_meta_cb(glb_cache_elem_t *elem, void *cb_data);

// int glb_cache_item_get_meta_cb(glb_cache_elem_t *elem, void *cb_data);
// int glb_cache_item_get_state_cb(glb_cache_elem_t *elem, void *cb_data);
// int glb_cache_item_get_nextcheck_cb(glb_cache_elem_t *elem, void *cb_data);
// int glb_cache_item_update_nextcheck_cb(glb_cache_elem_t *elem, int nextcheck);
// int glb_cache_item_values_json_cb(glb_cache_elem_t *elem, void *data);
// int glb_cache_items_marshall_item_cb(glb_cache_elem_t *elem, void *data);
// int glb_cache_items_umarshall_item_cb(glb_cache_elem_t *elem, void *data);

#endif