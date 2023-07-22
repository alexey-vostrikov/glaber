/*
** Glaber
** Copyright (C) Glaber
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


#ifndef GLB_PREPROC_H
#define GLB_PREPROC_H

#include "zbxcommon.h"
#include "metric.h"

int preproc_ipc_init();

int preprocess_send_metric(const metric_t *metric);
int preprocess_send_metric_ext(const metric_t *metric, int send_mode, int priority);
int preprocessing_flush();

int preprocessing_force_flush();
int processing_force_flush();
void preprocessing_dump_sender_queues();

typedef void (*ipc_data_create_cb_t)(mem_funcs_t *memf, void *ipc_data, void *buffer);
typedef void (*ipc_data_free_cb_t)(mem_funcs_t *memf, void *ipc_data);
typedef void (*ipc_data_process_cb_t)(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data);

int preproc_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count);
int process_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count);

#define IPC_CREATE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data, void *local_data)

#define IPC_FREE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data)

#define IPC_PROCESS_CB(name) \
		static void name(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data)

#endif