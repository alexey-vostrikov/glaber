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

#include "glb_common.h"
#include "glb_state_problem.h"
#include "glb_state_ids.h"
#include "load_dump.h"
#include "../glb_macro/glb_macro.h"
#include "zbxserver.h"

static mem_funcs_t heap_memf = { .malloc_func = zbx_default_mem_malloc_func, 
              .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};

struct glb_problem_t {
    u_int64_t id;
    glb_problem_source_t source;
    u_int64_t objectid;
    const char *name;
	unsigned char acknowledged;
	unsigned char severity; 

    glb_problem_oper_state_t oper_state;
	u_int64_t cause_problem;

    u_int64_t recovery_time; 
    u_int64_t correlation_id;

};

glb_problem_t *glb_problem_create_by_trigger(mem_funcs_t *memf, strpool_t *strpool, u_int64_t problemid, DC_TRIGGER *trigger) {

    if (NULL == memf)
        memf = &heap_memf;
    
    char problem_name[MAX_STRING_LEN];

    if (0 == problemid && 0 == (problemid = glb_state_id_gen_new())) // no id, failed to gen a new one
        return NULL;
    
    glb_problem_t* problem = memf->malloc_func(NULL, sizeof(glb_problem_t));
    
    if (NULL == problem || NULL == trigger->event_name)
        return NULL;
    
    bzero(problem, sizeof(glb_problem_t));

    if (FAIL == glb_macro_translate_event_name(trigger, problem_name, MAX_STRING_LEN))
        return NULL;

    if (NULL != strpool) 
        problem->name = strpool_add(strpool, problem_name);
    else 
        problem->name = zbx_strdup(NULL, problem_name);
 
    LOG_INF("Translated problem name '%s'", problem->name);
    
    //ZBX_DB_EVENT event = {0};
    //char *err;
    //event.trigger = trigger;
    //zbx_substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    //					 &event.name, MACRO_TYPE_EVENT_NAME, err, sizeof(err));
     
    //glb_macro_translate_event_name();
    //glb_macro_translate_event_name();

    problem->id = problemid;
    problem->objectid = trigger->triggerid;
    problem->oper_state = GLB_PROBLEM_OPER_STATE_PROBLEM;
    problem->severity = trigger->priority;
    problem->source = GLB_PROBLEM_SOURCE_TRIGGER;

    return problem;
}

void glb_problem_destroy(mem_funcs_t *memf, strpool_t *strpool, glb_problem_t *problem) {
    if (NULL == memf)
        memf = &heap_memf;
       
    LOG_INF("name handling");
    if (NULL != strpool) 
        strpool_free(strpool, problem->name);
    else {
        LOG_INF("name handling: freeing the name %p", problem->name);
        memf->free_func((char *)problem->name);
    }
    LOG_INF("Freeing the problem");
    memf->free_func(problem);
}

glb_problem_t * glb_problem_copy(mem_funcs_t *memf, strpool_t *strpool,  glb_problem_t *src_problem) {
    if (NULL == memf)
        memf = &heap_memf;
    
    glb_problem_t* dst_problem = memf->malloc_func(NULL, sizeof(glb_problem_t));
    bzero(dst_problem, sizeof(glb_problem_t));

    memcpy(dst_problem, src_problem, sizeof(glb_problem_t));

    if (NULL != strpool) 
        dst_problem->name = strpool_add(strpool, src_problem->name);
    else 
        dst_problem->name = zbx_strdup(NULL, src_problem->name);
    
    return dst_problem;
}


glb_problem_source_t glb_problem_get_source(glb_problem_t *problem) {
    return problem->source;
}

u_int64_t glb_problem_get_objectid(glb_problem_t *problem) {
    return problem->objectid;
}


u_int64_t glb_problem_get_id(glb_problem_t *problem) {
    return problem->id;
}

int  glb_problem_set_oper_state(glb_problem_t *problem, glb_problem_oper_state_t oper_state) {
    if ( GLB_PROBLEM_OPER_STATE_COUNT <= oper_state || 0 > oper_state ) 
        return FAIL;
    

    problem->oper_state = oper_state;
    
    LOG_INF("Set oper state to %d for problem %lld", problem->oper_state, problem->id);
    if (GLB_PROBLEM_OPER_STATE_OK == oper_state )
         problem->recovery_time = time(NULL);
    return SUCCEED;
}

glb_problem_oper_state_t  glb_problem_get_oper_state(glb_problem_t *problem) {
    return problem->oper_state;
}

int glb_problem_marshall_to_json(glb_problem_t *problem, struct zbx_json *json)
{   
    zbx_json_addint64(json, "id", problem->id);
    zbx_json_addint64(json, "source", problem->source);
    zbx_json_addint64(json, "objectid", problem->objectid);
    zbx_json_addstring(json, "name", problem->name, ZBX_JSON_TYPE_STRING);
    zbx_json_addint64(json, "acknowledged", problem->acknowledged);
    zbx_json_addint64(json, "severity", problem->severity);
    zbx_json_addint64(json, "oper_state", problem->oper_state);
    zbx_json_addint64(json, "cause_problem", problem->cause_problem);
    zbx_json_addint64(json, "recovery_time", problem->recovery_time);
    zbx_json_addint64(json, "correlation_id", problem->recovery_time);
}

int glb_problem_unmarshall_from_json(mem_funcs_t *memf, strpool_t *strpool, glb_problem_t *problem, struct zbx_json_parse *jp)
{   
    char name[MAX_STRING_LEN];
    zbx_json_type_t type;

    int errflag = 0;
    if ( (0 == (problem->id = glb_json_get_int_value_by_name(jp, "id", &errflag)) && errflag == 1) ||
         (0 == (problem->source = glb_json_get_int_value_by_name(jp, "source", &errflag)) && errflag == 1) || 
         (0 == (problem->objectid = glb_json_get_int_value_by_name(jp, "objectid", &errflag)) && errflag == 1)||
         (0 == (problem->acknowledged = glb_json_get_int_value_by_name(jp, "acknowledged", &errflag)) && errflag == 1) ||
         (0 == (problem->severity = glb_json_get_int_value_by_name(jp, "severity", &errflag)) && errflag == 1) ||
         (0 == (problem->oper_state = glb_json_get_int_value_by_name(jp, "oper_state", &errflag)) && errflag == 1) ||
         (0 == (problem->recovery_time = glb_json_get_int_value_by_name(jp, "recovery_time", &errflag)) && errflag == 1) ||
         (0 == (problem->correlation_id = glb_json_get_int_value_by_name(jp, "correlation_id", &errflag)) && errflag == 1)) 
    return FAIL;

    if (FAIL == zbx_json_value_by_name(jp, "name", name, MAX_STRING_LEN, &type)) 
        return FAIL;

    if (NULL != strpool) 
        problem->name = strpool_add(strpool, name);
    else 
        problem->name = zbx_strdup(NULL, name);
    return SUCCEED;
}

int glb_problem_get_create_time(glb_problem_t *problem) {
    return glb_state_id_get_timestamp(problem->id);
}