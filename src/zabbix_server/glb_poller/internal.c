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
    strpool_t strpool;
} conf_t;

static conf_t conf = {0};

typedef struct {
    const char *name;
    internal_metric_handler_func_t func;
} func_t;

ELEMS_CALLBACK(func_register_cb) {
    func_t *func, *reg_func = data;

    if (NULL != elem->data) 
        return FAIL;

    func = memf->malloc_func(NULL, sizeof(func_t));
    func->func = reg_func->func;

    func->name = strpool_copy(reg_func->name);
    elem->data = func;
    
    return SUCCEED;
}

int glb_register_internal_metric_handler(const char *name, internal_metric_handler_func_t func) {
    int ret;

    if ( NULL == name || NULL == func )
        return FAIL;
 
    const char *pooled_name = strpool_add(&conf.strpool, name);

    func_t new_func  = {.func = func, .name = pooled_name };
    
    ret = elems_hash_process(conf.funcs, (u_int64_t)pooled_name, func_register_cb,  &new_func, 0);
    
    strpool_free(&conf.strpool, pooled_name);
    
    return ret;
};

ELEMS_CALLBACK(fetch_func_cb) {
    if (NULL == elem->data)
        return FAIL;
    func_t *func = elem->data, 
           *req_func = data;
    
    req_func->func = func->func;
    
    return SUCCEED;
}

internal_metric_handler_func_t get_func_by_first_param(const char* name) {
    int ret;
    const char *pooled_name = strpool_add(&conf.strpool, name);
    func_t func_request = {.name = pooled_name};

    ret = elems_hash_process(conf.funcs, (u_int64_t)pooled_name, fetch_func_cb, &func_request, ELEM_FLAG_DO_NOT_CREATE);
    strpool_free(&conf.strpool, pooled_name);
    
    if (FAIL == ret) {
        LOG_INF("Couldn't find function int the cache");
        return NULL;
    };
    return func_request.func;
}

int glb_get_internal_metric(const char *first_param, int nparams, AGENT_REQUEST *request, char **result) 
{
    internal_metric_handler_func_t func = get_func_by_first_param(first_param);
    
    if (NULL == func)
        return FAIL;

    return func(first_param, nparams, request, result);
}

ELEMS_CREATE(func_create_cb) {
    elem->data = NULL;
}

ELEMS_FREE(func_free_cb) {
    if (NULL != elem->data)
        memf->free_func(elem->data);
}

int glb_internal_metrics_init() {

    conf.funcs = elems_hash_init(NULL, func_create_cb, func_free_cb);
    strpool_init(&conf.strpool, NULL);
    return SUCCEED;
}

void glb_internal_metrics_destory() {
    elems_hash_destroy(conf.funcs);
    strpool_destroy(&conf.strpool);
}