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
#ifndef STATE_PROBLEMS_H
#define STATE_PROBLEMS_H

#include "common.h"
#include "zbxalgo.h"
#include "state.h"

//TODO: move all declarations to some common triggers.h file
typedef struct trigger_problems_t trigger_problems_t;
#include "../glb_state/state_triggers.h"
#include "../glb_conf/conf_triggers.h"

typedef struct {
    zbx_uint64_t	eventid;
    u_int64_t problem_eventid;
	zbx_uint64_t	triggerid;
	zbx_timespec_t	ts;
} trigger_recovery_event_t ;


typedef struct problem_t problem_t;

int state_problems_init(strpool_t *strpool, mem_funcs_t *memf);

trigger_problems_t *trigger_problems_init(mem_funcs_t *memf);
void trigger_problems_destroy(trigger_problems_t *t_problems, mem_funcs_t *memf);


int  problems_close_by_trigger(trigger_problems_t *t_problems, trigger_conf_t *conf, DB_EVENT *event);
int  problems_create_problem(trigger_problems_t *problems, DB_EVENT *event, u_int64_t triggerid);

#endif