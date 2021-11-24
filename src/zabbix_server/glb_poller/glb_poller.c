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

extern char *CONFIG_SNMP_WORKER_LOCATION;

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

	const GLB_POLLER_EVENT *i1 = (const GLB_POLLER_EVENT *)e1->data;
	const GLB_POLLER_EVENT *i2 = (const GLB_POLLER_EVENT *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->time, i2->time);
	return 0;
}

/* this is a good candidate for macro */
static void add_event(zbx_binary_heap_t *events, char type, zbx_uint64_t id, unsigned int time)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s: starting", __func__);
	GLB_POLLER_EVENT *event = zbx_malloc(NULL, sizeof(GLB_POLLER_EVENT));

	event->id = id;
	event->type = type;
	event->time = time;

	zbx_binary_heap_elem_t elem = {time, (const void *)event};
	zbx_binary_heap_insert(events, &elem);
	zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);	
}

/****************************************************************
 * deletes the item, does all the cleaning job, including items *
 * and hosts hashsets. 
 ****************************************************************/
//static
void glb_free_item_data(void *engine, GLB_POLLER_ITEM *glb_item)
{
	if (NULL == glb_item->itemdata) {
		zabbix_log(LOG_LEVEL_WARNING, "Called clearing of item %ld which is already cleared, this is BUG", glb_item->itemid);
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}

	switch (glb_item->item_type)
	{
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP: 
			glb_snmp_free_item( (GLB_SNMP_ITEM*) glb_item->itemdata);
			break;
	
#endif
		case ITEM_TYPE_SIMPLE: /* only pinger items are handled actually */
			glb_pinger_free_item( (GLB_PINGER_ITEM*) glb_item->itemdata );
			break;
		
		case ITEM_TYPE_EXTERNAL: /* only pinger items are handled actually */
			glb_worker_free_item( (GLB_WORKER_ITEM*) glb_item->itemdata );
			break;
		
		case ITEM_TYPE_TRAPPER: 
			glb_server_free_item(engine, glb_item);
			break;

		case ITEM_TYPE_AGENT: 
			glb_agent_free_item((GLB_AGENT_ITEM*)glb_item->itemdata);
			break;
			
		default:
			zabbix_log(LOG_LEVEL_WARNING,"Cannot free unsupport item typ %d, this is a BUG",glb_item->item_type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}
	zbx_free(glb_item->itemdata);
	glb_item->itemdata = NULL;

}
/************************************************
* general "interface" to start polling an item  *
* adds an item to the joblist 					*
* for a certain connection type					*
************************************************/
static void glb_poller_schedule_poll_item(void *engine, GLB_POLLER_ITEM *glb_item) {

	glb_item->state = POLL_POLLING;
	glb_item->lastpolltime = time(NULL);

	switch (glb_item->item_type)
	{
#ifdef HAVE_NETSNMP
	case ITEM_TYPE_SNMP:
		//adding the item to the connection's joblist
		glb_snmp_add_poll_item(engine, glb_item);
		break;
#endif
	case ITEM_TYPE_SIMPLE:
		glb_pinger_start_ping(engine, glb_item);
		break;

	case ITEM_TYPE_EXTERNAL:
		glb_worker_send_request(engine, glb_item);
		break;

	case ITEM_TYPE_AGENT:
		glb_agent_add_poll_item(engine, glb_item);
		break;

	default:
		zabbix_log(LOG_LEVEL_WARNING,"Unsupported item type %d has been send to polling, this is a BUG",glb_item->item_type);
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
		break;
	}
}
/******************************************************************
 * adds next check event to the events b-tree					  *
 * this is stripped version of DCItem_nextcheck_update			  *
 * ****************************************************************/
//static
int add_item_check_event(zbx_binary_heap_t *events, zbx_hashset_t *hosts, GLB_POLLER_ITEM *glb_item, int now)
{
	int simple_interval;
	unsigned long nextcheck;
	zbx_custom_interval_t *custom_intervals;
	char *error;
	GLB_POLLER_HOST *glb_host;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s - started", __func__);

	if (SUCCEED != zbx_interval_preproc(glb_item->delay, &simple_interval, &custom_intervals, &error))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "Itemd %ld has wrong delay time set :%s", glb_item->itemid, glb_item->delay);
		return FAIL;
	}

	if ( NULL != (glb_host = zbx_hashset_search(hosts, &glb_item->hostid)))  {
	  if ( glb_host->disabled_till > now ) {
			nextcheck = calculate_item_nextcheck_unreachable(simple_interval,
																   custom_intervals, glb_host->disabled_till);
		}	else
		{
			nextcheck = calculate_item_nextcheck(glb_host->hostid, glb_item->item_type, simple_interval,
													   custom_intervals, now);
		}
		zbx_custom_interval_free(custom_intervals);
		
		if (CONFIG_DEBUG_ITEM == glb_item->itemid) 
			zabbix_log(LOG_LEVEL_INFORMATION, "In %s - Added item %ld poll event in %ld sec", __func__, glb_item->itemid, nextcheck - now);
		
		glb_cache_item_meta_t meta;
		meta.nextcheck = nextcheck;
		
		glb_cache_item_update_meta( glb_item->itemid, &meta,  GLB_CACHE_ITEM_UPDATE_NEXTCHECK, glb_item->value_type);
		add_event(events, GLB_EVENT_ITEM_POLL, glb_item->itemid, nextcheck);
	} else {
		zabbix_log(LOG_LEVEL_INFORMATION, "No host has been fount for itemid %ld", glb_item->itemid);
	}
	return SUCCEED;
}

/******************************************************************
* Creates new item for polling, calls apropriate conversion func  *
* for the type, and fills the new glb_item with key vals          *
* adds new item to the items hashset, if one already exists,      *
* updates it (old key vals are released), udpates or creates      *
* a hosts entry for the item						  			  *
******************************************************************/
int glb_create_item(zbx_binary_heap_t *events, zbx_hashset_t *hosts, zbx_hashset_t *items, DC_ITEM *dc_item, void *poll_engine)
{
	GLB_POLLER_ITEM *glb_item;
	GLB_POLLER_ITEM new_glb_item;
	GLB_POLLER_HOST *glb_host;
	
	unsigned int now = time(NULL);
	int i;
	zabbix_log(LOG_LEVEL_DEBUG,"In %s: Started for item %ld", __func__, dc_item->itemid);
	
	DEBUG_ITEM(dc_item->itemid,"Creating/updating glb item");
	DEBUG_HOST(dc_item->host.hostid,"Creating/updating glb item");

	if (NULL == (glb_item = (GLB_POLLER_ITEM *)zbx_hashset_search(items, &dc_item->itemid)))
	{
		DEBUG_ITEM(dc_item->itemid,"Adding new item");
		zabbix_log(LOG_LEVEL_DEBUG,"Adding new item %ld to the local queue", dc_item->itemid);

		glb_item = &new_glb_item;
		bzero(glb_item, sizeof(GLB_POLLER_ITEM));
		
		glb_item->itemid = dc_item->itemid;
		glb_item = zbx_hashset_insert(items, glb_item, sizeof(GLB_POLLER_ITEM));
		
		glb_item->state = POLL_FREE;
	}
	
	if (NULL == (glb_host = (GLB_POLLER_HOST *)zbx_hashset_search(hosts, &dc_item->host.hostid))) {
		zabbix_log(LOG_LEVEL_DEBUG, "Creating a new host %ld",dc_item->host.hostid);
		//the new host
		GLB_POLLER_HOST new_host = {0};

		zabbix_log(LOG_LEVEL_DEBUG, "Creating new host %s addr is %s:%hu", dc_item->host.host,
					   dc_item->interface.addr, dc_item->interface.port);

		new_host.hostid = dc_item->host.hostid;
		glb_host = zbx_hashset_insert(hosts, &new_host, sizeof(GLB_POLLER_HOST));
	} 

	glb_host->items++;
	glb_item->hostid = dc_item->host.hostid;
	
	//common attributes
	zbx_heap_strpool_release(glb_item->delay);
	glb_item->delay = zbx_heap_strpool_intern(dc_item->delay);
	glb_item->value_type = dc_item->value_type;
	glb_item->ttl = now + CONFIG_CONFSYNCER_FREQUENCY * 1.5; //updating item's aging
	glb_item->flags = dc_item->flags;
	glb_item->item_type = dc_item->type;

	size_t item_size;
	void *item_data = NULL;
	//allocating itemdata
	if (NULL == glb_item->itemdata ) {
		switch (glb_item->item_type) {
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP: 
			item_size = sizeof(GLB_SNMP_ITEM);
			break;
#endif
		case ITEM_TYPE_SIMPLE: 
			item_size = sizeof(GLB_PINGER_ITEM);
			break;
		case ITEM_TYPE_EXTERNAL: 
			item_size = sizeof(GLB_WORKER_ITEM);
			break;
		case ITEM_TYPE_TRAPPER:
			item_size = 0;
			break;
		case ITEM_TYPE_ZABBIX: 
			item_size = sizeof(GLB_AGENT_ITEM);
			break;
		default:
			zabbix_log(LOG_LEVEL_WARNING, "Cannot create glaber item, unsuported glb_poller item_type %d, this is a BUG", dc_item->type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit (-1);
		}
		
		if (0 < item_size) {
			if (NULL == (item_data = zbx_malloc(NULL, item_size))) {
				zabbix_log(LOG_LEVEL_WARNING, "Couldn't allocate mem for the new item, exiting");
				exit(-1);
			}
		
			bzero(item_data,item_size);
			glb_item->itemdata = item_data;
		}
		
	}

	int res = SUCCEED;

	if (POLL_POLLING != glb_item->state) {
		switch (glb_item->item_type ) {
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP: 
			res = glb_snmp_init_item(dc_item, glb_item->itemdata );
			break;
#endif
		case ITEM_TYPE_SIMPLE: 
			res = glb_pinger_init_item(dc_item, glb_item->itemdata);
			break;
		case ITEM_TYPE_EXTERNAL: 
			res = glb_worker_init_item(dc_item, glb_item->itemdata );		
			break;
		case ITEM_TYPE_TRAPPER:
			break;
		case ITEM_TYPE_ZABBIX: 
			res = glb_agent_init_item(dc_item, glb_item->itemdata );
			break;
		default:
			zabbix_log(LOG_LEVEL_WARNING, "Cannot create glaber item, unsuported glb_poller item_type %d, this is a BUG", dc_item->type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit (-1);
		}

		if (FAIL == res ) {
			zabbix_log(LOG_LEVEL_WARNING, "Coudln't init worker item %ld, not placing to the poll queue", glb_item->itemid);
			//removing the item from the hashset
			zbx_free(glb_item->itemdata);
			zbx_heap_strpool_release(glb_item->delay);
			zbx_hashset_remove_direct(items,glb_item);
			return FAIL;
		}
	}

	if ( POLL_FREE == glb_item->state &&  ITEM_TYPE_TRAPPER != glb_item->item_type) {
		//zabbix_log(LOG_LEVEL_INFORMATION,"Adding item %ld to the events queue ", glb_item->itemid);
		glb_item->state = POLL_QUEUED;
		add_item_check_event(events, hosts, glb_item, now);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s finished", __func__);
	return SUCCEED;
}

/******************************************************************
* inits the proper worker for the item_type 					  *
*
 * ***************************************************************/
void *glb_poller_engine_init(unsigned char item_type, zbx_hashset_t *hosts, zbx_hashset_t *items, int *requests, int *responses) {
	
	switch (item_type) {
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP:
			return glb_snmp_init(hosts, items, requests, responses);
			break;
#endif
		case ITEM_TYPE_SIMPLE:
			return glb_pinger_init(items, requests, responses);
			break;
		
		case ITEM_TYPE_EXTERNAL:
			return glb_worker_init(items, requests, responses);
			break;

		case ITEM_TYPE_TRAPPER:
			return glb_server_init(requests, responses);
			break;
		
		case ITEM_TYPE_AGENT:
			return glb_agent_init(items, hosts, requests, responses);
			break;

		default: 
			zabbix_log(LOG_LEVEL_WARNING,"Cannot init worker for item type %d, this is a BUG",item_type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}

}
/******************************************************************
 * destroys the proper worker for the item_type 					  *
 * since worker init accepts json as a param, forms the			  *
 * json string and passes to the worker init sequence			  *
 * ***************************************************************/
void glb_poller_engine_shutdown(void *engine, unsigned char item_type) {
	
	switch (item_type) {
#ifdef HAVE_NETSNMP
		case ITEM_TYPE_SNMP:
			glb_snmp_shutdown(engine); 
			break;

		case ITEM_TYPE_AGENT:
			glb_agent_shutdown(engine); 
			break;
#endif
		case ITEM_TYPE_SIMPLE:
			glb_pinger_shutdown(engine); 
			break;

		case ITEM_TYPE_EXTERNAL:
			glb_worker_shutdown(engine); 
			break;

		case ITEM_TYPE_TRAPPER:
			glb_server_shutdown(engine); 
			break;

		default: 
			zabbix_log(LOG_LEVEL_WARNING,"Cannot shutdown engine for item type %d, this is a BUG",item_type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}
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
static void glb_poller_handle_async_io(void *engine, unsigned char item_type ) {
	switch (item_type)
	{
#ifdef HAVE_NETSNMP
	case ITEM_TYPE_SNMP:
		glb_snmp_handle_async_io(engine);
		break;
#endif	
	case ITEM_TYPE_SIMPLE:
		glb_pinger_handle_async_io(engine);
		break;

	case ITEM_TYPE_EXTERNAL:
		glb_worker_handle_async_io(engine);
		break;
	
	case ITEM_TYPE_TRAPPER:
		glb_server_handle_async_io(engine);
		break;

	case ITEM_TYPE_AGENT:
		glb_agent_handle_async_io(engine);
		break;

	default:
		zabbix_log(LOG_LEVEL_WARNING,"Unsupported item type %d has been send to polling, this is a BUG",item_type);
		THIS_SHOULD_NEVER_HAPPEN;
		break;
	}
}


ZBX_THREAD_ENTRY(glbpoller_thread, args)
{
	//time_t last_stat_time, total_sec = 0.0;
	double sec;
	int requests = 0, responses = 0, sleeptime = 0, next_stat_time = 0;
	unsigned char item_type;

	//zbx_hashset_ptr_t strpooll; //strings pool for string interning
	zbx_binary_heap_t events; //this b-tree holds tasks sorted by time to fetch them in time order for execution
	zbx_hashset_t items;	  //this is hashset of all the tasks
	zbx_hashset_t hosts;	  //this is hashset of all the hosts to control host - related behavior, like contention, timeouts
	
	
	GLB_POLLER_ITEM *glb_item;
	GLB_POLLER_HOST *glb_host;
	GLB_EXT_WORKER *worker;
	void *poll_engine=NULL;
	int old_activity;

	zbx_heap_strpool_init();
	glb_preprocessing_init();

	zbx_binary_heap_create(&events, event_elem_compare, 0);
	
	zbx_hashset_create(&items, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_hashset_create(&hosts, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	item_type = *(unsigned char *)((zbx_thread_args_t *)args)->args;
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	add_event(&events, GLB_EVENT_AGING, 0, time(NULL) + 1);
	add_event(&events, GLB_EVENT_NEW_ITEMS_CHECK, 0, time(NULL) + process_num);

	if (NULL == ( poll_engine = glb_poller_engine_init(item_type, &hosts, &items, &requests, &responses))) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't init polling engine for item type %d, exiting", item_type);
		exit(-1);
	}
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
		server_num, get_process_type_string(process_type), process_num);

 	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);

	DBconnect(ZBX_DB_CONNECT_NORMAL);
	
#define STAT_INTERVAL 5

	while (ZBX_IS_RUNNING())
	{
		unsigned int now = time(NULL);
		sec = zbx_time();
		zbx_update_env(sec);

		while (FAIL == zbx_binary_heap_empty(&events))
		{
			const zbx_binary_heap_elem_t *min;

			min = zbx_binary_heap_find_min(&events);
			
			GLB_POLLER_EVENT *event = (GLB_POLLER_EVENT *)min->data;
			zabbix_log(LOG_LEVEL_DEBUG, "Got event loop for id %ld run in %d seconds",event->id, event->time - now);
			
			if ( event->time > now) break;
			
			zbx_binary_heap_remove_min(&events);

			switch (event->type) {
			
			case GLB_EVENT_ITEM_POLL:
				zabbix_log(LOG_LEVEL_DEBUG, "Item %ld poll event", event->id);
				if (NULL != (glb_item = zbx_hashset_search(&items, &event->id)) && 
					NULL != (glb_host = zbx_hashset_search(&hosts, &glb_item->hostid))) {
						
					DEBUG_ITEM(glb_item->itemid,"Item poll event");
					//checking if host is disabled
					if (glb_host->disabled_till > now) { 
						zabbix_log(LOG_LEVEL_DEBUG, "Skipping item %ld from polling, host is down for %ld sec",
					  	glb_item->itemid, glb_host->disabled_till - now);

						//replaning item polling after disabled_till time
						add_item_check_event(&events, &hosts, glb_item, glb_host->disabled_till);

					} else {
	
						add_item_check_event(&events, &hosts, glb_item, now);
						
						//the only state common poller logic care - _QUEUED - such an items are considred
						//to be pollabale, updatable and etc. All other states indicates the item is busy
						//with type specific poller logic
						if ( POLL_QUEUED != glb_item->state || glb_item->ttl < now ) {
							DEBUG_ITEM(glb_item->itemid, "Skipping from polling, no in QUEUED state");
							zabbix_log(LOG_LEVEL_DEBUG, "Not sending item %ld to polling, it's in the %d state or aged", glb_item->itemid, glb_item->state);
						} else {
							//ok, adding the item to the poller's internal logic for polling
							
							//todo:remove this state to internal poller logic
							glb_item->state = POLL_POLLING;
							glb_item->lastpolltime = now;
					
							DEBUG_ITEM(glb_item->itemid,"Starting poller item poll");
							DEBUG_HOST(glb_item->hostid,"Satrting poller item poll");

							glb_poller_schedule_poll_item(poll_engine, glb_item);
						}
					}
				} 
			
			break;

			case GLB_EVENT_AGING: {
				//removing outdated items, that hasn't been updated for 1,5 server sync times
				//zabbix_log(LOG_LEVEL_DEBUG, "Aging processing CONFIG_GLB_REQUEUE_TIME is %d", CONFIG_GLB_REQUEUE_TIME);
				GLB_POLLER_ITEM *glb_item;
				zbx_hashset_iter_t iter;
				//zbx_vector_uint64_t deleted_items;
				int cnt = 0, i;

				zbx_hashset_iter_reset(&items, &iter);
				//zbx_vector_uint64_create(&deleted_items);

				//items are cleaned out in a bit conservatively  as this prevents creating extra events
				while (glb_item = zbx_hashset_iter_next(&iter)) {
					if (glb_item->ttl < now && 	POLL_QUEUED == glb_item->state ) {
					
						DEBUG_ITEM(glb_item->itemid,"Item aged");
						zabbix_log(LOG_LEVEL_INFORMATION, "Marking aged item %ld for deletion, ttl is %d",glb_item->itemid, glb_item->ttl);	
			
						cnt++;

						glb_free_item_data(poll_engine, glb_item);
						
						if (NULL != (glb_host=zbx_hashset_search(&hosts, &glb_item->hostid))) {
							
							glb_host->items--;

							if (0 == glb_host->items)  {
								zbx_hashset_remove_direct(&hosts, glb_host);
							}
						}
						
						zbx_hashset_iter_remove(&iter);
					}
				}

				add_event(&events, GLB_EVENT_AGING, 0, now + GLB_AGING_PERIOD);
				zabbix_log(LOG_LEVEL_DEBUG, "Finished aging, %d items",cnt);
			}
			break;

			case GLB_EVENT_NEW_ITEMS_CHECK: {
				int num;

				add_event(&events, GLB_EVENT_NEW_ITEMS_CHECK, 0, now + 120); 
	
				num = DCconfig_get_glb_poller_items(&events, &hosts, &items, item_type, process_num, poll_engine);
				zabbix_log(LOG_LEVEL_DEBUG, "Event: got %d new items from the config cache", num);
				
				//so, perhaps, it's better to control with 100% precision  of machine state tha returns items to the queue
				zbx_hashset_iter_t iter;
				zbx_hashset_iter_reset(&items, &iter );
				while ( NULL != (glb_item=zbx_hashset_iter_next(&iter))) {
					//an item may wait for some time while other items will be polled and thus get timedout
					//so after one minute of waiting we consider its timed out anyway whatever the poll process thinks about it
					//but it's a poller's business to submit timeout result
					if (glb_item->lastpolltime + SEC_PER_MIN < now && POLL_POLLING == glb_item->state) {
						zabbix_log(LOG_LEVEL_DEBUG, "Item %ld has timed out in the poller, resetting it's queue state",glb_item->itemid);
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
		
		glb_poller_handle_async_io(poll_engine, item_type);
		
		if ( old_activity == requests + responses ) {
			//zabbix_log(LOG_LEVEL_INFORMATION,"Nothing to do, sleeping");
			usleep(100000);
		}
		old_activity = requests + responses;

		if (next_stat_time < now ) {
			next_stat_time = now + STAT_INTERVAL;
			zbx_setproctitle("%s #%d [sent %d chks/sec, got %d chcks/sec, items: %d, events planned: %d]",
 					get_process_type_string(process_type), process_num, requests/STAT_INTERVAL, responses/STAT_INTERVAL, items.num_data, events.elems_num);
			requests = 0; 
			responses = 0;
		
		}
	}

	glb_poller_engine_shutdown(poll_engine,item_type);
	zbx_hashset_destroy(&hosts);
	zbx_hashset_destroy(&items);
	zbx_binary_heap_destroy(&events);
	zbx_heap_strpool_destroy();

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}

