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
#ifndef PROC_TRIGGERS_H
#define PROC_TRIGGERS_H

#include "common.h"

#include "zbxalgo.h"
#include "dbcache.h"
#include "log.h"


typedef struct
{
	zbx_uint64_t		triggerid;
	char			*description;
	char			*expression;
	char			*recovery_expression;
	char			*correlation_tag;
	char			*opdata;
	char			*event_name;
	unsigned char		*expression_bin;
	unsigned char		*recovery_expression_bin;
	unsigned char		topoindex;
	unsigned char		priority;
	unsigned char		type;
	unsigned char		recovery_mode;
	unsigned char		correlation_mode;
	unsigned char		flags;
	zbx_vector_ptr_t	tags;

	zbx_eval_context_t	*eval_ctx;
	zbx_eval_context_t	*eval_ctx_r;

} trigger_conf_t;


/*inits ipc for config notification */
int process_trigger_init(int process_num);

int process_metric_triggers(u_int64_t itemid);


#endif