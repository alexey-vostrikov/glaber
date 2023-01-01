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

#ifndef GLB_CACHE_ITEMS_H
#define GLB_CACHE_ITEMS_H

#include "glb_state.h"


#define STATE_ITEMS_INIT_SIZE (1000)
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
} glb_state_item_meta_t;

int glb_state_items_init(mem_funcs_t *memf);



int     glb_state_get_item_valuetype(u_int64_t itemid);
int     glb_state_item_get_values(u_int64_t itemid, int value_type, zbx_vector_history_record_t *values, int ts_start, int count, int ts_end);
int     glb_state_item_update_meta(u_int64_t itemid, glb_state_item_meta_t *meta, unsigned int flags, int value_type);
int     glb_state_item_get_nextcheck(u_int64_t itemid);
int     glb_state_item_update_nextcheck(u_int64_t itemid, int nextcheck);
int     glb_state_item_get_state(u_int64_t itemid);
int     glb_state_items_get_state_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);

int     glb_state_items_load();
int     glb_state_items_dump();

int     glb_state_item_add_values( ZBX_DC_HISTORY *history, int history_num);
int     glb_state_item_add_lld_value(ZBX_DC_HISTORY *h);

int     glb_state_get_items_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);
int     glb_state_get_items_status_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);
void    glb_state_get_item_stats(zbx_vector_ptr_t *stats);
int     glb_state_get_item_state(u_int64_t itemid);
int     glb_state_get_values_by_count(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int count, int ts_end);
int     glb_state_get_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds, int ts_end);

int     glb_state_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);

glb_state_item_meta_t *glb_state_get_item_meta(u_int64_t itemid);
void glb_state_items_housekeep();




// functions to emulate old valuecache interface
int zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
                      int count, const zbx_timespec_t *ts);
int zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value);

void	zbx_vc_remove_items_by_ids(zbx_vector_uint64_t *itemids);

#endif