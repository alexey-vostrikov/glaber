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
#include "poller_async_io.h"

#include <event2/event.h>
#include <event2/dns.h>
#include <event2/util.h>

typedef struct {
	struct evdns_base *evdns_base;
	struct event_base *events_base;
	resolve_cb resolve_callback;
	resolve_fail_cb resolve_fail_callback;
	u_int32_t dns_requests;
} async_poller_conf_t;

struct poller_event_t {
 	u_int64_t itemid;
 	poller_event_cb_func_t cb_func;
 	void* data;
 	struct event* event;
 };

static async_poller_conf_t conf = {0};

static void poller_cb(int fd, short int flags, void *data) {
	
	poller_event_t *poll_event = data;
	poller_item_t *poller_item = poller_get_poller_item(poll_event->itemid);
	DEBUG_ITEM(poll_event->itemid, "Item event %p has fired", poll_event->event);

	if (0 == poll_event->itemid || NULL != poller_item)  {
		poll_event->cb_func(poller_item,poll_event->data);
	} 
}

poller_event_t* poller_create_event(poller_item_t *poller_item, poller_event_cb_func_t callback_func, int fd, void *data, int persist) {
	short int flags = 0;
	poller_event_t *poller_event = zbx_calloc(NULL, 0, sizeof(poller_event_t));
	
	if ( 0 != fd)
		flags = EV_READ;
	
	if ( persist) 
		flags |= EV_PERSIST;

	poller_event->cb_func = callback_func;
	poller_event->data = data;
	
	poller_event->event = event_new(conf.events_base, fd, flags, poller_cb, poller_event);
	
	/*note: give io more priority */
	if (fd != 0) {
		 event_priority_set(poller_event->event, 0);
	} else {
		 event_priority_set(poller_event->event, 1);
	}


	if (NULL != poller_item) 
		poller_event->itemid = poller_get_item_id(poller_item);

	return poller_event;
};

void poller_disable_event(poller_event_t *poll_event) {
	event_del(poll_event->event);
}

int poller_destroy_event(poller_event_t *poll_event) {
	if (NULL == poll_event)
		return SUCCEED;
	event_del(poll_event->event);
	event_free(poll_event->event);
	zbx_free(poll_event);

	return SUCCEED;
}

int poller_run_timer_event( poller_event_t *poll_event, u_int64_t tm_msec) {
	struct timeval tv = { .tv_sec = tm_msec/1000, .tv_usec = (tm_msec % 1000) * 1000 };
	DEBUG_ITEM(poll_event->itemid, "Started timer event %p for the item in %ld sec, %ld msec", poll_event->event, tv.tv_sec, tv.tv_usec);;
	return event_add(poll_event->event, &tv);
}

void poller_run_fd_event(poller_event_t *poll_event) {
	event_add(poll_event->event, NULL);
}

void poller_async_resolve_cb(int result, char type, int count, int ttl, void *addresses, void *arg) {
	u_int64_t itemid = (u_int64_t)arg;
	poller_item_t *poller_item;

	poller_item = poller_get_poller_item(itemid);
	conf.dns_requests--;
	
	if (NULL == poller_item) 
		return;
	
	if (DNS_ERR_NONE != result) {
	    DEBUG_ITEM(poller_get_item_id(poller_item), "There was an error resolving item :%s", evutil_gai_strerror(result));

        if (NULL != conf.resolve_fail_callback) {
            DEBUG_ITEM(poller_get_item_id(poller_item), "Calling specific resolve fail func");
            conf.resolve_fail_callback(poller_item);
            return;
        }

		char buff[MAX_STRING_LEN];

		zbx_snprintf(buff, MAX_STRING_LEN, "Couldn't resolve item's hostname, errcode is %d, returned %d records", result, count);
		poller_preprocess_error(poller_item, buff);
		poller_return_item_to_queue(poller_item);
	
		return;
	}

	if (count > 0 ) {

		char buf[128];
		u_int32_t addr = *(u_int32_t *)addresses;

		evutil_inet_ntop(AF_INET, &addr, buf, sizeof(buf));	
		conf.resolve_callback(poller_item, buf);
	}
}
void poller_async_set_resolve_cb(resolve_cb callback) {
	conf.resolve_callback = callback;
}

void poller_async_set_resolve_fail_cb(resolve_fail_cb callback) {
       conf.resolve_fail_callback = callback;
}

int poller_async_get_dns_requests() {
	return conf.dns_requests;
}

int poller_async_resolve(poller_item_t *poller_item,  const char *name) {

	u_int64_t itemid = poller_get_item_id(poller_item);

	if (NULL == evdns_base_resolve_ipv4(conf.evdns_base, name, 0, poller_async_resolve_cb, (void *)itemid)) {
		
		if ( NULL!= poller_item) {
			DEBUG_ITEM(poller_get_item_id(poller_item), "Async dns lookup failed for addr '%s'", name);
			poller_preprocess_error(poller_item, "Cannot start DNS lookup. Check the item hostname or interface settings");
		}
	}
	conf.dns_requests++;
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
	event_base_priority_init(conf.events_base, 2);
	evdns_base_set_option(conf.evdns_base,"timeout","4");
	evdns_base_set_option(conf.evdns_base, "randomize-case", "0");
}

struct event_base* poller_async_get_events_base() {
	return conf.events_base;
}