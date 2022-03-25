/*
** Glaber
** Copyright (C) 2001-2020 Glaber JSC
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

#ifndef GLABER_POLLER_H
#define GLABER_POLLER_H
#include "dbcache.h"
#include "threads.h"

#define GLB_ASYNC_POLLING_MAX_ITERATIONS 10000000
#define GLB_EVENT_ITEM_POLL 1
#define GLB_EVENT_NEW_ITEMS_CHECK 2

#define GLB_DNS_CACHE_TIME 120 //for how long name to ip resolvings have to be remembered

//#define GLB_AGING_PERIOD 62	 //how often to check the items if they are aged

#define GLB_MAX_FAILS 6 //how many times in a row items should fail to mark host as unreachable and pause polling for CONFIG_UREACHABLE_PERIOD

#define GLB_PROTO_ITEMID "itemid"
#define GLB_PROTO_VALUE "value"
#define GLB_PROTO_ERRCODE "errcode"
#define GLB_PROTO_ERROR "error"

typedef struct glb_poll_module_t glb_poll_module_t;

typedef struct
{
	zbx_uint64_t itemid;
	zbx_uint64_t hostid;
	unsigned char state;
	unsigned char value_type;
	const char *delay;
	unsigned char item_type;
	unsigned char flags;
	unsigned int lastpolltime;
	void *itemdata;		 //item type specific data
	int change_time; //rules if the item should be updated or it's the same config
} GLB_POLLER_ITEM;

struct glb_poll_module_t  {
	int 		(*init_item)(glb_poll_module_t *poll_mod, DC_ITEM *dc_item, GLB_POLLER_ITEM *glb_poller_item);
	void		(*delete_item)(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item);
	
	void	(*handle_async_io)(glb_poll_module_t *poll_mod);
	void		(*start_poll)(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item);
	
	void 	(*shutdown)(glb_poll_module_t *poll_mod);
	int 	(*forks_count)(glb_poll_module_t *poll_mod);

	void *poller_data;
	
	int requests;
	int responses;
} ;

typedef struct {
	glb_poll_module_t poller;

	unsigned char item_type;

	zbx_binary_heap_t events; 
	zbx_hashset_t items;	  
	zbx_hashset_t hosts;	  
	
	int next_stat_time;
	int old_activity;
} glb_poll_engine_t;

typedef struct
{
	zbx_uint64_t hostid;
	unsigned int poll_items;
	unsigned int items;
	unsigned int fails;
	unsigned int first_fail;
	time_t disabled_till;

} GLB_POLLER_HOST;

int event_elem_compare(const void *d1, const void *d2);
void add_host_fail(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
void add_host_succeed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);

int host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
int glb_poller_create_item(void *poller_data, DC_ITEM *dc_item);
int glb_poller_delete_item(void *poller_data, u_int64_t itemid);
int glb_poller_get_forks(void *poller_data);

u_int64_t glb_ms_time(); //retruns time in millisecodns

ZBX_THREAD_ENTRY(glbpoller_thread, args);
GLB_POLLER_ITEM *glb_get_poller_item(zbx_uint64_t itemid);

#endif
