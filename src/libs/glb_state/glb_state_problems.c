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
#include "../glb_actions/glb_actions.h"
#include "../glb_conf/tags/tags.h"
#include "load_dump.h"
#include "../../zabbix_server/glb_events_processor/glb_events_processor.h"
#include "glbstr.h"


#define MAX_PROBLEMS 1000000
#define MAX_PROBLEMS_PER_TRIGGER 1024

typedef struct
{
    elems_hash_t *problems;

    index_uint64_t *hosts_idx;
    index_uint64_t *trigger_idx;

    mem_funcs_t memf;
    strpool_t strpool;

} problems_conf_t;

static problems_conf_t *conf = NULL;

ELEMS_CREATE(problem_new_cb)
{
    elem->data = NULL;
    //
}

ELEMS_FREE(problem_free_cb)
{
    glb_problem_t *problem = elem->data;

    if (GLB_PROBLEM_SOURCE_TRIGGER == glb_problem_get_source(problem))
        index_uint64_del(conf->trigger_idx, glb_problem_get_objectid(problem), elem->id);
    DEBUG_TRIGGER(glb_problem_get_objectid(problem), "Deleting the problem %ld", elem->id);

    glb_problem_destroy(memf, &conf->strpool, problem);
    elem->data = NULL;
}

int glb_state_problems_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(problems_conf_t))))
    {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };

    conf->problems = elems_hash_init(memf, problem_new_cb, problem_free_cb);

    conf->hosts_idx = index_uint64_init(memf);
    conf->trigger_idx = index_uint64_init(memf);

    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);

    return SUCCEED;
}

int glb_state_problems_destroy()
{
    elems_hash_destroy(conf->problems);
    strpool_destroy(&conf->strpool);
}

ELEMS_CALLBACK(problem_save_cb)
{
    glb_problem_t *problem = data;

    if (NULL != elem->data)
        return FAIL;

    elem->data = problem;

    return SUCCEED;
}

u_int64_t glb_state_problems_create_by_trigger(calc_trigger_t *trigger)
{
    int i;
    glb_problem_t *problem;
    
    DEBUG_TRIGGER(trigger->triggerid, "Creating new PROBLEM for the trigger");
    
    if (NULL == (problem = glb_problem_create_by_trigger(&conf->memf, &conf->strpool, 0, trigger)))
        return 0;

    u_int64_t problemid = glb_problem_get_id(problem);

    DEBUG_TRIGGER(trigger->triggerid, "PROBLEM %ld created, adding to the problems table", problemid);

    if (SUCCEED == elems_hash_process(conf->problems, problemid, problem_save_cb, problem, 0))
    {
        index_uint64_add(conf->trigger_idx, trigger->triggerid, problemid);
        
        zbx_vector_uint64_t *hostids;
        
        if (SUCCEED == conf_calc_trigger_get_all_hostids(trigger, &hostids))
            for ( i = 0; i < hostids->values_num; i++)
                index_uint64_add(conf->hosts_idx, hostids->values[i], problemid);

        //glb_actions_process_new_problem(problemid);
        
        glb_event_processing_send_notify(problemid, EVENT_SOURCE_PROBLEM, EVENTS_TYPE_NEW);

        return problemid;
    } 
    
    DEBUG_TRIGGER(trigger->triggerid, "Failed to add problem to problems table");
    glb_problem_destroy(&conf->memf, &conf->strpool, problem);

    return 0;
}

int glb_state_problems_get_count()
{
    return elems_hash_get_num(conf->problems);
}

ELEMS_CALLBACK(problem_recover_cb)
{
    glb_problem_t *problem = elem->data;

    if (GLB_PROBLEM_OPER_STATE_PROBLEM != glb_problem_get_oper_state(problem))
    {
        LOG_INF("Cannot recover problem which isn't in a problem state, but %d", glb_problem_get_oper_state(problem));
        return FAIL;
    }

    if (FAIL == glb_problem_set_oper_state(problem, GLB_PROBLEM_OPER_STATE_OK))
        return FAIL;
    DEBUG_TRIGGER(glb_problem_get_objectid(problem), "Problem %ld is recovered", elem->id);
    write_event_log(glb_problem_get_objectid(problem), elem->id, GLB_EVENT_LOG_SOURCE_TRIGGER, "State change: problem is recovered");

    return SUCCEED;
}

int glb_state_problem_recover(u_int64_t problemid, u_int64_t userid)
{
    if (0 == problemid)
        return FAIL;

    if (SUCCEED == elems_hash_process(conf->problems, problemid, problem_recover_cb, NULL, ELEM_FLAG_DO_NOT_CREATE)) {
        glb_event_processing_send_notify(problemid, EVENT_SOURCE_PROBLEM, EVENTS_TYPE_RECOVER);
        return SUCCEED;
    }
    
    return FAIL;
}

// ELEMS_CALLBACK(fetch_problem_cb)
// {
//     glb_problem_t *new_problem = glb_problem_copy(NULL, NULL, (glb_problem_t *)elem->data);
//     *(void **)data = new_problem;

//     return SUCCEED;
// }

// static int fetch_problems_by_index(index_uint64_t *index, u_int64_t id, zbx_vector_ptr_t *problems)
// {
//     if (NULL == problems || NULL == index)
//         return FAIL;

//     zbx_vector_uint64_t problemids;
//     zbx_vector_uint64_create(&problemids);

//     if (FAIL == index_uint64_get(index, id, &problemids))
//     {
//         zbx_vector_uint64_destroy(&problemids);
//         return FAIL;
//     }

//     for (int i = 0; i < problemids.values_num; i++)
//     {

//         glb_problem_t *problem;

//         if (SUCCEED == elems_hash_process(conf->problems, problemids.values[i], fetch_problem_cb, &problem, ELEM_FLAG_DO_NOT_CREATE))
//             zbx_vector_ptr_append(problems, problem);
//     }

//     zbx_vector_uint64_destroy(&problemids);
//     return SUCCEED;
// }

// int glb_state_problems_get_by_triggerid(u_int64_t triggerid, zbx_vector_ptr_t *problems)
// {
//     return fetch_problems_by_index(conf->trigger_idx, triggerid, problems);
// }

// int glb_state_problems_get_by_hostid(u_int64_t hostid, zbx_vector_ptr_t *problems)
// {
//     return fetch_problems_by_index(conf->hosts_idx, hostid, problems);
// }

ELEMS_CALLBACK(problem_get_hostids) {
    zbx_vector_uint64_t *ids = data;
    glb_problem_t *problem = elem->data;
    glb_problem_get_hostids(problem, ids);
    return SUCCEED;
}

int glb_state_problems_get_hostids(u_int64_t problemid, zbx_vector_uint64_t *ids)
{
    return  elems_hash_process(conf->problems, problemid, problem_get_hostids, ids, ELEM_FLAG_DO_NOT_CREATE);
}


void glb_state_problems_clean(zbx_vector_ptr_t *problems)
{
    int i;

    for (i = 0; i < problems->values_num; i++)
    {
        LOG_INF("Cleaning problem ptr %p", problems->values[i]);
        glb_problem_destroy(NULL, NULL, problems->values[i]);
    }
}

/* func to be called on the trigger recalc event*/
/*note: sets the severity of the problem with the highest severity created*/
void  glb_state_problems_process_trigger_value(calc_trigger_t *trigger, int *severity)
{
    int problems_count, total_count;
    glb_state_problems_housekeep();

    problems_count = glb_state_problems_get_count_by_trigger_unresolved(trigger->triggerid);
    total_count = glb_state_problems_get_count_by_trigger(trigger->triggerid);

    DEBUG_TRIGGER(trigger->triggerid,"There are %d unresolved problems for the trigger, total %d triggers", problems_count, total_count);

    if (TRIGGER_TYPE_MULTIPLE_TRUE != trigger->type && problems_count > 1)
    {

        DEBUG_TRIGGER(trigger->triggerid, "Trigger has more then 1 problem open (%d) and no miltiple problem generation flag is set, closing all except one",
                      problems_count);

        glb_state_problems_recover_by_trigger(trigger->triggerid, 1);
        problems_count = 1; // glb_state_problems_get_count_by_trigger(trigger->triggerid);
    }

    switch (trigger->new_value)
    {
    case TRIGGER_VALUE_OK:
        DEBUG_TRIGGER(trigger->triggerid, "Handling OK value: deleting if any problems exists (%d)", problems_count);
        if (TRIGGER_RECOVERY_MODE_NONE != trigger->recovery_mode && problems_count > 0)
            glb_state_problems_recover_by_trigger(trigger->triggerid, 0);
        break;

    case TRIGGER_VALUE_PROBLEM:
        DEBUG_TRIGGER(trigger->triggerid, "Handling PROBLEM value, now (%d) problems exist, trigger type is %d", problems_count, trigger->type);
        if (TRIGGER_TYPE_MULTIPLE_TRUE == trigger->type || 0 == problems_count)
        {
            LOG_INF("Creating PROBLEM for trigger %ld", trigger->triggerid);
            glb_state_problems_create_by_trigger(trigger);
        }
        else
            DEBUG_TRIGGER(trigger->triggerid, "Handling PROBLEM value:  no need to create a new PROBLEM, already exists");
        break;
    }
}

int glb_state_problems_get_count_by_trigger(u_int64_t triggerid)
{
    int ret;

    if (FAIL == (ret = index_uint64_get_count_by_key(conf->trigger_idx, triggerid)))
        return 0;

    return ret;
}

ELEMS_CALLBACK(count_problems_by_value_cb)
{
    int *count = data;
    glb_problem_t *problem = elem->data;
    
    DEBUG_TRIGGER(glb_problem_get_objectid(problem),"Accounting problem %lld, oper_state %d", elem->id, 
            glb_problem_get_oper_state(problem));
    
    if (GLB_PROBLEM_OPER_STATE_OK != glb_problem_get_oper_state(problem)) 
        *count += 1;
}

int glb_state_problems_get_count_by_trigger_unresolved(u_int64_t triggerid)
{
    zbx_vector_uint64_t problemids;
    zbx_vector_uint64_create(&problemids);
    
    int count = 0;

    index_uint64_get(conf->trigger_idx, triggerid, &problemids);
    DEBUG_TRIGGER(triggerid,"Got %d problems for the trigger", problemids.values_num)
    elems_hash_iterate_by_vector(conf->problems, &problemids, count_problems_by_value_cb, &count, ELEM_FLAG_ITER_WRLOCK);

    zbx_vector_uint64_destroy(&problemids);

    return count;
}

int glb_state_problems_recover_by_trigger(u_int64_t triggerid, int leave_unrecovered)
{
    zbx_vector_uint64_t problemids;
    zbx_vector_uint64_create(&problemids);

    DEBUG_TRIGGER(triggerid, "Recovering problems for the trigger except %d", leave_unrecovered);
    if (SUCCEED == index_uint64_get(conf->trigger_idx, triggerid, &problemids))
        for (int i = leave_unrecovered; i < problemids.values_num; i++)
        {

            DEBUG_TRIGGER(triggerid, "Recovering problem %ld", problemids.values[i]);
            elems_hash_process(conf->problems, problemids.values[i], problem_recover_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
        }

    zbx_vector_uint64_destroy(&problemids);
    return SUCCEED;
}

ELEMS_CALLBACK(remove_outdated_problems_cb)
{
    glb_problem_t *problem = elem->data;
    switch (glb_problem_get_oper_state(problem))
    {
    case GLB_PROBLEM_OPER_STATE_PROBLEM:
    case GLB_PROBLEM_OPER_STATE_SUPPRESSED:
        if (glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_PROBLEM_STATE < time(NULL))
        {
            elem->flags |= ELEM_FLAG_DELETE;
            DEBUG_TRIGGER(glb_problem_get_objectid(problem), "Marking problem %lld  PROBLEM/UNKNOWN for deletion, it's %d secodns old: ",
                          elem->id, time(NULL) - (glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_PROBLEM_STATE));
        }
        break;

    case GLB_PROBLEM_OPER_STATE_OK:
        if (glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_OK_STATE < time(NULL))
        {
            DEBUG_TRIGGER(glb_problem_get_objectid(problem), "Marking problem %lld in OK state for deletion, it's %d secodns old: ",
                          elem->id, time(NULL) - (glb_problem_get_create_time(problem) + GLB_PROBLEM_TTL_OK_STATE));
            elem->flags |= ELEM_FLAG_DELETE;
        }
        break;
    }
}

void glb_state_problems_housekeep()
{
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

DUMPER_TO_JSON(problems_to_json_cb)
{
    glb_problem_t *problem = data;
    glb_problem_marshall_to_json(problem, json);
}

int glb_state_problems_dump()
{
    state_dump_objects(conf->problems, "problems", problems_to_json_cb);

    return SUCCEED;
}

ELEMS_CALLBACK(problem_to_json_cb)
{
    glb_problem_t *problem = elem->data;
    struct zbx_json *json = data;
    zbx_json_addobject(json, NULL);
    glb_problem_marshall_to_json(problem, json);
    zbx_json_close(json);
}

static void problems_fetch_json_by_index(index_uint64_t *index, zbx_vector_uint64_t *ids, struct zbx_json *json)
{
    zbx_vector_uint64_t problemids;
    zbx_vector_uint64_create(&problemids);

    index_uint64_get_keys_values(index, ids, &problemids);
    zbx_vector_uint64_uniq(&problemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    elems_hash_iterate_by_vector(conf->problems, &problemids, problem_to_json_cb, json, ELEM_FLAG_ITER_WRLOCK);

    zbx_vector_uint64_destroy(&problemids);
}

int glb_state_problems_get_by_hostids_json(zbx_vector_uint64_t *ids, struct zbx_json *json)
{
    problems_fetch_json_by_index(conf->hosts_idx, ids, json);
}

int glb_state_problems_get_by_triggerids_json(zbx_vector_uint64_t *ids, struct zbx_json *json)
{
    problems_fetch_json_by_index(conf->trigger_idx, ids, json);
}

int glb_state_problems_get_all_json(struct zbx_json *json)
{
    zbx_json_addarray(json, ZBX_PROTO_TAG_DATA);
    elems_hash_iterate(conf->problems, problem_to_json_cb, json, ELEM_FLAG_ITER_WRLOCK);
    zbx_json_close(json);
}

ELEMS_CALLBACK(get_severity_cb) {
    *(int*)data = glb_problem_get_severity((glb_problem_t*)elem->data);
    return SUCCEED;
}

int glb_state_problems_get_severity(u_int64_t problemid, int *severity) {
    return elems_hash_process(conf->problems, problemid, get_severity_cb, severity, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(get_ack_cb) {
    *(int*)data = glb_problem_is_acknowledged((glb_problem_t*)elem->data);
    return SUCCEED;
}

int glb_state_problems_is_acknowledged(u_int64_t problemid, int *is_ack) {
    return elems_hash_process(conf->problems, problemid, get_ack_cb, is_ack, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(get_triggerid_cb) {
    *(int*)data = glb_problem_get_triggerid((glb_problem_t*)elem->data);
    return SUCCEED;
}

int glb_state_problems_get_triggerid(u_int64_t problemid, u_int64_t *triggerid) {
    return elems_hash_process(conf->problems, problemid, get_triggerid_cb, triggerid, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(get_suppressed_cb) {
    *(glb_problem_suppress_state_t*)data = glb_problem_get_suppressed((glb_problem_t*)elem->data);
    return SUCCEED;
}

int glb_state_problems_get_suppressed(u_int64_t problemid, glb_problem_suppress_state_t *is_suppressed) {
    return elems_hash_process(conf->problems, problemid, get_suppressed_cb, is_suppressed, ELEM_FLAG_DO_NOT_CREATE);
}

int glb_state_problems_get_clock(u_int64_t problemid, int *clock) {
    
    if (FAIL == elems_hash_id_exists(conf->problems, problemid)) 
        return FAIL;
    
    *clock = glb_state_id_get_timestamp(problemid);
    return SUCCEED;
}


ELEMS_CALLBACK(get_problem_name_cb) {
    strlen_t *name_s = data;
    glb_problem_get_name((glb_problem_t *)elem->data, name_s->str, name_s->len);   
}

int glb_state_problems_get_name(u_int64_t problemid, char *name, size_t len) {
    strlen_t name_s ={.str = name, .len = len };
 
    return elems_hash_process(conf->problems, problemid, get_problem_name_cb, &name_s, ELEM_FLAG_DO_NOT_CREATE);
}

typedef struct {
    tag_t* tag;
    int* result;
    unsigned char oper;
} tags_request_t;

ELEMS_CALLBACK(check_tag_name_cb) {
    
    tags_request_t *t_req = data;
    *t_req->result = glb_problem_check_tag_name((glb_problem_t *)elem->data, t_req->tag->tag, t_req->oper);
    
    return SUCCEED;
}

int glb_state_problems_check_tag_name(u_int64_t problemid, tag_t *tag, unsigned char operation, int *result) {
    tags_request_t tags_request = {.oper = operation, .result = result, .tag = tag};
    return elems_hash_process(conf->problems, problemid, check_tag_name_cb, &tags_request, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(check_tag_value_cb) {
   
    tags_request_t *t_req = data;
    *t_req->result = glb_problem_check_tag_value((glb_problem_t *)elem->data, t_req->tag, t_req->oper);
    
    return SUCCEED;
}

int glb_state_problems_check_tag_value(u_int64_t problemid, tag_t *tag, unsigned char operation, int *result) {
    tags_request_t tags_request = {.oper = operation, .result = result, .tag = tag};
    return elems_hash_process(conf->problems, problemid, check_tag_value_cb, &tags_request, ELEM_FLAG_DO_NOT_CREATE);
}