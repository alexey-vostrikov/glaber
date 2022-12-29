
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

/* this is simple monitoring engine to export internal metrics
via prometheus protocol via standart monitoring url */

/* most ideas and rules are taken from 
https://prometheus.io/docs/instrumenting/writing_clientlibs/ 

however this doesn't pretend to be full prometehus objects support library
it rather limited to serve the only practical need required for internal Glaber 
monitoring */
#ifndef APM_IPC_H
#define APM_IPC_H

#include "zbxcommon.h"
#include "zbxalgo.h"

#define APM_UPDATE_TIEMOUT  1000

typedef struct apm_client_t apm_client_t;

int apm_init();

apm_client_t *apm_client_init();
void apm_client_shutdown();

/* this is quite simplified but very easy to uses apm interface, just
 need to register pointer to var and call apm_flush periodically */
void apm_track_counter(u_int64_t *counter, const char *name, const char* labels); 
void apm_track_gauge(double *gauge, const char *name, const char * labels);

void apm_add_str_label(void *metric_ptr, const char *key, const char *value);
void apm_add_int_label(void *metric_ptr, const char *key, const int *value);
void apm_untrack(void *metric_ptr);

void apm_flush();
const char *apm_server_dump_metrics();
void apm_recieve_new_metrics();
void apm_add_proc_labels(void *metric);


void apm_add_heap_usage();
void apm_update_heap_usage();

#endif