
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

#define MIN_STEP_DURATION 60

typedef struct {
    u_int16_t count;
    glb_operation_t **operations;
} operations_t ;

typedef struct {
    int default_step_duration;
    operations_t step_operations;
    operations_t update_operations;
    operations_t recovery_operations;
} glb_action_t;

struct glb_events_operations_conf_t {
    elems_hash_t *actions; //action id - indexed operations
};

static void operations_free(mem_funcs_t *memf,  operations_t *opers) {
    int i;
    
    for (int i =0; i < opers->count; i ++) 
        glb_operation_free(memf, opers->operations[i]);    
    memf->free_func(opers->operations);
    opers->count = 0;
}

int     glb_events_operations_get_max_steps(glb_events_operations_conf_t *operations, u_int64_t actionid) {
    HALT_HERE("Not implemented");
}

CONF_ARRAY_ITERATE_CB(get_step_duration_cb) {
    
}

ELEMS_CALLBACK(get_step_duration_cb) {
    glb_action_t *action = elem->data;

    int default_duration = get_simple_interval(action->default_step_duration);
    HALT_HERE("Check out if step duration might have macro, if so - transalte");
    glb_conf_array_iterate(action->step_operations.operations, get_step_duration_cb, get_min_step_durtion_data);


    HALT_HERE("Need to translate string based time to seconds");
}

static operations_get_default_step_duration(glb_events_operations_conf_t *operations, u_int64_t actionid) {
    int duration;
    
    if (SUCCEED == elems_hash_process(operations->actions, actionid, get_step_duration_cb, &duration, ELEM_FLAG_DO_NOT_CREATE))
        return duration;
    
    return MIN_STEP_DURATION;
}

int     glb_events_operations_get_step_delay(glb_events_operations_conf_t *operations, u_int64_t actionid, int step) {
    LOG_INF("Getting step delay for action %ld", actionid);
    return MAX( 
                MIN(
                    operations_get_default_step_duration(operations, actionid),
                    operations_get_min_step_duration(operations, actionid, step)
                ) , 
                MIN_STEP_DURATION 
    );
}

void    glb_event_operations_execute_step(glb_events_operations_conf_t *operations, u_int64_t actionid, int step_no) {
    HALT_HERE("Not implemented");
}

void    glb_events_operations_execute_update(glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid){
    HALT_HERE("Not implemented");
}

void    glb_events_operations_execute_recovery(glb_events_operations_conf_t *operations, u_int64_t problem_id, u_int64_t actionid) {
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

void operations_create_from_json( operations_t *opers, struct zbx_json_parse *jp, mem_funcs_t *memf) {
    const char *operation_ptr = NULL;
    struct zbx_json_parse jp_oper;
    int i=0;

    opers->count = zbx_json_count(jp);
    opers->operations = memf->malloc_func(NULL, sizeof(glb_operation_t *) * opers->count);
    
    LOG_INF("There are %d operations in the action", opers->count);

    while (NULL != (operation_ptr = zbx_json_next(jp, operation_ptr))) {
         if (SUCCEED == zbx_json_brackets_open(operation_ptr, &jp_oper)) {
                opers->operations[i] = glb_operation_create_from_json(&jp_oper, memf, NULL);
         }
         i++;
    }
}

ELEMS_CALLBACK(process_action_json) {
    LOG_INF("Processing actions %ld", elem->id);
    glb_action_t *action = elem->data;
    struct zbx_json_parse *jp = data, jp_opers;
    
    action_clear(memf, action);
    LOG_INF("Processing action %s", jp->start);
    HALT_HERE();
    
    if (SUCCEED == zbx_json_brackets_by_name(jp, "operations", &jp_opers))
        operations_create_from_json(&action->step_operations, &jp_opers, memf);

    if (SUCCEED == zbx_json_brackets_by_name(jp, "recovery_operations", &jp_opers))
        operations_create_from_json(&action->recovery_operations, &jp_opers, memf);
    
    if (SUCCEED == zbx_json_brackets_by_name(jp, "update_operations", &jp_opers))
        operations_create_from_json(&action->update_operations, &jp_opers, memf);

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
