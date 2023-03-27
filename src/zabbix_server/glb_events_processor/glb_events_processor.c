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
//escalator uses actions-operations cache and schedules operations and performs them (or uses exec services, like alerter?)

#include "glb_events_processor.h"
#include "zbxcommon.h"
#include "zbxthreads.h"
#include "zbxalgo.h"
#include "zbxnix.h"
#include "zbxshmem.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../glb_poller/poller_async_io.h"
#include "process_problem_events.h"

extern int		CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

//TODO: check out of mem situation
#define CONFIG_EVENTS_PROC_IPC_SIZE 16 * ZBX_MEBIBYTE

#define NEW_ESCALATIONS_CHECK_INTERVAL 1
#define PROCTITLE_UPDATE_INTERVAL	5

typedef struct {
    strpool_t strpool;
	zbx_thread_args_t args;
	poller_event_t *new_escalations_check;
	poller_event_t *update_proctitle;
	u_int64_t requests;
	zbx_hashset_t escalations;
	ipc_conf_t *ipc;
	int process_num;
	
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

	event_ipc->event_type = event_local->event_type;
	event_ipc->object_id = event_local->object_id;

	switch (event_local->event_type) {
		case EVENTS_PROC_NOTIFY_TYPE_NEW_PROBLEM: 
			return;
		case EVENTS_PROC_NOTIFY_TYPE_LLD:
			HALT_HERE("Not implemented");
			return;		
	}
	HALT_HERE("Wrond event type");
}

IPC_FREE_CB(event_free_cb) {

}

IPC_PROCESS_CB(ipc_events_processing_process_cb) {
	events_processor_event_t *event = ipc_data;
	LOG_INF("Recieved notify on objectid %ld", event->object_id);

	switch (event->event_type) {
		case EVENTS_PROC_NOTIFY_TYPE_NEW_PROBLEM: 
			process_problem_new_event(event->object_id);
			return;
		case EVENTS_PROC_NOTIFY_TYPE_LLD:
			HALT_HERE("LLD actions is pending");
			return;
	}
}

void glb_event_processing_send_problem_notify(u_int64_t problemid) {
	events_processor_event_t event ={.data = NULL, .event_type = EVENTS_PROC_NOTIFY_TYPE_NEW_PROBLEM, .object_id = problemid};
	//HALT_HERE("Send catch rule")
	glb_ipc_send(conf.ipc, problemid % CONFIG_FORKS[GLB_PROCESS_TYPE_EVENTS_PROCESSOR], &event, IPC_LOCK_WAIT);
	glb_ipc_flush(conf.ipc);
	LOG_INF("Sent event escalation notify on problem %ld", problemid);
	glb_ipc_dump_sender_queues(conf.ipc, "Problems events escalations");
}


static void new_escalations_check_cb(poller_item_t *garbage, void *data) {
	events_processor_event_t  event;
	LOG_INF("Checking for the new escalations, proc num is %d, %p",conf.args.info.process_num -1, &conf.ipc);

	glb_ipc_process(conf.ipc, conf.args.info.process_num -1 , ipc_events_processing_process_cb, NULL, 1000 );
	glb_ipc_dump_receiver_queues(conf.ipc, "Incoming events escalations", 0);
	glb_ipc_dump_receiver_queues(conf.ipc, "Incoming events escalations", 1);
}

static void update_proc_title_cb(poller_item_t *garbage, void *dat)
{
	static u_int64_t last_call = 0;
	//conf_t * conf = data;
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
	poller_async_loop_init();

	conf.new_escalations_check = poller_create_event(NULL, new_escalations_check_cb, 0, NULL, 1);
	poller_run_timer_event(conf.new_escalations_check, NEW_ESCALATIONS_CHECK_INTERVAL * 1000);
	
	conf.update_proctitle = poller_create_event(NULL, update_proc_title_cb, 0, NULL, 1);
	poller_run_timer_event(conf.update_proctitle, PROCTITLE_UPDATE_INTERVAL * 1000);

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
    
//	conf = memf.malloc_func(NULL, sizeof(conf_t));
//	bzero(conf, sizeof(conf_t));

    conf.ipc = glb_ipc_init(1024, sizeof(events_processor_event_t), CONFIG_FORKS[GLB_PROCESS_TYPE_EVENTS_PROCESSOR] , &ipc_memf, 
								ipc_events_processing_send_cb, event_free_cb,
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