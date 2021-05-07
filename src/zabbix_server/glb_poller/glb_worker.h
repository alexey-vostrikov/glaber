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

#ifndef GLABER_WORKER_H
#define GLABER_WORKER_H
#include "glb_poller.h"
#define GLB_WORKER_MAXCALLS 1000000;

typedef struct {
	u_int64_t workerid;
//	char *path; //fill path to the worker
	u_int64_t lastrequest; //to shudown and cleanup idle workers
	GLB_EXT_WORKER worker;
} GLB_WORKER_T;

typedef struct {
	//config params
	//const char *key;
	u_int64_t finish_time;
	char state; //internal item state for async ops
	unsigned int timeout; //timeout in ms - for how long to wait for the response before consider worker dead
	u_int64_t lastrequest; //to control proper intrevals between packets in case is for some reason 
							   //there was a deleay in sending a packet, we need control that next 
							   //packet won't be sent too fast
	u_int64_t workerid; //id of a worker to process the data
	char *params_dyn; //dynamic translated params
	const char *full_cmd; //fill path and unparsed params - command to start the worker 

} GLB_WORKER_ITEM;

unsigned int glb_worker_init_item(DC_ITEM *dc_item, GLB_WORKER_ITEM *glb_worker_item);
void glb_worker_free_item(GLB_WORKER_ITEM *glb_worker_item );

void* glb_worker_init(zbx_hashset_t *items, int *requests, int *responces );
void glb_worker_shutdown(void *engine);

void  glb_worker_handle_async_io(void *engine);
int glb_worker_send_request(void *engine, GLB_POLLER_ITEM *glb_item);

#endif
