/*
** Glaber
** Copyright (C) 2001-2028 Glaber JSC
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

#define GLB_DNS_CACHE_TIME 300 * 1000 // for how long name to ip resolvings have to be remembered in msec

#define GLB_MAX_FAILS 6 // how many times in a row items should fail to mark host as unreachable and pause polling for CONFIG_UREACHABLE_PERIOD

#define GLB_PROTO_ITEMID "itemid"
#define GLB_PROTO_VALUE "value"
#define GLB_PROTO_ERRCODE "errcode"
#define GLB_PROTO_ERROR "error"

typedef struct poll_engine_t poll_engine_t;
typedef struct poller_item_t poller_item_t;

typedef int (*init_item_cb)(void *mod_conf, DC_ITEM *dc_item, poller_item_t *glb_poller_item);
typedef void (*delete_item_cb)(void *mod_comf, poller_item_t *glb_item);
typedef void (*handle_async_io_cb)(void *mod_conf);
typedef void (*start_poll_cb)(void *mod_conf, poller_item_t *glb_item);
typedef void (*shutdown_cb)(void *mod_conf);
typedef int (*forks_count_cb)(void *mod_conf);

int host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
int glb_poller_create_item(void *poller_data, DC_ITEM *dc_item);
int glb_poller_delete_item(void *poller_data, u_int64_t itemid);
int glb_poller_get_forks();

poller_item_t *poller_get_pollable_item(u_int64_t itemid);
u_int64_t poller_get_item_id(poller_item_t *poll_item);
void *poller_get_item_specific_data(poller_item_t *poll_item);
void poller_set_item_specific_data(poller_item_t *poll_item, void *data);

void poller_return_item_to_queue(poller_item_t *glb_item);
void poller_register_item_succeed(poller_item_t *glb_item);
void poller_register_item_timeout(poller_item_t *glb_item);
int poller_if_host_is_failed(poller_item_t *glb_item);
u_int64_t poller_get_host_id(poller_item_t *glb_item);

void poller_set_poller_module_data(void *data);

void poller_set_poller_callbacks(init_item_cb init_item, delete_item_cb delete_item,
								 handle_async_io_cb handle_async_io, start_poll_cb start_poll, shutdown_cb shutdown, forks_count_cb forks_count);

void poller_preprocess_value(poller_item_t *poller_item, AGENT_RESULT *result, u_int64_t mstime, unsigned char state, char *error);

void poller_inc_requests();
void poller_inc_responces();

/*in some cases full item iteration might need  - in such
 cases use iterator callback intreface */
#define POLLER_ITERATOR_CONTINUE 8
#define POLLER_ITERATOR_STOP 9

typedef int (*items_iterator_cb)(void *poller_data, poller_item_t *item, void *data);
#define ITEMS_ITERATOR(func) int func(void *poller_data, poller_item_t *poller_item, void *data)

void poller_items_iterate(items_iterator_cb iter_func, void *data);

ZBX_THREAD_ENTRY(glbpoller_thread, args);
poller_item_t *glb_get_poller_item(zbx_uint64_t itemid);
int poller_notify_ipc_init(size_t mem_size);
int poller_item_add_notify(int item_type, u_int64_t itemid, u_int64_t hostid);
int poller_item_notify_init();
void poller_item_notify_flush();
#endif
