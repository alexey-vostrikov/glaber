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

#include "common.h"

#include "db.h"
#include "log.h"
#include "daemon.h"
#include "zbxself.h"
#include "sighandler.h"

#include "dbcache.h"
#include "dbsyncer.h"
#include "export.h"
#include "../../libs/glb_process/process.h"
#include "../../libs/glb_process/proc_triggers.h"
#include "../../libs/glb_process/proc_trigger_timers.h"

#include "../../libs/zbxipcservice/glb_ipc.h"
#include "../../libs/glb_process/proc_trends.h"

extern int		CONFIG_HISTSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern void *ipc_processing;
static sigset_t		orig_mask;


/******************************************************************************
 *                                                                            *
 * Function: main_dbsyncer_loop                                               *
 *                                                                            *
 * Purpose: periodically synchronises data in memory cache with database      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(dbsyncer_thread, args)
{
	int		sleeptime = -1, total_values_num = 0, values_num, more, total_triggers_num = 0, triggers_num;
	double		sec, total_sec = 0.0;
	time_t		last_stat_time;
	char		*stats = NULL;
	const char	*process_name;
	size_t		stats_alloc = 0, stats_offset = 0;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type), server_num,
			(process_name = get_process_type_string(process_type)), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	zbx_setproctitle("%s #%d [connecting to the database]", process_name, process_num);
	last_stat_time = time(NULL);

	zbx_strcpy_alloc(&stats, &stats_alloc, &stats_offset, "started");

	/* database APIs might not handle signals correctly and hang, block signals to avoid hanging */
	zbx_block_signals(&orig_mask);
	DBconnect(ZBX_DB_CONNECT_NORMAL);

//	if (1 == process_num)
//		db_trigger_queue_cleanup();

	zbx_unblock_signals(&orig_mask);

	glb_ipc_init_reciever(IPC_PROCESSING);
	glb_ipc_init_reciever(IPC_PROCESSING_NOTIFY);

	trends_init_cache();
	LOG_INF("Loooping");
	for (;;)
	{
		sec = zbx_time();
		zbx_update_env(sec);

		if (0 != sleeptime)
			zbx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);

		/* database APIs might not handle signals correctly and hang, block signals to avoid hanging */
		zbx_block_signals(&orig_mask);
		values_num = process_metric_values(4096, process_num);
		triggers_num = process_time_triggers(2048, process_num);
		more = ( (values_num + triggers_num) > 0);	

		zbx_unblock_signals(&orig_mask);

		total_values_num += values_num;
		total_triggers_num += triggers_num;
		total_sec += zbx_time() - sec;
		sleeptime = (ZBX_SYNC_MORE == more ? 0 : CONFIG_HISTSYNCER_FREQUENCY);

		if ( (STAT_INTERVAL <= time(NULL) - last_stat_time) && total_sec > 0 )
		{
			stats_offset = 0;
			zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, "process %.0f val/s", total_values_num/total_sec);

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
			{
				zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, ", %.0f triggers/s",
						total_triggers_num/total_sec);
			}

			zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, " in " ZBX_FS_DBL " sec", total_sec);

			if (0 == sleeptime)
				zbx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);
			else
				zbx_setproctitle("%s #%d [%s, idle %d sec]", process_name, process_num, stats, sleeptime);

			total_values_num = 0;
			total_triggers_num = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		if (values_num + triggers_num > 0) {
			continue;
		}

		if (!ZBX_IS_RUNNING())
			break;
		
		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);
		usleep(1000);
		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
	}

	/* database APIs might not handle signals correctly and hang, block signals to avoid hanging */
	zbx_block_signals(&orig_mask);

	DBclose();
	zbx_unblock_signals(&orig_mask);

	zbx_log_sync_history_cache_progress();

	zbx_free(stats);

	exit(EXIT_SUCCESS);
#undef STAT_INTERVAL
}
