/*
** Glaber
** Copyright (C) 2018-2023
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
#include "zbxthreads.h"
#include "zbxalgo.h"
#include "zbxserver.h"
#include "glb_events_actions.h"
#include "zbxcacheconfig.h"
#include "glb_events_processor.h"
#include "glb_events_conditions_check.h"

struct glb_actions_t {
    zbx_vector_ptr_t	actions;
    zbx_hashset_t		uniq_conditions[EVENT_SOURCE_COUNT];
};

/******************************************************************************
 * Purpose: compare to find equal conditions                                  *
 ******************************************************************************/
static int	conditions_compare_func(const void *d1, const void *d2)
{
	const condition_t	*condition1 = (const condition_t *)d1, *condition2 = (const condition_t *)d2;
	int			ret;

	ZBX_RETURN_IF_NOT_EQUAL(condition1->conditiontype, condition2->conditiontype);
	ZBX_RETURN_IF_NOT_EQUAL(condition1->op, condition2->op);

	if (0 != (ret = strcmp(condition1->value, condition2->value)))
		return ret;

	if (0 != (ret = strcmp(condition1->value2, condition2->value2)))
		return ret;

	return 0;
}

/******************************************************************************
 * Purpose: generate hash based on condition values                           *
 ******************************************************************************/
static zbx_hash_t	conditions_hash_func(const void *data)
{
	const condition_t	*condition = (const condition_t *)data;
	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_STRING_HASH_ALGO(condition->value, strlen(condition->value), ZBX_DEFAULT_HASH_SEED);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(condition->value2, strlen(condition->value2), hash);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO((char *)&condition->conditiontype, 1, hash);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO((char *)&condition->op, 1, hash);

	return hash;
}

/*releases  heap members of the condition*/
static void condition_clean(condition_t *condition) {
    zbx_free(condition->value2);
	zbx_free(condition->value);
}

/*releases all conditions for all actions*/
static void	conditions_clean(glb_actions_t *actions)
{
	zbx_hashset_iter_t	iter;
    condition_t		*condition;
	int i;
    
    for (i =0 ; i < EVENT_SOURCE_COUNT; i++ ) {
        zbx_hashset_iter_reset(&actions->uniq_conditions[i], &iter);

	    while (NULL != (condition = (condition_t *)zbx_hashset_iter_next(&iter))) {
            condition_clean(condition);
            zbx_hashset_iter_remove(&iter);
        }
    }
}

static void	action_free_func(zbx_action_eval_t *action)
{
	zbx_free(action->formula);
	zbx_vector_ptr_destroy(&action->conditions);
    zbx_free(action);
}

/******************************************************************************
* Purpose: make actions to point to conditions from hashset, where all      *
 *          conditions are unique, this ensures that we don't double check    *
 *          same conditions.                                                  *
 ******************************************************************************/
static void	event_actions_make_unique_conditions(glb_actions_t *actions)
{
	int	i, j;

	for (i = 0; i < actions->actions.values_num; i++)
	{
		zbx_action_eval_t	*action = actions->actions.values[i];
   
		for (j = 0; j < action->conditions.values_num; j++)
		{
			condition_t	*uniq_condition = NULL, *condition = action->conditions.values[j];
        
			if (EVENT_SOURCE_COUNT <= action->eventsource)
			{
			    condition_clean(condition);
			}
			else if (NULL == (uniq_condition = zbx_hashset_search(&actions->uniq_conditions[action->eventsource],
					condition)))
			{
				uniq_condition = zbx_hashset_insert(&actions->uniq_conditions[action->eventsource],
						condition, sizeof(condition_t));
			}
			else
			{
              	if (ZBX_CONDITION_EVAL_TYPE_EXPRESSION == action->evaltype)
				{
					char	search[ZBX_MAX_UINT64_LEN + 2];
					char	replace[ZBX_MAX_UINT64_LEN + 2];
					char	*old_formula;

					zbx_snprintf(search, sizeof(search), "{" ZBX_FS_UI64 "}",
							condition->conditionid);
					zbx_snprintf(replace, sizeof(replace), "{" ZBX_FS_UI64 "}",
							uniq_condition->conditionid);

					old_formula = action->formula;
					action->formula = zbx_string_replace(action->formula, search, replace);
					zbx_free(old_formula);
				}
               
               	condition_clean(condition);
			}
            
			zbx_free(action->conditions.values[j]);
			action->conditions.values[j] = uniq_condition;
		}
	}
}

static void event_actions_clean(glb_actions_t *actions) {
  
    zbx_vector_ptr_clear_ext(&actions->actions, (zbx_clean_func_t)action_free_func);
    conditions_clean(actions);
}

static void event_actions_sync(glb_actions_t *actions) {
    zbx_dc_config_history_sync_get_actions_eval(&actions->actions);  //warn! this should go one after another
    event_actions_make_unique_conditions(actions);                    //update and clean methods are rely on structure produced after make_unique
}

void glb_actions_update(glb_actions_t *actions) {
    event_actions_clean(actions);
    event_actions_sync(actions);
}

glb_actions_t *glb_actions_create() {
    glb_actions_t *actions;
    int i;

    actions = zbx_malloc(NULL, sizeof(glb_actions_t));
    zbx_vector_ptr_create(&actions->actions);
    
    for (i = 0; i < EVENT_SOURCE_COUNT; i++)
		zbx_hashset_create(&actions->uniq_conditions[i], 0, conditions_hash_func, conditions_compare_func);

    return actions;
}

void glb_actions_destroy(glb_actions_t *actions) { 
    int i;
    
    event_actions_clean(actions);
    zbx_vector_ptr_destroy(&actions->actions);

   	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
		zbx_hashset_destroy(&actions->uniq_conditions[i]);
}

static void  event_actions_reset_conditions(events_processor_event_t *event, glb_actions_t *actions) {
	zbx_hashset_iter_t iter;
	condition_t		*condition;

	zbx_hashset_iter_reset(&actions->uniq_conditions[event->event_type], &iter);
	
	//calculating the conditions
	while (NULL != (condition = zbx_hashset_iter_next(&iter))) 
		condition->result = UNSET;
	
}

static int check_and_calc_action_conditions(events_processor_event_t *event, const zbx_action_eval_t *action)
{
	condition_t	*condition;
	int		ret = SUCCEED, id_len, i;
	unsigned char	old_type = 0xff;
	char		*expression = NULL, tmp[ZBX_MAX_UINT64_LEN + 2], *ptr, error[256];
	double		eval_result;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() actionid:" ZBX_FS_UI64 " eventsource:%d", __func__,
			action->actionid, (int)action->eventsource);

	if (ZBX_CONDITION_EVAL_TYPE_EXPRESSION == action->evaltype)
		expression = zbx_strdup(expression, action->formula);

	for (i = 0; i < action->conditions.values_num; i++)
	{
		if (action->eventsource != event->event_source) 
			continue; 

		condition = (condition_t *)action->conditions.values[i];

		if (ZBX_CONDITION_EVAL_TYPE_AND_OR == action->evaltype && 
				old_type == condition->conditiontype && SUCCEED == ret)
		{
			continue;	/* short-circuit true OR condition block to the next AND condition */
		}


		if (UNSET == condition->result) 
			glb_event_condition_calc(event, condition);
		
		switch (action->evaltype)
		{
			case ZBX_CONDITION_EVAL_TYPE_AND_OR:
				if (old_type == condition->conditiontype) /* assume conditions are sorted by type */
				{
					if (SUCCEED == condition->result)
						ret = SUCCEED;
				}
				else
				{
					if (FAIL == ret)
						goto clean;

					ret = condition->result;
					old_type = condition->conditiontype;
				}

				break;
			case ZBX_CONDITION_EVAL_TYPE_AND:
				if (FAIL == condition->result)	/* break if any AND condition is FALSE */
				{
					ret = FAIL;
					goto clean;
				}

				break;
			case ZBX_CONDITION_EVAL_TYPE_OR:
				if (SUCCEED == condition->result)	/* break if any OR condition is TRUE */
				{
					ret = SUCCEED;
					goto clean;
				}
				ret = FAIL;

				break;
			case ZBX_CONDITION_EVAL_TYPE_EXPRESSION:
				zbx_snprintf(tmp, sizeof(tmp), "{" ZBX_FS_UI64 "}", condition->conditionid);
				id_len = strlen(tmp);

				for (ptr = expression; NULL != (ptr = strstr(ptr, tmp)); ptr += id_len)
				{
					*ptr = (SUCCEED == condition->result ? '1' : '0');
					memset(ptr + 1, ' ', id_len - 1);
				}

				break;
			default:
				ret = FAIL;
				goto clean;
		}
	}

	if (ZBX_CONDITION_EVAL_TYPE_EXPRESSION == action->evaltype)
	{
		if (SUCCEED == zbx_evaluate(&eval_result, expression, error, sizeof(error), NULL))
			ret = (SUCCEED != zbx_double_compare(eval_result, 0) ? SUCCEED : FAIL);

		zbx_free(expression);
	}
clean:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));
	return ret;
}

static int event_actions_calc_escalate_actions(events_processor_event_t *event, glb_actions_t *actions) {
	int j=0;

	for (j = 0; j < actions->actions.values_num; j++)
		{
			zbx_action_eval_t	*action = (zbx_action_eval_t *)actions->actions.values[j];

			if (action->eventsource != event->event_type)
				continue;

			if (SUCCEED == check_and_calc_action_conditions(event, action))
			{
				//zbx_escalation_new_t	*new_escalation;
				/* command and message operations handled by escalators even for    */
				/* EVENT_SOURCE_DISCOVERY and EVENT_SOURCE_AUTOREGISTRATION events  */
				//new_escalation = (zbx_escalation_new_t *)zbx_malloc(NULL, sizeof(zbx_escalation_new_t));
				//new_escalation->actionid = action->actionid;
				//new_escalation->event = event;
				//zbx_vector_ptr_append(&new_escalations, new_escalation);

				//if (EVENT_SOURCE_DISCOVERY == event->source ||
				//		EVENT_SOURCE_AUTOREGISTRATION == event->source)
				//{
				//	execute_operations(event, action->actionid);
				//}
				HALT_HERE("This is where the escalation starts, not implemented yet");
			}
		}

}


void glb_actions_process_event(events_processor_event_t *event, glb_actions_t *actions) {

	event_actions_reset_conditions(event, actions);
	event_actions_calc_escalate_actions(event, actions);

}