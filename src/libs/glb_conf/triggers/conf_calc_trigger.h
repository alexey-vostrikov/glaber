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
#ifndef CALC_TRIGGER_H
#define CALC_TRIGGER_H

#include "zbxcacheconfig.h"
#include "../tags/tags.h"
//typedef struct trigger_cache_t trigger_cache_t;
//typedef int (*zbx_trigger_func_t)(zbx_variant_t *, const DC_EVALUATE_ITEM *, const char *, const char *,
//		const zbx_timespec_t *, char **);

typedef struct
{
	zbx_uint32_t		init;
	zbx_uint32_t		done;
	zbx_eval_context_t	eval_ctx;
	zbx_eval_context_t	eval_ctx_r;
	zbx_vector_uint64_t	hostids;
}
trigger_cache_t;


typedef struct calc_trigger
{
	zbx_uint64_t		triggerid;
	char			*description;
	char			*error;
	char			*new_error;
	char			*correlation_tag;
	char			*opdata;
	char			*event_name;
	char			*expression;
	char			*recovery_expression;
	unsigned char		*expression_bin;
	unsigned char		*recovery_expression_bin;
	zbx_timespec_t		timespec;
	int			lastchange;
//	unsigned char		topoindex;
	unsigned char		priority;
	unsigned char		type;
	unsigned char		value;
	unsigned char		new_value;
	unsigned char		status;
	unsigned char		recovery_mode;
	unsigned char		correlation_mode;
	tags_t 				*tags;
	//zbx_vector_ptr_t	tags;
	trigger_cache_t 	eval_cache;
} calc_trigger_t;

void conf_calc_trigger_clean(calc_trigger_t *trigger);
void conf_calc_triggers_clean(calc_trigger_t *triggers, int *errcodes, size_t num);
void conf_calc_triggers_free(zbx_vector_ptr_t *triggers);

int	conf_calc_trigger_get_N_itemid(calc_trigger_t *trigger, int index, zbx_uint64_t *itemid);
int	conf_calc_trigger_get_all_hostids(calc_trigger_t *trigger, zbx_vector_uint64_t **hostids);

void	conf_calc_triggers_prepare_expressions(calc_trigger_t **triggers, int triggers_num);
//void	conf_calc_trigger_explain_expression(const calc_trigger_t *trigger, char **expression,
//		zbx_trigger_func_t eval_func_cb, int recovery);
int	conf_calc_trigger_get_N_itemid(calc_trigger_t *trigger, int index, zbx_uint64_t *itemid);
int	conf_calc_trigger_get_N_hostid(calc_trigger_t *trigger, int index, zbx_uint64_t *hostid);
void	conf_calc_trigger_get_expression(calc_trigger_t *trigger, char **expression);
void	conf_calc_trigger_get_recovery_expression(calc_trigger_t *trigger, char **expression);

int conf_calc_trigger_calculate(u_int64_t triggerid);
char* conf_calc_trigger_get_severity_name(unsigned char priority);
char* conf_calc_trigger_get_admin_state_name(unsigned char admin_state);

#endif