/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

#ifndef ZABBIX_HISTORY_H
#define ZABBIX_HISTORY_H

//#define ZBX_HISTORY_IFACE_SQL		0
//#define ZBX_HISTORY_IFACE_ELASTIC	1

typedef void (*zbx_history_destroy_func_t)(void *data);
typedef int (*zbx_history_add_values_func_t)(void *data, const zbx_vector_ptr_t *history);
typedef int (*zbx_history_get_values_func_t)(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end, zbx_vector_history_record_t *values);
typedef int (*zbx_history_get_agg_values_func_t)(void *data, zbx_uint64_t itemid,int value_type, int start, int end, int aggregates, char **buffer);
typedef int (*zbx_history_preload_values_func_t)(void *data);

/*
struct zbx_history_iface
{
	unsigned char			value_type;
	unsigned char			requires_trends;
	void				*data;

	zbx_history_destroy_func_t	destroy;
	zbx_history_add_values_func_t	add_values;
	zbx_history_get_values_func_t	get_values;
	zbx_history_get_agg_values_func_t agg_values;

};
*/
//the first little two guys really need to be fixed, but not important to glaber yet
//todo: come back and fix later

/* SQL hist */
//int	zbx_history_sql_init(zbx_history_iface_t *hist, unsigned char value_type, char **error);

/* elastic hist */
//int	zbx_history_elastic_init(zbx_history_iface_t *hist, unsigned char value_type, char **error);

/* backend specific init funcs */
int glb_history_clickhouse_init(char *params);
int glb_history_vmetrics_init(char *params);
int glb_history_worker_init(char *params);


int glb_set_process_types(u_int8_t *types_array, char *setting);
int glb_types_array_sum(u_int8_t *types_array);
history_value_t	history_str2value(char *str, unsigned char value_type);
/* victoria metrics hist */
//int glb_history_vmetrics_init(char *params, char **error);

#endif
