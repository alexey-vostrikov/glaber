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

#include "zbxcommon.h"
#include "zbx_item_constants.h"
#include "zbxcomms.h"
#include "zbxnix.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "preproc.h"
#include "glb_preproc.h"
#include "metric.h"

#include "../glb_events.h"
#include "log.h"
#include "glb_poller.h"
#include "glb_pinger.h"
#include "glb_worker.h"
#include "glb_server.h"
#include "poller_tcp.h"
#include "calculated.h"
#include "snmp.h"
#include "../../libs/glb_state/glb_state_items.h"
#include "../../libs/glb_state/glb_state_interfaces.h"
#include "../poller/poller.h"
#include "../../libs/zbxexec/worker.h"
#include "poller_async_io.h"
#include "poller_ipc.h"
#include "poller_sessions.h"
#include "poller_contention.h"
#include "../../libs/apm/apm.h"
#include "zbxsysinfo.h"

extern int CONFIG_CONFSYNCER_FREQUENCY;

/* poller timings in milliseconds */
#define LOST_ITEMS_CHECK_INTERVAL 30 * 1000
#define NEW_ITEMS_CHECK_INTERVAL 1 * 1000
#define PROCTITLE_UPDATE_INTERVAL 5 * 1000
#define ASYNC_RUN_INTERVAL 1
#define ITEMS_REINIT_INTERVAL 300 * 1000 // after this time in poller item will be repolled whatever it's state is

typedef struct
{
	
	int server_num;
	int process_num;
	int process_type; //by type of polling -agent, snmp, worker, etc polling
	int program_type; //server, proxy etc....
} poller_proc_info_t;

typedef struct
{
	zbx_uint64_t hostid;
	unsigned int poll_items;
	unsigned int items;
	unsigned int fails;
	unsigned int first_fail;
	time_t disabled_till;
} poller_host_t;

struct poller_item_t
{
	zbx_uint64_t itemid;
	zbx_uint64_t hostid;
	unsigned char state;
	unsigned char value_type;
	const char *delay;
	unsigned char item_type;
	unsigned char flags;
	u_int64_t lastpolltime;
	void *itemdata; // item type specific data
	poller_event_t *poll_event;
	u_int64_t interfaceid;
};

typedef struct
{
	init_item_cb init_item;
	delete_item_cb delete_item;
	handle_async_io_cb handle_async_io;
	start_poll_cb start_poll;
	shutdown_cb shutdown;
	forks_count_cb forks_count;
	poller_resolve_cb resolve_callback;
	poller_resolve_fail_cb resolve_fail_callback;
} poll_module_t;

typedef struct
{
	poll_module_t poller;
	zbx_hashset_t items;
	zbx_hashset_t hosts;
	poller_proc_info_t procinfo;

	u_int64_t requests;
	u_int64_t responces;

	u_int64_t total_requests;
	u_int64_t total_responces;

	strpool_t strpool;

	poller_event_t *new_items_check;
	poller_event_t *update_proctitle;
	poller_event_t *async_io_proc;
	poller_event_t *lost_items_check;
	poller_event_t *preprocessing_flush;

} poll_engine_t;

// typedef struct poll_engine_t poll_engine_t;

static poll_engine_t conf = {0};

static int is_active_item_type(unsigned char item_type)
{
	switch (item_type)
	{
	case ITEM_TYPE_WORKER_SERVER:
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************
 * adds next check event to the events b-tree					  *
 * ****************************************************************/
static int add_item_check_event(poller_item_t *poller_item, u_int64_t mstime)
{
	int simple_interval;
	u_int64_t nextcheck;
	zbx_custom_interval_t *custom_intervals;
	char *error = NULL;
	const char *delay;
	poller_host_t *glb_host;

	if (SUCCEED == is_active_item_type(poller_item->item_type))
		delay = poller_item->delay;
	else
		delay = "86400s";

	if (SUCCEED != zbx_interval_preproc(delay, &simple_interval, &custom_intervals, &error))
	{
	
		zabbix_log(LOG_LEVEL_INFORMATION, "Itemd %ld has wrong delay time set :%s :%s", poller_item->itemid, poller_item->delay, error);
		poller_preprocess_error(poller_item, "error");
		return FAIL;
	}

	if (NULL == (glb_host = zbx_hashset_search(&conf.hosts, &poller_item->hostid)))
	{
		LOG_WRN("No host has been found for itemid %ld", poller_item->itemid);
		return FAIL;
	}

	if (glb_host->disabled_till > mstime)
	{
		nextcheck = zbx_calculate_item_nextcheck_unreachable(simple_interval,
															 custom_intervals, glb_host->disabled_till);
	}
	else
	{
		nextcheck = zbx_calculate_item_nextcheck(poller_item->itemid, poller_item->item_type, simple_interval,
												 custom_intervals, (mstime / 1000) + 1);
	}

	/*note: since original algo is still seconds-based adding some millisecond-noise
	to distribute checks evenly during one second */
	glb_state_item_update_nextcheck(poller_item->itemid, nextcheck);

	nextcheck = nextcheck * 1000 + rand() % 1000;
	zbx_custom_interval_free(custom_intervals);

	DEBUG_ITEM(poller_item->itemid, "Added item %ld poll event in %ld msec", poller_item->itemid, nextcheck - mstime);

	poller_run_timer_event(poller_item->poll_event, nextcheck - mstime);
	return SUCCEED;
}

void item_poll_cb(poller_item_t *poller_item, void *data) {

	u_int64_t mstime = glb_ms_time();

	DEBUG_ITEM(poller_item->itemid, "Item poll event");

	if ( poller_sessions_count() > POLLER_MAX_SESSIONS || poller_contention_sessions_count() > POLLER_MAX_SESSIONS) 
	{
		DEBUG_ITEM(poller_item->itemid, "Item delayed %d sec due to poller is too busy", POLLER_MAX_SESSIONS_DELAY / 1000);
		poller_run_timer_event(poller_item->poll_event, POLLER_MAX_SESSIONS_DELAY);
		return;
	}

	//	if (poller_async_get_dns_requests() > POLLER_MAX_DNS_REQUESTS) {
	//		DEBUG_ITEM(poller_item->itemid, "Item delayed %d sec due to too many DNS requests are int progress (%d) busy", POLLER_MAX_SESSIONS_DELAY / 1000, POLLER_MAX_DNS_REQUESTS );
	//		poller_run_timer_event(poller_item->poll_event, POLLER_MAX_SESSIONS_DELAY);
	//		return;
	//	}

	add_item_check_event(poller_item, mstime);

	if (POLL_QUEUED != poller_item->state)
	{
		DEBUG_ITEM(poller_item->itemid, "Skipping from polling, not in QUEUED state (%d)", poller_item->state);
		LOG_DBG("Not sending item %ld to polling, it's in the %d state or aged", poller_item->itemid, poller_item->state);
		return;
	}

	poller_item->state = POLL_POLLING;
	poller_item->lastpolltime = mstime;

	DEBUG_ITEM(poller_item->itemid, "Starting poller item poll");
	
	if (NULL != conf.poller.start_poll)
		conf.poller.start_poll(poller_item);
}

static int add_item_to_host(u_int64_t hostid)
{
	poller_host_t *glb_host, new_host = {0};

	if (NULL == (glb_host = (poller_host_t *)zbx_hashset_search(&conf.hosts, &hostid)))
	{
		poller_host_t new_host = {0};

		new_host.hostid = hostid;
		glb_host = zbx_hashset_insert(&conf.hosts, &new_host, sizeof(poller_host_t));
	}

	glb_host->items++;
}

static void delete_item_from_host(u_int64_t hostid)
{
	poller_host_t *glb_host;

	if (NULL != (glb_host = zbx_hashset_search(&conf.hosts, &hostid)))
	{
		glb_host->items--;

		if (0 == glb_host->items)
			zbx_hashset_remove_direct(&conf.hosts, glb_host);
	}
}

int glb_poller_get_forks()
{
	return conf.poller.forks_count();
}

int glb_poller_delete_item(u_int64_t itemid)
{
	poller_item_t *poller_item;

	if (NULL != (poller_item = (poller_item_t *)zbx_hashset_search(&conf.items, &itemid)))
	{
		DEBUG_ITEM(itemid, "Item has been deleted, removing from the poller config");

		conf.poller.delete_item(poller_item);
		delete_item_from_host(poller_item->hostid);
		strpool_free(&conf.strpool, poller_item->delay);
		poller_destroy_event(poller_item->poll_event);
		zbx_hashset_remove_direct(&conf.items, poller_item);

		LOG_DBG("in: %s: ended", __func__);
		return SUCCEED;
	}

	return FAIL;
}

static int get_simple_interval(const char *delay)
{
	int interval;
	char *delim;

	if (SUCCEED != zbx_is_time_suffix(delay, &interval, (int)(NULL == (delim = strchr(delay, ';')) ? ZBX_LENGTH_UNLIMITED : delim - delay)))
		return 0;

	return interval;
}

/******************************************************************
 * Creates new item for polling, calls appropriate conversion func  *
 * for the type, and fills the new poller_item with key vals          *
 * adds new item to the items hashset, if one already exists,      *
 * updates it (old key vals are released), updates or creates      *
 * a hosts entry for the item						  			  *
 ******************************************************************/
int glb_poller_create_item(DC_ITEM *dc_item)
{
	poller_item_t *poller_item, local_glb_item;
	poller_host_t *glb_host;
	u_int64_t mstime = glb_ms_time();
	int i;
	
	DEBUG_ITEM(dc_item->itemid, "Creating/updating item");

	if (NULL != (poller_item = (poller_item_t *)zbx_hashset_search(&conf.items, &dc_item->itemid)))
	{
		DEBUG_ITEM(dc_item->itemid, "Item has changed: deleting the old configuration");
		glb_poller_delete_item(poller_item->itemid);
	}
	
	DEBUG_ITEM(dc_item->itemid, "Adding new item to poller");
	bzero(&local_glb_item, sizeof(poller_item_t));

	local_glb_item.itemid = dc_item->itemid;

	if (NULL == (poller_item = (poller_item_t *)zbx_hashset_insert(&conf.items, &local_glb_item, sizeof(poller_item_t))))
		return FAIL;

	poller_item->state = POLL_QUEUED;
	poller_item->hostid = dc_item->host.hostid;
	
	zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &dc_item->host.hostid, NULL, NULL,
						NULL, NULL, NULL, NULL, NULL, &dc_item->delay, MACRO_TYPE_COMMON,
						NULL, 0);

	poller_item->delay = strpool_add(&conf.strpool, dc_item->delay);
	poller_item->value_type = dc_item->value_type;
	poller_item->flags = dc_item->flags;
	poller_item->item_type = dc_item->type;
	poller_item->poll_event = poller_create_event(poller_item, item_poll_cb, 0, NULL, 0);
	poller_item->interfaceid = dc_item->interface.interfaceid;

	add_item_to_host(poller_item->hostid);
	
	if (FAIL == conf.poller.init_item(dc_item, poller_item))
	{
		strpool_free(&conf.strpool, poller_item->delay);
		delete_item_from_host(poller_item->hostid);
		zbx_hashset_remove(&conf.items, &poller_item->itemid);
		
		return FAIL;
	};

	if (get_simple_interval(poller_item->delay) > 0)
	{
		/*	to avoid system hummering new items are planned to not exceed rate of 10k/sec */
		poller_run_timer_event(poller_item->poll_event, conf.items.num_data / 10000);
	}
	else
		add_item_check_event(poller_item, mstime);

	return SUCCEED;
}

int host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now)
{
	poller_host_t *glb_host;

	if (NULL != (glb_host = (poller_host_t *)zbx_hashset_search(hosts, &hostid)) && glb_host->disabled_till > now)
		return SUCCEED;

	return FAIL;
}

static int poll_module_init()
{
	switch (conf.procinfo.process_type)
	{
#ifdef HAVE_NETSNMP
	case GLB_PROCESS_TYPE_SNMP:
		snmp_async_init();
		break;
#endif
	case GLB_PROCESS_TYPE_PINGER:
		glb_pinger_init();
		break;

	case GLB_PROCESS_TYPE_WORKER:
		glb_worker_poller_init();
		break;

	case GLB_PROCESS_TYPE_SERVER:
		glb_worker_server_init();
		break;

	case GLB_PROCESS_TYPE_AGENT:
		glb_tcp_init(&conf);
		break;

	case ZBX_PROCESS_TYPE_HISTORYPOLLER:
		calculated_poller_init();
		break;

	default:
		LOG_WRN("Cannot init worker for item type %d, this is a BUG", conf.procinfo.process_type);
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	return SUCCEED;
}

void poll_shutdown()
{
	if (NULL != conf.poller.shutdown) 
		conf.poller.shutdown();
	

	zbx_hashset_destroy(&conf.hosts);
	zbx_hashset_destroy(&conf.items);
	poller_contention_destroy();
	strpool_destroy(&conf.strpool);
}

int DCconfig_get_glb_poller_items_by_ids(void *poll_data, zbx_vector_uint64_t *itemids);

void new_items_check_cb(poller_item_t *garbage, void *data)
{
	zbx_thread_args_t *args = data;
	zbx_vector_uint64_t changed_items;
	zbx_vector_uint64_create(&changed_items);
	
	poller_ipc_notify_rcv(args->info.process_type, args->info.process_num - 1, &changed_items);
	DCconfig_get_glb_poller_items_by_ids(&conf, &changed_items);

	zbx_vector_uint64_destroy(&changed_items);

}

void lost_items_check_cb(poller_item_t *garbage, void *data)
{
	u_int64_t mstime = glb_ms_time();

	poller_item_t *poller_item;
	zbx_hashset_iter_t iter;
	zbx_hashset_iter_reset(&conf.items, &iter);

	while (NULL != (poller_item = zbx_hashset_iter_next(&iter)))
	{
		if (poller_item->lastpolltime + ITEMS_REINIT_INTERVAL < mstime && POLL_POLLING == poller_item->state)
		{
			DEBUG_ITEM(poller_item->itemid, "Item has timeout in the poller, resetting the sate")
			poller_item->state = POLL_QUEUED;
		}
	}

	poller_run_timer_event(conf.lost_items_check, LOST_ITEMS_CHECK_INTERVAL);
}
static void preprocessing_flush_cb(poller_item_t *garbage, void *data) {
	preprocessing_flush();
}

static void update_proc_title_cb(poller_item_t *garbage, void *data)
{
	static u_int64_t last_call = 0;
	zbx_thread_args_t *args = data;
	u_int64_t now = glb_ms_time(), time_diff = now - last_call;
	
	//zbx_update_env(zbx_time());

	zbx_setproctitle("%s #%d [sent %ld chks/sec, got %ld chcks/sec, items: %d, sessions: %d, dns_requests: %d]",
					get_process_type_string(args->info.process_type), args->info.process_num, (conf.requests * 1000) / (time_diff),
					(conf.responces * 1000) / (time_diff), conf.items.num_data, poller_sessions_count() + poller_contention_sessions_count(),
						 poller_async_get_dns_requests());

	conf.total_requests += conf.requests;
	conf.total_responces += conf.responces;

	conf.requests = 0;
	conf.responces = 0;

	apm_update_heap_usage();
	apm_flush();

	last_call = now;

	if (!ZBX_IS_RUNNING())
		poller_async_loop_stop();
	
}

void async_io_cb(poller_item_t *garbage, void *data)
{
if (NULL != conf.poller.handle_async_io) 
		conf.poller.handle_async_io();
}

static int poller_init(zbx_thread_args_t *args)
{
	mem_funcs_t local_memf = {.free_func = ZBX_DEFAULT_MEM_FREE_FUNC, 
	  					.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, 
						.realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC};

	//glb_preprocessing_init();
	poller_contention_init();
	// conf.item_type = args->info.process_type;

	zbx_hashset_create(&conf.items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_hashset_create(&conf.hosts, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	strpool_init(&conf.strpool, &local_memf);

	poller_async_loop_init();

	conf.new_items_check = poller_create_event(NULL, new_items_check_cb, 0, args, 1);
	poller_run_timer_event(conf.new_items_check, NEW_ITEMS_CHECK_INTERVAL);
	
	conf.update_proctitle = poller_create_event(NULL, update_proc_title_cb, 0, args, 1);
	poller_run_timer_event(conf.update_proctitle, PROCTITLE_UPDATE_INTERVAL);

	conf.preprocessing_flush = poller_create_event(NULL, preprocessing_flush_cb, 0, args, 1);
	poller_run_timer_event(conf.preprocessing_flush, PROCTITLE_UPDATE_INTERVAL);

	conf.lost_items_check = poller_create_event(NULL, lost_items_check_cb, 0, NULL, 1);
	poller_run_timer_event(conf.lost_items_check, LOST_ITEMS_CHECK_INTERVAL);

	/*poll modules _should_ be init after loop event */
	if (SUCCEED != poll_module_init())
	{
		LOG_WRN("Couldnt init type-specific module for type %d", args->info.process_type);
		return FAIL;
	}

	if (NULL != conf.poller.handle_async_io) {
		conf.async_io_proc = poller_create_event(NULL, async_io_cb, 0, NULL, 1); 
		poller_run_timer_event(conf.async_io_proc, ASYNC_RUN_INTERVAL);
	}
	
	poller_async_set_resolve_cb(conf.poller.resolve_callback);
	poller_async_set_resolve_fail_cb(conf.poller.resolve_fail_callback);

	apm_track_counter(&conf.total_requests, "requests", NULL);
	apm_add_proc_labels(&conf.total_requests);
	apm_track_counter(&conf.total_responces, "responces", NULL);
	apm_add_proc_labels(&conf.total_responces);
	apm_add_heap_usage();

	return SUCCEED;
}

void *poller_get_item_specific_data(poller_item_t *poll_item)
{
	return poll_item->itemdata;
}

u_int64_t poller_get_item_id(poller_item_t *poll_item)
{
	return poll_item->itemid;
}

int poller_get_item_type(poller_item_t *poll_item ) {
	return poll_item->value_type;
}

void poller_set_item_specific_data(poller_item_t *poll_item, void *data)
{
	poll_item->itemdata = data;
}

poller_item_t *poller_get_poller_item(u_int64_t itemid)
{
	poller_host_t *host;
	poller_item_t *item;

	if (NULL == (item = zbx_hashset_search(&conf.items, &itemid)) ||
		NULL == (host = zbx_hashset_search(&conf.hosts, &item->hostid)))

		return NULL;

	return item;
}

void poller_return_item_to_queue(poller_item_t *item)
{
	DEBUG_ITEM(item->itemid, "Item returned to the poller's queue");
	item->lastpolltime = glb_ms_time();
	item->state = POLL_QUEUED;
}

#define CONFIG_REPOLL_DELAY 10

void poller_return_delayed_item_to_queue(poller_item_t *item)
{
	int nextcheck =  CONFIG_REPOLL_DELAY + rand() % 5 ;
	//LOG_INF("Item %ld returned to the poller's queue, will repoll in %d msec", item->itemid, nextcheck);
	DEBUG_ITEM(item->itemid,"Item returned to the poller's queue, will repoll in %d sec", nextcheck);
	
	glb_state_item_update_nextcheck(item->itemid, time(NULL) + nextcheck * 1000 );
	item->lastpolltime = glb_ms_time();
	item->state = POLL_QUEUED;

	poller_disable_event(item->poll_event);
	poller_run_timer_event(item->poll_event, nextcheck * 1000);
}

u_int64_t poller_get_host_id(poller_item_t *item)
{
	poller_host_t *host;

	if (NULL == (host = zbx_hashset_search(&conf.hosts, &item->hostid)))
		return 0;

	return host->hostid;
}

int poller_if_host_is_failed(poller_item_t *item)
{
	poller_host_t *host;

	if (NULL == (host = zbx_hashset_search(&conf.hosts, &item->hostid)))
		return SUCCEED;
	if (host->disabled_till > time(NULL))
		return SUCCEED;

	return FAIL;
}

void poller_register_item_timeout(poller_item_t *item)
{
	poller_host_t *glb_host;

	if (NULL != (glb_host = (poller_host_t *)zbx_hashset_search(&conf.hosts, &item->hostid)) &&
		(++glb_host->fails > GLB_MAX_FAILS))
	{
		glb_host->fails = 0;
		glb_host->disabled_till = time(NULL) + CONFIG_UNREACHABLE_DELAY;
	}
}

void poller_register_item_succeed(poller_item_t *item)
{
	poller_host_t *glb_host;

	if (NULL != (glb_host = (poller_host_t *)zbx_hashset_search(&conf.hosts, &item->hostid)))
	{
		glb_host->fails = 0;
		glb_host->disabled_till = 0;
	}
}

void poller_set_poller_callbacks(init_item_cb init_item, delete_item_cb delete_item,
								 handle_async_io_cb handle_async_io, start_poll_cb start_poll, shutdown_cb shutdown,
								 forks_count_cb forks_count, poller_resolve_cb resolve_callback,
								 poller_resolve_fail_cb resolve_fail_callback)
{
	conf.poller.init_item = init_item;
	conf.poller.delete_item = delete_item;
	conf.poller.handle_async_io = handle_async_io;
	conf.poller.start_poll = start_poll;
	conf.poller.shutdown = shutdown;
	conf.poller.forks_count = forks_count;
	conf.poller.resolve_callback = resolve_callback;
	conf.poller.resolve_fail_callback = resolve_fail_callback;
}

void poller_preprocess_error(poller_item_t *poller_item, const char *error)  
{
	preprocess_error(poller_item->hostid, poller_item->itemid, poller_item->flags, NULL, (char*)error);
	glb_state_item_set_error(poller_item->itemid, error);
	glb_state_interfaces_register_fail(poller_item->interfaceid, error);
}

void poller_preprocess_uint64(poller_item_t *poller_item, zbx_timespec_t *ts, u_int64_t value, int orig_type) {
	
	if (ITEM_VALUE_TYPE_FLOAT == orig_type) {
		double dbl_val = value;
		preprocess_dbl(poller_item->hostid, poller_item->itemid, poller_item->flags, ts, dbl_val);
	} else 
		preprocess_uint64(poller_item->hostid, poller_item->itemid, poller_item->flags, ts, value);

	glb_state_interfaces_register_ok(poller_item->interfaceid, "Polled normally");
}

void poller_preprocess_str(poller_item_t *poller_item, zbx_timespec_t *ts, const char *value) {
	preprocess_str(poller_item->hostid, poller_item->itemid, poller_item->flags, ts, value);
	glb_state_interfaces_register_ok(poller_item->interfaceid, "Polled normally");
}

void poller_preprocess_dbl(poller_item_t *poller_item, zbx_timespec_t *ts, double dbl_value) {
	preprocess_dbl(poller_item->hostid, poller_item->itemid, poller_item->flags, ts, dbl_value);
	glb_state_interfaces_register_ok(poller_item->interfaceid, "Polled normally");
}

void poller_preprocess_agent_result_value(poller_item_t *poller_item, zbx_timespec_t *ts, AGENT_RESULT *ar) {
	preprocess_agent_result(poller_item->hostid, poller_item->itemid, poller_item->flags, ts, ar, poller_item->value_type);
	glb_state_interfaces_register_ok(poller_item->interfaceid, "Polled normally");
}

void poller_inc_responses()
{
	conf.responces++;
}

void poller_inc_requests()
{
	conf.requests++;
}

void poller_items_iterate(items_iterator_cb iter_func, void *data)
{
	zbx_hashset_iter_t iter;
	poller_item_t *item;
	unsigned char ret = POLLER_ITERATOR_CONTINUE;

	zbx_hashset_iter_reset(&conf.items, &iter);

	while (POLLER_ITERATOR_CONTINUE == ret &&
		   NULL != (item = zbx_hashset_iter_next(&iter)))
	{
		ret = iter_func(item, data);
	}
}

void poller_strpool_free(const char *str)
{
	strpool_free(&conf.strpool, str);
};

const char *poller_strpool_add(const char *str)
{
	return strpool_add(&conf.strpool, str);
};

const char *poller_strpool_copy(const char *str)
{
	return strpool_copy(str);
};


void set_poller_proc_info(zbx_thread_args_t *args)
{
	zbx_thread_poller_args *poller_args_in = (zbx_thread_poller_args *)(args->args);

	conf.procinfo.server_num = args->info.server_num;
	conf.procinfo.process_num = args->info.process_num;
	conf.procinfo.process_type = args->info.process_type;
	conf.procinfo.program_type = poller_args_in->zbx_get_program_type_cb_arg();
}

/*in milliseconds */
#define STAT_INTERVAL 5000

ZBX_THREAD_ENTRY(glbpoller_thread, args)
{
	set_poller_proc_info((zbx_thread_args_t *)args);

	if (SUCCEED != poller_init(args))
		exit(-1);

	LOG_INF("%s #%d started [%s  #%d]", get_program_type_string(conf.procinfo.program_type),
			conf.procinfo.server_num, get_process_type_string(conf.procinfo.process_type), conf.procinfo.process_num);

	zbx_setproctitle("%s #%d [started]", get_process_type_string(conf.procinfo.process_type), conf.procinfo.process_num);

	poller_async_loop_run();

	poll_shutdown(&conf);

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(conf.procinfo.process_type), conf.procinfo.process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
	
#undef STAT_INTERVAL
}