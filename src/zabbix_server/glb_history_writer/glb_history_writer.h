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

#ifndef GLB_ALERTER_H
#define GLB_ALERTER_H

#include "zbxcommon.h"
#include "zbxcomms.h"
#include "zbxthreads.h"

typedef struct
{
	//zbx_config_tls_t	*zbx_config_tls;
	zbx_get_program_type_f	zbx_get_program_type_cb_arg;
	int			config_timeout;
} glb_alerter_args;

typedef struct {
	void *placeholder;
} glb_alert_t;

int glb_alerting_init(); //called from the parent fork to init alerting shmem ipc
int glb_alerter_send_alert();

ZBX_THREAD_ENTRY(glb_alerter_thread, args);

#endif