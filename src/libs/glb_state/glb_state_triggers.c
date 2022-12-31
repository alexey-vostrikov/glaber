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
#include "glb_state_triggers.h"

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
    if ( (TRIGGER_VALUE_NONE != info->value) && 
         (TRIGGER_VALUE_OK != info->value) &&
         (TRIGGER_VALUE_PROBLEM != info->value) &&
         (TRIGGER_VALUE_UNKNOWN != info->value))
        return FAIL;   
    if ((NULL != info->error) && (TRIGGER_VALUE_UNKNOWN != info->value))
        return FAIL;

    return SUCCEED;
}

int glb_state_trigger_set_info(state_trigger_info_t *info){
    
    if (FAIL == validate_info(info))
        return FAIL;

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
/******************************************************************************
* mass trigger change apply - used in processing                              *
 ******************************************************************************/
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

		DEBUG_TRIGGER(diff->triggerid,"Saving new trigger state to glb_state cache");
        bzero(&info, sizeof(info));
		
		info.id = diff->triggerid;
		info.value = diff->value;
		info.lastcalc = diff->lastchange;   

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
			info.error = diff->error;		

		if (FAIL == glb_state_trigger_set_info(&info))
			HALT_HERE("Trigger state set logic error id= %ld, value = %d, error = %s", info.id, info.value, info.error);
	}
}