
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
/* note: https://www.atlassian.com/incident-management/devops/incident-vs-problem-management 
  in zabbix this part of business is called "problems", but in Glaber - it's incidents
*/

#include "common.h"

#include "zbxalgo.h"
#include "state.h"
#include "glb_lock.h"


// typedef struct
// {
// 	zbx_uint64_t		eventid;
// 	DB_TRIGGER		trigger;
// 	zbx_uint64_t		objectid;
// 	u_int64_t		problem_eventid;
// 	char			*name;
// 	int			source;
// 	int			object;
// 	int			clock;
// 	int			value;
// 	int			acknowledged;
// 	int			ns;
// 	int			severity;
// 	unsigned char		suppressed;
// 	zbx_vector_ptr_t	tags;	/* used for both zbx_tag_t and zbx_host_tag_t */

// #define ZBX_FLAGS_DB_EVENT_UNSET		0x0000
// #define ZBX_FLAGS_DB_EVENT_CREATE		0x0001
// #define ZBX_FLAGS_DB_EVENT_NO_ACTION		0x0002
// #define ZBX_FLAGS_DB_EVENT_RECOVER		0x0004
// 	zbx_uint64_t		flags;
// }
// DB_EVENT;

zbx_timespec_t t;

typedef struct {
    u_int64_t incidentid;
    u_int64_t triggerid;
    const char *name;
    zbx_vector_ptr_t	tags;	/* used for both zbx_tag_t and zbx_host_tag_t */
    int			acknowledged;

    int value;
} incident_t;

typedef struct {
    elems_hash_t *incidents;
    strpool_t strpool;
    mem_funcs_t memf;
} incidents_conf_t;

static incidents_conf_t *conf;

void	tag_free_func(zbx_tag_t *tag)
{
	conf->memf.free_func(tag->tag);
	conf->memf.free_func(tag->value);
	conf->memf.free_func(tag);
}

ELEMS_CREATE(incident_create_cb) {
    incident_t *incident;
    if (NULL == (elem->data = memf->malloc_func(NULL, sizeof(incident_t))))
        return FAIL;
    incident = elem->data;
    bzero(incident, sizeof(incident_t));
    zbx_vector_ptr_create_ext(&incident->tags, memf->malloc_func, memf->realloc_func, memf->free_func);
    
}

ELEMS_FREE(incident_free_cb) {
    incident_t *incident = elem->data;
    zbx_vector_ptr_clear_ext(&incident->tags, (zbx_ptr_free_func_t)tag_free_func);

    memf->free_func(incident);
}

int state_incidents_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(incidents_conf_t))) ||
        NULL == (conf->incidents = elems_hash_init(memf, incident_create_cb, incident_free_cb)) 
      ) {
        LOG_WRN("Cannot allocate memory for incidents state struct");
        return FAIL;
    };
    
    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
 
    return SUCCEED;
}


// /* entry point intreface to register incident open and recovery */
// int state_incidents_account_trigger_change() {

// 	DB_EVENT event;
	
// 	trigger_event_create(&event, conf, state , old_value, ts);
	
// 	if (SUCCEED == is_event_supressed(&event))
// 		return SUCCEED;

// 	save_event_to_history(&event);	
	
// 	if ( SUCCEED == is_recovery_change(state->value)) { 
// 		DEBUG_TRIGGER(conf->triggerid,"Processing trigger PROBLEM->OK change");			
		
//         /* note: trigger's value might change back to problem if not all correlated
//         problems has been recovered */
        
//         //trigger_problems_close_problems
//         state->value = state_incidents_close_by_recovery_event(&event, conf);
// 	} else {
// 		DEBUG_TRIGGER(conf->triggerid,"Processing trigger OK->PROBLEM change");	
// 	//	LOG_INF("Processing trigger OK->PROBLEM change triggerid %ld", conf->triggerid);		
// 		//trigger_problems_create_problem(&event, conf);
//         state_incidents_create_incident(&event, conf);
// 	}

// 	clean_event(&event);
// 	return SUCCEED;	
// }
ELEMS_CALLBACK(incident_cleanup_cb) {

}


static u_int64_t create_incident() {
    u_int64_t incidentid = gen_incident_id();
    elems_hash_iterate(conf->incidents, incident_cleanup_cb, NULL, 0)
    return incidentid;
}

int incidents_process_trigger_fired(u_int64_t triggerid) {
    DEBUG_TRIGGER(triggerid,"Creating new incident for the trigger");
    u_int64_t incidentid;
    
    // problem_t *problem = conf.memf.malloc_func(NULL, sizeof(problem_t));
    // bzero(problem, sizeof(problem_t));
    
    // problem->eventid = event->eventid;
    // problem->lastchange = time(NULL);
    // problem->state = PROBLEM_STATE_OPEN;
    
    // zbx_vector_ptr_create_ext(&problem->tags, conf.memf.malloc_func, conf.memf.realloc_func, conf.memf.free_func);
    // copy_event_tags_to_problem(event, problem, &conf.memf);
    // zbx_vector_ptr_append(&problems->vector, problem);

    if (0 == (incidentid = create_incident()))
        return FAIL;
    
    state_triggers_add_incident(triggerid, incidentid);
    actions_notify_incident_change(incidentid);

    return SUCCEED;
}

