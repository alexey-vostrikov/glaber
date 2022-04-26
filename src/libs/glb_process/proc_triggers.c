
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
#include "dbcache.h"
#include "log.h"
#include "process.h"
#include "zbxserver.h"
#include "../../zabbix_server/events.h"
#include "../glb_conf/conf_index.h"
#include "../glb_state/state_triggers.h"
#include "../glb_conf/conf_triggers.h"
#include "../zbxipcservice/glb_ipc.h"
#include "../zbxserver/expression.h"


/* based on code from dbconfig.c */
/* for time-based triggers maybe a separate wrapper required to fetch proc_info */
void	prepare_trigger_conf(trigger_conf_t *tr)
{
	tr->eval_ctx = zbx_eval_deserialize_dyn(tr->expression_bin, tr->expression, ZBX_EVAL_EXCTRACT_ALL);
	DEBUG_TRIGGER(tr->triggerid,"Extracted trigger expression to binary");
	
	if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
	{
		tr->eval_ctx_r = zbx_eval_deserialize_dyn(tr->recovery_expression_bin, tr->recovery_expression,
					ZBX_EVAL_EXCTRACT_ALL);
		DEBUG_TRIGGER(tr->triggerid,"Extracted trigger recovery expression to binary");
	}
}

// int check_conf_values(trigger_state_t *conf) {
// 	switch (conf->state)
// 	{	
// 		case TRIGGER_STATE_NORMAL:
// 		case TRIGGER_STATE_UNKNOWN:
// 			break;
// 		default: 
// 			LOG_INF("Impossible trigger state %d", conf->state);
// 			return FAIL;
// 	}
// 	switch (conf->value) {
// 		case TRIGGER_VALUE_NONE:
// 		case TRIGGER_VALUE_OK:
// 		case TRIGGER_VALUE_PROBLEM:
// 		case TRIGGER_VALUE_UNKNOWN:
// 			break;
// 		default: 
// 			LOG_INF("Impossible trigger value %d", conf->value);
// 			return FAIL;
// 	}
// 	return SUCCEED;
// }

// ELEMS_CALLBACK(trigger_process_cb) {
// 	trigger_conf_t *conf = data;
// 	trigger_state_t *state = elem->data;
	
// 	unsigned char old_value = state_trigger_get_value(state);
	 
			
// 	//zbx_determine_items_in_expressions(&trigger_order, itemids, item_num);
// 	//HAS_TO_BE_IMPLEMENTED YET
// 	//maybe this should figure by tests, but so far i couldn't find
// 	//reasoning for this
// 	//zbx_determine_items_in_expressions(&trigger_order, itemids, item_num);
// 	LOG_INF("Trigger %ld: Calling evaluate expression", conf->triggerid);
// 	//evaluate_expressions(&trigger_order, history_itemids, history_items, history_errcodes);
// 	if ( FAIL == evaluate_trigger_expressions(conf, state, &ts)) {
// 		LOG_INF("Trigger %ld: Evaluate expression failed, finishing processing", conf->triggerid);
// 		return FAIL;
// 	}
// 	//zbx_process_triggers(&trigger_order, trigger_diff);
// 	/* FAST, NO IO, NO LOCKS, OK! */
// 	LOG_INF("Trigger %ld: Calling trigger changes processing", conf->triggerid);
// 	state_trigger_process_changes(conf, state, old_value, &ts);
// 	//process_trigger_changes(conf, state, old_value, &ts);
	
// 	//zbx_process_events(&trigger_diff, &triggerids);
// 	/* SLOW! HAS DB IO, MOVE OUT OF THE LOCK! */
// 	/* but... it needs the state object for the processing! */
// 	LOG_INF("Trigger %ld: Calling trigger events processing", conf->triggerid);
// 	//flush_trigger_events();
	
	 
// 	//if (old_state.value != state->value) {
// 	//	LOG_INF("Trigger %ld state change %d -> %d", conf->triggerid, old_state.state, state->state);
// 	//}

// }
int	recalculate_trigger(u_int64_t triggerid)
{
	LOG_INF("Recalculating trigger %ld", triggerid);
	DEBUG_TRIGGER(triggerid, "Calculating trigger")
	static trigger_conf_t conf = {0};

	trigger_state_t *state;
	zbx_timespec_t ts = {.sec = time(NULL), .ns = 0};
	//DB_EVENT *event = NULL;

	DEBUG_ITEM(conf.triggerid, "Freeing trigger config data");
	conf_trigger_free_trigger(&conf);

	/* there is no locking here, if needed, put locking fields to state or state flag */
	if (FAIL == conf_trigger_get_trigger_conf_data(triggerid, &conf)) {
		LOG_INF("Trigger %ld configuration fetch failed", triggerid);
		return FAIL;
	}
	
	if (NULL ==(state = state_trigger_get_state(triggerid))) {
		LOG_INF("State %ld configuration fetch failed", triggerid);
		return FAIL;
	}
	
	DEBUG_TRIGGER(triggerid, "Trigger preparing configuration");
	prepare_trigger_conf(&conf);

	unsigned char old_value = state_trigger_get_value(state);
	DEBUG_TRIGGER(triggerid, "Saved current trigger value: %d", old_value);

	DEBUG_TRIGGER(triggerid, "Checking trigger dependencies");
	
	if (SUCCEED != DCconfig_check_trigger_dependencies(triggerid)) {
	 	DEBUG_TRIGGER(triggerid,"Trigger depends on triggers in PROBLEM state, trigger cannot be calculated right now");
		 //todo: set new trigger state and error message
		return FAIL;
	}

	DEBUG_TRIGGER(triggerid, "Evaluating trigger expressions");
	if ( FAIL == evaluate_trigger_expressions(&conf, state, &ts)) {
		DEBUG_TRIGGER(conf.triggerid,"Evaluate expression failed, finishing processing");
		return FAIL;
	}

	DEBUG_TRIGGER(conf.triggerid, "Calling trigger changes processing");
	if (SUCCEED == state_trigger_is_changed(&conf, state, old_value)) {
	
		/*trigger calculated to a new value or it's has multiple problem gen */
		DEBUG_TRIGGER(conf.triggerid, "Trigger processing t, doing event processing");
	
		/*note: processing might change trigger state */
		process_trigger_change(&conf, state, old_value, &ts);
	
	} else {
		DEBUG_TRIGGER(conf.triggerid, "Trigger is not changed, nothing to process");
	}
	
	DEBUG_TRIGGER(conf.triggerid, "Saving new state");
	state_trigger_set_state(conf.triggerid, state, ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ALL);

	DEBUG_TRIGGER(conf.triggerid, "Finished trigger processing");
	
	return SUCCEED;
}

int process_trigger(u_int64_t triggerid) {

	LOG_INF("Processing trigger %ld", triggerid);
	if ( SUCCEED != glb_state_trigger_is_functional(triggerid) ) {
		LOG_INF("Trigger %ld is not functional, not calculated", triggerid);
	}
	DEBUG_TRIGGER(triggerid, "Starting processing");

/*
	if (FAIL == conf_trigger_get(tiggerid, &trigger) )
		return FAIL;
	
	prepare_trigger(&trigger);
	//this also finds functions and fills them
	zbx_determine_items_in_expressions(&trigger_order, itemids, item_num);
	

	if (0 != timers_num)
	{
		int	offset = trigger_order.values_num;

		zbx_dc_get_triggers_by_timers(&trigger_info, &trigger_order, timers);

		if (offset != trigger_order.values_num)
		{
			prepare_triggers((DC_TRIGGER **)trigger_order.values + offset,
					trigger_order.values_num - offset);
		}
	}

	zbx_vector_ptr_sort(&trigger_order, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	evaluate_expressions(&trigger_order, history_itemids, history_items, history_errcodes);
//LOG_INF("Calling zbx_process_triggers");
	zbx_process_triggers(&trigger_order, trigger_diff);
*/
	DEBUG_TRIGGER(triggerid, "Finished processing");

}

int process_metric_triggers(u_int64_t itemid) {
     
	static zbx_vector_uint64_t triggers = {.mem_free_func = ZBX_DEFAULT_MEM_FREE_FUNC, .mem_malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, 
	 	.mem_realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC, .values = NULL , .values_alloc = 0, .values_num = 0 };
	 
	int i;

	DEBUG_ITEM(itemid, "Processing triggers for the item");
	zbx_vector_uint64_clear(&triggers);
	
	if (SUCCEED == conf_index_items_to_triggers_get_triggers(itemid, &triggers)) {
	
	 	DEBUG_ITEM(itemid, "Got %d triggers for item %ld from the configuration", triggers.values_num, itemid);
	
	 	for (i = 0; i < triggers.values_num; i++) {
	 		DEBUG_ITEM(itemid, "Recalculating trigger %ld", triggers.values[i]);
			DEBUG_TRIGGER( triggers.values[i], "Recalculating trigger on new data from item %ld", itemid);
	 		recalculate_trigger(triggers.values[i]);
			DEBUG_ITEM(itemid, "Recalculating trigger %ld completed", triggers.values[i]);
			DEBUG_TRIGGER( triggers.values[i], "Recalculating trigger on new data from item %ld completed", itemid);
	 	}
	} else {
		LOG_INF("Couldn't get triggers form item %ld", itemid);
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}
	LOG_INF("Finished retrieving triggers");
	return SUCCEED;
}


