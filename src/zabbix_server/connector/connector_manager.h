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

#ifndef ZABBIX_CONNECTOR_MANAGER_H
#define ZABBIX_CONNECTOR_MANAGER_H

#include "zbxthreads.h"

typedef struct
{
	zbx_get_config_forks_f	get_process_forks_cb_arg;
}
zbx_thread_connector_manager_args;

int	worker_item_preproc_execute(u_int64_t itemid, zbx_preproc_cache_t *cache, unsigned char value_type,
		const zbx_variant_t *value_in, zbx_variant_t *value_out, const zbx_timespec_t *ts,
		zbx_preproc_op_t *steps, int steps_num, zbx_vector_ptr_t *history_in, zbx_vector_ptr_t *history_out,
		zbx_preproc_result_t *results, int *results_num, u_int64_t flags, char **error);

ZBX_THREAD_ENTRY(connector_manager_thread, args);

#endif
