
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
#include "operation.h"
#include "zbxcommon.h"
#include "log.h"
#include "../../libs/glb_conf/api_sync.h"
#include "../../libs/glb_conf/conf.h"
#include "zbxalgo.h"
#include "../conf.h"
#include "operations.h"


#define MIN_STEP_DURATION 60
#define MAX_STEP_DURATION ZBX_MAX_UINT31_1

typedef struct {
    u_int16_t count;
    void *operations;
} operations_t ;

typedef struct {
    int default_step_duration;
    operations_t step_operations;
    operations_t update_operations;
    operations_t recovery_operations;
    int event_source;
} glb_action_t;

struct glb_events_operations_conf_t {
    elems_hash_t *actions; //action id - indexed operations
};

static void operations_free(mem_funcs_t *memf,  operations_t *opers) {
    int i;
    
    //for (int i =0; i < opers->count; i ++) 
    //glb_operation_free(memf, opers->operations);    
    memf->free_func(opers->operations);
    opers->count = 0;
}

int  glb_events_operations_get_max_steps(glb_events_operations_conf_t *operations, u_int64_t actionid) {
    HALT_HERE("Not implemented");
}

ELEMS_CALLBACK(get_step_default_duration_cb) {
    glb_action_t *action = elem->data;
    int *duration = data;

    *duration = action->default_step_duration;

    return SUCCEED;
}

static int operations_get_default_step_duration(glb_events_operations_conf_t *operations, u_int64_t actionid) {
    int duration;
    
    if (SUCCEED == elems_hash_process(operations->actions, actionid, get_step_default_duration_cb,
                                                                 &duration, ELEM_FLAG_DO_NOT_CREATE))
        return duration;
    
    return MIN_STEP_DURATION;
}

typedef struct {
    int step_no;
    int duration;
} get_min_step_duration_t;

CONF_ARRAY_ITERATE_CB(operation_get_duration_cb) {
    glb_operation_t *operation = elem_ptr;
    get_min_step_duration_t *step_dur = data;

    if (SUCCEED == glb_operation_match_step(operation, step_dur->step_no) &&
        glb_operation_get_duration(operation) < step_dur->duration) 
    {
        step_dur->duration = glb_operation_get_duration(operation);
    }
}

ELEMS_CALLBACK(get_action_min_step_duration_cb) {
    glb_action_t *action = elem->data;
    get_min_step_duration_t *step_dur = data;

    step_dur->duration = MAX_STEP_DURATION;
    glb_conf_array_iterate(action->step_operations.operations, action->step_operations.count,
                            glb_operation_size(), operation_get_duration_cb, step_dur );

}

int operations_get_min_step_duration(glb_events_operations_conf_t *operations, u_int64_t actionid, int step_no ) {
    get_min_step_duration_t get_dur = {.step_no = step_no};
    
    if (SUCCEED == elems_hash_process(operations->actions, actionid, get_action_min_step_duration_cb,
                                                                 &get_dur, ELEM_FLAG_DO_NOT_CREATE))
        return get_dur.duration;

     return MIN_STEP_DURATION;
}

int     glb_events_operations_get_step_delay(glb_events_operations_conf_t *operations, u_int64_t actionid, int step) {
    LOG_INF("Getting step delay for action %ld", actionid);
    int duration;
    
    if (MAX_STEP_DURATION > (duration == operations_get_min_step_duration(operations, actionid, step))) {
        LOG_INF("Step %d delay is %d", step, duration);
        return  duration;
    }
    LOG_INF("Step delay is default, %d", operations_get_default_step_duration(operations, actionid));

    return operations_get_default_step_duration(operations, actionid);
}

typedef struct {
    int step;
    u_int64_t problemid;
} oper_exec_data_t;


CONF_ARRAY_ITERATE_CB(problem_oper_exec_data_cb) {
    glb_operation_t *oper = elem_ptr;
    oper_exec_data_t *oper_exec_data = data;

    LOG_INF("Processing operation %d for problem %ld", i, oper_exec_data->problemid);

    if (FAIL == glb_operation_match_step(oper, oper_exec_data->step))
        return ;
    
    glb_operation_execute(oper);
}

//for implementation of executions please look at esacalator.c: 1943
ELEMS_CALLBACK(problem_action_execute_cb) {
    glb_action_t *action = elem->data;
    oper_exec_data_t *oper_exec_data = data;
    
    LOG_INF("There are %d operations for the action %ld", action->step_operations.count, elem->id);

    glb_conf_array_iterate(action->step_operations.operations, action->step_operations.count, 
                    glb_operation_size(), problem_oper_exec_data_cb, oper_exec_data);

}

void    glb_event_operations_execute_step(u_int64_t problemid, glb_events_operations_conf_t *operations, u_int64_t actionid, int step_no) {
    LOG_INF("Need to exec step %d for action %ld for problem %ld", step_no, actionid, problemid);
    elems_hash_process(operations->actions,actionid, problem_action_execute_cb, &problemid, ELEM_FLAG_DO_NOT_CREATE);
    //HALT_HERE("Not implemented");
}

void    glb_events_operations_execute_update(u_int64_t problemid, glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid){
    HALT_HERE("Not implemented");
}

void    glb_events_operations_execute_recovery(u_int64_t problemid, glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid) {
    HALT_HERE("Not implemented");
}

ELEMS_CREATE(operation_create_cb) {
    elem->data = memf->malloc_func(NULL, sizeof(glb_action_t));
    
    if (NULL == elem->data)
        return FAIL;

    bzero(elem->data, sizeof(glb_action_t));
    return SUCCEED;
}

static void action_clear(mem_funcs_t *memf, glb_action_t *action) {
    operations_free(memf, &action->step_operations);
    operations_free(memf, &action->update_operations);
    operations_free(memf, &action->recovery_operations);
}

ELEMS_FREE(operation_free_cb) {
    action_clear(memf, (glb_action_t *)elem->data);
    memf->free_func(elem->data);
}

void operations_create_from_json(operations_t *opers, struct zbx_json_parse *jp, mem_funcs_t *memf) {
    const char *operation_ptr = NULL;
    struct zbx_json_parse jp_oper;
    int i=0;

    opers->count = zbx_json_count(jp);
    opers->operations = memf->malloc_func(NULL, glb_operation_size() * opers->count);
    
    LOG_INF("There are %d operations in the action", opers->count);

    while (NULL != (operation_ptr = zbx_json_next(jp, operation_ptr))) {
         if (SUCCEED == zbx_json_brackets_open(operation_ptr, &jp_oper)) {
                //opers->operations[i] = 
                glb_operation_create_from_json(opers->operations + i * glb_operation_size(), &jp_oper, memf, NULL);
         }
         i++;
    }
}

ELEMS_CALLBACK(process_action_json) {
    LOG_INF("Processing actions %ld", elem->id);
    glb_action_t *action = elem->data;
    struct zbx_json_parse *jp = data, jp_opers;
    int errflag;
    
    action_clear(memf, action);
 
   // LOG_INF("Processing action %s", jp->start);
    action->event_source = glb_json_get_uint64_value_by_name(jp, "eventsource", &errflag);
    
    //LOG_INF("Got eventsource %d", action->event_source);

    if (SUCCEED == zbx_json_brackets_by_name(jp, "operations", &jp_opers))
        operations_create_from_json(&action->step_operations, &jp_opers, memf);

    if (SUCCEED == zbx_json_brackets_by_name(jp, "recovery_operations", &jp_opers))
        operations_create_from_json(&action->recovery_operations, &jp_opers, memf);
    
    if (SUCCEED == zbx_json_brackets_by_name(jp, "update_operations", &jp_opers))
        operations_create_from_json(&action->update_operations, &jp_opers, memf);
    
   // HALT_HERE();
} 

glb_events_operations_conf_t *glb_events_operations_init(mem_funcs_t *memf) {
    glb_events_operations_conf_t *opers = memf->malloc_func(NULL, sizeof(glb_events_operations_conf_t));
    opers->actions = elems_hash_init(memf, operation_create_cb, operation_free_cb);
    
    return opers;
}   

void    glb_events_operations_update(glb_events_operations_conf_t *operations) {
    char *ops_json = NULL;
    LOG_INF("Updating the operations, oper is %p", operations);  
    ops_json = glb_conf_get_json_data_table(GLB_CONF_API_ACTIONS_OPERATIONS);
    
    if (NULL != ops_json)
        glb_conf_iterate_on_set_data(ops_json, "actionid", operations->actions, process_action_json);
  
  //  HALT_HERE();
    
}

void    glb_events_operations_free(glb_events_operations_conf_t *operations) {
    HALT_HERE("Not implemented");
}
