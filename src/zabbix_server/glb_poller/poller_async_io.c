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
#include "common.h"
#include "glb_poller.h"
#include "poller_async_io.h"

#include "event2/event.h"
#include "event2/dns.h"

typedef struct {
	u_int64_t itemid;
	resolve_cb cb;
} event_resolve_cb_t;

typedef struct {
	struct evdns_base *evdns_base;
	struct event_base *events_base;
} async_poller_conf_t;

struct poller_event_t {
 	u_int64_t itemid;
 	poller_event_cb_func_t cb_func;
 	void* data;
 	struct event* event;
 };

static async_poller_conf_t conf;

static void poller_cb(int fd, short int flags, void *data) {
	
	poller_event_t *poll_event = data;
	poller_item_t *poller_item = poller_get_poller_item(poll_event->itemid);
	
	if (0 == poll_event->itemid || NULL != poller_item)  {
		poll_event->cb_func(poller_item,poll_event->data);
	}
}

poller_event_t* poller_create_event(poller_item_t *poller_item, poller_event_cb_func_t callback_func, int fd, void *data) {
	poller_event_t *poller_event = zbx_calloc(NULL, 0, sizeof(poller_event_t));
	poller_event->cb_func = callback_func;
	poller_event->data = data;
	
	if (NULL != poller_item)
		poller_event->itemid = poller_get_item_id(poller_item);

	poller_event->event = event_new(conf.events_base, fd, 0, poller_cb, poller_event);

	return poller_event;
};

int poller_destroy_event(poller_event_t *poll_event) {
	event_del(poll_event->event);
	event_free(poll_event->event);
	zbx_free(poll_event);
}

void poller_run_timer_event( poller_event_t *poll_event, u_int64_t tm_msec) {
	struct timeval tv = { .tv_sec = tm_msec/1000, .tv_usec = (tm_msec % 1000) * 1000 };
	event_add(poll_event->event, &tv);
}

void poller_run_fd_event(poller_event_t *poll_event) {
	event_add(poll_event->event, NULL);
}


void poller_async_resolve_cb(int result, char type, int count, int ttl, void *addresses, void *arg) {
	event_resolve_cb_t *cb_data = arg;
	poller_item_t *poller_item;
	resolve_cb cb_func;

	//LOG_INF("Resolve result callback: itemid %ld", cb_data->itemid);

	poller_item = poller_get_poller_item(cb_data->itemid);
	cb_func = cb_data->cb;
	
	free(cb_data);

	if (NULL == poller_item) 
		return;
	
	if (DNS_ERR_NONE != result) {

		//LOG_INF("There was an error resolving item %ld: %s", poller_get_item_id(poller_item), evutil_gai_strerror(result));
		DEBUG_ITEM(poller_get_item_id(poller_item), "There was an error resolving item :%s", evutil_gai_strerror(result));
		
		poller_preprocess_error(poller_item, glb_ms_time(), "Couldn't resolve item's hostname");
		return;
	}

	//LOG_INF("Returned %d addresses", count);
	
	if (count > 0 ) {

		char buf[128];
		u_int32_t addr = *(u_int32_t *)addresses;

		evutil_inet_ntop(AF_INET, &addr, buf, sizeof(buf));
		//LOG_INF("Item %ld resolved to ip addr %s", poller_get_item_id(poller_item), buf);

		cb_func(poller_item, buf);
	}
}

int poller_async_resolve(poller_item_t *poller_item,  char *name, resolve_cb resolve_func ) {
	
	event_resolve_cb_t *cb_data = zbx_malloc(NULL, sizeof(event_resolve_cb_t));
	
	cb_data->itemid = poller_get_item_id(poller_item);
	cb_data->cb = resolve_func;

	if (NULL == evdns_base_resolve_ipv4(conf.evdns_base,name, 0, poller_async_resolve_cb, cb_data)) {
		
		if ( NULL!= poller_item) {
			DEBUG_ITEM(poller_get_item_id(poller_item), "Async dns lookup failed for addr '%s'", name);
			poller_preprocess_error(poller_item, glb_ms_time(), "Cannot start DNS lookup. Check the item hostname or interface settings");
		}

		zbx_free(cb_data);
	}
}

void poller_async_loop_run() {
	event_base_loop(conf.events_base, EVLOOP_NO_EXIT_ON_EMPTY);
}

void poller_async_loop_stop() {
    event_base_loopbreak(conf.events_base);
}

void poller_async_loop_init() {
    conf.events_base = event_base_new();
	conf.evdns_base = evdns_base_new(conf.events_base, EVDNS_BASE_DISABLE_WHEN_INACTIVE | 	EVDNS_BASE_INITIALIZE_NAMESERVERS );
	evdns_base_resolv_conf_parse(conf.evdns_base, DNS_OPTIONS_ALL, "/etc/resolv.conf");
}