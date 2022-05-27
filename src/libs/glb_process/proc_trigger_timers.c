
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

static zbx_mem_info_t	*proc_ipc_notify = NULL;
ZBX_MEM_FUNC_IMPL(__proc_ipc_notify, proc_ipc_notify);
static mem_funcs_t notify_memf = {.free_func = __proc_ipc_notify_mem_free_func, .malloc_func = __proc_ipc_notify_mem_malloc_func, .realloc_func = __proc_ipc_notify_mem_realloc_func};
static ipc_conf_t *ipc_processing_notify;

typedef struct {
    void *event_queue;
} conf_t;

static conf_t *tm_conf = NULL;
zbx_vector_uint64_t ipc_triggers = {0};

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
	
	if (glb_state_trigger_get_revision(triggerid) == revision) {
        DEBUG_TRIGGER(triggerid, "Trigger's revision %ld haven't changed, not updating", revision);
    	return;
    }
	
    nextcheck = calc_time_trigger_nextcheck(triggerid, time(NULL) );

	state_trigger_set_revision(triggerid, time_to_ms_time(nextcheck));
    DEBUG_TRIGGER(triggerid, "requeue trigger: Adding trigger requeue event in %ld seconds, conf is %p", nextcheck - time(NULL), tm_conf->event_queue);
    event_queue_add_event(tm_conf->event_queue, time_to_ms_time(nextcheck), PROCESS_TRIGGER, (void *)triggerid);

}

static void get_new_queue_triggers(int proc_num) {
	int i=0;
    static int lastcheck = 0;
    u_int64_t triggerid;
    zbx_vector_uint64_t triggers = {0};

    if (lastcheck == time(NULL))
        return;
    lastcheck = time(NULL);

    zbx_vector_uint64_create(&triggers);

    i = ipc_vector_uint64_recieve(ipc_processing_notify, proc_num-1, &triggers, IPC_PROCESS_ALL);
	
    for (i = 0; i< triggers.values_num; i++) 
        requeue_trigger(triggers.values[i]);  
   
    zbx_vector_uint64_destroy(&triggers);
}

int process_time_triggers(int max_triggers, int process_num){
    get_new_queue_triggers(process_num);
    return  event_queue_process_events(tm_conf->event_queue, max_triggers);
}

EVENT_QUEUE_CALLBACK(process_timer_triggers_cb){
	u_int64_t triggerid = (u_int64_t) data;
	u_int64_t revision = event_time;

	if (FAIL == glb_state_trigger_check_revision(triggerid, revision)) {
           DEBUG_TRIGGER( triggerid, "Trigger revision number changed %d->%d, not processing", revision,
            glb_state_trigger_check_revision(triggerid, revision) );
		return FAIL;
	}
    
    state_trigger_set_revision(triggerid, 0);

    recalculate_trigger(triggerid);


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


int processing_trigger_timers_init(size_t mem_size) {
 
    char *error;
    tm_conf = zbx_calloc(NULL, 0, sizeof(conf_t));
 
 
    if (NULL == (tm_conf->event_queue = event_queue_init(NULL)) ) 
        return FAIL;
    LOG_INF("Allocating mem of size %ld", mem_size);
    if (SUCCEED != zbx_mem_create(&proc_ipc_notify, mem_size, "Processing IPC notify queue", "Processing IPC  notify queue", 1, &error))
		return FAIL;

    event_queue_add_callback(tm_conf->event_queue, PROCESS_TRIGGER, process_timer_triggers_cb);
    int messages_count = CONFIG_HISTSYNCER_FORKS * 2 * IPC_BULK_COUNT;

    if (NULL ==(ipc_processing_notify = ipc_vector_uint64_init(IPC_PROCESSING_NOTIFY, messages_count, CONFIG_HISTSYNCER_FORKS,
        IPC_LOW_LATENCY, &notify_memf))) {
        
        return FAIL;
    }

    zbx_vector_uint64_create(&ipc_triggers);
    
    return SUCCEED;
}

void processing_notify_changed_trigger(uint64_t triggerid) {
    zbx_vector_uint64_append(&ipc_triggers, triggerid);
}
    
void  processing_notify_flush() {
    LOG_INF("Sending trigger notifications: %d triggers", ipc_triggers.values_num);
    ipc_vector_uint64_send(ipc_processing_notify, &ipc_triggers, 1);
    zbx_vector_uint64_clear(&ipc_triggers);
}
