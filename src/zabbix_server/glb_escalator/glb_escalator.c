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

#include "zbxcommon.h"
#include "zbxthreads.h"
#include "zbxalgo.h"
#include "zbxnix.h"
//#include "log.h"
#include "../glb_poller/poller_async_io.h"
#define NEW_ESCALATIONS_CHECK_INTERVAL 1
#define PROCTITLE_UPDATE_INTERVAL	5


typedef struct {
    strpool_t strpool;
	zbx_thread_args_t args;
	poller_event_t *new_escalations_check;
	poller_event_t *update_proctitle;
	u_int64_t requests;
	zbx_hashset_t escalations;
} conf_t;

static void new_escalations_check_cb(poller_item_t *garbage, void *data) {

}

static void update_proc_title_cb(poller_item_t *garbage, void *data)
{
	static u_int64_t last_call = 0;
	conf_t * conf = data;
	u_int64_t now = glb_ms_time(), time_diff = now - last_call;

	zbx_setproctitle("%s #%d [sent %ld chks/sec, got %ld esc/sec, escalations: %d]",
					get_process_type_string(conf->args.info.process_type), conf->args.info.process_num, (conf->requests * 1000) / (time_diff),
					conf->escalations.num_data);

	conf->requests = 0;
	
	last_call = now;

	if (!ZBX_IS_RUNNING())
		poller_async_loop_stop();
}

static int escalator_init(conf_t *conf)
{

	strpool_init(&conf->strpool, NULL);
	poller_async_loop_init();

	conf->new_escalations_check = poller_create_event(NULL, new_escalations_check_cb, 0, conf, 1);
	poller_run_timer_event(conf->new_escalations_check, NEW_ESCALATIONS_CHECK_INTERVAL);
	
	conf->update_proctitle = poller_create_event(NULL, update_proc_title_cb, 0, conf, 1);
	poller_run_timer_event(conf->update_proctitle, PROCTITLE_UPDATE_INTERVAL);

	return SUCCEED;
}


ZBX_THREAD_ENTRY(glb_escalator_thread, args)
{
    conf_t conf = {.args = *(zbx_thread_args_t *)args};
	
	//set_poller_proc_info(&conf.args);

	if (SUCCEED != escalator_init(&conf))
		exit(-1);

	poller_async_loop_run();

	//poll_shutdown(&conf);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}