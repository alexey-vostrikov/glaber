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

#ifndef ZABBIX_PREPROC_H
#define ZABBIX_PREPROC_H

#include "module.h"
#include "zbxcacheconfig.h"
#include "zbxpreproc.h"


/* preprocessing step execution result */
typedef struct
{
	zbx_variant_t	value;
	unsigned char	action;
	char		*error;
}
zbx_preproc_result_t;

typedef struct
{
	zbx_uint64_t	itemid;
	int		values_num;
	int		steps_num;
}
zbx_preproc_item_stats_t;

/* the following functions are implemented differently for server and proxy */

//void	zbx_preprocess_item_value(zbx_uint64_t hostid, zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
//		AGENT_RESULT *result, const zbx_timespec_t *ts, unsigned char state, const char *error);
//void	zbx_preprocessor_flush(void);

zbx_uint64_t	zbx_preprocessor_get_queue_size(void);

void	zbx_preproc_op_free(zbx_preproc_op_t *op);
void	zbx_preproc_result_free(zbx_preproc_result_t *result);

int	zbx_preprocessor_test(unsigned char value_type, const char *value, const zbx_timespec_t *ts,
		unsigned char state, zbx_pp_step_t *steps, int steps_num, zbx_pp_result_t **results, 
		int *results_num);

int	zbx_preprocessor_get_diag_stats(int *total, int *queued, int *processing, int *done,
		int *pending, char **error);

int	zbx_preprocessor_get_top_items(int limit, zbx_vector_ptr_t *items, char **error);
int	zbx_preprocessor_get_top_oldest_preproc_items(int limit, zbx_vector_ptr_t *items, char **error);

int preprocess_error	(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *error);
int preprocess_str		(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *str);
int preprocess_uint64	(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, u_int64_t int_val);
int preprocess_dbl		(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, double dbl_val);
int preprocess_agent_result(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const AGENT_RESULT *ar, int desired_type);
int preprocessing_force_flush();

int processing_send_agent_result(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const AGENT_RESULT *ar);
int processing_send_error(u_int64_t hostid, u_int64_t itemid, u_int64_t flags, const zbx_timespec_t *ts, const char *error);
int processing_force_flush();

#endif /* ZABBIX_PREPROC_H */
