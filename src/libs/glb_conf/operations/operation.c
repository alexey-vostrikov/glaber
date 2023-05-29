
/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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

#include "operations.h"
#include "zbxcommon.h"
#include "log.h"
#include "zbxalgo.h"
#include "operation.h"
#include "../conf.h"

/*
    "actionid": "2",
    "name": "Auto discovery. Linux servers.",
    "eventsource": "1",
    "status": "1",
    "esc_period": "0s",
    "pause_suppressed": "1",
    "filter": {               
          "evaltype": "0",
          "formula": "",
           "conditions": [
            {   "conditiontype": "10",
                "operator": "0",                 
                "value": "0",
                "value2": "",
                "formulaid": "B"
            },
*/

typedef struct {

} condition_t;

typedef struct glb_operation_t {
    u_int64_t id;
    u_int64_t action_id;
    int operationtype;
    int esc_period;
    int esc_step_from;
    int esc_step_to;
    int evaltype;
    void *conditions;
};

void glb_operation_free(mem_funcs_t *memf, glb_operation_t *operation) {
    memf->free_func(operation);
}

glb_operation_t *glb_operation_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool ) {
    int errflag;
    glb_operation_t *oper = memf->malloc_func(NULL, sizeof(glb_operation_t));
    bzero(oper, sizeof(glb_operation_t));

    LOG_INF("Creating operation from json: %s", jp->start);
    oper->id            = glb_json_get_uint64_value_by_name(jp, "operationid", &errflag);
    oper->action_id     = glb_json_get_uint64_value_by_name(jp, "actionid", &errflag);
    oper->operationtype = glb_json_get_uint64_value_by_name(jp, "operationtype", &errflag);
    oper->esc_step_from = glb_json_get_uint64_value_by_name(jp, "esc_step_from", &errflag);
    oper->esc_step_to   = glb_json_get_uint64_value_by_name(jp, "esc_step_to", &errflag);
    oper->esc_period    = glb_json_get_uint64_value_by_name(jp, "esc_period", &errflag);
    oper->evaltype      = glb_json_get_uint64_value_by_name(jp, "evaltype", &errflag);
    LOG_INF("Creating operation conditions");
    LOG_INF("Also loading operations messages");
    return oper;
}