
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
#include "glb_state_triggers.h"
#include "glb_state_problems.h"
#include "zbxcacheconfig.h"
//TODO: move trigger constatnts to GLB_CODE to ease sources upgrade
#include "zbx_trigger_constants.h"
#include "../glb_events_log/glb_events_log.h"
#include "zbxcacheconfig.h"


//TODO: move DC_TRIGGER here, make it closed, make the neccessary methods for accessing the fields

static void trigger_check_dependency(DC_TRIGGER *trigger) {
	if (FAIL == glb_check_trigger_has_value_ok_masters(trigger->triggerid)) {
		zbx_free(trigger->new_error);
		trigger->new_error = zbx_strdup(NULL,"There are master trigger(s) in PROBLEM or UNKNOWN state");
		trigger->value = TRIGGER_VALUE_UNKNOWN;
	}
}

static int trigger_error_changed(DC_TRIGGER *trigger) {
	if (NULL == trigger->error && NULL != trigger->new_error)
		return SUCCEED;
	if (NULL == trigger->new_error && NULL != trigger->error)
		return SUCCEED;
	
	return strcmp(trigger->error, trigger->new_error);
}

static void trigger_write_events(DC_TRIGGER *trigger) {

	if (trigger->value != trigger->new_value) 
		write_event_log_attr_int_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "value", trigger->value, trigger->new_value );

	if (trigger_error_changed(trigger))
		write_event_log_attr_str_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "error", trigger->error, trigger->new_error );
}

int glb_trigger_register_calculation(DC_TRIGGER *trigger) {
	//LOG_INF("Handling trigger recalculation");	
	trigger_check_dependency(trigger);
	trigger_write_events(trigger);
	glb_state_problems_process_trigger_value(trigger);

}


//glb_state_triggers_apply_diffs(&trigger_diff);
// int glb_trigger_recalculate(u_int64_t triggerid) {
// 	char * error = NULL;
// 	unsigned char new_value;
// 	LOG_INF("Checking dependencies");
// 	if (FAIL == glb_trigger_dependancies_operational(triggerid, error)) {
// 		new_value = TRIGGER_VALUE_UNKNOWN;
// 	}
// }




// void	zbx_evaluate_expressions(zbx_vector_ptr_t *triggers, const zbx_vector_uint64_t *history_itemids,
// 		const zbx_history_sync_item_t *history_items, const int *history_errcodes)
// {
// 	ZBX_DB_EVENT		event;
// 	DC_TRIGGER		*tr;
// 	zbx_history_sync_item_t	*items = NULL;
// 	int			i, *items_err, items_num = 0;
// 	double			expr_result;
// 	zbx_dc_um_handle_t	*um_handle;
// 	zbx_vector_uint64_t	hostids;

// 	LOG_DBG("In %s() tr_num:%d", __func__, triggers->values_num);

// 	event.object = EVENT_OBJECT_TRIGGER;

// 	zbx_vector_uint64_create(&hostids);
// 	um_handle = zbx_dc_open_user_macros();

// 	//
// 	HALT_HERE("it's better to leave only expression-specific functions ");
// 	//will substitute expressions to funcs ids
// 	substitute_functions(triggers, history_itemids, history_items, history_errcodes, &items, &items_err,
// 			&items_num);

// 	//below:
// 	fetch_trigger_hostids();
// 	/*so, when we calculate a trigger, we need it's hostids for macro processing
// 	and for proper host->problem indexing in problems
// 	there are two sources the hostid could be retrieved:
// 	1. item data that is already fetched for the trigger's items
// 	2. for all other items in the trigger we need to fetch hostids from the configuration
// 	*/


// 	for (i = 0; i < triggers->values_num; i++)
// 	{
// 		char	*error = NULL;
// 		int	j, k;

// 		tr = (DC_TRIGGER *)triggers->values[i];
// 		DEBUG_TRIGGER(tr->triggerid,"Evaluating trigger expressions");

// 		for (j = 0; j < tr->itemids.values_num; j++)
// 		{
// 			if (FAIL != (k = zbx_vector_uint64_bsearch(history_itemids, tr->itemids.values[j],
// 					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
// 			{
// 				if (SUCCEED != history_errcodes[k])
// 					continue;
// 				zbx_vector_uint64_append(&hostids, history_items[k].host.hostid);
// 			}
// 			else
// 			{
// 				zbx_history_sync_item_t	*item;

// 				item = (zbx_history_sync_item_t *)bsearch(&tr->itemids.values[j], items,
// 						(size_t)items_num, sizeof(zbx_history_sync_item_t),
// 						dc_item_compare_by_itemid);

// 				if (NULL == item || SUCCEED != items_err[item - items])
// 					continue;

// 				zbx_vector_uint64_append(&hostids, item->host.hostid);
// 			}
// 		}

// 		zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// 		zbx_vector_uint64_uniq(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

// 		/* one more comment here: for quite a lot of situation there is no need 
// 		to expand recovery expression (if trigger expr == PROBLEM or trigger_val == OK)
// 		there is no need to recover 
		
// 		another question: if a host item mentioned in the recovery - should we
// 			consider the host as the host's trigger? 
// 			For start, i believe, no, we shoudln't do it. It's better to think this
// 				as the trigger works on host A, trigger recovers based on information from the
// 				host B. So, this is a trigger for host A.

// 		overall structure and logic: 
// 			1. remove awareness of trigger operations from expression
// 			2. expresstion must be able to a) handle, translate expressions
// 			b) calculate them to either OK, PROBLEM, UNKNOWN+error values

// 		all logic about the trigger should be in the trigger code 
		
				
// 		*/

// 		if (SUCCEED != expand_trigger_macros(tr, &event, um_handle, &hostids, &error))
// 		{
// 			tr->new_error = zbx_dsprintf(tr->new_error, "Couldn't expand macro: %s", error);
// 			tr->new_value = TRIGGER_VALUE_UNKNOWN;
// 			DEBUG_TRIGGER(tr->triggerid, "Couldn't expand trigger macro, set to UNKNOWN value :%s", error);
// 			zbx_free(error);
// 		}

// 		zbx_vector_uint64_clear(&hostids);
// 	}

// 	zbx_dc_close_user_macros(um_handle);
	
// 	HALT_HERE("This is the place where new trigger value is best to calculate and start the problems creation");
// 	/*put the trigger calc code to the glb_trigger.c lib so all the trigger's behaviour 
// 	logic would be there
// 	*/

// 	zbx_vector_uint64_destroy(&hostids);

// 	if (0 != items_num)
// 	{
// 		zbx_dc_config_clean_history_sync_items(items, items_err, (size_t)items_num);
// 		zbx_free(items);
// 		zbx_free(items_err);
// 	}

// 	//here is the not only the calc, but processing logic
// 	/* calculate new trigger values based on their recovery modes and expression evaluations */
// 	for (i = 0; i < triggers->values_num; i++)
// 	{
// 		tr = (DC_TRIGGER *)triggers->values[i];

// 		if (NULL != tr->new_error)
// 			continue;

// 		if (SUCCEED != evaluate_expression(tr->triggerid, tr->eval_ctx, &tr->timespec, &expr_result, &tr->new_error)) {
// 			LOG_DBG("Failed to eval %ld trigger expression",tr->triggerid);
// 			DEBUG_TRIGGER(tr->triggerid,"Failed to eval expression: %s", tr->new_error);
// 			tr->new_value = TRIGGER_VALUE_UNKNOWN;
// 			continue;
// 		}
		
// 		tr->new_value = tr->value;
		
// 		/* trigger expression evaluates to true, set PROBLEM value */
// 		if (SUCCEED != zbx_double_compare(expr_result, 0.0))
// 		{
// 			DEBUG_TRIGGER(tr->triggerid, "Trigger expression calculated to non-zero (PROBLEM)");
			
// 			/* trigger value should remain unchanged and no PROBLEM events should be generated if */
// 			/* problem expression evaluates to true, but trigger recalculation was initiated by a */
// 			/* time-based function or a new value of an item in recovery expression */
// 		//	if (0 != (tr->flags & ZBX_DC_TRIGGER_PROBLEM_EXPRESSION)) 
// 			tr->new_value = TRIGGER_VALUE_PROBLEM;
			
// 			continue;
// 		}

// 		/* OK value */
// 		if (TRIGGER_RECOVERY_MODE_NONE == tr->recovery_mode)
// 			continue;

// 		if (TRIGGER_RECOVERY_MODE_EXPRESSION == tr->recovery_mode)
// 		{
// 			tr->new_value = TRIGGER_VALUE_OK;
// 			continue;
// 		}

// 		/* processing recovery expression mode */
// 		if (SUCCEED != evaluate_expression(tr->triggerid, tr->eval_ctx_r, &tr->timespec, &expr_result, &tr->new_error))
// 		{
// 			tr->new_value = TRIGGER_VALUE_UNKNOWN;
// 			continue;
// 		}

// 		if (SUCCEED != zbx_double_compare(expr_result, 0.0))
// 		{
// 			tr->new_value = TRIGGER_VALUE_OK;
// 			continue;
// 		}
// 	}
// 	HALT_HERE("This is the place a trigger gots it's value, it could be processed right here");
	
// 	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
// 	{
// 		for (i = 0; i < triggers->values_num; i++)
// 		{
// 			tr = (DC_TRIGGER *)triggers->values[i];

// 			if (NULL != tr->new_error)
// 			{
// 				zabbix_log(LOG_LEVEL_DEBUG, "%s():expression [%s] cannot be evaluated: %s",
// 						__func__, tr->expression, tr->new_error);
// 			}
// 		}

// 		LOG_INF( "End of %s()", __func__);
// 	}
// }




// }