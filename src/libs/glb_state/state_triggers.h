#ifndef STATE_TRIGGERS_H
#define STATE_TRIGGERS_H

#include "common.h"
#include "zbxalgo.h"
#include "../glb_process/proc_triggers.h"

typedef struct trigger_state_t trigger_state_t;

/* trigger is functional unless its expression contains disabled or not monitored items */
#define TRIGGER_FUNCTIONAL_TRUE		0
#define TRIGGER_FUNCTIONAL_FALSE	1

int glb_state_trigger_get_status(u_int64_t triggerid);

int glb_state_trigger_set_status(u_int64_t triggerid, unsigned char status);
int glb_state_trigger_set_lastchange(u_int64_t triggerid, int lastchange);

int glb_state_trigger_set_value(u_int64_t triggerid, unsigned char value);
unsigned char glb_state_trigger_get_value(u_int64_t triggerid);

int glb_state_trigger_get_errmsg(u_int64_t triggerid, char **errmsg);
int glb_state_trigger_set_errmsg(u_int64_t triggerid, char *errmsg);

//int glb_state_trigger_set_meta(u_int64_t triggerid, trigger_state_t *meta, u_int64_t flags);
trigger_state_t *state_trigger_get_state(u_int64_t triggerid);

int state_trigger_set_state(u_int64_t triggerid, trigger_state_t *state, u_int64_t flags);

int glb_state_triggers_init(mem_funcs_t *memf);
int glb_state_trigger_delete(u_int64_t triggerid);

int glb_state_trigger_is_functional(u_int64_t triggerid);
int glb_state_trigger_is_problem(u_int64_t triggerid);
int glb_state_trigger_set_functional(u_int64_t triggerid, unsigned char f_value);

void	glb_state_triggers_apply_diff_changes(zbx_vector_ptr_t *trigger_diff);

/*****move required functions for own queues impl below ***/
int glb_state_trigger_set_revision(u_int64_t triggerid, u_int64_t revision);
u_int64_t glb_state_trigger_get_revision(u_int64_t triggerid);
int glb_state_trigger_check_exists(u_int64_t triggerid);

int glb_state_trigger_check_revision(u_int64_t triggerid, u_int64_t revision);
//int glb_state_trigger_set_problem_count(u_int64_t triggerid, int problem_count);
int  glb_state_trigger_set_lastchange(u_int64_t triggerid, int lastchange);

void state_trigger_set_error(trigger_state_t *state, char *error);
void state_trigger_set_value(trigger_state_t *state, unsigned char value);
//void state_trigger_set_problem_count(trigger_state_t *state, int problem_count);

const char* state_trigger_get_errstr(trigger_state_t *state);
int   state_trigger_get_lastchange(trigger_state_t *state);
unsigned char state_trigger_get_status(trigger_state_t *state);
unsigned char state_trigger_get_value(trigger_state_t *state);

int state_trigger_process(u_int64_t triggerid, elems_hash_process_cb_t trigger_state_process_cb, void *data);
int state_trigger_is_changed(trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value);


void trigger_event_create(DB_EVENT *event, trigger_conf_t *conf, trigger_state_t *state, unsigned char old_value, zbx_timespec_t *ts);

#endif