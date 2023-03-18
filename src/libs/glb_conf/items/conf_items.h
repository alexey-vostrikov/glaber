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

#ifndef GLB_CONF_ITEMS_H
#define GLB_CONF_ITEMS_H

#include "zbxcommon.h"
#include "zbxalgo.h"


typedef struct glb_conf_item_valuemap_info_t {
    u_int64_t       valuemapid;
    char            units[MAX_ID_LEN];
    unsigned char   value_type;
} glb_conf_item_valuemap_info_t;


void conf_items_init(mem_funcs_t *memf);

int glb_conf_items_get_valuemap_info(u_int64_t itemid, glb_conf_item_valuemap_info_t *vm_info);
int glb_conf_items_get_valuetype(u_int64_t itemid);
int glb_conf_items_get_hostid(u_int64_t itemid);
char* glb_conf_item_get_key(u_int64_t itemid);
char* glb_conf_item_get_name(u_int64_t itemid);
u_int64_t glb_conf_item_get_hostid(u_int64_t itemid);
int glb_conf_item_get_triggerids(u_int64_t itemid, zbx_vector_uint64_t *triggerids);

#endif