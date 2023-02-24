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
#include "zbxcommon.h"

int glb_actions_process_discovery();
int glb_actions_process_autoregister();

/* old actions interface

int		check_action_condition(const ZBX_DB_EVENT *event, zbx_condition_t *condition);
void	process_actions(const zbx_vector_ptr_t *events, const zbx_vector_uint64_pair_t *closed_events);
int	process_actions_by_acknowledgments(const zbx_vector_ptr_t *ack_tasks);
void	get_db_actions_info(zbx_vector_uint64_t *actionids, zbx_vector_ptr_t *actions);
void	free_db_action(DB_ACTION *action);

*/