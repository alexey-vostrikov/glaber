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
#include "state_problems.h"
//#include "../zbxserver/expression.h"
#include "zbxserver.h"

typedef struct {
    elems_hash_t *triggers;
    mem_funcs_t memf;
    strpool_t strpool;
} conf_t;

struct trigger_state_t {
    unsigned char status; //enabled or disabled
    unsigned char value;
    int lastchange;
    u_int64_t revision;
    const char *errorstr; 
    unsigned char	functional;	 /* see TRIGGER_FUNCTIONAL_* defines  expcted to be set according to the item's state */
    trigger_problems_t *problems;
} ;
typedef struct trigger_state_t trigger_state_t;

static conf_t *conf;

ELEMS_CREATE(trigger_create_cb){
    trigger_state_t *state = NULL;
    
    if (NULL == ( state = memf->malloc_func(NULL, sizeof(trigger_state_t)))) 
        return FAIL;
    
    bzero(state, sizeof(trigger_state_t));
    elem->data = state;
    
    //LOG_INF("Doing problems init");
    if (NULL == ( state->problems = trigger_problems_init(memf)))
        return FAIL;
    //LOG_INF("finished problems init");
    //LOG_INF("Init of t_problems for triggerid %ld is succesifull", elem->id, state);
   
    state->errorstr = strpool_add(&conf->strpool,"Trigger hasn't been calculated since the server start yet");  
    state->lastchange= 0; 
    state->value = TRIGGER_VALUE_UNKNOWN;

    //LOG_INF("Init of t_problems for triggerid %ld is succesifull2", elem->id, state);
    return SUCCEED;
}

ELEMS_FREE(trigger_free_cb) {
    trigger_state_t *tr_state = elem->data;
    
    trigger_problems_destroy(tr_state->problems, memf);
    
    strpool_free(&conf->strpool, tr_state->errorstr);
    
    conf->memf.free_func(elem->data);
    elem->data = NULL;

    return SUCCEED;
}

int glb_state_triggers_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(conf_t))) ||
        NULL == (conf->triggers = elems_hash_init(memf, trigger_create_cb, trigger_free_cb)) 
      ) {
        LOG_WRN("Cannot allocate memory for triggers state struct");
        return FAIL;
    };
    
    conf->memf = *memf;

    strpool_init(&conf->strpool, memf);
    state_problems_init(&conf->strpool, memf);
  
    return SUCCEED;
}

static int  is_event_supressed(DB_EVENT *event) {
	/* note: should be implemented and put into events */
	return FAIL;
}

static int is_recovery_change(unsigned char trigger_value) {
	if ( TRIGGER_VALUE_OK == trigger_value )
		return SUCCEED;
	return FAIL;
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

ELEMS_CALLBACK(set_error_cb){
    trigger_state_t *tr_state = elem->data;
    strpool_replace(&conf->strpool, &tr_state->errorstr, (char *)data);
}

int state_trigger_set_error(u_int64_t triggerid, char *errmsg) {
 //   LOG_INF("in: %s, processing trigger %ld", __func__, triggerid);
    return elems_hash_process(conf->triggers, triggerid, set_error_cb, errmsg, 0); 
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
    return elems_hash_process(conf->triggers, triggerid, is_problem_cb, NULL, 0);  
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
        
    int ret = elems_hash_process(conf->triggers, triggerid, get_meta_cb, &state, 0);
         
    if (FAIL == ret)
        return NULL;

    return state;
}


ELEMS_CALLBACK(get_revison_cb) {
    trigger_state_t *tr_state = elem->data;
    u_int64_t *revision = data;
    *revision = tr_state->revision;

    return SUCCEED;
}

u_int64_t glb_state_trigger_get_revision(u_int64_t triggerid) {
    u_int64_t revision;
    if (SUCCEED == elems_hash_process(conf->triggers, triggerid, get_revison_cb, &revision, ELEM_FLAG_DO_NOT_CREATE))
        return revision;

    return 0;
}

ELEMS_CALLBACK(set_revison_cb) {
  
    trigger_state_t *tr_state = elem->data;
   // LOG_INF("Setting revision in the callback, state is %p", tr_state);
    
    tr_state->revision = (u_int64_t)data;

    return SUCCEED;
}

int state_trigger_set_revision(u_int64_t triggerid, u_int64_t revision) {
    return elems_hash_process(conf->triggers, triggerid, set_revison_cb, (void *)revision, 0);
}

ELEMS_CALLBACK(exists_cb) {
    return SUCCEED;
}

int glb_state_trigger_check_exists(u_int64_t triggerid) {
    return elems_hash_process(conf->triggers, triggerid, exists_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(get_status_cb) {
    trigger_state_t *tr_state = elem->data;
    
    return tr_state->status;
}

int glb_state_trigger_get_status(u_int64_t triggerid) {
    return elems_hash_process(conf->triggers, triggerid, get_status_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(check_revision_cb) {
    trigger_state_t *tr_state = elem->data;
    u_int64_t revision = *(u_int64_t*)data;
//    LOG_INF("Checking revision for trigger %ld : state has %ld, revision is %ld", elem->id, tr_state->revision, revision);

    if (tr_state->revision == revision) 
        return SUCCEED;

    return FAIL;
}

int glb_state_trigger_check_revision(u_int64_t triggerid, u_int64_t revision) {
//    LOG_INF("in: %s, processing trigger %ld check for revision %d", __func__, triggerid, revision);
    return elems_hash_process(conf->triggers, triggerid, check_revision_cb, &revision, ELEM_FLAG_DO_NOT_CREATE);
}

ELEMS_CALLBACK(set_lastchange_cb) {
    trigger_state_t *tr_state = elem->data;
    
    tr_state->lastchange = *(int*) data; 
       
    DEBUG_TRIGGER(elem->id, "Trigger problem count is set to %d", tr_state->lastchange);
    return FAIL;
}

int  glb_state_trigger_set_lastchange(u_int64_t triggerid, int lastchange) {
      return elems_hash_process(conf->triggers, triggerid, set_lastchange_cb, &lastchange, ELEM_FLAG_DO_NOT_CREATE);
} 

int state_trigger_process(u_int64_t triggerid, elems_hash_process_cb_t trigger_state_process_cb, void *data) {
    return elems_hash_process(conf->triggers, triggerid, trigger_state_process_cb, data, 0);
}

trigger_problems_t *state_trigger_get_problems(trigger_state_t *state) {
    return state->problems;
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

static void trigger_event_create(DB_EVENT *event, trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value, zbx_timespec_t *ts){

    if (state->value == TRIGGER_VALUE_UNKNOWN || old_value == TRIGGER_VALUE_UNKNOWN ) {
        DEBUG_TRIGGER(conf->triggerid,"Creating internal event for the trigger");
        
      
        create_event(event, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER, conf->triggerid,
	 		ts, state->value, NULL, conf->expression,
	 		conf->recovery_expression, 0, 0, &conf->tags, 0, NULL, 0, NULL, NULL,
	 		state->errorstr );

        return;
    }
    
    DEBUG_TRIGGER(conf->triggerid,"Creating normal event for the trigger");
   	
	create_event(event, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, conf->triggerid,
	 	ts, state->value, conf->description,
	 	conf->expression, conf->recovery_expression,
	 	conf->priority, conf->type, &conf->tags,
	 	conf->correlation_mode, conf->correlation_tag, state->value, conf->opdata,
	 	conf->event_name, NULL);
}

int state_trigger_is_changed(trigger_conf_t *conf, unsigned char new_value, unsigned char old_value)
{
	const char		*new_error;
	int			new_state;
	
	DEBUG_TRIGGER(conf->triggerid, "Checking trigger %ld value: %d -> %d ", conf->triggerid, 
		old_value, new_value );

	
    if ( new_value != old_value || 
        (TRIGGER_VALUE_PROBLEM == new_value &&  TRIGGER_TYPE_MULTIPLE_TRUE == conf->type) ) {
        DEBUG_TRIGGER(conf->triggerid,"Trigger is changed");

        return SUCCEED;
    }
    
	return FAIL;
}


typedef struct {
    trigger_conf_t *conf;
    DB_EVENT *event;
} problem_params_t;

ELEMS_CALLBACK(close_problems_cb) {
    trigger_state_t *state = elem->data;
    problem_params_t *params = data;

    return problems_close_by_trigger(state->problems, params->conf, params->event);
}

int trigger_problems_close_problems(DB_EVENT *event, trigger_conf_t *t_conf)  {

    problem_params_t params = {.conf = t_conf, .event = event};
    return elems_hash_process(conf->triggers, t_conf->triggerid, close_problems_cb, &params, 0);

}

ELEMS_CALLBACK(create_problem_cb) {
    trigger_state_t *state = elem->data;
    problem_params_t *params = data;
    DEBUG_TRIGGER(elem->id, "Called trigger create problem callback for trigger");
  //  LOG_INF("Called trigger create problem callback for trigger %ld",params->conf->triggerid);
    problems_create_problem(state->problems, params->event, params->conf->triggerid);
}

int trigger_problems_create_problem(DB_EVENT *event, trigger_conf_t *t_conf)  {
    problem_params_t params = {.conf = t_conf, .event = event};
  //  LOG_INF("Got t_conf %p, event %p", t_conf, event);
  
    DEBUG_TRIGGER(t_conf->triggerid, "Calling trigger create problem callback");
    elems_hash_process(conf->triggers, t_conf->triggerid, create_problem_cb, &params, 0);
}

ELEMS_CALLBACK(fill_dc_trigger_cb) {
    DC_TRIGGER *tr_conf = data;
    trigger_state_t *state = elem->data;

    tr_conf->status = state->status;

	if (NULL != state->errorstr )
		tr_conf->error = zbx_strdup(NULL, state->errorstr);
	else tr_conf->error = NULL;

	tr_conf->value = state->value;
	tr_conf->lastchange = state->lastchange;

    return SUCCEED;
}

int state_trigger_fill_DC_TRIGGER_state(u_int64_t triggerid, DC_TRIGGER *dctrigger) {
    elems_hash_process(conf->triggers, triggerid, fill_dc_trigger_cb, dctrigger, 0);
}

static int	process_trigger_change(trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value, zbx_timespec_t *ts)
{

    /*note: unlike in Zabbix - trigger is not related to the incidents it creates
    it's much easy to undestand logic and do unvestigation that way: 
    
    so an administrator might distingush trigger behaviur and incidents open/close logic
    that way

    that's why incidents(problems) are separated from the triggers state */
    state_incidents_account_trigger_change();

	// DB_EVENT event;
	
	// trigger_event_create(&event, conf, state , old_value, ts);
	
	// if (SUCCEED == is_event_supressed(&event))
	// 	return SUCCEED;

	// save_event_to_history(&event);	
	
	// if ( SUCCEED == is_recovery_change(state->value)) { 
	// 	DEBUG_TRIGGER(conf->triggerid,"Processing trigger PROBLEM->OK change");			
		
    //     /* note: trigger's value might change back to problem if not all correlated
    //     problems has been recovered */
        
    //     //trigger_problems_close_problems
    //     state->value = state_incidents_close_by_recovery_event(&event, conf);
	// } else {
	// 	DEBUG_TRIGGER(conf->triggerid,"Processing trigger OK->PROBLEM change");	
	// //	LOG_INF("Processing trigger OK->PROBLEM change triggerid %ld", conf->triggerid);		
	// 	//trigger_problems_create_problem(&event, conf);
    //     state_incidents_create_incident(&event, conf);
	// }

	// clean_event(&event);
	// return SUCCEED;	
}


int	recalculate_trigger(u_int64_t triggerid)
{
    DEBUG_TRIGGER(triggerid, "Calculating trigger");
	static trigger_conf_t conf = {0};

	trigger_state_t *state;
	zbx_timespec_t ts = {.sec = time(NULL), .ns = 0};

	DEBUG_TRIGGER(triggerid, "Freeing trigger config data");
	conf_trigger_free_trigger(&conf);

	/* there is no locking here, if needed, put locking fields to state or state flag */
	if (FAIL == conf_trigger_get_trigger_conf_data(triggerid, &conf)) {
	
		DEBUG_TRIGGER(triggerid, "Trigger configuration fetch failed");
		return FAIL;
	}
	
	if (NULL ==(state = state_trigger_get_state(triggerid))) {
		DEBUG_TRIGGER(triggerid,"State configuration fetch failed");
		return FAIL;
	}
	
	unsigned char old_value = state_trigger_get_value(state);

	DEBUG_TRIGGER(triggerid, "Saved current trigger value: %d", old_value);

	if (SUCCEED != DCconfig_check_trigger_dependencies(triggerid)) {
	 	DEBUG_TRIGGER(triggerid,"Trigger depends on triggers in PROBLEM state, trigger cannot be calculated right now");
		 //todo: set new trigger state and error message
		return FAIL;
	}

	if ( FAIL == evaluate_trigger_expressions(&conf, &state->value)) {
		DEBUG_TRIGGER(triggerid,"Evaluate expression failed, finishing processing");
		return FAIL;
	}

	if (SUCCEED == state_trigger_is_changed(&conf, state->value, old_value)) {
	
		/*trigger calculated to a new value or it's has multiple problem gen */
		DEBUG_TRIGGER(triggerid, "Trigger processing t, doing event processing");
	
		/*note: processing might change trigger state */
		process_trigger_change(&conf, state, old_value, &ts);
	
	} else {
		DEBUG_TRIGGER(triggerid, "Trigger is not changed, nothing to process");
	}
	
	DEBUG_TRIGGER(triggerid, "Saving new state");
	state_trigger_set_state(triggerid, state, ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ALL);

	DEBUG_TRIGGER(triggerid, "Finished trigger processing");
	
	return SUCCEED;
}
