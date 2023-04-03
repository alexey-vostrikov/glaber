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
#include "../glb_conf/tags/tags.h"
#include "zbxserver.h"

static mem_funcs_t heap_memf = { .malloc_func = zbx_default_mem_malloc_func, 
              .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};

struct glb_problem_t {
    u_int64_t id;
    glb_problem_source_t source; //it looks like the only problem reason might be a trigger, not needed?
    u_int64_t objectid; //this must be changed to triggerid ? 
    const char *name;
	unsigned char acknowledged;
	unsigned char severity; 
    unsigned char suppressed;
    glb_problem_oper_state_t oper_state;
	u_int64_t cause_problem;
    u_int64_t recovery_time; 
    u_int64_t correlation_id;
    zbx_vector_uint64_t hostids;
    tags_t* tags;
};

glb_problem_t *glb_problem_create_by_trigger(mem_funcs_t *memf, strpool_t *strpool, u_int64_t problemid, calc_trigger_t *trigger) {
    if (NULL == memf)
        memf = &heap_memf;
    int i;
    
  //  char problem_name[MAX_STRING_LEN];

    if (0 == problemid && 0 == (problemid = glb_state_id_gen_new())) // no id, failed to gen a new one
        return NULL;
    
    glb_problem_t* problem = memf->malloc_func(NULL, sizeof(glb_problem_t));
    
    if (NULL == problem || NULL == trigger->event_name)
        return NULL;

    bzero(problem, sizeof(glb_problem_t));
    zbx_vector_uint64_create_ext(&problem->hostids, memf->malloc_func, memf->realloc_func, memf->free_func);    
    
    char *problem_name;
    
    if (NULL != trigger->event_name && trigger->event_name[0] != '\0')
        problem_name = zbx_strdup(NULL, trigger->event_name);
    else
        problem_name = zbx_strdup(NULL, trigger->description);
    
    LOG_INF("Problem name is %s", problem_name);

    if (FAIL == glb_macro_expand_by_trigger(trigger, &problem_name, NULL, 0))
        return NULL;

    if (NULL != strpool) 
        problem->name = strpool_add(strpool, problem_name);
    else 
        problem->name = zbx_strdup(NULL, problem_name);
 
    LOG_INF("Translated problem name '%s'", problem->name);
    
    zbx_free(problem_name);

    problem->id = problemid;
    problem->objectid = trigger->triggerid;
    problem->oper_state = GLB_PROBLEM_OPER_STATE_PROBLEM;
    problem->severity = trigger->priority;
    problem->source = GLB_PROBLEM_SOURCE_TRIGGER;
    
    zbx_vector_uint64_t *t_hostids;

    HALT_HERE("Add triggers from hosts and items either");
    
    if (SUCCEED == conf_calc_trigger_get_all_hostids(trigger, &t_hostids))
        zbx_vector_uint64_append_array(&problem->hostids, t_hostids->values, t_hostids->values_num);
    
    problem->tags = tags_create_ext(memf);
    tags_add_tags(problem->tags, trigger->tags);
   
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
    zbx_vector_uint64_destroy(&problem->hostids);

    LOG_INF("Freeing the problem");
    memf->free_func(problem);
}

// glb_problem_t * glb_problem_copy(mem_funcs_t *memf, strpool_t *strpool,  glb_problem_t *src_problem) {
//     if (NULL == memf)
//         memf = &heap_memf;
    
//     glb_problem_t* dst_problem = memf->malloc_func(NULL, sizeof(glb_problem_t));
//     bzero(dst_problem, sizeof(glb_problem_t));

//     memcpy(dst_problem, src_problem, sizeof(glb_problem_t));

//     if (NULL != strpool) 
//         dst_problem->name = strpool_add(strpool, src_problem->name);
//     else 
//         dst_problem->name = zbx_strdup(NULL, src_problem->name);
    
//     return dst_problem;
// }


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
    if ( (0 == (problem->id = glb_json_get_uint64_value_by_name(jp, "id", &errflag)) && errflag == 1) ||
         (0 == (problem->source = glb_json_get_uint64_value_by_name(jp, "source", &errflag)) && errflag == 1) || 
         (0 == (problem->objectid = glb_json_get_uint64_value_by_name(jp, "objectid", &errflag)) && errflag == 1)||
         (0 == (problem->acknowledged = glb_json_get_uint64_value_by_name(jp, "acknowledged", &errflag)) && errflag == 1) ||
         (0 == (problem->severity = glb_json_get_uint64_value_by_name(jp, "severity", &errflag)) && errflag == 1) ||
         (0 == (problem->oper_state = glb_json_get_uint64_value_by_name(jp, "oper_state", &errflag)) && errflag == 1) ||
         (0 == (problem->recovery_time = glb_json_get_uint64_value_by_name(jp, "recovery_time", &errflag)) && errflag == 1) ||
         (0 == (problem->correlation_id = glb_json_get_uint64_value_by_name(jp, "correlation_id", &errflag)) && errflag == 1)) 
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

void glb_problem_get_hostids(glb_problem_t *problem, zbx_vector_uint64_t *ids) {
    zbx_vector_uint64_append_array(ids, problem->hostids.values, problem->hostids.values_num);
}

int glb_problem_get_severity(glb_problem_t *problem) {
    return problem->severity;
}

int glb_problem_is_acknowledged(glb_problem_t *problem) {
    return problem->acknowledged;
}

u_int64_t glb_problem_get_triggerid(glb_problem_t *problem) {
    return problem->objectid;
}

glb_problem_suppress_state_t glb_problem_get_suppressed(glb_problem_t *problem) {
    return problem->suppressed;
}

void glb_problem_get_name(glb_problem_t *problem, char *name, size_t max_len) {
    zbx_strlcpy(name, problem->name, max_len);
}

int glb_problem_check_tag_name(glb_problem_t *problem, const char *pattern, unsigned char oper) {
    return tags_check_name(problem->tags, pattern, oper);
}

int glb_problem_check_tag_value(glb_problem_t *problem, tag_t *tag, unsigned char oper) {
    return tags_check_value(problem->tags, tag, oper);
}