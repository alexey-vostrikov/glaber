/*
** Glaber
** Copyright (C)  Glaber
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
#include "log.h"
#include "zbxcommon.h"
#include "glb_poller.h"


typedef struct poller_event_t poller_event_t;

/*async resolve interface */
typedef void (*resolve_cb)(poller_item_t *poller_item, const char* ipv4_addr);
typedef void (*poller_event_cb)(poller_item_t *poller_item, void *data);


int poller_async_resolve(poller_item_t *poller_item, const char* hostname);

typedef void (*poller_event_cb_func_t)(poller_item_t *poller_item, void *data);

poller_event_t* poller_create_event(poller_item_t *poller_item, poller_event_cb_func_t callback_func, int fd, void *data, int persist);

int   poller_run_timer_event( poller_event_t *poll_event, u_int64_t tm_msec);
void  poller_run_fd_event(poller_event_t *poll_event);
int   poller_destroy_event(poller_event_t *event);
void  poller_disable_event(poller_event_t *poll_event);

void  poller_async_set_resolve_cb(resolve_cb callback);
int   poller_async_get_dns_requests();
struct event_base* poller_async_get_events_base();

//library abstraction
void  poller_async_loop_init();
void  poller_async_loop_run();
void  poller_async_loop_destroy();
void  poller_async_loop_stop();
   