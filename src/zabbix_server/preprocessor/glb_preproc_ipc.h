
/*
** Copyright Glaber
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

#include "../../libs/zbxipcservice/glb_ipc.h"
#include "metric.h"

int preproc_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count);
int process_receive_metrics(int process_num, ipc_data_process_cb_t proc_func, void *cb_data, int max_count);

int processing_send_metric(const metric_t *metric);
int preprocess_send_metric_ext(const metric_t *metric, int send_mode);
int preprocessing_send_metric(const metric_t *metric);
