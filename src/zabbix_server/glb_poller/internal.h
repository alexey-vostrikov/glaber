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


typedef int (*internal_metric_handler_func_t)(const char *first_param, int nparams,  
                    AGENT_REQUEST *request, char **result);

#define INTERNAL_METRIC_CALLBACK(name) \
        int name(const char *first_param, int nparams, AGENT_REQUEST *request, char **result)

int glb_internal_metrics_init();
void glb_internal_metrics_destory();
int glb_register_internal_metric_handler(const char *name, internal_metric_handler_func_t func);
int glb_get_internal_metric(const char *first_param, int nparams, AGENT_REQUEST *request, char **result);
