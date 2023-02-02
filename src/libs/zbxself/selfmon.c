/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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
#include "zbxself.h"

#include "zbxcommon.h"

#ifndef _WINDOWS
#	include "zbxmutexs.h"
#	include "zbxnix.h"
#	include "log.h"
#	include "zbxtime.h"
#	include "zbxthreads.h"

#	define MAX_HISTORY	60

#define ZBX_SELFMON_FLUSH_DELAY		(ZBX_SELFMON_DELAY * 0.5)

/* process state cache, updated only by the processes themselves */
typedef struct
{
	/* the current usage statistics */
	zbx_uint64_t	counter[ZBX_PROCESS_STATE_COUNT];

	/* ticks of the last self monitoring update */
	clock_t		ticks;

	/* ticks of the last self monitoring cache flush */
	clock_t		ticks_flush;

	/* the current process state (see ZBX_PROCESS_STATE_* defines) */
	unsigned char	state;
}
zbx_stat_process_cache_t;

/* process state statistics */
typedef struct
{
	/* historical process state data */
	unsigned short			h_counter[ZBX_PROCESS_STATE_COUNT][MAX_HISTORY];

	/* process state data for the current data gathering cycle */
	unsigned short			counter[ZBX_PROCESS_STATE_COUNT];

	/* the process state that was already applied to the historical state data */
	zbx_uint64_t			counter_used[ZBX_PROCESS_STATE_COUNT];

	/* the process state cache */
	zbx_stat_process_cache_t	cache;
}
zbx_stat_process_t;

typedef struct
{
	zbx_stat_process_t	**process;
	int			first;
	int			count;

	/* number of ticks per second */
	int			ticks_per_sec;

	/* ticks of the last self monitoring sync (data gathering) */
	clock_t			ticks_sync;
}
zbx_selfmon_collector_t;

static zbx_selfmon_collector_t	*collector = NULL;
static int			shm_id;
static zbx_get_config_forks_f	get_config_forks_cb = NULL;

#	define LOCK_SM		zbx_mutex_lock(sm_lock)
#	define UNLOCK_SM	zbx_mutex_unlock(sm_lock)

static zbx_mutex_t	sm_lock = ZBX_MUTEX_NULL;
#endif

#ifndef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Purpose: Initialize structures and prepare state                           *
 *          for self-monitoring collector                                     *
 *                                                                            *
 ******************************************************************************/
int	zbx_init_selfmon_collector(zbx_get_config_forks_f get_config_forks, char **error)
{
	size_t		sz, sz_array, sz_process[ZBX_PROCESS_TYPE_COUNT], sz_total;
	char		*p;
	unsigned char	proc_type;
	int		proc_num, process_forks, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	get_config_forks_cb = get_config_forks;
	sz_total = sz = sizeof(zbx_selfmon_collector_t);
	sz_total += sz_array = sizeof(zbx_stat_process_t *) * ZBX_PROCESS_TYPE_COUNT;

	for (proc_type = 0; ZBX_PROCESS_TYPE_COUNT > proc_type; proc_type++)
		sz_total += sz_process[proc_type] = sizeof(zbx_stat_process_t) * get_config_forks_cb(proc_type);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() size:" ZBX_FS_SIZE_T, __func__, (zbx_fs_size_t)sz_total);

	if (SUCCEED != zbx_mutex_create(&sm_lock, ZBX_MUTEX_SELFMON, error))
	{
		zbx_error("unable to create mutex for a self-monitoring collector");
		exit(EXIT_FAILURE);
	}

	if (-1 == (shm_id = shmget(IPC_PRIVATE, sz_total, 0600)))
	{
		*error = zbx_strdup(*error, "cannot allocate shared memory for a self-monitoring collector");
		goto out;
	}

	if ((void *)(-1) == (p = (char *)shmat(shm_id, NULL, 0)))
	{
		*error = zbx_dsprintf(*error, "cannot attach shared memory for a self-monitoring collector: %s",
				zbx_strerror(errno));
		goto out;
	}

	if (-1 == shmctl(shm_id, IPC_RMID, NULL))
		zbx_error("cannot mark shared memory %d for destruction: %s", shm_id, zbx_strerror(errno));

	collector = (zbx_selfmon_collector_t *)p; p += sz;
	collector->process = (zbx_stat_process_t **)p; p += sz_array;
	collector->ticks_per_sec = sysconf(_SC_CLK_TCK);
	collector->ticks_sync = 0;

	for (proc_type = 0; ZBX_PROCESS_TYPE_COUNT > proc_type; proc_type++)
	{
		collector->process[proc_type] = (zbx_stat_process_t *)p; p += sz_process[proc_type];
		memset(collector->process[proc_type], 0, sz_process[proc_type]);

		process_forks = get_config_forks_cb(proc_type);
		for (proc_num = 0; proc_num < process_forks; proc_num++)
		{
			collector->process[proc_type][proc_num].cache.state = ZBX_PROCESS_STATE_IDLE;
		}
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() collector:%p", __func__, (void *)collector);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Free memory allocated for self-monitoring collector               *
 *                                                                            *
 ******************************************************************************/
void	zbx_free_selfmon_collector(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() collector:%p", __func__, (void *)collector);

	if (NULL == collector)
		return;

	LOCK_SM;

	(void)shmdt(collector);
	collector = NULL;

	UNLOCK_SM;

	zbx_mutex_destroy(&sm_lock);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Parameters: info  - [IN] caller process info                               *
 *             state - [IN] new process state; ZBX_PROCESS_STATE_*            *
 *                                                                            *
 ******************************************************************************/
void	zbx_update_selfmon_counter(const zbx_thread_info_t *info, unsigned char state)
{
	zbx_stat_process_t	*process;
	clock_t			ticks;
	struct tms		buf;
	int			i;

	if (ZBX_PROCESS_TYPE_UNKNOWN == info->process_type)
		return;

	process = &collector->process[info->process_type][info->process_num - 1];

	if (-1 == (ticks = times(&buf)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get process times: %s", zbx_strerror(errno));
		process->cache.state = state;
		return;
	}

	if (0 == process->cache.ticks_flush)
	{
		process->cache.ticks_flush = ticks;
		process->cache.state = state;
		process->cache.ticks = ticks;
		return;
	}

	/* update process statistics in local cache */
	process->cache.counter[process->cache.state] += ticks - process->cache.ticks;

	if (ZBX_SELFMON_FLUSH_DELAY < (double)(ticks - process->cache.ticks_flush) / collector->ticks_per_sec)
	{
		LOCK_SM;

		for (i = 0; i < ZBX_PROCESS_STATE_COUNT; i++)
		{
			/* If process did not update selfmon counter during one self monitoring data   */
			/* collection interval, then self monitor will collect statistics based on the */
			/* current process state and the ticks passed since last self monitoring data  */
			/* collection. This value is stored in counter_used and the local statistics   */
			/* must be adjusted by this (already collected) value.                         */
			if (process->cache.counter[i] > process->counter_used[i])
			{
				process->cache.counter[i] -= process->counter_used[i];
				process->counter[i] += process->cache.counter[i];
			}

			/* reset current cache statistics */
			process->counter_used[i] = 0;
			process->cache.counter[i] = 0;
		}

		process->cache.ticks_flush = ticks;

		UNLOCK_SM;
	}

	/* update local self monitoring cache */
	process->cache.state = state;
	process->cache.ticks = ticks;
}

static void	collect_selfmon_stats(void)
{
	zbx_stat_process_t	*process;
	clock_t			ticks, ticks_done;
	struct tms		buf;
	unsigned char		proc_type, i;
	int			proc_num, process_forks, index, last;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (-1 == (ticks = times(&buf)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get process times: %s", zbx_strerror(errno));
		goto out;
	}

	if (0 == collector->ticks_sync)
	{
		collector->ticks_sync = ticks;
		goto out;
	}

	if (MAX_HISTORY <= (index = collector->first + collector->count))
		index -= MAX_HISTORY;

	if (collector->count < MAX_HISTORY)
		collector->count++;
	else if (++collector->first == MAX_HISTORY)
		collector->first = 0;

	if (0 > (last = index - 1))
		last += MAX_HISTORY;

	LOCK_SM;

	ticks_done = ticks - collector->ticks_sync;

	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		process_forks = get_config_forks_cb(proc_type);
		for (proc_num = 0; proc_num < process_forks; proc_num++)
		{
			process = &collector->process[proc_type][proc_num];

			if (process->cache.ticks_flush < collector->ticks_sync)
			{
				/* If the process local cache was not flushed during the last self monitoring  */
				/* data collection interval update the process statistics based on the current */
				/* process state and ticks passed during the collection interval. Store this   */
				/* value so the process local self monitoring cache can be adjusted before     */
				/* flushing.                                                                   */
				process->counter[process->cache.state] += ticks_done;
				process->counter_used[process->cache.state] += ticks_done;
			}

			for (i = 0; i < ZBX_PROCESS_STATE_COUNT; i++)
			{
				/* The data is gathered as ticks spent in corresponding states during the */
				/* self monitoring data collection interval. But in history the data are  */
				/* stored as relative values. To achieve it we add the collected data to  */
				/* the last values.                                                       */
				process->h_counter[i][index] = process->h_counter[i][last] + process->counter[i];
				process->counter[i] = 0;
			}
		}
	}

	collector->ticks_sync = ticks;

	UNLOCK_SM;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: calculate statistics for selected process                         *
 *                                                                            *
 * Parameters: proc_type    - [IN] type of process; ZBX_PROCESS_TYPE_*        *
 *             aggr_func    - [IN] one of ZBX_SELFMON_AGGR_FUNC_*             *
 *             proc_num     - [IN] process number; 1 - first process;         *
 *                                 0 - all processes                          *
 *             state        - [IN] process state; ZBX_PROCESS_STATE_*         *
 *             value        - [OUT] a pointer to a variable that receives     *
 *                                  requested statistics                      *
 *                                                                            *
 ******************************************************************************/
void	zbx_get_selfmon_stats(unsigned char proc_type, unsigned char aggr_func, int proc_num, unsigned char state,
		double *value)
{
	unsigned int	total = 0, counter = 0;
	unsigned char	s;
	int		process_forks, current;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	process_forks = get_config_forks_cb(proc_type);

	switch (aggr_func)
	{
		case ZBX_SELFMON_AGGR_FUNC_ONE:
			assert(0 < proc_num && proc_num <= process_forks);
			process_forks = proc_num--;
			break;
		case ZBX_SELFMON_AGGR_FUNC_AVG:
		case ZBX_SELFMON_AGGR_FUNC_MAX:
		case ZBX_SELFMON_AGGR_FUNC_MIN:
			assert(0 == proc_num && 0 < process_forks);
			break;
		default:
			assert(0);
	}

	LOCK_SM;

	if (1 >= collector->count)
		goto unlock;

	if (MAX_HISTORY <= (current = (collector->first + collector->count - 1)))
		current -= MAX_HISTORY;

	for (; proc_num < process_forks; proc_num++)
	{
		zbx_stat_process_t	*process;
		unsigned int		one_total = 0, one_counter;

		process = &collector->process[proc_type][proc_num];

		for (s = 0; s < ZBX_PROCESS_STATE_COUNT; s++)
		{
			one_total += (unsigned short)(process->h_counter[s][current] -
					process->h_counter[s][collector->first]);
		}

		one_counter = (unsigned short)(process->h_counter[state][current] -
				process->h_counter[state][collector->first]);

		switch (aggr_func)
		{
			case ZBX_SELFMON_AGGR_FUNC_ONE:
			case ZBX_SELFMON_AGGR_FUNC_AVG:
				total += one_total;
				counter += one_counter;
				break;
			case ZBX_SELFMON_AGGR_FUNC_MAX:
				if (0 == proc_num || one_counter > counter)
				{
					counter = one_counter;
					total = one_total;
				}
				break;
			case ZBX_SELFMON_AGGR_FUNC_MIN:
				if (0 == proc_num || one_counter < counter)
				{
					counter = one_counter;
					total = one_total;
				}
				break;
		}
	}

unlock:
	UNLOCK_SM;

	*value = (0 == total ? 0 : 100. * (double)counter / (double)total);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves internal metrics of all running processes based on      *
 *          process type                                                      *
 *                                                                            *
 * Parameters: stats - [OUT] process metrics                                  *
 *                                                                            *
 ******************************************************************************/
int	zbx_get_all_process_stats(zbx_process_info_t *stats)
{
	int		current, ret = FAIL;
	unsigned char	proc_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	LOCK_SM;

	if (1 >= collector->count)
		goto unlock;

	if (MAX_HISTORY <= (current = (collector->first + collector->count - 1)))
		current -= MAX_HISTORY;

	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		int		proc_num;
		unsigned int	total_avg = 0, counter_avg_busy = 0, counter_avg_idle = 0,
				total_max = 0, counter_max_busy = 0, counter_max_idle = 0,
				total_min = 0, counter_min_busy = 0, counter_min_idle = 0;

		stats[proc_type].count = get_config_forks_cb(proc_type);

		for (proc_num = 0; proc_num < stats[proc_type].count; proc_num++)
		{
			zbx_stat_process_t	*process;
			unsigned int		one_total = 0, busy_counter, idle_counter;
			unsigned char		s;

			process = &collector->process[proc_type][proc_num];

			for (s = 0; s < ZBX_PROCESS_STATE_COUNT; s++)
			{
				one_total += (unsigned short)(process->h_counter[s][current] -
						process->h_counter[s][collector->first]);
			}

			busy_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_BUSY][current] -
					process->h_counter[ZBX_PROCESS_STATE_BUSY][collector->first]);

			idle_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_IDLE][current] -
					process->h_counter[ZBX_PROCESS_STATE_IDLE][collector->first]);

			total_avg += one_total;
			counter_avg_busy += busy_counter;
			counter_avg_idle += idle_counter;

			if (0 == proc_num || busy_counter > counter_max_busy)
			{
				counter_max_busy = busy_counter;
				total_max = one_total;
			}

			if (0 == proc_num || idle_counter > counter_max_idle)
			{
				counter_max_idle = idle_counter;
				total_max = one_total;
			}

			if (0 == proc_num || busy_counter < counter_min_busy)
			{
				counter_min_busy = busy_counter;
				total_min = one_total;
			}

			if (0 == proc_num || idle_counter < counter_min_idle)
			{
				counter_min_idle = idle_counter;
				total_min = one_total;
			}
		}

		stats[proc_type].busy_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_busy / (double)total_avg);
		stats[proc_type].busy_max = (0 == total_max ? 0 : 100. * (double)counter_max_busy / (double)total_max);
		stats[proc_type].busy_min = (0 == total_min ? 0 : 100. * (double)counter_min_busy / (double)total_min);

		stats[proc_type].idle_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_idle / (double)total_avg);
		stats[proc_type].idle_max = (0 == total_max ? 0 : 100. * (double)counter_max_idle / (double)total_max);
		stats[proc_type].idle_min = (0 == total_min ? 0 : 100. * (double)counter_min_idle / (double)total_min);
	}

	ret = SUCCEED;
unlock:
	UNLOCK_SM;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	sleep_remains;

/******************************************************************************
 *                                                                            *
 * Purpose: sleeping process                                                  *
 *                                                                            *
 * Parameters: info      - [IN] caller process info                           *
 *             sleeptime - [IN] required sleeptime, in seconds                *
 *                                                                            *
 ******************************************************************************/
void	zbx_sleep_loop(const zbx_thread_info_t *info, int sleeptime)
{
	if (0 >= sleeptime)
		return;

	sleep_remains = sleeptime;

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_IDLE);

	do
	{
		sleep(1);
	}
	while (0 < --sleep_remains);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);
}

ZBX_THREAD_ENTRY(zbx_selfmon_thread, args)
{
	zbx_thread_args_t	*thread_args = (zbx_thread_args_t *)args;
	const zbx_thread_info_t	*info = &thread_args->info;
	int			process_num = info->process_num;
	const char		*program_type_str = get_program_type_string(info->program_type);
	const char		*process_type_str = get_process_type_string(info->process_type);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]",  program_type_str, info->server_num,
			process_type_str, process_num);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

	while (ZBX_IS_RUNNING())
	{
		double	sec = zbx_time();

		zbx_update_env(get_process_type_string(info->process_type), sec);

		zbx_setproctitle("%s [processing data]", process_type_str);

		collect_selfmon_stats();
		sec = zbx_time() - sec;

		zbx_setproctitle("%s [processed data in " ZBX_FS_DBL " sec, idle 1 sec]", process_type_str, sec);

		zbx_sleep_loop(info, ZBX_SELFMON_DELAY);
	}

	zbx_setproctitle("%s #%d [terminated]", process_type_str, process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}


#endif
