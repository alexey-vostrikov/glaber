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
#include "glb_state.h"
#include "glb_state_problems.h"
#include "glb_state_triggers.h"
#include "glb_state_ids.h"
#include "zbx_trigger_constants.h"
#include "zbxcacheconfig.h"
#include "../glb_events_log/glb_events_log.h"
#include "load_dump.h"


#define MAX_PROBLEMS 1000000
#define MAX_PROBLEMS_PER_TRIGGER 1024

typedef struct {
    elems_hash_t *problems;

    index_uint64_t *hosts_idx;    
    index_uint64_t *trigger_idx;
    
    mem_funcs_t memf;
    strpool_t strpool;

} problems_conf_t;

static problems_conf_t *conf = NULL; 

ELEMS_CREATE(problem_new_cb) {
    elem->data = NULL;
//   
}

ELEMS_FREE(problem_free_cb) {
    glb_problem_t *problem = elem->data;
    
    if (GLB_PROBLEM_SOURCE_TRIGGER == glb_problem_get_source(problem) )
        index_uint64_del(conf->trigger_idx, glb_problem_get_objectid(problem), elem->id);

    LOG_INF("Deleting problem %ld trigger %ld", elem->id, glb_problem_get_objectid(problem));
    glb_problem_destroy(memf, &conf->strpool, problem);    
    
    elem->data = NULL;
}

int glb_state_problems_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(problems_conf_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
    conf->problems = elems_hash_init(memf, problem_new_cb, problem_free_cb );
    
    conf->hosts_idx = index_uint64_init(memf);
    conf->trigger_idx = index_uint64_init(memf);

    conf->memf = *memf;
    strpool_init(&conf->strpool,memf);
    
    return SUCCEED;
}

int glb_state_problems_destroy() {
    elems_hash_destroy(conf->problems);
    strpool_destroy(&conf->strpool);
}

ELEMS_CALLBACK(problem_create_cb) {
    glb_problem_info_t *info = data;
    glb_problem_t *problem;

    if (NULL!= elem->data)
        return FAIL;
      
    if (NULL == (problem = glb_problem_create(memf, &conf->strpool, info)))
        return FAIL;
    
    elem->data = problem;

    return SUCCEED;
}


u_int64_t   glb_state_problem_create(u_int64_t problemid, glb_problem_source_t source, u_int64_t objectid,  
                        const char *name, unsigned char severity, zbx_vector_uint64_t *hosts) {
    int i;
    glb_problem_info_t info = {.name = name, .objectid = objectid, .severity = severity, .source = source};
    
    if ( (0 == problemid && 0 == (problemid = glb_state_id_gen_new())) || //no id, failed to gen a new one
        source < 0 || source >= GLB_PROBLEM_SOURCE_COUNT ||
        NULL == name || 
        0 == objectid ||
        0 > severity || SEVERITY_COUNT <= severity ) {
            LOG_INF("Problem params check failed");
            return 0;
        }
    
    info.problemid = problemid;

    LOG_INF("Creating new PROBLEM id %lld, name %s", problemid, name);

    if (SUCCEED == elems_hash_process(conf->problems, problemid, problem_create_cb, &info, 0)) {
        LOG_INF("Problem %lld created", problemid);

        if (GLB_PROBLEM_SOURCE_TRIGGER == source)
            index_uint64_add(conf->trigger_idx, objectid, problemid);
        
        if (NULL != hosts )
            for (int i = 0; i < hosts->values_num; i++) {
                LOG_INF("Added host %lld -> problem %lld index", hosts->values[i], problemid);
                index_uint64_add(conf->hosts_idx, hosts->values[i], problemid);  
            }

        return problemid;
    }
    return 0;
}

int glb_state_problems_get_count() {
    return elems_hash_get_num(conf->problems);
}

ELEMS_CALLBACK(problem_recover_cb){
    glb_problem_t *problem = elem->data;
     
    if (GLB_PROBLEM_OPER_STATE_PROBLEM != glb_problem_get_oper_state(problem)) {
        LOG_INF("Cannot recover problem which isn't in a problem state, but %d",glb_problem_get_oper_state(problem) );
        return FAIL;
    }

    if (FAIL ==  glb_problem_set_oper_state(problem, GLB_PROBLEM_OPER_STATE_OK)) 
        return FAIL;
    
    write_event_log(glb_problem_get_objectid(problem), elem->id, GLB_EVENT_LOG_SOURCE_TRIGGER, "State change: problem is recovered");
    
    return SUCCEED;
}

int glb_state_problem_recover(u_int64_t problemid, u_int64_t userid) {
    if (0 == problemid) 
        return FAIL;
        
    return elems_hash_process(conf->problems, problemid, problem_recover_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(fetch_problem_cb) {
    glb_problem_t *new_problem = glb_problem_copy(NULL, NULL, (glb_problem_t *)elem->data);
    *(void **)data = new_problem;

    return SUCCEED;
}


static int fetch_problems_by_index(index_uint64_t *index, u_int64_t id, zbx_vector_ptr_t *problems ) {
    if (NULL == problems || NULL == index)
        return FAIL;

    zbx_vector_uint64_t problemids;
    zbx_vector_uint64_create(&problemids);

    if (FAIL == index_uint64_get(index, id, &problemids) ) {
        zbx_vector_uint64_destroy(&problemids);
        return FAIL;
    }
    
    for( int i =0 ; i < problemids.values_num; i++) {
        
        glb_problem_t *problem;

        if (SUCCEED == elems_hash_process(conf->problems, problemids.values[i], fetch_problem_cb, &problem, ELEM_FLAG_DO_NOT_CREATE)) 
            zbx_vector_ptr_append(problems, problem);
    }
    
    zbx_vector_uint64_destroy(&problemids);
    return SUCCEED;
}

int glb_state_problems_get_by_triggerid(u_int64_t triggerid, zbx_vector_ptr_t *problems) {
    return fetch_problems_by_index(conf->trigger_idx , triggerid, problems);
}

int glb_state_problems_get_by_hostid(u_int64_t hostid, zbx_vector_ptr_t *problems) {
    return fetch_problems_by_index(conf->hosts_idx , hostid, problems);
}

void glb_state_problems_clean(zbx_vector_ptr_t *problems) {
    int i;   

    for (i = 0; i < problems->values_num; i++) {
        LOG_INF("Cleaning problem ptr %p", problems->values[i] );
        glb_problem_destroy(NULL, NULL, problems->values[i]);
    }
}

void glb_state_problems_process_trigger_value(DC_TRIGGER *trigger) {
    
    glb_state_problems_housekeep();
    
    //unsigned char old_value = glb_state_trigger_get_value(triggerid);
    int problems_count;

    problems_count = glb_state_problems_get_count_by_trigger(trigger->triggerid);

    if (TRIGGER_TYPE_MULTIPLE_TRUE != trigger->type && problems_count > 1) {
        
        LOG_INF("Trigger %ld has more then 1 problem open (%d) and no miltiple problem generation flag is set, closing all except one",
                    trigger->triggerid, problems_count);

        DEBUG_TRIGGER(trigger->triggerid, "Trigger has more then 1 problem open (%d) and no miltiple problem generation flag is set, closing all except one",
                    problems_count);

        glb_state_problems_recover_by_trigger(trigger->triggerid, 1);
        problems_count = 1; //glb_state_problems_get_count_by_trigger(trigger->triggerid);
    }
    
    switch (trigger->new_value) {
        case TRIGGER_VALUE_OK: 
            if (TRIGGER_RECOVERY_MODE_NONE != trigger->recovery_mode && problems_count > 0 )
                glb_state_problems_recover_by_trigger(trigger->triggerid, 0);
            break;

        case TRIGGER_VALUE_PROBLEM:
            if (TRIGGER_TYPE_MULTIPLE_TRUE == trigger->recovery_mode || 0 == problems_count) {
                
                char *problem_name;
                if  (NULL != trigger->event_name && trigger->event_name[0] != '\0' ) 
                    problem_name = trigger->event_name;
                else 
                    problem_name = trigger->description;
                
                LOG_INF("Creating PROBLEM for trigger %ld: %s", trigger->triggerid, problem_name);
                glb_state_problem_create(0, GLB_PROBLEM_SOURCE_TRIGGER, trigger->triggerid, problem_name, 
                        trigger->priority, &trigger->hostids);
            }
            break;
    }
}

int     glb_state_problems_get_count_by_trigger(u_int64_t triggerid) {
    return index_uint64_get_count_by_key(conf->trigger_idx, triggerid);
}

int glb_state_problems_recover_by_trigger(u_int64_t triggerid, int leave_unrecovered) {
    zbx_vector_uint64_t problemids;
    zbx_vector_uint64_create(&problemids);   
    
    if (SUCCEED == index_uint64_get(conf->trigger_idx, triggerid, &problemids) ) 
        for( int i = leave_unrecovered; i < problemids.values_num; i++) 
            elems_hash_process(conf->problems, problemids.values[i], problem_recover_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
    
    zbx_vector_uint64_destroy(&problemids);
    return SUCCEED;

}

ELEMS_CALLBACK(remove_outdated_problems_cb) {
    glb_problem_t *problem = elem->data;
    switch (  glb_problem_get_oper_state(problem)) {
        case GLB_PROBLEM_OPER_STATE_PROBLEM:
        case GLB_PROBLEM_OPER_STATE_SUPPRESSED:
            if ( glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_PROBLEM_STATE < time(NULL))
                elem->flags |= ELEM_FLAG_DELETE;
            break;
        case GLB_PROBLEM_OPER_STATE_OK:
            if ( glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_OK_STATE < time(NULL))
                elem->flags |= ELEM_FLAG_DELETE;
            break;
    }

}

void glb_state_problems_housekeep() {
    RUN_ONCE_IN(10);
    
    zbx_vector_uint64_t triggerids;
    zbx_vector_uint64_create(&triggerids);

    LOG_INF("Will house keep problems: total problems is %d", elems_hash_get_num(conf->problems));
    elems_hash_iterate(conf->problems, remove_outdated_problems_cb, &triggerids, ELEMS_HASH_READ_ONLY);

    LOG_INF("Removing outdated problems from the triggers index %d:", index_uint64_get_keys_num(conf->trigger_idx));
    index_uint64_sync_objects(conf->trigger_idx, conf->problems);

    LOG_INF("Removing outdated problens from the hosts index %d", index_uint64_get_keys_num(conf->hosts_idx));
    index_uint64_sync_objects(conf->hosts_idx, conf->problems);
}


DUMPER_TO_JSON(problems_to_json_cb) {
    glb_problem_t *problem = data;
    glb_problem_marshall_to_json(problem, json);

}

int glb_state_problems_dump() {
    state_dump_objects(conf->problems, "problems", problems_to_json_cb );
   
	return SUCCEED;
}

int glb_state_problems_get_by_hostids_json(zbx_vector_uint64_t *ids, struct zbx_json *response_json){

}

int glb_state_problems_get_by_triggerids_json(zbx_vector_uint64_t *ids, struct zbx_json *response_json){
    
}

int glb_state_problems_get_all_json(struct zbx_json *response_json){
    
}

