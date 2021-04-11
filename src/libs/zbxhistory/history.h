/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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
#include "dbcache.h"

#ifndef ZABBIX_HISTORY_H
#define ZABBIX_HISTORY_H



typedef void (*glb_history_destroy_func_t)(void *data);

typedef int (*glb_history_add_func_t)(void *data, const zbx_vector_ptr_t *history);
typedef int (*glb_history_get_func_t)(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end, unsigned char interactive, zbx_vector_history_record_t *values);

typedef int (*glb_history_add_trends_func_t)(void *data, ZBX_DC_TREND *trends, int trends_num);
//it's very logical to return vector of trend values in from hist backend, but the only use for it is to send back to 
//the caller, so to avoid double conversion it's done via simple buffer transfer
typedef int (*glb_history_get_agg_buff_func_t)(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end, char **buffer);
typedef int (*glb_history_get_trends_func_t)(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end, char **buffer);

typedef int (*glb_history_preload_values_func_t)(void *data);

/* backend specific init funcs */
int glb_history_worker_init(char *params);
int glb_set_process_types(u_int8_t *types_array, char *setting);
int glb_types_array_sum(u_int8_t *types_array);

history_value_t	history_str2value(char *str, unsigned char value_type);
//int glb_history_json2val(struct zbx_json_parse *jp, u_int64_t * itemid, char *value_type, zbx_history_record_t * value);
//int glb_history_val2(struct json_parse *jp, u_int64_t * itemid, char *value_type, zbx_history_record_t * value){


#endif
