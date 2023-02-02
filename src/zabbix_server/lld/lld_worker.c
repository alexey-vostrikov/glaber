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

#include "lld_worker.h"
#include "lld.h"

#include "zbxnix.h"
#include "log.h"
#include "zbxipcservice.h"
#include "zbxself.h"
#include "../glb_events.h"
#include "lld_protocol.h"
#include "zbxtime.h"
#include "zbxdbwrap.h"
#include "zbx_item_constants.h"
#include "../../libs/glb_state/glb_state_items.h"

/******************************************************************************
 *                                                                            *
 * Purpose: registers lld worker with lld manager                             *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
static void	lld_register_worker(zbx_ipc_socket_t *socket)
{
	pid_t	ppid;

	ppid = getppid();

	zbx_ipc_socket_write(socket, ZBX_IPC_LLD_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}

/******************************************************************************
 *                                                                            *
 * Purpose: processes lld task and updates rule state/error in configuration  *
 *          cache and database                                                *
 *                                                                            *
 * Parameters: message - [IN] the message with LLD request                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_process_task(zbx_ipc_message_t *message)
{
	zbx_uint64_t		itemid, hostid, lastlogsize;
	char			*value, *error;
	zbx_timespec_t		ts;
	
	
	ZBX_DC_HISTORY hist = {0};
	DC_ITEM			item;
	int			errcode, mtime;
	unsigned char		state, meta;
	glb_state_item_meta_t cache_state = {0};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_lld_deserialize_item_value(message->data, &itemid, &hostid, &value, &ts, &meta, &lastlogsize, &mtime, &error);

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	if (SUCCEED != errcode)
		goto out;

	zabbix_log(LOG_LEVEL_DEBUG, "processing discovery rule:" ZBX_FS_UI64, itemid);
	DEBUG_ITEM(itemid, "Processing LLD rule in the worker");

	if (NULL != error || NULL != value)
	{
		if (NULL == error && SUCCEED == lld_process_discovery_rule(itemid, value, &error))
			state = ITEM_STATE_NORMAL;
		else
			state = ITEM_STATE_NOTSUPPORTED;

		if (state != item.state)
		{
			hist.state = state;

			if (ITEM_STATE_NORMAL == state)
			{
				zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s:%s\" became supported",
						item.host.host, item.key_orig);

				zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, itemid, &ts,
						ITEM_STATE_NORMAL, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL,
						NULL, NULL, 0);
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s:%s\" became not supported: %s",
						item.host.host, item.key_orig, error);

				zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, itemid, &ts,
						ITEM_STATE_NOTSUPPORTED, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0,
						NULL, NULL, error, -1);
			}

			zbx_process_events(NULL, NULL, NULL);
			zbx_clean_events();
		}
	}

	
	cache_state.state = state;
	cache_state.lastdata = time(NULL);
	cache_state.error = error;
		
	glb_state_item_update_meta(itemid, &cache_state, 
			GLB_CACHE_ITEM_UPDATE_LASTDATA | GLB_CACHE_ITEM_UPDATE_STATE | 	GLB_CACHE_ITEM_UPDATE_ERRORMSG , ITEM_VALUE_TYPE_STR );	
	/* with successful LLD processing LLD error will be set to empty string */
		
	if (NULL != error)
		hist.value.err = error;
			
	if (NULL != value )	 {
		hist.value_type = ITEM_VALUE_TYPE_TEXT;
		hist.value.str = value;
		hist.ts = ts;
		hist.itemid = itemid;
		glb_state_item_add_lld_value(&hist);
 	}

	DCconfig_clean_items(&item, &errcode, 1);

out:
	zbx_free(value);
	zbx_free(error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

ZBX_THREAD_ENTRY(lld_worker_thread, args)
{
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	char			*error = NULL;
	zbx_ipc_socket_t	lld_socket;
	zbx_ipc_message_t	message;
	double			time_stat, time_idle = 0, time_now, time_read;
	zbx_uint64_t		processed_num = 0;
	zbx_thread_info_t	*info = &((zbx_thread_args_t *)args)->info;
	int			server_num = ((zbx_thread_args_t *)args)->info.server_num;
	int			process_num = ((zbx_thread_args_t *)args)->info.process_num;
	unsigned char		process_type = ((zbx_thread_args_t *)args)->info.process_type;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(info->program_type),
			server_num, get_process_type_string(process_type), process_num);

	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	zbx_ipc_message_init(&message);

	if (FAIL == zbx_ipc_socket_open(&lld_socket, ZBX_IPC_SERVICE_LLD, SEC_PER_MIN, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to lld manager service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	lld_register_worker(&lld_socket);

	time_stat = zbx_time();

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

	while (ZBX_IS_RUNNING())
	{
		time_now = zbx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			zbx_setproctitle("%s #%d [processed " ZBX_FS_UI64 " LLD rules, idle " ZBX_FS_DBL " sec during "
					ZBX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					processed_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			processed_num = 0;
		}

		zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_IDLE);
		if (SUCCEED != zbx_ipc_socket_read(&lld_socket, &message))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot read LLD manager service request");
			exit(EXIT_FAILURE);
		}
		zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

		time_read = zbx_time();
		time_idle += time_read - time_now;
		zbx_update_env(get_process_type_string(process_type), time_read);

		switch (message.code)
		{
			case ZBX_IPC_LLD_TASK:
				lld_process_task(&message);
				zbx_ipc_socket_write(&lld_socket, ZBX_IPC_LLD_DONE, NULL, 0);
				processed_num++;
				break;
		}

		zbx_ipc_message_clean(&message);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);

	DBclose();

	zbx_ipc_socket_close(&lld_socket);
}
