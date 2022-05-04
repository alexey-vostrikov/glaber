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
#include "state_problems.h"
#include "../glb_conf/conf_triggers.h"
#include "../../zabbix_server/actions.h"

#define PROBLEM_STATE_OPEN 1
#define PROBLEM_STATE_RESOLVED 2

typedef struct {
    strpool_t *strpool;
    mem_funcs_t memf;
} conf_t;

static conf_t conf = {0};

struct trigger_problems_t {
    zbx_vector_ptr_t vector;
};

struct problem_t
{
	zbx_uint64_t		eventid;
	zbx_vector_ptr_t	tags;
    unsigned char state; //PROBLEM_STATE_OPEN; PROBLEM_STATE_CLOSED
    int lastchange; //to clean resolved problems after some time
};


trigger_problems_t *trigger_problems_init(mem_funcs_t *memf) {

    trigger_problems_t *t_problems;
    
    if (NULL == (t_problems = memf->malloc_func(NULL, sizeof(trigger_problems_t)))) {
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
        return NULL;
    }
    zbx_vector_ptr_create_ext(&t_problems->vector, memf->malloc_func, memf->realloc_func, memf->free_func);
    return t_problems;
}

static void problem_free_func(void *ptr) {
    problem_t *problem = ptr;

 	zbx_vector_ptr_clear_ext(&problem->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&problem->tags);
	zbx_free(problem);
}


void trigger_problems_destroy(trigger_problems_t *t_problems, mem_funcs_t *memf) {
    
    zbx_vector_ptr_clear_ext(&t_problems->vector, problem_free_func);
    zbx_vector_ptr_destroy(&t_problems->vector);
    
    memf->free_func(t_problems);
}

/******************************************************************************
 *                                                                            *
 * Function: match_tag                                                        *
 *                                                                            *
 * Purpose: checks if the two tag sets have matching tag                      *
 *                                                                            *
 * Parameters: name  - [IN] the name of tag to match                          *
 *             tags1 - [IN] the first tag vector                              *
 *             tags2 - [IN] the second tag vector                             *
 *                                                                            *
 * Return value: SUCCEED - both tag sets contains a tag with the specified    *
 *                         name and the same value                            *
 *               FAIL    - otherwise.                                         *
 *                                                                            *
 ******************************************************************************/
static int	match_tag(const char *name, const zbx_vector_ptr_t *tags1, const zbx_vector_ptr_t *tags2)
{
	int		i, j;
	zbx_tag_t	*tag1, *tag2;

	for (i = 0; i < tags1->values_num; i++)
	{
		tag1 = (zbx_tag_t *)tags1->values[i];

		if (0 != strcmp(tag1->tag, name))
			continue;

		for (j = 0; j < tags2->values_num; j++)
		{
			tag2 = (zbx_tag_t *)tags2->values[j];

			if (0 == strcmp(tag2->tag, name) && 0 == strcmp(tag1->value, tag2->value))
				return SUCCEED;
		}
	}

	return FAIL;
}

int problems_close_by_trigger(trigger_problems_t *t_problems, trigger_conf_t *conf, DB_EVENT *event) {
    unsigned char trigger_value = TRIGGER_VALUE_OK;
    int i;

   // LOG_INF("Closing problems by trigger, trigger %ld, problems addr is %p", conf->triggerid,t_problems);
    for (i = 0; i < t_problems->vector.values_num; i++) {
        problem_t *problem = t_problems->vector.values[i];
     //   LOG_INF("Closing problem %d out of %d", i, t_problems->vector.values_num);
        if ( PROBLEM_STATE_OPEN != problem->state )
            continue;
        
        if ( ZBX_TRIGGER_CORRELATION_NONE != conf->correlation_mode &&
             SUCCEED != match_tag(conf->correlation_tag, &problem->tags, &event->tags)) {
		       	DEBUG_TRIGGER(conf->triggerid,"Not recovering problem for event %ld: tag not matched by correlation rule", problem->eventid);
			    trigger_value =  TRIGGER_VALUE_PROBLEM;
                continue;
        }       
                   
        DEBUG_TRIGGER(conf->triggerid,"Recovering problem for event %ld matched by correlation rule", problem->eventid);

        problem->state = PROBLEM_STATE_RESOLVED;
        problem->lastchange = time(NULL);

        trigger_recovery_event_t rec_event = { .eventid = DBget_maxid_num("events", 1), .ts.sec = time(NULL), .ts.ns = 0,  .problem_eventid = problem->eventid, .triggerid = conf->triggerid};
        actions_proccess_trigger_recovery(&rec_event);
        return trigger_value;
    }

} 

static void copy_event_tags_to_problem(DB_EVENT *event, problem_t *problem, mem_funcs_t *memf) {
    
    int i;

    for (i=0; i< event->tags.values_num; i++) {

        zbx_tag_t *tag = event->tags.values[i], 
                  *new_tag = memf->malloc_func(NULL, sizeof(zbx_tag_t));

        new_tag->tag = (char *)strpool_add(conf.strpool, tag->tag);
        new_tag->value =(char *)strpool_add(conf.strpool, tag->value);

        zbx_vector_ptr_append(&problem->tags, new_tag);
    }

}

int  problems_create_problem(trigger_problems_t *problems, DB_EVENT *event, u_int64_t triggerid) {
 
    DEBUG_TRIGGER(triggerid,"Creating new problem for the trigger");
    //LOG_INF("In %s", __func__);
    problem_t *problem = conf.memf.malloc_func(NULL, sizeof(problem_t));
    bzero(problem, sizeof(problem_t));
    //LOG_INF("Created and zeored");
    problem->eventid = event->eventid;
    problem->lastchange = time(NULL);
    problem->state = PROBLEM_STATE_OPEN;
    //LOG_INF("Creating tags");
    zbx_vector_ptr_create_ext(&problem->tags, conf.memf.malloc_func, conf.memf.realloc_func, conf.memf.free_func);
    //LOG_INF("Doing tags copy");
    copy_event_tags_to_problem(event, problem, &conf.memf);
    //LOG_INF("Finished tags copy");
    zbx_vector_ptr_append(&problems->vector, problem);
    //LOG_INF("Calling action process for the event");
    actions_process_trigger_problem(event);
}

int state_problems_init(strpool_t *strpool, mem_funcs_t *memf) {
    conf.strpool = strpool;
    conf.memf = *memf;
}