/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#ifndef ZABBIX_STATS_H
#define ZABBIX_STATS_H

#include "zbxalgo.h"
#include "zbxcomms.h"
#include "zbxjson.h"

typedef void (*zbx_zabbix_stats_ext_get_func_t)(struct zbx_json *json, const void *arg);

typedef struct
{
	const void			*arg;
	zbx_zabbix_stats_ext_get_func_t	stats_ext_get_cb;
}
zbx_stats_ext_func_entry_t;

ZBX_PTR_VECTOR_DECL(stats_ext_func, zbx_stats_ext_func_entry_t *)

void	zbx_init_library_stats(zbx_get_program_type_f get_program_type);

void	zbx_register_stats_ext_func(zbx_zabbix_stats_ext_get_func_t stats_ext_get_cb, const void *arg);
void	zbx_register_stats_data_func(zbx_zabbix_stats_ext_get_func_t stats_ext_get_cb, const void *arg);

void	zbx_zabbix_stats_get(struct zbx_json *json, int config_startup_time);

#endif
