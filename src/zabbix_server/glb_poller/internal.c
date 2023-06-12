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
#include "module.h"
#include "zbxalgo.h"
#include "internal.h"

//internal metrics in glaber is a registration-based mechanism
//so any system, module, whatever might register itself to provide some data to 
//internal monitoring
typedef struct {
    elems_hash_t *funcs;
} conf_t;

static conf = {0};


#define INTERNAL_METRIC_CALLBACK(name) \
        int name(char *first_param, int nparams, AGENT_RESULT *result, char **result);


int register_internal_metric_handler(const char *name, internal_metric_handler_func_t func) {
    HALT_HERE("Not implemented");
};

int glb_get_internal_metric(const char *first_param, int nparams, AGENT_REQUEST *request, char **result) 
{
    HALT_HERE("Not implemented");
    internal_metric_handler_func_t func = get_func_by_name(first_param);
    
    if (NULL == func)
        return FAIL;
    
    return func(first_param, nparams, request, result);
}