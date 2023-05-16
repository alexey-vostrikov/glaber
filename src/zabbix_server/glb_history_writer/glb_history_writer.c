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
#include "glb_alerter.h"

#include "zbxcommon.h"
#include "zbxthreads.h"
#include "zbxalgo.h"
#include "zbxnix.h"
#include "zbxshmem.h"
#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../glb_poller/poller_async_io.h"

extern int		CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

//TODO: check out of mem situation
#define CONFIG_ALERTER_IPC_SIZE 16 * ZBX_MEBIBYTE

#define ALERTS_CHECK_INTERVAL 		1
#define PROCTITLE_UPDATE_INTERVAL	5
#define MEDIATYPES_UPDATE_INTERVAL	30

typedef struct {
	void *placeholder;
} glb_mediatypes_t;

typedef struct {
    strpool_t strpool;
	zbx_thread_args_t args;
	poller_event_t *new_alerts_check;
	poller_event_t *update_proctitle;
	poller_event_t *update_mediatypes;
	u_int64_t requests;
	ipc_conf_t *ipc;
	int process_num;
	glb_mediatypes_t *mediatypes;
} conf_t;

static zbx_shmem_info_t	*alerter_shmem = NULL;

ZBX_SHMEM_FUNC_IMPL(__alerter, alerter_shmem);

static mem_funcs_t ipc_memf = {
		.free_func = __alerter_shmem_free_func, 
		.malloc_func = __alerter_shmem_malloc_func, 
		.realloc_func = __alerter_shmem_realloc_func};

static conf_t conf = {0};

IPC_CREATE_CB(alert_send_cb) {
	HALT_HERE("Not implemented");
}

IPC_FREE_CB(alert_free_cb) {

}

IPC_PROCESS_CB(alert_process_cb) {
	HALT_HERE("The actual alert sending isn't implemented")
}

static void new_alerts_check_cb(poller_item_t *garbage, void *data) {
	LOG_INF("Checking for the new alerts, proc num is %d, %p",conf.args.info.process_num -1, &conf.ipc);

	glb_ipc_process(conf.ipc, conf.args.info.process_num -1 , alert_process_cb, NULL, 1000 );
}

static void update_config_cb(poller_item_t *garbage, void *data){
	LOG_INF("Updating mediatypes");
	
	HALT_HERE("Not implemented");
	//glb_actions_update(conf.actions_conf);

	LOG_INF("Finished update of mediatypes");
}
 
static void update_proc_title_cb(poller_item_t *garbage, void *dat)
{
	static u_int64_t last_call = 0;

	u_int64_t now = glb_ms_time(), time_diff = now - last_call;

	if (0 == time_diff)
		return;

	zbx_setproctitle("%s #%d [sent %ld chks/sec, got %ld esc/sec]",
					get_process_type_string(conf.args.info.process_type), conf.args.info.process_num, (conf.requests * 1000) / (time_diff));

	conf.requests = 0;
	
	last_call = now;

	if (!ZBX_IS_RUNNING())
		poller_async_loop_stop();
}

static glb_mediatypes_t * mediatypes_create() {
	glb_mediatypes_t *mediatypes = zbx_calloc(NULL, 0, sizeof(glb_mediatypes_t));
	return mediatypes;
}

static int alerter_fork_init(zbx_thread_args_t *args)
{
    conf.args = *(zbx_thread_args_t *)args;

	strpool_init(&conf.strpool, NULL);

	conf.mediatypes = mediatypes_create();


	poller_async_loop_init();

	conf.new_alerts_check = poller_create_event(NULL, new_alerts_check_cb, 0, NULL, 1);
	poller_run_timer_event(conf.new_alerts_check, ALERTS_CHECK_INTERVAL * 1000);
	
	conf.update_proctitle = poller_create_event(NULL, update_proc_title_cb, 0, NULL, 1);
	poller_run_timer_event(conf.update_proctitle, PROCTITLE_UPDATE_INTERVAL * 1000);

	conf.update_mediatypes = poller_create_event(NULL, update_config_cb, 0, NULL, 1);
	poller_run_timer_event(conf.update_mediatypes, MEDIATYPES_UPDATE_INTERVAL * 1000);

	return SUCCEED;
}

/* must be called from the parent process */
int glb_alerting_init() {
    char *error = NULL;
    
	LOG_INF("Doing init of event proc");
    if (SUCCEED != zbx_shmem_create(&alerter_shmem, CONFIG_ALERTER_IPC_SIZE, "alerting ipc cache size", 
				"CONFIG_IPC_MEM_SIZE", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	return FAIL;
    }
    
    conf.ipc = glb_ipc_init(1024, sizeof(glb_alert_t), CONFIG_FORKS[GLB_PROCESS_TYPE_ALERTER] , 
					&ipc_memf, alert_send_cb, alert_free_cb, IPC_LOW_LATENCY);

    return SUCCEED;
}

ZBX_THREAD_ENTRY(glb_alerter_thread, args)
{
	LOG_INF("Alerter is started");
	
	if (SUCCEED != alerter_fork_init(args))
		exit(-1);

	poller_async_loop_run();

	while (1)
		zbx_sleep(SEC_PER_MIN);
}

int  glb_alerter_send_alert(void) {
	HALT_HERE("Not implemented");
}