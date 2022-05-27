/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#ifndef ZABBIX_ACTIONS_H
#define ZABBIX_ACTIONS_H

#include "common.h"
#include "db.h"
#include "../libs/glb_state/state_triggers.h"

#define ZBX_ACTION_RECOVERY_NONE	0
#define ZBX_ACTION_RECOVERY_OPERATIONS	1

typedef struct
{
	zbx_uint64_t	eventid;
	zbx_uint64_t	acknowledgeid;
	zbx_uint64_t	taskid;
}
zbx_ack_task_t;

typedef struct
{
	zbx_uint64_t	taskid;
	zbx_uint64_t	actionid;
	zbx_uint64_t	eventid;
	zbx_uint64_t	triggerid;
	zbx_uint64_t	acknowledgeid;
}
zbx_ack_escalation_t;

typedef struct
{
	zbx_uint64_t			conditionid;
	zbx_uint64_t			actionid;
	char				*value;
	char				*value2;
	unsigned char			conditiontype;
	unsigned char			op;
	zbx_vector_uint64_t		eventids;
}
zbx_condition_t;

int	check_action_condition(const DB_EVENT *event, zbx_condition_t *condition);
void	process_actions(const zbx_vector_ptr_t *events, const zbx_vector_uint64_pair_t *closed_events);
int	process_actions_by_acknowledgements(const zbx_vector_ptr_t *ack_tasks);
void	get_db_actions_info(zbx_vector_uint64_t *actionids, zbx_vector_ptr_t *actions);
void	free_db_action(DB_ACTION *action);

void	actions_proccess_trigger_recovery(trigger_recovery_event_t *rec_event);
void	actions_process_trigger_problem(DB_EVENT *event);

int		actions_ipc_init(size_t ipc_mem_size);
int 	actions_notify_trigger_event(u_int64_t eventid);
int		actions_notify_get_events(int consumerid, zbx_vector_uint64_t* events);

#endif
