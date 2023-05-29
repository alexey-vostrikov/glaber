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

//glb escalator is responsible for performing operations based on actions configuration
//whenever an actionable object is created it is passed to process_actions_xxxx, which in 
//turn should notify escalator on object id and type
//escalator uses actions-operations cache and schedules operations and performs them via sending exec
//tasks to the alerter

//escalator holds internal time events for handling the escalations 
#include "glb_events_processor.h"
#include "glb_events_actions.h"
#include "zbxcommon.h"
#include "zbxthreads.h"
#include "zbxalgo.h"
#include "zbxnix.h"
#include "zbxshmem.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../../libs/glb_state/glb_state_problems.h"
#include "../../libs/glb_conf/operations/operations.h"
#include "../glb_poller/poller_async_io.h"
#include "process_problem_events.h"
#include "glb_events_conditions_check.h"

extern int		CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];
mem_funcs_t heap_memf = {.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, 
						 .realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC, 
						 .free_func = ZBX_DEFAULT_MEM_FREE_FUNC};

#define CONFIG_EVENTS_PROC_IPC_SIZE 16 * ZBX_MEBIBYTE

#define NEW_ESCALATIONS_CHECK_INTERVAL 1
#define PROCTITLE_UPDATE_INTERVAL	5
#define UPDATE_SETTINGS 1 

typedef struct {
	u_int64_t action_id;
	u_int64_t problem_id;
	int current_step;
	int step_delay;
	poller_event_t *event;
} escalation_t;

typedef struct {
    strpool_t strpool;
	zbx_thread_args_t args;
	poller_event_t *escalations_check;
	poller_event_t *update_proctitle;
	poller_event_t *update_config;
	u_int64_t requests;
	zbx_hashset_t escalations;
	ipc_conf_t *ipc;
	int process_num;
	glb_actions_t *actions_conf;
	glb_events_operations_conf_t  *operations;
} conf_t;

static zbx_shmem_info_t	*events_proc_notify_shmem = NULL;

ZBX_SHMEM_FUNC_IMPL(__events_proc_notify, events_proc_notify_shmem);

static mem_funcs_t ipc_memf = {
		.free_func = __events_proc_notify_shmem_free_func, 
		.malloc_func = __events_proc_notify_shmem_malloc_func, 
		.realloc_func = __events_proc_notify_shmem_realloc_func};

static conf_t conf = {0};

IPC_CREATE_CB(ipc_events_processing_send_cb) {
	events_processor_event_t *event_local = local_data;
	events_processor_event_t *event_ipc = ipc_data;

	*event_ipc = *event_local;
	event_ipc->data = NULL;
	
	switch (event_local->event_source) {
		case EVENT_SOURCE_PROBLEM: 
			return;
		case EVENTS_PROC_NOTIFY_TYPE_LLD:
			HALT_HERE("Not implemented");
			return;		
	}
	HALT_HERE("Wrong event type");
}

static void destroy_escalation_event(escalation_t *escalation) {
	poller_destroy_event(escalation->event);
	
	zbx_free(escalation);
}

void process_escalation_step_cb(poller_item_t *garbage, void *esc) {
	escalation_t *escalation = esc;
	int step_delay = 60;

	if (FAIL == glb_state_problems_if_exists(escalation->problem_id)) {
		destroy_escalation_event(escalation);
		return;
	}
	
	glb_event_operations_execute_step(conf.operations, escalation->action_id, escalation->current_step );
		
	if (glb_events_operations_get_max_steps(conf.operations, escalation->action_id) <= escalation->current_step) {
		//no steps left, destroying escalation
		destroy_escalation_event(escalation);
		return;
	}

	escalation->current_step++;
	step_delay = glb_events_operations_get_step_delay(conf.operations, escalation->action_id, escalation->current_step);

	poller_run_timer_event(escalation->event, step_delay * 1000);
}

void create_new_event_operations(events_processor_event_t *event, u_int64_t actionid) {
	
	poller_event_t *esc_event;

	escalation_t *escalation = zbx_malloc(NULL, sizeof(escalation_t));
	escalation->problem_id = event->object_id;
	escalation->action_id = actionid;
	escalation->current_step = 1;
	escalation->step_delay = glb_events_operations_get_step_delay(conf.operations, actionid, escalation->current_step);
	escalation->event = poller_create_event(NULL, process_escalation_step_cb, 0, escalation, 0);
	
	poller_run_timer_event(escalation->event, 0); //planning first step right now
}

void exec_event_recovery_operations(events_processor_event_t *event, u_int64_t actionid) {
	
	escalation_t *escalation;
	
	// if (NULL != (escalation = get_active_escalation(event->object_id, actionid))) {
		
	// 	destroy_escalation_event(escalation);
	// }
	
	HALT_HERE("Create get_active_escalation first");
	glb_events_operations_execute_recovery(conf.operations, event->object_id, actionid);
}

static void run_event_operations(events_processor_event_t *event, u_int64_t actionid) {
	switch(event->event_type) {
		case EVENTS_TYPE_NEW:
			create_new_event_operations(event, actionid);
			break;
		case EVENTS_TYPE_CHANGE:
			//glb_operations_execute_change(conf.operations, event->object_id, actionid);			
			HALT_HERE("Not implemented");
			break;
		case EVENTS_TYPE_RECOVER:
			exec_event_recovery_operations(event, actionid);
			break;
	}
}

static void start_operations_for_event(events_processor_event_t *event, zbx_vector_uint64_t *matched_actions) {
	int i;

	LOG_INF("Problem %ld matched %d actions", event->object_id, matched_actions->values_num);
	
	for (i = 0 ; i < matched_actions->values_num; i++) 
		run_event_operations(event, matched_actions->values[i]);
}


IPC_PROCESS_CB(ipc_events_processing_process_cb) {
	events_processor_event_t *event = ipc_data;
	zbx_vector_uint64_t matched_actions;

	LOG_INF("Recieved notify on problemid %ld, event_source %d, event_type %d", event->object_id, event->event_source, event->event_type);
	zbx_vector_uint64_create(&matched_actions);

	glb_actions_process_event(event, conf.actions_conf, &matched_actions);
	start_operations_for_event(event, &matched_actions);

	zbx_vector_uint64_destroy(&matched_actions);
}

void glb_event_processing_send_notify(u_int64_t problemid, unsigned char event_source, events_processor_event_type_t event_type) {
	events_processor_event_t event ={.data = NULL, .event_source = event_source, 
		.event_type = event_type, .object_id = problemid, .event_type = event_type};

	glb_ipc_send(conf.ipc, problemid % CONFIG_FORKS[GLB_PROCESS_TYPE_EVENTS_PROCESSOR], &event, IPC_LOCK_WAIT);
	glb_ipc_flush(conf.ipc);
	LOG_INF("Sent event escalation notify on problem %ld", problemid);

}

static void escalations_check_cb(poller_item_t *garbage, void *data) {
	events_processor_event_t  event;
	LOG_INF("Checking for the new escalations, proc num is %d, %p",conf.args.info.process_num -1, &conf.ipc);

	glb_ipc_process(conf.ipc, conf.args.info.process_num -1 , ipc_events_processing_process_cb, NULL, 1000 );
}

static void update_configuration(poller_item_t *garbage, void *data){
	LOG_INF("Updating configuration");
	glb_actions_update(conf.actions_conf);
	glb_events_operations_update(conf.operations);
	LOG_INF("Finished updating configuration");
}
 
static void update_proc_title_cb(poller_item_t *garbage, void *dat)
{
	static u_int64_t last_call = 0;

	u_int64_t now = glb_ms_time(), time_diff = now - last_call;

	if (0 == time_diff)
		return;

	zbx_setproctitle("%s #%d [sent %ld chks/sec, got %ld esc/sec, escalations: %d]",
					get_process_type_string(conf.args.info.process_type), conf.args.info.process_num, (conf.requests * 1000) / (time_diff),
					conf.escalations.num_data);

	conf.requests = 0;
	last_call = now;

	if (!ZBX_IS_RUNNING())
		poller_async_loop_stop();
}

static int events_processor_fork_init(zbx_thread_args_t *args)
{
    conf.args = *(zbx_thread_args_t *)args;

	strpool_init(&conf.strpool, NULL);

	conf.actions_conf = glb_actions_create();

	poller_async_loop_init();

	conf.escalations_check = poller_create_event(NULL, escalations_check_cb, 0, NULL, 1);
	poller_run_timer_event(conf.escalations_check, NEW_ESCALATIONS_CHECK_INTERVAL * 1000);
	
	conf.update_proctitle = poller_create_event(NULL, update_proc_title_cb, 0, NULL, 1);
	poller_run_timer_event(conf.update_proctitle, PROCTITLE_UPDATE_INTERVAL * 1000);

	conf.update_config = poller_create_event(NULL, update_configuration, 0, NULL, 1);
	poller_run_timer_event(conf.update_config, UPDATE_SETTINGS * 5000);

	conf.operations =  glb_events_operations_init(&heap_memf);

	return SUCCEED;
}

/* must be called from the parent process */
int glb_events_processing_init() {
    char *error = NULL;
    
	LOG_INF("Doing init of event proc");
    if (SUCCEED != zbx_shmem_create(&events_proc_notify_shmem, CONFIG_EVENTS_PROC_IPC_SIZE, "preproc notify cache size", 
				"CONFIG_IPC_MEM_SIZE", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    conf.ipc = glb_ipc_init(1024, sizeof(events_processor_event_t), CONFIG_FORKS[GLB_PROCESS_TYPE_EVENTS_PROCESSOR] , &ipc_memf, 
								ipc_events_processing_send_cb, NULL,
								IPC_LOW_LATENCY);

    return SUCCEED;
}

ZBX_THREAD_ENTRY(glb_events_processor_thread, args)
{
	if (SUCCEED != events_processor_fork_init(args))
		exit(-1);

	poller_async_loop_run();

	while (1)
		zbx_sleep(SEC_PER_MIN);
}