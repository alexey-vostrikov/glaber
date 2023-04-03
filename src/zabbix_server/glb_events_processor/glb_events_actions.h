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

//glb escalator is responsible for performing operations based on actions configuration
//whenever an actionable object is created it is passed to process_actions_xxxx, which in 
//turn should notify escalator on object id and type
//escalator uses actions-operations cache and schedules operations and performs them (or uses exec services, like alerter?)

#ifndef GLB_EVENT_ACTIONS_H
#define GLB_EVENT_ACTIONS_H

#include "zbxcommon.h"
#include "zbxalgo.h"
#include "glb_events_processor.h"

typedef struct
{
	zbx_uint64_t			conditionid;
	zbx_uint64_t			actionid;
	char					*value;
	char				    *value2;
	unsigned char			conditiontype;
	unsigned char			op;
    int                     result;
}
condition_t;

typedef struct glb_actions_t glb_actions_t;

glb_actions_t *glb_actions_create();
void glb_actions_destroy(glb_actions_t *actions);
void glb_actions_update(glb_actions_t *actions);

void glb_actions_process_event(events_processor_event_t *event, glb_actions_t *actions);

#endif