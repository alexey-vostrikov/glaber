#include "common.h"
#include "db.h"
#include "dbcache.h"
#include "comms.h"
#include "daemon.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "preproc.h"
#include "../events.h"
#include "log.h"
#include "glb_poller.h"
#include "glb_pinger.h"
#include "glb_worker.h"
#include "glb_server.h"
#include "glb_agent.h"
#include "snmp.h"
//#include "../../libs/glb_state/state.h"
#include "../../libs/glb_state/glb_state_items.h"
#include "../poller/poller.h"

#include "../../libs/zbxexec/worker.h"

extern unsigned char process_type, program_type;
extern int server_num, process_num;
extern int CONFIG_GLB_REQUEUE_TIME;
extern int CONFIG_CONFSYNCER_FREQUENCY;

#define EVENT_ITEM_POLL 1
#define EVENT_NEW_ITEMS_CHECK 2

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
	void *itemdata;		  // item type specific data
	u_int64_t event_time; // rules if the item should be updated or it's the same config
};

typedef struct
{
	unsigned int time;
	char type;
	zbx_uint64_t id; // using weak linking assuming events might be outdated
	int change_time;
} poller_event_t;

typedef struct
{
	init_item_cb init_item;
	delete_item_cb delete_item;
	handle_async_io_cb handle_async_io;
	start_poll_cb start_poll;
	shutdown_cb shutdown;
	forks_count_cb forks_count;
	void *poller_data;
} poll_module_t;

struct poll_engine_t
{
	poll_module_t poller;

	unsigned char item_type;

	//	zbx_binary_heap_t events;
	event_queue_t *event_queue;
	zbx_hashset_t items;
	zbx_hashset_t hosts;

	int next_stat_time;
	int old_activity;

	u_int64_t requests;
	u_int64_t responces;
};

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
	char *error;
	const char *delay;
	poller_host_t *glb_host;

	if (SUCCEED == is_active_item_type(poller_item->item_type))
		delay = poller_item->delay;
	else
		delay = "86400s";

	if (SUCCEED != zbx_interval_preproc(delay, &simple_interval, &custom_intervals, &error))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "Itemd %ld has wrong delay time set :%s", poller_item->itemid, poller_item->delay);
		return FAIL;
	}

	if (NULL == (glb_host = zbx_hashset_search(&conf.hosts, &poller_item->hostid)))
	{
		LOG_WRN("No host has been found for itemid %ld", poller_item->itemid);
		return FAIL;
	}

	if (glb_host->disabled_till > mstime)
	{
		nextcheck = calculate_item_nextcheck_unreachable(simple_interval,
														 custom_intervals, glb_host->disabled_till);
	}
	else
	{
		nextcheck = calculate_item_nextcheck(poller_item->itemid, poller_item->item_type, simple_interval,
											 custom_intervals, mstime / 1000);
	}
	/*note: since original algo is still seconds-based adding some millisecond-noise
	to distribute checks evenly during one second */
	nextcheck = nextcheck * 1000 + rand() % 1000;

	zbx_custom_interval_free(custom_intervals);

	DEBUG_ITEM(poller_item->itemid, "Added item %ld poll event in %ld msec", poller_item->itemid, nextcheck - mstime);

	glb_state_item_update_nextcheck(poller_item->itemid, nextcheck);
	poller_item->event_time = nextcheck;

	event_queue_add_event(conf.event_queue, nextcheck, EVENT_ITEM_POLL, (void *)poller_item->itemid);
	return SUCCEED;
}

static int add_item_to_host(u_int64_t hostid)
{

	poller_host_t *glb_host, new_host = {0};

	if (NULL == (glb_host = (poller_host_t *)zbx_hashset_search(&conf.hosts, &hostid)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Creating a new host %ld", hostid);

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
	return conf.poller.forks_count(&conf.poller.poller_data);
}

int glb_poller_delete_item(void *poller_data, u_int64_t itemid)
{
	poller_item_t *poller_item;

	if (NULL != (poller_item = (poller_item_t *)zbx_hashset_search(&conf.items, &itemid)))
	{
		DEBUG_ITEM(itemid, "Item has been deleted, removing from the poller config");

		LOG_DBG("Calling item delete");
		conf.poller.delete_item(conf.poller.poller_data, poller_item);

		zbx_heap_strpool_release(poller_item->delay);
		zbx_hashset_remove_direct(&conf.items, poller_item);
		LOG_DBG("in: %s: ended", __func__);
		return SUCCEED;
	}

	// LOG_INF("Item %ld hasn't been found in the local queue", itemid);
	return FAIL;
}

static int get_simple_interval(const char *delay)
{
	int interval;
	char *delim;

	if (SUCCEED != is_time_suffix(delay, &interval, (int)(NULL == (delim = strchr(delay, ';')) ? ZBX_LENGTH_UNLIMITED : delim - delay)))
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
int glb_poller_create_item(void *poll_data, DC_ITEM *dc_item)
{
	poller_item_t *poller_item, local_glb_item;
	poller_host_t *glb_host;
	u_int64_t mstime = glb_ms_time();
	int i;

	DEBUG_ITEM(dc_item->itemid, "Creating/updating item");

	if (NULL != (poller_item = (poller_item_t *)zbx_hashset_search(&conf.items, &dc_item->itemid)))
	{
		LOG_DBG("Item %ld has changed, creating new configuration", poller_item->itemid);

		DEBUG_ITEM(dc_item->itemid, "Item has changed: re-creating new config");
		glb_poller_delete_item(conf.poller.poller_data, poller_item->itemid);
	//	LOG_INF("Fix improper update logic!");
	//	THIS_SHOULD_NEVER_HAPPEN;
	//	exit(-1);
	}

	DEBUG_ITEM(dc_item->itemid, "Adding new item to poller");

	bzero(&local_glb_item, sizeof(poller_item_t));

	local_glb_item.itemid = dc_item->itemid;

	if (NULL == (poller_item = (poller_item_t *)zbx_hashset_insert(&conf.items, &local_glb_item, sizeof(poller_item_t))))
		return FAIL;

	poller_item->state = POLL_QUEUED;
	poller_item->hostid = dc_item->host.hostid;
	poller_item->delay = zbx_heap_strpool_intern(dc_item->delay);
	poller_item->value_type = dc_item->value_type;
	poller_item->flags = dc_item->flags;
	poller_item->item_type = dc_item->type;

	add_item_to_host(poller_item->hostid);

	if (FAIL == conf.poller.init_item(&conf.poller.poller_data, dc_item, poller_item))
	{
		LOG_DBG("Item creation func failed, not adding item %ld", poller_item->itemid);

		zbx_heap_strpool_release(poller_item->delay);
		delete_item_from_host(poller_item->hostid);
		zbx_hashset_remove(&conf.items, &poller_item);

		return FAIL;
	};

	if (get_simple_interval(poller_item->delay) > 0) {
		poller_item->event_time = mstime;
		event_queue_add_event(conf.event_queue, mstime, EVENT_ITEM_POLL, (void *)poller_item->itemid);
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
	LOG_INF("Doing module init call");
	switch (conf.item_type)
	{
#ifdef HAVE_NETSNMP
	case ITEM_TYPE_SNMP:
		async_snmp_init(&conf);
		break;
#endif
	case ITEM_TYPE_SIMPLE:
		glb_pinger_init(&conf);
		break;

	case ITEM_TYPE_EXTERNAL:
		glb_worker_init(&conf);
		break;

	case ITEM_TYPE_WORKER_SERVER:
		return glb_worker_server_init(&conf);
		break;

	case ITEM_TYPE_AGENT:
		glb_agent_init(&conf);
		break;

	default:
		LOG_WRN("Cannot init worker for item type %d, this is a BUG", conf.item_type);
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
	LOG_INF("Finished module init call");
	return SUCCEED;
}

void poll_shutdown()
{

	conf.poller.shutdown(conf.poller.poller_data);
	zbx_hashset_destroy(&conf.hosts);
	zbx_hashset_destroy(&conf.items);
	event_queue_destroy(conf.event_queue);
	zbx_heap_strpool_destroy();
	glb_heap_binpool_destroy();
}

static int sumtime(u_int64_t *sum, zbx_timespec_t start)
{
	zbx_timespec_t ts;
	zbx_timespec(&ts);
	*sum = *sum + (ts.sec - start.sec) * 1000000000 + ts.ns - start.ns;
}

EVENT_QUEUE_CALLBACK(item_poll_cb)
{
	poller_item_t *poller_item;
	u_int64_t mstime = glb_ms_time();

	if (NULL == (poller_item = poller_get_pollable_item((u_int64_t)data)))
		return FAIL;

	DEBUG_ITEM(poller_item->itemid, "Item poll event");
	
	if (poller_item->event_time != event_time)
	{
		// this means the item has been rescheduled since the last scheduling and current event is outdated
		DEBUG_ITEM(poller_item->itemid, "Outdated event has been found in the event queue, skipping")
	//	LOG_DBG("Item %ld poll skipped, event's time is %d, item's is %d", poller_item->itemid, event_time, poller_item->event_time);
		return FAIL;
	}

	// checking if host is disabled
	if (SUCCEED == poller_if_host_is_failed(poller_item))
	{
		LOG_DBG("Skipping item %ld from polling, host is still down ",
				poller_item->itemid);
		// replaning item polling after disabled_till time
		add_item_check_event(poller_item, mstime + 10 * 1000);
		return FAIL;
	}

	add_item_check_event(poller_item, mstime);

	// the only state common poller logic care - _QUEUED - such an items are considred
	// to be pollabale, updatable and etc. All other states indicates the item is busy
	// with type specific poller logic
	if (POLL_QUEUED != poller_item->state)
	{
		//|| poller_item->ttl < now ) {
		DEBUG_ITEM(poller_item->itemid, "Skipping from polling, not in QUEUED state (%d)", poller_item->state);
		LOG_DBG("Not sending item %ld to polling, it's in the %d state or aged", poller_item->itemid, poller_item->state);
		return FAIL;
	}

	poller_item->state = POLL_POLLING;
	poller_item->lastpolltime = mstime;

	DEBUG_ITEM(poller_item->itemid, "Starting poller item poll");

	conf.poller.start_poll(conf.poller.poller_data, poller_item);

	return SUCCEED;
}

EVENT_QUEUE_CALLBACK(new_items_check_cb)
{
	poller_item_t *poller_item;
	u_int64_t mstime = glb_ms_time();
	int num;
	LOG_INF("new items check callback");
	num = DCconfig_get_glb_poller_items(&conf, conf.item_type, process_num);
	// sumtime(&get_items_time, ts_get_items);
	LOG_DBG("Event: got %d new items from the config cache", num);
	event_queue_add_event(conf.event_queue, mstime + 10 * 1000, EVENT_NEW_ITEMS_CHECK, NULL);

	zbx_hashset_iter_t iter;
	zbx_hashset_iter_reset(&conf.items, &iter);
	while (NULL != (poller_item = zbx_hashset_iter_next(&iter)))
	{
		// an item may wait for some time while other items will be polled and thus get timedout
		// so after one minute of waiting we consider its timed out anyway whatever the poll process thinks about it
		// but it's a poller's business to submit timeout result
		if (poller_item->lastpolltime + 5 * SEC_PER_MIN * 1000 < mstime && POLL_POLLING == poller_item->state)
		{
			LOG_INF("Item %ld has timed out in the poller, resetting it's queue state", poller_item->itemid);
			DEBUG_ITEM(poller_item->itemid, "Item has timeout in the poller, resetting the sate")
			poller_item->state = POLL_QUEUED;
		}
	}
}

static int poll_init(zbx_thread_args_t *args)
{

	zbx_heap_strpool_init();
	glb_heap_binpool_init();

	glb_preprocessing_init();
	conf.item_type = *(unsigned char *)((zbx_thread_args_t *)args)->args;

	zbx_hashset_create(&conf.items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_hashset_create(&conf.hosts, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	conf.event_queue = event_queue_init(NULL);

	event_queue_add_callback(conf.event_queue, EVENT_ITEM_POLL, item_poll_cb);
	event_queue_add_callback(conf.event_queue, EVENT_NEW_ITEMS_CHECK, new_items_check_cb);

	if (SUCCEED != poll_module_init())
	{
		LOG_WRN("Couldnt init type-specific module for type %d", conf.item_type);
		return FAIL;
	}

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

void poller_set_item_specific_data(poller_item_t *poll_item, void *data)
{
	poll_item->itemdata = data;
}

poller_item_t *poller_get_pollable_item(u_int64_t itemid)
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
	item->lastpolltime = time(NULL);
	item->state = POLL_QUEUED;
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

void poller_set_poller_module_data(void *data)
{
	conf.poller.poller_data = data;
}

typedef int (*init_item_cb)(void *mod_conf, DC_ITEM *dc_item, poller_item_t *glb_poller_item);
typedef void (*delete_item_cb)(void *mod_comf, poller_item_t *poller_item);
typedef void (*handle_async_io_cb)(void *mod_conf);
typedef void (*start_poll_cb)(void *mod_conf, poller_item_t *poller_item);
typedef void (*shutdown_cb)(void *mod_conf);
typedef int (*forks_count_cb)(void *mod_conf);

void poller_set_poller_callbacks(init_item_cb init_item, delete_item_cb delete_item,
								 handle_async_io_cb handle_async_io, start_poll_cb start_poll, shutdown_cb shutdown, forks_count_cb forks_count)
{
	conf.poller.init_item = init_item;
	conf.poller.delete_item = delete_item;
	conf.poller.handle_async_io = handle_async_io;
	conf.poller.start_poll = start_poll;
	conf.poller.shutdown = shutdown;
	conf.poller.forks_count = forks_count;
}

void poller_preprocess_value(poller_item_t *poller_item, AGENT_RESULT *result, u_int64_t mstime, unsigned char state, char *error)
{
	zbx_timespec_t ts = {.sec = mstime / 1000, .ns = (mstime % 1000) * 1000000};

	if (ITEM_STATE_NORMAL == state)
	{
		zbx_preprocess_item_value(poller_item->hostid, poller_item->itemid, poller_item->value_type, poller_item->flags,
								  result, &ts, state, NULL);
	}

	if (ITEM_STATE_NOTSUPPORTED == state)
	{
		zbx_preprocess_item_value(poller_item->hostid, poller_item->itemid, poller_item->value_type, poller_item->flags,
								  NULL, &ts, state, error);
	}
}

void poller_inc_responces()
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
		ret = iter_func(conf.poller.poller_data, item, data);
	}
}

/*in milliseconds */
#define STAT_INTERVAL 5000

ZBX_THREAD_ENTRY(glbpoller_thread, args)
{
	int old_activity = 0;
	u_int64_t old_stat_time = glb_ms_time();
	poller_item_t *poller_item;
	poller_host_t *glb_host;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	if (SUCCEED != poll_init((zbx_thread_args_t *)args))
		exit(-1);

	event_queue_add_event(conf.event_queue, glb_ms_time() + (10 + process_num) * 1000, EVENT_NEW_ITEMS_CHECK, NULL);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			   server_num, get_process_type_string(process_type), process_num);

	while (ZBX_IS_RUNNING())
	{
		u_int64_t mstime = glb_ms_time();
		zbx_update_env(zbx_time());

		LOG_DBG("In %s() processing events", __func__);

		if (0 == event_queue_process_events(conf.event_queue, 200))
		{
	//		usleep(100000);
		}

		conf.poller.handle_async_io(conf.poller.poller_data);

		if (old_activity == conf.requests + conf.responces)
		{
	//		usleep(100000);
		}
		old_activity = conf.requests + conf.responces;

		if (old_stat_time + STAT_INTERVAL < mstime)
		{
			zbx_setproctitle("%s #%d [sent %ld chks/sec, got %ld chcks/sec, items: %d, events planned: %d]",
							 get_process_type_string(process_type), process_num, (conf.requests * 1000) / (mstime - old_stat_time),
							 (conf.responces * 1000) / (mstime - old_stat_time), conf.items.num_data,
							 event_queue_get_events_count(conf.event_queue));
			conf.requests = 0;
			conf.responces = 0;
			old_stat_time = mstime;
		}
	}

	poll_shutdown(&conf);

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}