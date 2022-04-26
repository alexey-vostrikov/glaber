
/*
** Copyright Glaber
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
#include "common.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "log.h"
#include "process.h"
#include "proc_triggers.h"
#include "proc_trigger_timers.h"
#include "../zbxipcservice/glb_ipc.h"
#include "../glb_state/state_triggers.h"

extern void *ipc_processing_notify;

typedef struct {
    void *event_queue;
} conf_t;

static conf_t *tm_conf = NULL;

//TODO: return original code to calc trigger calc time
//or at least make exception to count trend-based triggers hourly
#define ZBX_TIMER_DELAY 	30

int calc_time_trigger_nextcheck(u_int64_t seed, int from) {
    int	nextcheck;

	nextcheck = ZBX_TIMER_DELAY * (int)(from / (time_t)ZBX_TIMER_DELAY) +
			(int)(seed % (zbx_uint64_t)ZBX_TIMER_DELAY);

	while (nextcheck <= from)
		nextcheck += ZBX_TIMER_DELAY;

	return nextcheck;
}

static void requeue_trigger(u_int64_t triggerid) {
	int revision = time(NULL);
	int nextcheck;
    LOG_INF("In %s: starting, trigger %ld", __func__, triggerid);
	/* to avoid double addition to the queue with the same timer */
	if (glb_state_trigger_get_revision(triggerid) == revision) {
        LOG_INF("Trigger's %ld revision %d haven't changed, not updating", triggerid, revision);
    	return;
    }
	//calc_time_trigger_nextcheck
	nextcheck = calc_time_trigger_nextcheck(triggerid, time(NULL) );
	
	//add_event(EVENT_TRIGGER_CHECK, triggerid, nextcheck, revision);

	glb_state_trigger_set_revision(triggerid, nextcheck);
    LOG_INF("requeue triggr: Conf is %p", tm_conf->event_queue);
    LOG_INF("Adding trigger %ld event in %d seconds, conf is %p", triggerid, nextcheck - time(NULL), tm_conf->event_queue);
    event_queue_add_event(tm_conf->event_queue, nextcheck, PROCESS_TRIGGER, (void *)triggerid);
    
    LOG_INF("In %s: finished", __func__);

}

IPC_PROCESS_CB(process_time_triggers_cb) {
    notify_t *notify = ipc_data;
    DEBUG_TRIGGER(notify->id,"Recalculating time trigger");
    
    recalculate_trigger(notify->id);
    LOG_INF("Requeueing trigger from the notification %ld", notify->id);
    requeue_trigger(notify->id);

}

static void get_new_queue_triggers(int proc_num) {
	int i=0;
    static int lastcheck = 0;
    u_int64_t triggerid;


    if (lastcheck == time(NULL))
        return;
    lastcheck = time(NULL);

	
	i = glb_ipc_process(ipc_processing_notify, proc_num-1, process_time_triggers_cb, NULL, 1024);

   
    glb_ipc_dump_reciever_queues(ipc_processing_notify, "Reciever notify queues", proc_num);
	LOG_INF("Saved %d triggers", i);
}



int process_time_triggers(int *processed_triggers, int max_triggers, int process_num){
    get_new_queue_triggers(process_num);
    LOG_INF("Calling queue events processor, conf is %p", tm_conf);
    *processed_triggers = event_queue_process_events(tm_conf->event_queue, max_triggers);
    LOG_INF("Finished queue events processor");
    return SUCCEED;
}



EVENT_QUEUE_CALLBACK(process_timer_triggers_cb){
	int triggerid = (u_int64_t) data;
	int revision = event_time;
	LOG_INF("In %s: Got trigger %ld in the event queue for processing",__func__, triggerid);
	
    //TODO: do processing of functionality and revision in one state call

	/* permanent problem - no host/item or the new version is in the queue already*/
	if (FAIL ==glb_state_trigger_check_revision(triggerid, revision)) {
		LOG_INF("Trigger %ld revision number changed %d->%d, not processing", triggerid, revision,
            glb_state_trigger_check_revision(triggerid, revision) );
		return FAIL;
	}
    
    glb_state_trigger_set_revision(triggerid, 0);
	
	if (SUCCEED == glb_state_trigger_is_functional(triggerid)) {
		LOG_INF("Trigger is functional, will process it");
		process_trigger(triggerid);
	} else {
        LOG_INF("Trigger is not functional, will not check, but will requeue");
    }
	
    //LOG_INF("Requeueing trigger after processing %ld", triggerid);
	requeue_trigger(triggerid);
    //LOG_INF("In %s: Got trigger %ld in the event queue for processing: finished",__func__);
	return SUCCEED;
}



IPC_CREATE_CB(notify_ipc_create_cb) {
	LOG_INF("Creating the notification message callback");
	notify_t *local_message = local_data, *ipc_message = ipc_data;
	LOG_INF("Doing memcpy of size %ld",sizeof(notify_t));
	memcpy(ipc_message, local_message, sizeof(notify_t));
	LOG_INF("Finished");
}

IPC_FREE_CB(notify_ipc_free_cb) {
    //while notify_t has static size, there is no need for dynamic free
}


int processing_trigger_timers_init(int notify_queue_size, mem_funcs_t *memf) {
    LOG_INF("Doing trigger timers processing queue init");

    if ( NULL == (tm_conf = zbx_calloc(NULL, 0, sizeof(conf_t)))) 
        return FAIL;


    event_queue_cb_conf_t cbs[] = {
        {.event_id = PROCESS_TRIGGER, .func = process_timer_triggers_cb}, 
        { NULL }
    }; 

    if (NULL == (tm_conf->event_queue = event_queue_init(NULL, 0, cbs)) ) 
        return FAIL;
   
    LOG_INF("init: Conf is %p", tm_conf->event_queue);

    if (NULL == (ipc_processing_notify = glb_ipc_init(IPC_PROCESSING_NOTIFY, 64 * ZBX_MEBIBYTE , "Processing notify queue", 
				notify_queue_size, sizeof(notify_t), CONFIG_HISTSYNCER_FORKS,  memf, notify_ipc_create_cb, 
                    notify_ipc_free_cb)))
			return FAIL;
    
    return SUCCEED;
}

/*note: local ipc sender init is required */
int processing_notify_changed_trigger(uint64_t new_triggerid) {
    LOG_INF("Sending changed trigger %ld notification", new_triggerid);
    glb_ipc_send(ipc_processing_notify, new_triggerid % CONFIG_HISTSYNCER_FORKS, (void *)&new_triggerid);
    glb_ipc_dump_sender_queues(ipc_processing_notify, "Sender notify queues");
}
    
int processing_notify_flush() {
    glb_ipc_flush_all(ipc_processing_notify);
}
