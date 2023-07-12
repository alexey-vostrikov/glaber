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

#include "zbxcommon.h"
#include "zbxalgo.h"
#include "glb_state.h"
#include "zbx_trigger_constants.h"
#include "zbx_item_constants.h"
#include "glb_state_triggers.h"
#include "load_dump.h"
#include "zbxdbhigh.h"
#include "zbxjson.h"
extern int zbx_log_level;

typedef struct {
    elems_hash_t *triggers;
    strpool_t strpool;
    mem_funcs_t memf;

} triggers_state_t;

typedef struct {
    int			lastchange;
    int         lastcalc;
    unsigned char value;
    const char *error;
} state_trigger_t;

static triggers_state_t *state = NULL; 

ELEMS_CREATE(trigger_create_cb) {
    elem->data = memf->malloc_func(NULL, sizeof(state_trigger_t));
    state_trigger_t* trigger = elem->data;
    bzero(trigger, sizeof(state_trigger_t));

    trigger->value = TRIGGER_VALUE_NONE;
}

ELEMS_FREE(trigger_free_cb) {
    state_trigger_t* trigger = elem->data;
    if (NULL != trigger->error)
        strpool_free(&state->strpool, trigger->error);

    memf->free_func(trigger);
    elem->data = NULL;
}

ELEMS_CALLBACK(get_trigger_info) {
    state_trigger_t *trigger = elem->data;
    state_trigger_info_t *info = data;
   
    info->value = trigger->value;
    info->lastcalc = trigger->lastcalc;
    info->lastchange = trigger->lastchange;
    
    if ((info->flags & STATE_GET_TRIGGER_ERROR) && NULL != trigger->error && TRIGGER_VALUE_UNKNOWN == trigger->value)
        info->error = zbx_strdup(NULL, trigger->error);
    else 
        info->error = NULL;
    return SUCCEED;
}

ELEMS_CALLBACK(set_trigger_info) {
    state_trigger_t *trigger = elem->data;
    state_trigger_info_t *info = data;

    if (trigger->value != info->value) {
        trigger->value = info->value;
        trigger->lastchange = time(NULL);
    }

    trigger->lastcalc = info->lastcalc;
    
    if (NULL != info->error) {
        if (NULL != trigger->error)
            strpool_free(&state->strpool, trigger->error);
        trigger->error = strpool_add(&state->strpool, info->error);
    }

    if ( (TRIGGER_VALUE_OK == trigger->value || TRIGGER_VALUE_PROBLEM == trigger->value) &&
            NULL != trigger->error ) {
        strpool_free(&state->strpool, trigger->error);
        trigger->error = NULL;
    }

    DEBUG_TRIGGER(elem->id, "Set new trigger state: value: %d, lastcalc: %d, lastchange: %d, error: %s",
          trigger->value, trigger->lastcalc, trigger->lastchange, trigger->error);

    return SUCCEED;
}

int glb_state_trigger_get_info(state_trigger_info_t *info){
    if (NULL == info || 0 == info->id)
        return FAIL;
  
    return elems_hash_process(state->triggers, info->id, get_trigger_info, info, ELEM_FLAG_DO_NOT_CREATE);
}

static int validate_info(state_trigger_info_t *info){
    if (NULL == info || 0 == info->id || info->lastcalc == 0) 
        return FAIL;
  
    if ( //(TRIGGER_VALUE_NONE != info->value) &&  this shouln't ever be stored in the state
         (TRIGGER_VALUE_OK != info->value) &&
         (TRIGGER_VALUE_PROBLEM != info->value) &&
         (TRIGGER_VALUE_UNKNOWN != info->value))
        return FAIL;   
    if ((NULL != info->error) && (TRIGGER_VALUE_UNKNOWN != info->value))
        return FAIL;

    return SUCCEED;
}

int glb_state_trigger_set_info(state_trigger_info_t *info){
    
    if (FAIL == validate_info(info)) {
        if (NULL != info)
            DEBUG_TRIGGER(info->id, "Validation of the new trigger state has been failed");
        return FAIL;
    }
    return elems_hash_process(state->triggers, info->id, set_trigger_info, info, 0);
}
    
int glb_state_triggers_init(mem_funcs_t *memf)
{
    if (NULL == (state = memf->malloc_func(NULL, sizeof(triggers_state_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
    state->triggers = elems_hash_init(memf, trigger_create_cb, trigger_free_cb );
    state->memf = *memf;
    strpool_init(&state->strpool, memf);

    return SUCCEED;
}

int glb_state_triggers_destroy() {
    elems_hash_destroy(state->triggers);
    strpool_destroy(&state->strpool);
}

ELEMS_CALLBACK(get_trigger_value) {
    state_trigger_t *trigger = elem->data;
    return trigger->value;
}

unsigned char glb_state_trigger_get_value(u_int64_t id) {
    if ( 0 == id)
        return TRIGGER_VALUE_NONE;

    int ret = elems_hash_process(state->triggers,id, get_trigger_value, NULL, ELEM_FLAG_DO_NOT_CREATE);
    
    if (FAIL == ret)
        return TRIGGER_VALUE_NONE;
    
    return ret;
}

unsigned char glb_state_trigger_get_value_lastchange(u_int64_t id, unsigned char *value, int *lastchange) {
    state_trigger_info_t info = {0};
    if ( 0 == id)
        return FAIL;

    if (FAIL==elems_hash_process(state->triggers,id, get_trigger_info, &info, ELEM_FLAG_DO_NOT_CREATE))
        return FAIL;

    *value = info.value;
    *lastchange = info.lastchange;

    return SUCCEED;
}

void glb_state_trigger_set_value(u_int64_t id, unsigned char value, int lastcalc) {
    state_trigger_info_t info = {0};
    
    info.id = id;
    info.value = value;
    if (0 == lastcalc) 
        info.lastcalc = time(NULL);
    else 
        info.lastcalc = lastcalc;

    if (FAIL == glb_state_trigger_set_info(&info)) {
        HALT_HERE("Internal programming bug has happened");
    }

}

void glb_state_triggers_apply_diffs(zbx_vector_ptr_t *trigger_diff)
{
	int i;
	zbx_trigger_diff_t *diff;
    state_trigger_info_t info;
	
    if (0 == trigger_diff->values_num)
		return;

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (zbx_trigger_diff_t *)trigger_diff->values[i];
        
        if (ZBX_FLAGS_TRIGGER_DIFF_UNSET == diff->flags)
            continue;
        
        bzero(&info, sizeof(info));
		
		info.id = diff->triggerid;
		info.value = diff->value;
		info.lastcalc = diff->lastchange;   

        DEBUG_TRIGGER(diff->triggerid,"Saving trigger state to state cache: value %d, lastcalc: %d",
            info.value, info.lastcalc);

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
			info.error = diff->error;		

		if (FAIL == glb_state_trigger_set_info(&info))
			THIS_SHOULD_NEVER_HAPPEN;
	}
}

DUMPER_TO_JSON(trigger_to_json)
{
    state_trigger_t *trigger = data;

    zbx_json_addint64(json, "id", id);
    zbx_json_addint64(json, "value", trigger->value);
    zbx_json_addint64(json, "lastchange", trigger->lastchange);
    zbx_json_addint64(json, "lastcalc", trigger->lastcalc);
    
    if (NULL != trigger->error)
        zbx_json_addstring(json, "error", trigger->error, ZBX_JSON_TYPE_STRING);
    else 
        zbx_json_addstring(json, "error", "", ZBX_JSON_TYPE_STRING);

    DEBUG_TRIGGER(id, "Added trapper into json: id: %lld, value: %d, lastchange: %d, lastcalc: %d, error: %s",
          id, trigger->value, trigger->lastchange, trigger->lastcalc, trigger->error);

    return 1; //returns number of objects added
}

int glb_state_triggers_dump() {
	state_dump_objects(state->triggers, "triggers", trigger_to_json);
	return SUCCEED;
}

ELEMS_CALLBACK(check_outdated_triggers) {
    state_trigger_t *trigger = elem->data;
    zbx_vector_uint64_t *ids = data;
    
    if (trigger->lastcalc < time(NULL) - TRIGGER_STATE_TTL) 
        zbx_vector_uint64_append(ids, elem->id);
}

void glb_state_triggers_housekeep(int frequency) {
    RUN_ONCE_IN(frequency); 

    zbx_vector_uint64_t remove_ids;
    zbx_vector_uint64_create(&remove_ids);
    
    elems_hash_iterate(state->triggers, check_outdated_triggers, &remove_ids, ELEMS_HASH_READ_ONLY);
    elems_hash_mass_delete(state->triggers, &remove_ids);

    zbx_vector_uint64_destroy(&remove_ids);
}

DUMPER_FROM_JSON(unmarshall_trigger_cb) {
    state_trigger_t *trigger = elem->data;
    char buff[MAX_STRING_LEN];
     zbx_json_type_t type;

    int errflag = 0;
    
    trigger->value = glb_json_get_int_value_by_name(jp, "value", &errflag);
    if (1 == errflag) 
        trigger->value = TRIGGER_VALUE_UNKNOWN;
        
    trigger->lastcalc = glb_json_get_int_value_by_name(jp, "lastcalc", &errflag);
    if (1 == errflag) 
        trigger->lastcalc = 0;

    trigger->lastchange = glb_json_get_int_value_by_name(jp, "lastchange", &errflag);
    if (1 == errflag) 
        trigger->lastchange = 0;

    if (NULL != trigger->error) {
            strpool_free(&state->strpool, trigger->error);
            trigger->error = NULL;
    }

    if (SUCCEED == zbx_json_value_by_name(jp, "error", buff, MAX_STRING_LEN, &type))
        trigger->error = strpool_add(&state->strpool, buff);
    
    DEBUG_TRIGGER(elem->id, "Loaded trigger %ld data: value %d, lastcalc %d, lastchange %d, error:%s",
            elem->id, trigger->value, trigger->lastcalc, trigger->lastchange, trigger->error);
    return 1;
}

int glb_state_triggers_load() {
    state_load_objects(state->triggers,"triggers","id", unmarshall_trigger_cb);
    return SUCCEED;
}

ELEMS_CALLBACK(get_state_json) {
    zbx_json_addobject((struct zbx_json *)data, NULL); 
    trigger_to_json(elem->id, elem->data, (struct zbx_json *)data);
    zbx_json_close((struct zbx_json *)data);
}

int json_add_nonexsting_trigger(u_int64_t id, struct zbx_json *json) {
    zbx_json_type_t type = ZBX_JSON_TYPE_STRING;

    zbx_json_addobject(json, NULL); 
    zbx_json_addint64(json, "id", id);
    zbx_json_addint64(json, "value", TRIGGER_VALUE_UNKNOWN);
    zbx_json_addint64(json, "lastchange", 0);
    zbx_json_addint64(json, "lastcalc", 0 );
    zbx_json_addstring(json, "error", "trigger hasn't been calculated since the server startup", ZBX_JSON_TYPE_STRING);
    zbx_json_close(json);
}

int glb_state_triggers_get_state_json(zbx_vector_uint64_t *ids, struct zbx_json *json) 
{
    int i;
   
    zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    
    for (i=0; i < ids->values_num; i++) {
        
        if (FAIL == elems_hash_process(state->triggers, ids->values[i], get_state_json, json, ELEM_FLAG_DO_NOT_CREATE))
          json_add_nonexsting_trigger( ids->values[i], json);
    }

    zbx_json_close(json); 
}
