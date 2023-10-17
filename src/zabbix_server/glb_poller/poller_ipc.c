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
#include "../../libs/zbxipcservice/glb_ipc2.h"

extern int	CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

static zbx_shmem_info_t	*poller_ipc_notify = NULL;

ZBX_SHMEM_FUNC_IMPL(__poller_ipc_notify, poller_ipc_notify);
static mem_funcs_t ipc_memf = {
		.free_func = __poller_ipc_notify_shmem_free_func, 
		.malloc_func = __poller_ipc_notify_shmem_malloc_func, 
		.realloc_func = __poller_ipc_notify_shmem_realloc_func};

static ipc2_conf_t* ipc_poller_notify[ITEM_TYPE_MAX];

static int poller_init_ipc_type(ipc2_conf_t* ipc_poll[], int type, int forks, mem_funcs_t *memf) {
	char buffer[64];

	zbx_snprintf(buffer, 64,  "poller notify type %d", type);
	
	if (0 < forks) {
		ipc_poll[type] = ipc2_init(forks, memf, buffer, poller_ipc_notify)  ;
	}
}

int poller_notify_ipc_init(size_t mem_size) {
	char *error;
	
	if (SUCCEED != zbx_shmem_create(&poller_ipc_notify, mem_size, "Poller IPC notify queue", "Poller IPC notify queue", 1, &error))
		return FAIL;

	bzero(ipc_poller_notify, sizeof(ipc2_conf_t*) * ZBX_PROCESS_TYPE_COUNT);
	
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_SERVER, CONFIG_FORKS[GLB_PROCESS_TYPE_SERVER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_AGENT, CONFIG_FORKS[GLB_PROCESS_TYPE_AGENT], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_SNMP, CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_PINGER, CONFIG_FORKS[GLB_PROCESS_TYPE_PINGER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_WORKER, CONFIG_FORKS[GLB_PROCESS_TYPE_WORKER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, GLB_PROCESS_TYPE_SNMP_WORKER, CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP_WORKER], &ipc_memf);
	poller_init_ipc_type(ipc_poller_notify, ZBX_PROCESS_TYPE_HISTORYPOLLER, CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTORYPOLLER], &ipc_memf);
	
	return SUCCEED;
}

static int process_by_item_type[ITEM_TYPE_MAX] = {0};

static zbx_vector_uint64_pair_t *notify_buffer[ITEM_TYPE_MAX];

int poller_item_notify_init() {
	int i;
	
	if (0 < CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP_WORKER])
		process_by_item_type[ITEM_TYPE_SNMP] = GLB_PROCESS_TYPE_SNMP_WORKER;
	else 
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

void send_uint64_vector_cb(void *mem, void *ctx_data) {
	zbx_vector_uint64_t *vect = ctx_data;
	
	*(int *)mem = vect->values_num;
	memcpy(mem + sizeof(int), vect->values, vect->values_num * sizeof(u_int64_t));
}

int vector_uint64_send(ipc2_conf_t *ipc, zbx_vector_uint64_pair_t *vector, unsigned char lock ) {
	
	zbx_vector_uint64_t *snd = zbx_calloc(NULL, 0, sizeof(zbx_vector_uint64_t)*ipc2_get_consumers(ipc));
	int i;

	for (i = 0; i < ipc2_get_consumers(ipc); i++)
		zbx_vector_uint64_create(&snd[i]);
	
	for (i = 0; i < vector->values_num; i++) {
		zbx_vector_uint64_append(&snd[vector->values[i].first % ipc2_get_consumers(ipc)], vector->values[i].second);
	}
	
	for(i = 0; i < ipc2_get_consumers(ipc); i++) {
		ipc2_send_by_cb_fill(ipc, i, snd[i].values_num, sizeof(int) + sizeof(u_int64_t) * snd[i].values_num, 
				send_uint64_vector_cb, &snd[i], ALLOC_PRIORITY_NORMAL);

		zbx_vector_uint64_destroy(&snd[i]);
	}
	zbx_free(snd);
}

void poller_item_notify_flush(void) {
	int type, i;
	for (type = 0; type < ZBX_PROCESS_TYPE_COUNT; type++) {
	
		if (0 < notify_buffer[type]->values_num) {
			
			if (NULL ==  ipc_poller_notify[type]) {
				LOG_WRN("Got async-synced items of type %d with no async poller initilized, this is a programming bug", type);
				THIS_SHOULD_NEVER_HAPPEN;
			}

			vector_uint64_send(ipc_poller_notify[type], notify_buffer[type], 1);
			zbx_vector_uint64_pair_destroy(notify_buffer[type]);
		}
	}
};

void rcv_uint64_vector_cb(void *rcv_data, void *ctx_data) {
	zbx_vector_uint64_t* v = ctx_data;
	int count = *(int*)rcv_data;

	zbx_vector_uint64_append_array(v, rcv_data + sizeof(int), count);
}

int poller_ipc_notify_rcv(int value_type, int consumer, zbx_vector_uint64_t* changed_items) {
	ipc2_receive_one_chunk(ipc_poller_notify[value_type], NULL, consumer, rcv_uint64_vector_cb, changed_items);
}