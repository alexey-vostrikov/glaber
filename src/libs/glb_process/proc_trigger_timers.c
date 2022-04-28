
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
	u_int64_t revision = glb_ms_time();
	int nextcheck;
    DEBUG_TRIGGER(triggerid, "In %s: starting", __func__);
	
    /* to avoid double addition to the queue with the same timer */
	if (glb_state_trigger_get_revision(triggerid) == revision) {
        //LOG_INF("Revision isn't changed, not adding");
        DEBUG_TRIGGER(triggerid, "Trigger's revision %ld haven't changed, not updating", revision);
    	return;
    }
	
    nextcheck = calc_time_trigger_nextcheck(triggerid, time(NULL) );
	
	glb_state_trigger_set_revision(triggerid, time_to_ms_time(nextcheck));
    //LOG_INF("requeue trigger %ld: Adding trigger requeue event in %d seconds, state revision is %d", triggerid, nextcheck - time(NULL), 
    //    glb_state_trigger_get_revision(triggerid));
    DEBUG_TRIGGER(triggerid, "requeue trigger: Adding trigger requeue event in %ld seconds, conf is %p", nextcheck - time(NULL), tm_conf->event_queue);
    event_queue_add_event(tm_conf->event_queue, time_to_ms_time(nextcheck), PROCESS_TRIGGER, (void *)triggerid);
    
}

IPC_PROCESS_CB(process_time_triggers_cb) {
    notify_t *notify = ipc_data;
        
    DEBUG_TRIGGER(notify->id, "Requeueing trigger arrived on notification");
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
//	LOG_INF("Saved %d triggers", i);
}



int process_time_triggers(int max_triggers, int process_num){
    get_new_queue_triggers(process_num);
    //LOG_INF("Calling event process for triggers");
    return  event_queue_process_events(tm_conf->event_queue, max_triggers);
}



EVENT_QUEUE_CALLBACK(process_timer_triggers_cb){
	u_int64_t triggerid = (u_int64_t) data;
	u_int64_t revision = event_time;

   // LOG_INF("Processing time trigger %ld", triggerid);
	if (FAIL == glb_state_trigger_check_revision(triggerid, revision)) {
        // LOG_INF("Trigger %d revision number changed %ld->%ld, not processing", triggerid, revision,
        //    glb_state_trigger_check_revision(triggerid, revision) );
        DEBUG_TRIGGER( triggerid, "Trigger revision number changed %ld->%ld, not processing", revision,
            glb_state_trigger_check_revision(triggerid, revision) );
		return FAIL;
	}
    
    glb_state_trigger_set_revision(triggerid, 0);
	//LOG_INF("Recalculating trigger %ld", triggerid);
    recalculate_trigger(triggerid);

    //LOG_INF("Requeueing rigger %ld", triggerid);
	requeue_trigger(triggerid);
	return SUCCEED;
}



IPC_CREATE_CB(notify_ipc_create_cb) {

	notify_t *local_message = local_data, *ipc_message = ipc_data;
	memcpy(ipc_message, local_message, sizeof(notify_t));

}

IPC_FREE_CB(notify_ipc_free_cb) {
    //while notify_t has static size, there is no need for dynamic free
}


int processing_trigger_timers_init(int notify_queue_size, mem_funcs_t *memf) {
 
     if ( NULL == (tm_conf = zbx_calloc(NULL, 0, sizeof(conf_t)))) 
        return FAIL;
 
    if (NULL == (tm_conf->event_queue = event_queue_init(NULL)) ) 
        return FAIL;
   
    event_queue_add_callback(tm_conf->event_queue, PROCESS_TRIGGER, process_timer_triggers_cb);

    if (NULL == (ipc_processing_notify = glb_ipc_init(IPC_PROCESSING_NOTIFY, 64 * ZBX_MEBIBYTE , "Processing notify queue", 
				notify_queue_size, sizeof(notify_t), CONFIG_HISTSYNCER_FORKS,  memf, notify_ipc_create_cb, 
                    notify_ipc_free_cb)))
			return FAIL;
    
    return SUCCEED;
}

/*note: local ipc sender init is required */
int processing_notify_changed_trigger(uint64_t new_triggerid) {
    DEBUG_TRIGGER(new_triggerid,"Sending changed trigger %ld notification" );
    glb_ipc_send(ipc_processing_notify, new_triggerid % CONFIG_HISTSYNCER_FORKS, (void *)&new_triggerid, 1);
    glb_ipc_dump_sender_queues(ipc_processing_notify, "Sender notify queues");
}
    
int processing_notify_flush() {
    glb_ipc_flush_all(ipc_processing_notify);
}
