/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

/* note: idea of self monitoring in glaber is:
	using IPC shared memory forks send their metrics. They are 
	mission dependant and not limited in any way

	metrics are exposed in prometheus-style, so that most 
	monitoring systems might use them

	forks add their process name, server number and pid to the metric labels
	so the metrics might be distinguished */

/* apart from metric generation, there is a Glaber Server Template which 
   takes advantage of LLD and adopts to any new metrics automatically */

#include "common.h"
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "selfmon.h"
#include "../../libs/apm/apm.h"
#include <event2/event.h>
#include <evhttp.h>


extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

void respond_metrics_cb (struct evhttp_request *request, void *privParams) {
	//evhttp_send_error(request, HTTP_NOTFOUND, "Not implemented (yet)"); 
	struct evbuffer *buffer;
	
	evhttp_add_header (evhttp_request_get_output_headers (request),
			"Content-Type", "text/plain");
	
	buffer = evbuffer_new ();
	evbuffer_add_printf (buffer, "%s", apm_server_dump_metrics());
	evhttp_send_reply(request, HTTP_OK, "OK", buffer);
	//evhttp_clear_headers (&headers);
	evbuffer_free (buffer);

}

void notfound (struct evhttp_request *request, void *params) {
	evhttp_send_error(request, HTTP_NOTFOUND, "Not Found"); 
}

static void update_proctitle_cb(int fd, short int flags, void *data) {
	double	sec;
	zbx_update_env(sec);

	zbx_setproctitle("%s [processing data]", get_process_type_string(process_type));
		
	sec = zbx_time() - sec;

	zbx_setproctitle("%s [processed data in " ZBX_FS_DBL " sec, idle 1 sec]",
			get_process_type_string(process_type), sec);

}

static void recieve_metrics_cb(int fd, short int flags, void *data) {
	LOG_INF("APM: Recieving new metrics");
	apm_recieve_new_metrics();
	LOG_INF("APM: Dumping existing metrics");
		
	LOG_INF("APM: %s:", apm_server_dump_metrics());
	LOG_INF("APM: Finished");
}

static void collect_selfmon_cb(int fd, short int flags, void *data) {
	collect_selfmon_stats();
}


ZBX_THREAD_ENTRY(selfmon_thread, args)
{


	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	struct event_base *ebase;
	struct evhttp *server;
	
	ebase = event_base_new ();;
	server = evhttp_new (ebase);
	
	struct event* update_proctitle = event_new(ebase, 0, EV_PERSIST, update_proctitle_cb, NULL);
	struct event* recieve_metrics = event_new(ebase, 0, EV_PERSIST, recieve_metrics_cb, NULL);
	struct event* collect_selfmon_stats = event_new(ebase, 0, EV_PERSIST, collect_selfmon_cb, NULL);
	
	struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
	event_add(collect_selfmon_stats, &tv);
	event_add(recieve_metrics, &tv);
	tv.tv_sec = 5;
	event_add(update_proctitle, &tv);


	evhttp_set_allowed_methods (server, EVHTTP_REQ_GET);
	evhttp_set_cb (server, "/metrics", respond_metrics_cb, 0);
	evhttp_set_gencb (server, notfound, 0);

	if (evhttp_bind_socket (server, "127.0.0.1", 8100) != 0) {
		LOG_INF("Could not bind to 127.0.0.1:8100");
		exit(-1);
	}
		
	event_base_dispatch(ebase);
	
	/* shutdown tasks */
	event_del(update_proctitle);
	event_del(recieve_metrics);
	event_del(collect_selfmon_stats);

	event_free(update_proctitle);
	event_free(recieve_metrics);
	event_free(collect_selfmon_stats);
		
	evhttp_free (server);
	event_base_free (ebase);

	// update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
	// while (ZBX_IS_RUNNING())
	// {
	// 	sec = zbx_time();
	// 	zbx_sleep_loop(ZBX_SELFMON_DELAY);
	// }
	// zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
