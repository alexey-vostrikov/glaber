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

#ifndef GLB_EVENTS_PROC_H
#define GLB_EVENTS_PROC_H

#include "zbxcommon.h"
#include "zbxcomms.h"
#include "zbxthreads.h"

typedef struct
{
	//zbx_config_tls_t	*zbx_config_tls;
	zbx_get_program_type_f	zbx_get_program_type_cb_arg;
	int			config_timeout;
} glb_events_processor_args;

typedef enum { //internal run time only can change w/out harm
	EVENTS_PROC_NOTIFY_TYPE_NEW_PROBLEM = 1,
	EVENTS_PROC_NOTIFY_TYPE_LLD
} events_processor_object_type_t;

typedef enum {
	EVENTS_TYPE_NEW = 1,
	EVENTS_TYPE_CHANGE, 
	EVENTS_TYPE_RECOVER
} events_processor_event_type_t;

typedef struct {
	events_processor_object_type_t event_source;
	events_processor_event_type_t event_type;
	u_int64_t object_id;
	void *data;
} events_processor_event_t;

int glb_events_processing_init();

void glb_event_processing_send_notify(u_int64_t problemid, unsigned char event_source, events_processor_event_type_t event_type);

ZBX_THREAD_ENTRY(glb_events_processor_thread, args);

#endif