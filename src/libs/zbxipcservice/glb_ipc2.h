/*
** Copyright Glaber 2018-2023
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

#ifndef GLB_IPC2_SERVICE_H
#define GLB_IPC2_SERVICE_H

#include "zbxcommon.h"
#include "log.h"
#include "zbxshmem.h"
#include "zbxalgo.h"
#include "glb_lock.h"

#define ALLOC_PRIORITY_HIGH     1
#define ALLOC_PRIORITY_NORMAL	2

typedef struct ipc2_conf_t ipc2_conf_t;
typedef struct ipc2_rcv_conf_t ipc2_rcv_conf_t;

typedef void (*ipc2_receive_cb_t)(void *data, void *data_func);

ipc2_conf_t *ipc2_init(int consumers, mem_funcs_t *memf, char *name, zbx_shmem_info_t *mem_info);
void ipc2_destroy(ipc2_conf_t *ipc);
void ipc2_send_chunk(ipc2_conf_t *ipc, int consumerid, int sent_items, void *data, size_t len, int priority);
void ipc2_send_by_cb_fill(ipc2_conf_t *ipc, int consumerid, int items_count, size_t len, ipc2_receive_cb_t cb_func, void *ctx_data, int priority);
int ipc2_receive_one_chunk(ipc2_conf_t *ipc, ipc2_rcv_conf_t *rcv, int consumerid, ipc2_receive_cb_t receive_func, void *ctx_data);

ipc2_rcv_conf_t *ipc2_init_receiver();
void ipc2_deinit_receiver(ipc2_rcv_conf_t *rcv_ipc);

u_int64_t ipc2_get_sent_chunks(ipc2_conf_t *ipc);
u_int64_t ipc2_get_sent_items(ipc2_conf_t *ipc);
int ipc2_get_consumers(ipc2_conf_t *ipc);

void ipc2_dump_queues(ipc2_conf_t *ipc);
u_int64_t ipc2_get_queue_size(ipc2_conf_t *ipc);

#endif