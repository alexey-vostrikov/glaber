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

#ifndef ZABBIX_PREPROC_H
#define ZABBIX_PREPROC_H

#include "common.h"
#include "module.h"
#include "dbcache.h"

/* preprocessing step execution result */
typedef struct
{
	zbx_variant_t	value;
	unsigned char	action;
	char		*error;
}
zbx_preproc_result_t;


/* the following functions are implemented differently for server and proxy */

void	zbx_preprocess_item_value(zbx_uint64_t hostid, zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, zbx_timespec_t *ts, unsigned char state, char *error);

void	zbx_preprocessor_flush(void);
zbx_uint64_t	zbx_preprocessor_get_queue_size(void);

void	zbx_preproc_op_free(zbx_preproc_op_t *op);
void	zbx_preproc_result_free(zbx_preproc_result_t *result);

int	zbx_preprocessor_test(unsigned char value_type, const char *value, const zbx_timespec_t *ts,
		const zbx_vector_ptr_t *steps, zbx_vector_ptr_t *results, zbx_vector_ptr_t *history,
		char **preproc_error, char **error);
int	dc_preproc_item_init(zbx_preproc_item_t *item, zbx_uint64_t itemid);

void	worker_format_error(const zbx_variant_t *value, zbx_preproc_result_t *results, int results_num,
		const char *errmsg, char **error);
int worker_item_preproc_execute(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		zbx_preproc_op_t *steps, int steps_num, zbx_vector_ptr_t *history_in, zbx_vector_ptr_t *history_out,
		zbx_preproc_result_t *results, int *results_num, char **error);

void	preproc_item_clear(zbx_preproc_item_t *item);
void glb_fast_preprocess_item_value(zbx_uint64_t hostid, zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, zbx_timespec_t *ts, unsigned char state, char *error, DC_ITEM *item, zbx_hashset_t *history_cache, int poller_type);

void glb_preprocessing_init() ;
#endif /* ZABBIX_PREPROC_H */
