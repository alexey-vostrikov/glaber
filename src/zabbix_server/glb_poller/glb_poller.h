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
//#include "dbcache.h"
#include "zbxthreads.h"
#include "zbxcacheconfig.h"

#define GLB_ASYNC_POLLING_MAX_ITERATIONS 10000000

#define GLB_DNS_CACHE_TIME 300 * 1000 // for how long name to ip resolvings have to be remembered in msec
#define GLB_MAX_FAILS 4// how many times in a row items should fail to mark host as unreachable and pause polling for CONFIG_UREACHABLE_PERIOD

/*async pollers having too many sessions or dns requests will stagnate on session support and will loose data packets 
 so if there are more then this amount of sessions, item's polling is delayed for POLLER_MAX_SESSIONS_DELAY seconds */
#define POLLER_MAX_SESSIONS 8 * ZBX_KIBIBYTE 
#define POLLER_MAX_SESSIONS_DELAY 10000 /*in msec */
#define POLLER_NOT_IN_QUEUE_DELAY 60 * 1000 /*how long to wait till try to poll not-returned from the queue item*/
//#define POLLER_MAX_DNS_REQUESTS	2 * ZBX_KIBIBYTE /*maximum simultanious DNS requests */
#define POLLER_NEW_ITEM_DELAY_TIME 10

typedef enum {
	POLL_STARTED_OK = 1,
	POLL_STARTED_FAIL = 2,
	POLL_NEED_DELAY	= 3
} poll_ret_code_t;


typedef struct poller_item_t poller_item_t;

typedef int (*init_item_cb)(DC_ITEM *dc_item, poller_item_t *glb_poller_item);
typedef void (*delete_item_cb)(poller_item_t *glb_item);
typedef void (*handle_async_io_cb)(void);
typedef int (*start_poll_cb)(poller_item_t *glb_item);
typedef void (*shutdown_cb)(void);
typedef int (*forks_count_cb)(void);
typedef void (*poller_resolve_cb)(poller_item_t *glb_item, const char* ipaddr);
typedef void (*poller_resolve_fail_cb)(poller_item_t *glb_item);

int host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
int glb_poller_create_item(DC_ITEM *dc_item);
int glb_poller_delete_item(u_int64_t itemid);

int glb_poller_get_forks();

poller_item_t *poller_get_poller_item(u_int64_t itemid);
u_int64_t poller_item_get_id(poller_item_t *poll_item);
void    *poller_item_get_specific_data(poller_item_t *poll_item);
void 	poller_item_unbound_interface(poller_item_t *poller_item);
void	poller_set_item_specific_data(poller_item_t *poll_item, void *data);
int 	poller_get_item_type(poller_item_t *poll_item);

void poller_return_item_to_queue(poller_item_t *glb_item);
void poller_return_delayed_item_to_queue(poller_item_t *glb_item);
void poller_iface_register_succeed(poller_item_t *glb_item);
void poller_iface_register_timeout(poller_item_t *glb_item);
int poller_if_host_is_failed(poller_item_t *glb_item);
u_int64_t poller_get_host_id(poller_item_t *glb_item);

void poller_set_poller_callbacks(init_item_cb init_item, delete_item_cb delete_item,
								 handle_async_io_cb handle_async_io, start_poll_cb start_poll, 
								 shutdown_cb shutdown, forks_count_cb forks_count, 
								 poller_resolve_cb resolve_callback, poller_resolve_fail_cb resolve_fail_callback, 
								 char *proto_name, unsigned char is_named_iface, unsigned char is_iface_bound);

void poller_preprocess_uint64(poller_item_t *poller_item, zbx_timespec_t *ts, u_int64_t value, int desired_type);
void poller_preprocess_dbl(poller_item_t *poller_item, zbx_timespec_t *ts, double value);
void poller_preprocess_str(poller_item_t *poller_item, zbx_timespec_t *ts, const char *value);
void poller_preprocess_agent_result_value(poller_item_t *poller_item, zbx_timespec_t *ts, AGENT_RESULT *ar);

void poller_inc_requests();
void poller_inc_responses();

/*in some cases full item iteration might need  - in such
 cases use iterator callback intreface */
#define POLLER_ITERATOR_CONTINUE 8
#define POLLER_ITERATOR_STOP 9

typedef int (*items_iterator_cb)(poller_item_t *item, void *data);
#define ITEMS_ITERATOR(func) static int func(poller_item_t *poller_item, void *data)

void poller_items_iterate(items_iterator_cb iter_func, void *data);

ZBX_THREAD_ENTRY(glbpoller_thread, args);

void poller_strpool_free(const char* str);
const char *poller_strpool_add(const char * str);
const char *poller_strpool_copy(const char * str);

void poller_preprocess_error(poller_item_t *poller_item, const char *error);
void poller_preprocess_str(poller_item_t *poller_item, zbx_timespec_t *ts, const char *value);

#endif
