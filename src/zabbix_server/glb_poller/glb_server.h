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

#ifndef GLABER_SERVER_H
#define GLABER_SERVER_H
#include "glb_poller.h"

#define GLB_SERVER_MAXCALLS 10000000
#define GLB_DEFAULT_SERVER_MACRO_NAME "NAME"
#define GLB_SERVER_ITEMID_CACHE_TTL 1800

typedef struct {
	u_int64_t workerid;
	char auto_reg_hosts; // if set, then try to autoregister new hosts
	const char *auto_reg_metadata; //metadata to add for newly registered hosts
	const char *lld_key_name;  // if not NULL, then add items to lld data
	const char *lld_macro_name;  // name of macro with name of the keys to submit in the LLD JSON
	const char *item_key; //prefix for the item to conform key format
	const char *interface_param;
	GLB_EXT_WORKER worker;
} GLB_SERVER_T;


typedef struct {
	const char *hostname; //item's hostname value (strpooled)
	const char *key; //item key value (strpooled)
} GLB_SERVER_ITEM;




//unsigned int glb_server_init_item(void *engine, DC_ITEM *dc_item, GLB_SERVER_ITEM *glb_worker_item);

void glb_server_free_item(void* engine,GLB_POLLER_ITEM *glb_worker_item );

void* glb_server_init(int *requests, int *responces );
void  glb_server_shutdown(void *engine);

void  glb_server_handle_async_io(void *engine);

#endif
