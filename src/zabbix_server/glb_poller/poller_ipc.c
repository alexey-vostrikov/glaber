/*
** Glaber
** Copyright (C)  Glaber
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
#include "log.h"
#include "zbxcommon.h"
#include "zbxalgo.h"
#include "zbxshmem.h"
#include "../../libs/zbxipcservice/glb_ipc.h"

extern int	CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

static zbx_shmem_info_t	*poller_ipc_notify = NULL;

ZBX_SHMEM_FUNC_IMPL(__poller_ipc_notify, poller_ipc_notify);
static mem_funcs_t ipc_memf = {
		.free_func = __poller_ipc_notify_shmem_free_func, 
		.malloc_func = __poller_ipc_notify_shmem_malloc_func, 
		.realloc_func = __poller_ipc_notify_shmem_realloc_func};

static ipc_conf_t* ipc_poller_notify[ITEM_TYPE_MAX];


static int poller_init_ipc_type(ipc_conf_t* ipc_poll[], int type, int forks, mem_funcs_t *memf) {
	if (0 < forks) {
	//	LOG_INF("doing IPC init of type %d forks %d", type, forks);
		ipc_poll[type] = ipc_vector_uint64_init(forks *2 *IPC_BULK_COUNT, forks, IPC_LOW_LATENCY, &ipc_memf);
	}
}

int poller_notify_ipc_init(size_t mem_size) {
	char *error;
	
	if (SUCCEED != zbx_shmem_create(&poller_ipc_notify, mem_size, "Poller IPC notify queue", "Poller IPC notify queue", 1, &error))
		return FAIL;

	bzero(ipc_poller_notify, sizeof(ipc_conf_t*) * ZBX_PROCESS_TYPE_COUNT);
	
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_SERVER, CONFIG_FORKS[GLB_PROCESS_TYPE_SERVER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_AGENT, CONFIG_FORKS[GLB_PROCESS_TYPE_AGENT], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_SNMP, CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_PINGER, CONFIG_FORKS[GLB_PROCESS_TYPE_PINGER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_WORKER, CONFIG_FORKS[GLB_PROCESS_TYPE_WORKER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, ZBX_PROCESS_TYPE_HISTORYPOLLER, CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTORYPOLLER], &ipc_memf);
	
	return SUCCEED;
}

static int process_by_item_type[ITEM_TYPE_MAX] = {0};


static zbx_vector_uint64_pair_t *notify_buffer[ITEM_TYPE_MAX];

int poller_item_notify_init() {
	int i;
	
	process_by_item_type[ITEM_TYPE_SNMP] = GLB_PROCESS_TYPE_SNMP;
	process_by_item_type[ITEM_TYPE_AGENT] = GLB_PROCESS_TYPE_AGENT;
	process_by_item_type[ITEM_TYPE_SIMPLE] = GLB_PROCESS_TYPE_PINGER;
	process_by_item_type[ITEM_TYPE_CALCULATED] = ZBX_PROCESS_TYPE_HISTORYPOLLER;
	process_by_item_type[ITEM_TYPE_WORKER_SERVER] = GLB_PROCESS_TYPE_SERVER;
	process_by_item_type[ITEM_TYPE_EXTERNAL] = GLB_PROCESS_TYPE_WORKER;

	for(i = 0; i < ITEM_TYPE_MAX; i++) {
		notify_buffer[i] = zbx_malloc(NULL, sizeof(zbx_vector_uint64_pair_t));
		zbx_vector_uint64_pair_create(notify_buffer[i]);
	}
}

int poller_item_add_notify(int item_type, char *key, u_int64_t itemid, u_int64_t hostid) {
	zbx_uint64_pair_t pair = {.first = hostid, .second = itemid};

	DEBUG_ITEM(itemid,"Adding item to async polling notify for item type %d", item_type);

	if (ITEM_TYPE_SIMPLE == item_type && 0 == strncmp(key, "net.tcp.service[http", 19)) {
		item_type = ITEM_TYPE_AGENT;
		DEBUG_ITEM(itemid, "Set item type to %d, poller num is %d", item_type, process_by_item_type[item_type]);
	}

	int  process_type = process_by_item_type[item_type];

	if (item_type >= ITEM_TYPE_MAX || 0 > item_type  || 0 == process_type) {
		
		THIS_SHOULD_NEVER_HAPPEN;
		HALT_HERE("Item type is %d, process_type is %d", item_type, process_type);
		return FAIL;
	}

	if (NULL == ipc_poller_notify[process_type])
		return FAIL;
	
	DEBUG_ITEM(itemid,"Adding item to async polling notify for type %d to process type %d", item_type, process_by_item_type[process_type]);
	zbx_vector_uint64_pair_append(notify_buffer[process_type], pair);
	
	return SUCCEED;
}

void poller_item_notify_flush(void) {
	int type, i;
	for (type = 0; type < ZBX_PROCESS_TYPE_COUNT; type++) {
	
		if (0 < notify_buffer[type]->values_num) {
			
			if (NULL ==  ipc_poller_notify[type]) {
				LOG_WRN("Got async-synced items of type %d with no async poller initilized, this is a programming bug", type);
				THIS_SHOULD_NEVER_HAPPEN;
				//exit(-1);
			}
			//LOG_INF("IPC: flushing %d items for type %d", notify_buffer[type]->values_num, type);
			ipc_vector_uint64_send(ipc_poller_notify[type], notify_buffer[type], 1);
			zbx_vector_uint64_pair_destroy(notify_buffer[type]);
		}
	}
};

 int poller_ipc_notify_rcv(int value_type, int consumer, zbx_vector_uint64_t* changed_items) {
	ipc_vector_uint64_recieve(ipc_poller_notify[value_type], consumer, changed_items, IPC_PROCESS_ALL);
}
