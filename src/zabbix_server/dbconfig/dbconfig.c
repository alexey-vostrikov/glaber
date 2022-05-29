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
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "dbconfig.h"
#include "dbcache.h"
#include "../../libs/zbxdbcache/changeset.h"
#include "../glb_poller/glb_poller.h"

extern int		CONFIG_CONFSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static volatile sig_atomic_t	secrets_reload;
static volatile sig_atomic_t	nextcheck;

int 	DC_ConfigNeedsSync(); 
void	DC_RequestConfigSync();
void	DC_CleanOutdatedChangedItems();

static void	zbx_dbconfig_sigusr_handler(int flags)
{
	int	msg;

	/* it is assumed that only one signal is used at a time, any subsequent signals are ignored */
	if (ZBX_RTC_CONFIG_CACHE_RELOAD == (msg = ZBX_RTC_GET_MSG(flags)))
	{
		if (0 < zbx_sleep_get_remainder())
		{
			zabbix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache");
			nextcheck = time(NULL);
			//zbx_wakeup();
		}
		else
			zabbix_log(LOG_LEVEL_WARNING, "configuration cache reloading is already in progress");
	}
	else if (ZBX_RTC_SECRETS_RELOAD == msg)
	{
		if (0 < zbx_sleep_get_remainder())
		{
			secrets_reload = 1;
			zabbix_log(LOG_LEVEL_WARNING, "forced reloading of the secrets");
			//zbx_wakeup();
		}
		else
			zabbix_log(LOG_LEVEL_WARNING, "configuration cache reloading is already in progress");
	}
}

/******************************************************************************
 *                                                                            *
 * Function: main_dbconfig_loop                                               *
 *                                                                            *
 * Purpose: periodically synchronises database data with memory cache         *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(dbconfig_thread, args)
{
	int sec = time(NULL);
	int cset_time = 0;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	zbx_set_sigusr_handler(zbx_dbconfig_sigusr_handler);

	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	zbx_setproctitle("%s [init: syncing configuration]", get_process_type_string(process_type));
	
	changeset_flush_tables();
	
	DCsync_configuration(ZBX_DBSYNC_INIT);
	nextcheck = time(NULL) + CONFIG_CONFSYNCER_FREQUENCY;
	
	while (ZBX_IS_RUNNING())
	{
		sec = time(NULL) - sec;
		zbx_update_env(sec);
	
		while (nextcheck > time(NULL) &&  FAIL == DC_ConfigNeedsSync() &&  0 == secrets_reload) 
		{

			zbx_setproctitle("%s [synced configuration in %d sec; next sync in %ld sec]",
				get_process_type_string(process_type), sec, nextcheck - time(NULL) );
			
			cset_time = changeset_get_recent_time();
			 
			 if ( cset_time > 0 && (time(NULL) - cset_time > CHANGESET_AUTOLOAD_TIME )) {
				LOG_INF("auto load chnageset data");
				DC_RequestConfigSync();
			}
			
			zbx_sleep_loop(1);
		}
		
		zbx_setproctitle("%s [update: syncing configuration; last sync in %d sec]", get_process_type_string(process_type), sec);

		sec = time(NULL);
		if (1 == secrets_reload)
		{
			DCsync_configuration(ZBX_SYNC_SECRETS);
			secrets_reload = 0;
		}
		else
		{	int sync_type= GLB_DBSYNC_CHANGESET;
			
			if (SUCCEED != DC_ConfigNeedsSync()) 
				sync_type = ZBX_DBSYNC_UPDATE;

			DCsync_configuration(sync_type);
			DCupdate_interfaces_availability();
		}
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
