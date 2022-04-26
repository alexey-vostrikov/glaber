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

#include "common.h"

#include "zbxalgo.h"
#include "state.h"
#include "glb_lock.h"
#include "state_load_dump.h"
#include "state_triggers.h"
#include "../../zabbix_server/events.h"
#include "../glb_conf/conf_triggers.h"

typedef struct {
    elems_hash_t *triggers;
    mem_funcs_t memf;
    strpool_t strpool;
} conf_t;

struct trigger_state_t {
 //   unsigned char  state; //TRIGGER_STATE* if UNKNOWN - the errorstr is expected to be set
    unsigned char status; //enabled or disabled
    unsigned char value;
    int lastchange;
 //   int timer_revision;
    int revision;
    const char *errorstr; 
    unsigned char	functional;	 /* see TRIGGER_FUNCTIONAL_* defines  expcted to be set according to the item's state */
//    int problem_count;
} ;


//typedef struct {
//    trigger_state_t state;
//} trigger_t;

static conf_t *conf;

ELEMS_CREATE(trigger_create_cb){
    trigger_state_t *state = elem->data;
    if (NULL == ( state = memf->malloc_func(NULL, sizeof(trigger_state_t)))) 
        return FAIL;
    
    elem->data = state;
    bzero(elem->data, sizeof(trigger_state_t));
    
    state->errorstr = strpool_add(&conf->strpool,"Trigger hasn't been calculated since the server start yet");  
//	state->state = TRIGGER_STATE_UNKNOWN;
    state->lastchange= 0; //atoi(row[8]);
			//trigger->locked = 0;
	//state->timer_revision = 0;
    state->value = TRIGGER_VALUE_UNKNOWN;

    return SUCCEED;
}

ELEMS_FREE(trigger_free_cb) {
    trigger_state_t *tr_state = elem->data;
    
    strpool_free(&conf->strpool, tr_state->errorstr);
    
    conf->memf.free_func(elem->data);
    elem->data = NULL;

    return SUCCEED;
}

int glb_state_triggers_init(mem_funcs_t *memf)
{
  //  LOG_INF("Doing triggers state init");
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(conf_t))) ||
        NULL == (conf->triggers = elems_hash_init(memf, trigger_create_cb, trigger_free_cb)) 
      ) {
        LOG_WRN("Cannot allocate memory for triggers state struct");
        return FAIL;
    };
    
    conf->memf = *memf;

    strpool_init(&conf->strpool, memf);
    return SUCCEED;
}

ELEMS_CALLBACK(set_status_cb){
    trigger_state_t *tr_state = elem->data;
    tr_state->status = *(unsigned char *)data;
    return SUCCEED;
}

int glb_state_trigger_set_status(u_int64_t triggerid, unsigned char status) {
  //  LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_status_cb, &status, 0); 
}

ELEMS_CALLBACK(get_value_cb){
    trigger_state_t *tr_state = elem->data;
    unsigned char* value = data;
    
    *value = tr_state->value;
    
    return SUCCEED;
}

unsigned char glb_state_trigger_get_value(u_int64_t triggerid) {
    unsigned char value = TRIGGER_VALUE_UNKNOWN;
 //   LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    elems_hash_process(conf->triggers, triggerid, get_value_cb, &value, ELEM_FLAG_DO_NOT_CREATE); 

    return value;
}

ELEMS_CALLBACK(set_value_cb){
    trigger_state_t *tr_state = elem->data;
    tr_state->value = *(char *)data;
    tr_state->lastchange = time(NULL);
//    LOG_INF("Trigger %ld value has been set to %d", elem->id, tr_state->value);
    return SUCCEED;
}

int glb_state_trigger_set_value(u_int64_t triggerid, unsigned char value) {
 //   LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_value_cb, &value, 0); 
}

ELEMS_CALLBACK(set_errmsg_cb){
    trigger_state_t *tr_state = elem->data;
    strpool_replace(&conf->strpool, &tr_state->errorstr, (char *)data);
}

int glb_state_trigger_set_errmsg(u_int64_t triggerid, char *errmsg) {
 //   LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_errmsg_cb, errmsg, 0); 
};

ELEMS_CALLBACK(get_errmsg_cb) {
    trigger_state_t *tr_state = elem->data;
    char **errstr = data;

    if (NULL == tr_state->errorstr) {
        *errstr = NULL;
        return SUCCEED;
    }

    *errstr = zbx_strdup(NULL, tr_state->errorstr);
    return SUCCEED;
} 

int glb_state_trigger_get_errmsg(u_int64_t triggerid, char **errmsg) {
  //  LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, get_errmsg_cb, errmsg, 0 );
}

typedef struct {
    trigger_state_t *state;
    u_int64_t flags;
} meta_cb_params;


ELEMS_CALLBACK(set_meta_cb) {
    trigger_state_t *tr_state = elem->data;
    meta_cb_params *params = data;

    if (0 != (params->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR)) {
        strpool_replace(&conf->strpool, &tr_state->errorstr, params->state->errorstr);
    }
    
    if (0 != (params->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE)) {
        tr_state->value = params->state->value;
    }
    
    if (0 != (params->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE))
        tr_state->lastchange = params->state->lastchange;

}

int state_trigger_set_state(u_int64_t triggerid, trigger_state_t *state, u_int64_t flags) {
    meta_cb_params params = {.state = state, .flags = flags};
    return elems_hash_process(conf->triggers, triggerid, set_meta_cb, &params, 0 );
}

int glb_state_trigger_delete(u_int64_t triggerid) {
    return elems_hash_delete(conf->triggers, triggerid);
}

ELEMS_CALLBACK(is_problem_cb) {
    trigger_state_t *tr_state = elem->data;

    if (TRIGGER_STATUS_ENABLED == tr_state->status &&
		TRIGGER_FUNCTIONAL_TRUE == tr_state->functional &&
		TRIGGER_VALUE_PROBLEM == tr_state->value) 
        return SUCCEED;
    
    return FAIL;

}

int glb_state_trigger_is_problem(u_int64_t triggerid) {
    LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    int ret = elems_hash_process(conf->triggers, triggerid, is_problem_cb, NULL, 0);
    LOG_INF("in: %s, finished processing trigger %ld", __func__, triggerid);
    return ret;
}

ELEMS_CALLBACK(is_functional_cb) {
    trigger_state_t *tr_state = elem->data;

  //  LOG_INF("Trigger %ld state is %d, value is %d", elem->id, tr_state->state, tr_state->value);
    if (TRIGGER_STATUS_ENABLED == tr_state->status &&
    	TRIGGER_FUNCTIONAL_TRUE == tr_state->functional)
        return SUCCEED;
    
    return FAIL;
}

int glb_state_trigger_is_functional(u_int64_t triggerid) {
    //LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, is_functional_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(set_functional_cb) {
    trigger_state_t *tr_state = elem->data;

    tr_state->functional = *(unsigned char *)data;
    return FAIL;
}

int glb_state_trigger_set_functional(u_int64_t triggerid, unsigned char f_value) {
  //  LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_functional_cb, &f_value, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(get_meta_cb) {
    trigger_state_t *tr_state = elem->data;
    trigger_state_t **state = (trigger_state_t **) data;
    static trigger_state_t ret_state = {0}; //static object for returning states without mallocs
    *state = &ret_state;

    strpool_free(&conf->strpool, ret_state.errorstr);
    
    memcpy(&ret_state, tr_state, sizeof(trigger_state_t));
       
    /*note: it's not real copy, just an acquire, fast and efficient */
    tr_state->errorstr = strpool_copy(tr_state->errorstr);
    
    return SUCCEED;
}

trigger_state_t *state_trigger_get_state(u_int64_t triggerid) {
    trigger_state_t *state = NULL;
    LOG_INF("State requestedd");
    
    int ret = elems_hash_process(conf->triggers, triggerid, get_meta_cb, &state, 0);
    LOG_INF("ret returned %d, state addr is %p", ret, state);
     
    if (FAIL == ret)
        return NULL;

    return state;
}


ELEMS_CALLBACK(get_revison_cb) {
    trigger_state_t *tr_state = elem->data;
    
    return tr_state->revision;
}

int glb_state_trigger_get_revision(u_int64_t triggerid) {
//    LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, get_revison_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(set_revison_cb) {
    trigger_state_t *tr_state = elem->data;
    tr_state->revision = *(int *)data;
  //  LOG_INF("Trigger %ld: set revision to %d", elem->id, *(int *)data);
    return SUCCEED;
}

int glb_state_trigger_set_revision(u_int64_t triggerid, int revision) {
//    LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_revison_cb, &revision, 0);
}

ELEMS_CALLBACK(exists_cb) {
    return SUCCEED;
}

int glb_state_trigger_check_exists(u_int64_t triggerid) {
 //   LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, exists_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}


ELEMS_CALLBACK(get_status_cb) {
    trigger_state_t *tr_state = elem->data;
    
    return tr_state->status;
}

int glb_state_trigger_get_status(u_int64_t triggerid) {
  //  LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, get_status_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}


ELEMS_CALLBACK(check_revision_cb) {
    trigger_state_t *tr_state = elem->data;
    int revision = *(int*)data;
    if (tr_state->revision == revision) 
        return SUCCEED;
 //   LOG_INF("Trigger %ld revision check failed: expected %d, but current %d", revision, tr_state->revision);
    return FAIL;
}

int glb_state_trigger_check_revision(u_int64_t triggerid, int revision) {
   // LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, check_revision_cb, &revision, ELEM_FLAG_DO_NOT_CREATE);
}



ELEMS_CALLBACK(set_lastchange_cb) {
    trigger_state_t *tr_state = elem->data;
    
    tr_state->lastchange = *(int*) data; 
       
    DEBUG_TRIGGER(elem->id, "Trigger problem count is set to %d", tr_state->lastchange);
    return FAIL;
}

int  glb_state_trigger_set_lastchange(u_int64_t triggerid, int lastchange) {
//      LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
      return elems_hash_process(conf->triggers, triggerid, set_lastchange_cb, &lastchange, ELEM_FLAG_DO_NOT_CREATE);
} 

void state_trigger_set_error(trigger_state_t *state, char *error) {
 //   LOG_INF("Replacing state error '%s' -> '%s'", state->errorstr, error);
    strpool_replace(&conf->strpool,&state->errorstr, error);
}

void state_trigger_set_value(trigger_state_t *state, unsigned char value) {
    LOG_INF("Setting trigger value to %d", value);
    if (TRIGGER_VALUE_PROBLEM == value || TRIGGER_VALUE_OK == value) {
        LOG_INF("Releasing errorstr %p", state->errorstr);  
        //THIS_SHOULD_NEVER_HAPPEN;
       // LOG_INF("Releasing errorstr %s", state->errorstr);       
        strpool_free(&conf->strpool, state->errorstr);
        state->errorstr = NULL;
    }
    
    state->value = value;
}

//void state_trigger_set_problem_count(trigger_state_t *state, int problem_count) {
//    state->problem_count = problem_count;
//}

//general interface for trigger state processing during it's lock
int state_trigger_process(u_int64_t triggerid, elems_hash_process_cb_t trigger_state_process_cb, void *data) {
    
//    LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, trigger_state_process_cb, data, 0);
}

const char* state_trigger_get_errstr(trigger_state_t *state) {
    return state->errorstr;
}

int state_trigger_get_lastchange(trigger_state_t *state) {
    return state->lastchange;
}

unsigned char state_trigger_get_status(trigger_state_t *state) {
    return state->status;
}

unsigned char state_trigger_get_value(trigger_state_t *state) {
    return state->value;
}

void trigger_event_create(DB_EVENT *event, trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value, zbx_timespec_t *ts){
    
    if (state->value == TRIGGER_VALUE_UNKNOWN || old_value == TRIGGER_VALUE_UNKNOWN ) {
        DEBUG_TRIGGER(conf->triggerid,"Creating internal event for the trigger");
        
       
        create_event(event, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER, conf->triggerid,
	 		ts, state->value, NULL, conf->expression,
	 		conf->recovery_expression, 0, 0, &conf->tags, 0, NULL, 0, NULL, NULL,
	 		state->errorstr );
        return;
    }
    
    DEBUG_TRIGGER(conf->triggerid,"Creating normal event for the trigger");
    LOG_INF("Creating normal event for the trigger %ld", conf->triggerid);
	
	create_event(event, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, conf->triggerid,
	 	ts, state->value, conf->description,
	 	conf->expression, conf->recovery_expression,
	 	conf->priority, conf->type, &conf->tags,
	 	conf->correlation_mode, conf->correlation_tag, state->value, conf->opdata,
	 	conf->event_name, NULL);
}

int state_trigger_is_changed(trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value)
{
	const char		*new_error;
	int			new_state;
//    static DB_EVENT event={0};
	
	DEBUG_TRIGGER(conf->triggerid, "Checking trigger %ld value: %d -> %d, status: %d ", conf->triggerid, 
		old_value, state->value, state->status );

	
    if ( state->value != old_value || 
        (TRIGGER_VALUE_PROBLEM == state->value &&  TRIGGER_TYPE_MULTIPLE_TRUE == conf->type) ) {
        DEBUG_TRIGGER(conf->triggerid,"Trigger is changed");

    //LOG_INF("Creating trigger event");
    //trigger_event_create(&event, conf, state, old_value, ts);

    //    state->lastchange = ts->sec;
        
        return SUCCEED;
    }
    
	return FAIL;
}
