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

// typedef struct {
//     void * unused;
// } condition_t;

struct glb_operation_t {
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

size_t glb_operation_size(void) {
    return sizeof(glb_operation_t);
}

int glb_operation_create_from_json(glb_operation_t* oper,struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool ) {
    int errflag;
    //glb_operation_t *oper = memf->malloc_func(NULL, sizeof(glb_operation_t));
    bzero(oper, sizeof(glb_operation_t));

    //LOG_INF("Creating operation from json: %s", jp->start);
    oper->id            = glb_json_get_uint64_value_by_name(jp, "operationid", &errflag);
    oper->action_id     = glb_json_get_uint64_value_by_name(jp, "actionid", &errflag);
    oper->operationtype = glb_json_get_uint64_value_by_name(jp, "operationtype", &errflag);
    oper->esc_step_from = glb_json_get_uint64_value_by_name(jp, "esc_step_from", &errflag);
    oper->esc_step_to   = glb_json_get_uint64_value_by_name(jp, "esc_step_to", &errflag);
    oper->esc_period    = glb_json_get_uint64_value_by_name(jp, "esc_period", &errflag);
    oper->evaltype      = glb_json_get_uint64_value_by_name(jp, "evaltype", &errflag);
    //LOG_INF("Creating operation conditions");
    //LOG_INF("Also loading operations messages");
    return SUCCEED;
}

int glb_operation_match_step(glb_operation_t *oper, int step_no) {
    if ( oper->esc_step_from == step_no ||
         oper->esc_step_from < step_no && oper->esc_step_to >= step_no )
        return SUCCEED;
}

int glb_operation_get_duration(glb_operation_t *oper) {
    if ( 0 == oper->esc_period)
        return ZBX_MAX_UINT31_1;

    return oper->esc_period;   
}

int glb_operation_execute(glb_operation_t *oper) {
    LOG_INF("Need to execute operation id %ld of type %d esc_period %d, evaltype %d ", 
                 oper->id, oper->operationtype, oper->esc_period, oper->evaltype);

    //there might be many different event sources as well as object types
    //so here some universall (probably callback) interface should be implemented to
    //handle operations the same way except for calling the specific interface for, 
    //say, macro translation, or logging
    
    //anouther approach might be is to create specific interfaces for each object type
    

    HALT_HERE("Operations execution is not implemented yet");
}