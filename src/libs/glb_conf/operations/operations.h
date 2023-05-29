/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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
#include "zbxalgo.h"

typedef struct glb_events_operations_conf_t glb_events_operations_conf_t;

int     glb_events_operations_get_max_steps(glb_events_operations_conf_t *operations, u_int64_t actionid);
int     glb_events_operations_get_step_delay(glb_events_operations_conf_t *operations, u_int64_t actionid, int step_no);

void    glb_event_operations_execute_step(glb_events_operations_conf_t *operations, u_int64_t actionid, int step_no);
void    glb_events_operations_execute_change(glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid);
void    glb_events_operations_execute_recovery(glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid);

glb_events_operations_conf_t *glb_events_operations_init(mem_funcs_t *memf);
void    glb_events_operations_update(glb_events_operations_conf_t *operations);
void    glb_events_operations_free(glb_events_operations_conf_t *operations);