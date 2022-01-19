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
#include "glb_cache.h"
#include "glb_cache_items.h"
#include "../poller/poller.h"
#include "../poller/checks_snmp.h"
#include "../../libs/zbxexec/worker.h"

extern unsigned char process_type, program_type;
extern int server_num, process_num;
extern int CONFIG_GLB_REQUEUE_TIME;
extern int CONFIG_CONFSYNCER_FREQUENCY;

typedef struct
{
	unsigned int time;
	char type;
	zbx_uint64_t id; //using weak linking assuming events might be outdated
	int  change_time;
} poller_event_t;



u_int64_t glb_ms_time() {
	u_int64_t sec;

	zbx_timespec_t	ts;
	zbx_timespec(&ts);

	sec = ts.sec;
	return sec * 1000 + ts.ns/1000000;
}


int event_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const poller_event_t *i1 = (const poller_event_t *)e1->data;
	const poller_event_t *i2 = (const poller_event_t *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->time, i2->time);
	return 0;
}

#define add_event(events , type, id, time_sec) add_event_ext((events), (type), (id), (time_sec), 0)

/* this is a good candidate for macro */
static void add_event_ext(zbx_binary_heap_t *events, char type, zbx_uint64_t id, unsigned int time, int change_time)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	
	poller_event_t *event = zbx_calloc(NULL,0, sizeof(poller_event_t));

	event->id = id;
	event->type = type;
	event->time = time;
	event->change_time = change_time;

	zbx_binary_heap_elem_t elem = {time, (const void *)event};
	zbx_binary_heap_insert(events, &elem);
	zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);	
}

static void schedule_item_poll(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *glb_item) {
	void (*poll_func)(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM* glb_item);
	
	glb_item->state = POLL_POLLING;
	glb_item->lastpolltime = time(NULL);
	
	poll_func = poll_mod->start_poll;
	poll_func(poll_mod,glb_item);

}

static int is_active_item_type(unsigned char item_type) {
	switch (item_type) {
		case ITEM_TYPE_WORKER_SERVER:
			return FAIL;
	}
	
	return SUCCEED;
}
/******************************************************************
 * adds next check event to the events b-tree					  *
 * ****************************************************************/
static int add_item_check_event(zbx_binary_heap_t *events, zbx_hashset_t *hosts, GLB_POLLER_ITEM *glb_item, int now)
{
	int simple_interval;
	unsigned long nextcheck;
	zbx_custom_interval_t *custom_intervals;
	char *error;
	const char *delay;
	GLB_POLLER_HOST *glb_host;
	
	
	if (SUCCEED == is_active_item_type(glb_item->item_type)) 
		delay = glb_item->delay;
	 else 
		delay = "86400s";
	

	if (SUCCEED != zbx_interval_preproc(delay, &simple_interval, &custom_intervals, &error))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "Itemd %ld has wrong delay time set :%s", glb_item->itemid, glb_item->delay);
		return FAIL;
	}
	
	if ( NULL == (glb_host = zbx_hashset_search(hosts, &glb_item->hostid)))  {
		LOG_WRN("No host has been fount for itemid %ld", glb_item->itemid);
		return FAIL;
	}
	
	if ( glb_host->disabled_till > now ) {
		nextcheck = calculate_item_nextcheck_unreachable(simple_interval,
																   custom_intervals, glb_host->disabled_till);
	}	else
	{
		nextcheck = calculate_item_nextcheck(glb_host->hostid, glb_item->item_type, simple_interval,
											   custom_intervals, now);
	}
	
	zbx_custom_interval_free(custom_intervals);
	DEBUG_ITEM(glb_item->itemid,"Added item %ld poll event in %ld sec", glb_item->itemid, nextcheck - now);
		
	glb_cache_item_meta_t meta;
	meta.nextcheck = nextcheck;
	glb_cache_item_update_meta( glb_item->itemid, &meta,  GLB_CACHE_ITEM_UPDATE_NEXTCHECK, glb_item->value_type);
	
	add_event_ext(events, GLB_EVENT_ITEM_POLL, glb_item->itemid, nextcheck, glb_item->change_time);

	return SUCCEED;
}

static int add_item_to_host(glb_poll_engine_t *poll, u_int64_t hostid) {

	GLB_POLLER_HOST *glb_host, new_host = {0};

	if (NULL == (glb_host = (GLB_POLLER_HOST *)zbx_hashset_search(&poll->hosts, &hostid))) {
		zabbix_log(LOG_LEVEL_DEBUG, "Creating a new host %ld", hostid);

		GLB_POLLER_HOST new_host = {0};

		new_host.hostid = hostid;
		glb_host = zbx_hashset_insert(&poll->hosts, &new_host, sizeof(GLB_POLLER_HOST));
	} 

	glb_host->items++;
}

static void delete_item_from_host( glb_poll_engine_t *poll, u_int64_t hostid) {
	GLB_POLLER_HOST *glb_host;

	//LOG_INF("Deleting item from host %ld",hostid);
	if (NULL != (glb_host=zbx_hashset_search(&poll->hosts, &hostid))) {
		glb_host->items--;

		if (0 == glb_host->items)  {
	//		LOG_INF("Deleting host %ld, no items left",hostid);
			zbx_hashset_remove_direct(&poll->hosts, glb_host);
		}
	}
	//LOG_INF("Finished delete");
}

int glb_poller_get_forks(void *poller_data) {
	glb_poll_engine_t *poll = (glb_poll_engine_t *)poller_data;
	int (*forks_count_func)(glb_poll_module_t * poll_mod);
	forks_count_func = poll->poller.forks_count;
	return forks_count_func(&poll->poller);
}

int glb_poller_delete_item(void *poller_data, u_int64_t itemid) {
	
	GLB_POLLER_ITEM *glb_item;
	glb_poll_engine_t *poll = (glb_poll_engine_t *)poller_data;
	void (*item_free_func)(glb_poll_module_t *poll_mod, GLB_POLLER_ITEM *item);	
	item_free_func = poll->poller.delete_item;
	
	if (NULL != (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_search(&poll->items, &itemid))){
		DEBUG_ITEM(itemid,"Item has been deleted, removing from the poller config");		
		
		LOG_DBG("Calling item delete");
		item_free_func(&poll->poller, glb_item);

		//zbx_heap_strpool_release(glb_item->delay);
		zbx_hashset_remove_direct(&poll->items, glb_item);
		LOG_DBG("in: %s: ended",__func__);
		return SUCCEED;
	} 
	else {
		//LOG_INF("Item %ld hasn't been found in the local queue", itemid);
		return FAIL;
	}

}

/******************************************************************
* Creates new item for polling, calls appropriate conversion func  *
* for the type, and fills the new glb_item with key vals          *
* adds new item to the items hashset, if one already exists,      *
* updates it (old key vals are released), updates or creates      *
* a hosts entry for the item						  			  *
******************************************************************/
int glb_poller_create_item(void *poll_data, DC_ITEM *dc_item)
{
	glb_poll_engine_t *poll = (glb_poll_engine_t *)poll_data;
		
	int (*item_create_func)(glb_poll_module_t *poll_mod, DC_ITEM * dc_item, GLB_POLLER_ITEM* glb_item);
	item_create_func = poll->poller.init_item;

	GLB_POLLER_ITEM *glb_item, local_glb_item;
	GLB_POLLER_HOST *glb_host;
	unsigned int now = time(NULL);
	int i;
	
	DEBUG_ITEM(dc_item->itemid,"Creating/updating item");
	
	if (NULL != (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_search(&poll->items, &dc_item->itemid)))
	{	
		LOG_DBG("Item %ld has changed, creating new configuration", glb_item->itemid);
		
		DEBUG_ITEM(dc_item->itemid,"Item has changed: re-creating new config");		
		glb_poller_delete_item(poll, glb_item->itemid);
	}

	DEBUG_ITEM(dc_item->itemid,"Adding new item to poller");
	
	bzero(&local_glb_item, sizeof(GLB_POLLER_ITEM));

	local_glb_item.itemid = dc_item->itemid;

	if (NULL == (glb_item =(GLB_POLLER_ITEM*) zbx_hashset_insert(&poll->items, &local_glb_item, sizeof(GLB_POLLER_ITEM)))) 
		return FAIL;

	glb_item->state = POLL_QUEUED;
	glb_item->hostid = dc_item->host.hostid;
	glb_item->delay = zbx_heap_strpool_intern(dc_item->delay);
	glb_item->value_type = dc_item->value_type;
	glb_item->flags = dc_item->flags;
	glb_item->item_type = dc_item->type;
	glb_item->change_time = now;
	add_item_to_host(poll, glb_item->hostid);

	if (FAIL == item_create_func(&poll->poller, dc_item, glb_item)) {
		LOG_DBG("Item creation func failed, not adding item %ld", glb_item->itemid);
		zbx_heap_strpool_release(glb_item->delay);
		delete_item_from_host(poll,glb_item->hostid);
		zbx_hashset_remove(&poll->items,&glb_item);
		return FAIL;
	};

	//newly added items are planned to be polled immediately
	add_event_ext(&poll->events, GLB_EVENT_ITEM_POLL, glb_item->itemid, now, glb_item->change_time);
	
	return SUCCEED;
}


void glb_poller_engine_shutdown(glb_poll_engine_t *poll) {
	void (*shutdown_func)(glb_poll_module_t *poll_mod);

	shutdown_func = poll->poller.shutdown;
	shutdown_func(&poll->poller);
		
}

void add_host_fail(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now) {
	GLB_POLLER_HOST *glb_host;

	if ( NULL != (glb_host = (GLB_POLLER_HOST*)zbx_hashset_search(hosts,&hostid)) &&
		( ++glb_host->fails > GLB_MAX_FAILS) ) {
		
		glb_host->fails = 0;
		glb_host->disabled_till = now + CONFIG_UNREACHABLE_DELAY;		
	}
}

void add_host_succeed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now) {
	GLB_POLLER_HOST *glb_host;

	if ( NULL != (glb_host = (GLB_POLLER_HOST*)zbx_hashset_search(hosts,&hostid)) ) {
		glb_host->fails = 0;
		glb_host->disabled_till = 0;		
	}
}

int  host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now) {
	GLB_POLLER_HOST *glb_host;

	if ( NULL != (glb_host = (GLB_POLLER_HOST*)zbx_hashset_search(hosts,&hostid)) && glb_host->disabled_till > now ) 
		return SUCCEED;
	
	return FAIL;
}

/****************************************************
 * item-type specific io processing, assumed async	*
 * i.e. it shouldn't block or wait for long 		*
 * operatins										*
 * *************************************************/
static void handle_async_io(glb_poll_module_t *poll_mod) {
	void (*handle_async_io_func)(glb_poll_module_t *poll_mod);

	handle_async_io_func = poll_mod->handle_async_io;
	
	handle_async_io_func(poll_mod);
}

static int poll_module_init(glb_poll_engine_t *poll) {
	switch (poll->item_type) {
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP:
			glb_snmp_init(poll);
			break;
#endif
		case ITEM_TYPE_SIMPLE:
			glb_pinger_init(poll);
			break;
		
		case ITEM_TYPE_EXTERNAL:
			glb_worker_init(poll);
			break;

		case ITEM_TYPE_WORKER_SERVER:
			return glb_worker_server_init(poll);
			break;
		
		case ITEM_TYPE_AGENT:
			glb_agent_init(poll);
			break;

		default: 
			LOG_WRN("Cannot init worker for item type %d, this is a BUG",poll->item_type);
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
	return SUCCEED;
}

static int poll_init(glb_poll_engine_t *poll, zbx_thread_args_t *args) {
	
	zbx_heap_strpool_init();
	glb_heap_binpool_init();

	glb_preprocessing_init();
	poll->item_type = *(unsigned char *)((zbx_thread_args_t *)args)->args;

	zbx_hashset_create(&poll->items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_hashset_create(&poll->hosts, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_binary_heap_create(&poll->events, event_elem_compare, 0);
	
	if (SUCCEED != poll_module_init(poll)) {
		LOG_WRN("Couldnt init type-specific module for type %d",poll->item_type);
		return FAIL;
	}

	return SUCCEED;
}

void poll_shutdown(glb_poll_engine_t *poll) {

	glb_poller_engine_shutdown(poll);
	zbx_hashset_destroy(&poll->hosts);
	zbx_hashset_destroy(&poll->items);
	zbx_binary_heap_destroy(&poll->events);
	zbx_heap_strpool_destroy();
	glb_heap_binpool_destroy();

}


static int sumtime(u_int64_t *sum, zbx_timespec_t start) {
	zbx_timespec_t ts;
	zbx_timespec(&ts);
	*sum = *sum + (ts.sec - start.sec) * 1000000000 + ts.ns - start.ns;
}



ZBX_THREAD_ENTRY(glbpoller_thread, args)
{

	//static u_int64_t total_time, get_items_time;
	//zbx_timespec_t ts_total, ts_get_items;

	int old_activity=0, next_stat_time=0;
	glb_poll_engine_t poll ={0};
	GLB_POLLER_ITEM *glb_item;
	GLB_POLLER_HOST *glb_host;
	
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;
	
	if (SUCCEED != poll_init(&poll, (zbx_thread_args_t *)args) )
		exit(-1);
	
	//add_event(&poll.events, GLB_EVENT_AGING, 0, time(NULL) + 1);
	add_event(&poll.events, GLB_EVENT_NEW_ITEMS_CHECK, 0, time(NULL) + process_num);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
		server_num, get_process_type_string(process_type), process_num);
	
#define STAT_INTERVAL 5
	LOG_DBG("In %s() starting main loop", __func__);		
	
	while (ZBX_IS_RUNNING())
	{
		unsigned int now = time(NULL);
		double sec = zbx_time();
		zbx_update_env(sec);

		while (FAIL == zbx_binary_heap_empty(&poll.events))
		{
			const zbx_binary_heap_elem_t *min;

			min = zbx_binary_heap_find_min(&poll.events);
			
			poller_event_t *event = (poller_event_t *)min->data;
			zabbix_log(LOG_LEVEL_DEBUG, "Got event loop for id %ld run in %d seconds",event->id, event->time - now);
			
			if ( event->time > now) break;
			
			zbx_binary_heap_remove_min(&poll.events);
							
			switch (event->type) {
			
			case GLB_EVENT_ITEM_POLL:
				if (NULL != (glb_item = zbx_hashset_search(&poll.items, &event->id)) && 
					NULL != (glb_host = zbx_hashset_search(&poll.hosts, &glb_item->hostid))) {
						
					DEBUG_ITEM(glb_item->itemid,"Item poll event");
					LOG_DBG("Item %ld poll event", glb_item->itemid);

					if (glb_item->change_time > event->change_time) { 
						//this means the item has been changed since last scheduling and current event is outdated (a new event has already been planned)
						DEBUG_ITEM(glb_item->itemid,"Outdated event has been found in the event queue, skipping")
						LOG_DBG("Item %ld poll skipped, event's time is %d, item's is %d", glb_item->itemid, event->change_time, glb_item->change_time);

						break;
					
					}

					//checking if host is disabled
					if (glb_host->disabled_till > now) { 
						LOG_DBG("Skipping item %ld from polling, host is down for %ld sec",
					  	glb_item->itemid, glb_host->disabled_till - now);

						//replaning item polling after disabled_till time
						add_item_check_event(&poll.events, &poll.hosts, glb_item, glb_host->disabled_till);

					} else {
	
						add_item_check_event(&poll.events, &poll.hosts, glb_item, now);
						
						//the only state common poller logic care - _QUEUED - such an items are considred
						//to be pollabale, updatable and etc. All other states indicates the item is busy
						//with type specific poller logic
						if ( POLL_QUEUED != glb_item->state ) {
						//|| glb_item->ttl < now ) {
							DEBUG_ITEM(glb_item->itemid, "Skipping from polling, not in QUEUED state (%d)",glb_item->state);
							LOG_DBG("Not sending item %ld to polling, it's in the %d state or aged", glb_item->itemid, glb_item->state);
						} else {
							//ok, adding the item to the poller's internal logic for polling		
							glb_item->state = POLL_POLLING;
							glb_item->lastpolltime = now;
					
							DEBUG_ITEM(glb_item->itemid,"Starting poller item poll");
							schedule_item_poll(&poll.poller, glb_item);
						}
					}
				} 
			
			break;

			case GLB_EVENT_NEW_ITEMS_CHECK: {
				int num;

				
			//	zbx_timespec(&ts_get_items);
				num = DCconfig_get_glb_poller_items(&poll, poll.item_type, process_num);
				//sumtime(&get_items_time, ts_get_items);
				LOG_DBG("Event: got %d new items from the config cache", num);
				add_event(&poll.events, GLB_EVENT_NEW_ITEMS_CHECK, 0, now+1); 
				zbx_hashset_iter_t iter;
				zbx_hashset_iter_reset(&poll.items, &iter );
				while ( NULL != (glb_item=zbx_hashset_iter_next(&iter))) {
					//an item may wait for some time while other items will be polled and thus get timedout
					//so after one minute of waiting we consider its timed out anyway whatever the poll process thinks about it
					//but it's a poller's business to submit timeout result
					if (glb_item->lastpolltime + SEC_PER_MIN < now && POLL_POLLING == glb_item->state) {
						LOG_DBG("Item %ld has timed out in the poller, resetting it's queue state",glb_item->itemid);
						DEBUG_ITEM(glb_item->itemid, "Item has timeout in the poller, resetting the sate")
						glb_item->state = POLL_QUEUED;
					}
				}	
			}
			break;
				
			default:
				zabbix_log(LOG_LEVEL_WARNING, "Event: unknown event %d in the message queue", event->type);
				THIS_SHOULD_NEVER_HAPPEN;
				exit(-1);	
			}
			zbx_free(event);
		}

		handle_async_io(&poll.poller);
		
		if ( old_activity == poll.poller.requests + poll.poller.responses ) {
			usleep(1000000);
		}
		old_activity = poll.poller.requests + poll.poller.responses;

		if (next_stat_time < now ) {
			next_stat_time = now + STAT_INTERVAL;
			zbx_setproctitle("%s #%d [sent %d chks/sec, got %d chcks/sec, items: %d, events planned: %d]",
 					get_process_type_string(process_type), process_num, poll.poller.requests/STAT_INTERVAL, 
					 	poll.poller.responses/STAT_INTERVAL, poll.items.num_data, poll.events.elems_num);
			poll.poller.requests = 0; 
			poll.poller.responses = 0;
		
		}
	}
	
	poll_shutdown(&poll);
	
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}

