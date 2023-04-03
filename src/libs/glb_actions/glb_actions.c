/*
** Copyright Glaber 2018-2023
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

/* actions are action-object specific, actions are created during some processing events
and so they shouldn't use DB as might be used on the data plane */
#include "zbxcommon.h"
#include "zbxalgo.h"
#include "log.h"
#include "glb_actions.h"
#include "zbxcacheconfig.h"

struct glb_actions_t {
   zbx_vector_ptr_t		actions;
};


// /*returns actions cache to process rules, rules are loaded to the heap*/
// glb_actions_t *glb_actions_load_rules(glb_actions_t *actions) {
//     if (NULL != actions) 
//       clear_actions(actions);
//     else 
//       actions = init_actions();
  	
//     zbx_dc_config_history_sync_get_actions_eval(&actions->actions, ZBX_ACTION_OPCLASS_NORMAL | ZBX_ACTION_OPCLASS_RECOVERY);
    
//     return actions;
// }

int glb_actions_process_discovery_host() {
  //  HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_lld_status() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_autoregister() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_new_problem(u_int64_t problemid) {
  
   HALT_HERE("Not implemented yet: %s", __func__ );

// 	int				i;
// 	
// 	zbx_vector_ptr_t 		new_escalations;
// 	zbx_vector_uint64_pair_t	rec_escalations;
// 	zbx_hashset_t			uniq_conditions[EVENT_SOURCE_COUNT];
// 	zbx_vector_ptr_t		esc_events[EVENT_SOURCE_COUNT];
// 	zbx_hashset_iter_t		iter;
// 	zbx_condition_t			*condition;
// 	zbx_dc_um_handle_t		*um_handle;

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() events_num:" ZBX_FS_SIZE_T, __func__, (zbx_fs_size_t)events->values_num);

// 	zbx_vector_ptr_create(&new_escalations);
// 	zbx_vector_uint64_pair_create(&rec_escalations);

// 	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
// 	{
// 		zbx_hashset_create(&uniq_conditions[i], 0, uniq_conditions_hash_func, uniq_conditions_compare_func);
// 		zbx_vector_ptr_create(&esc_events[i]);
// 	}

// 	zbx_vector_ptr_create(&actions);

//THIS MOST LIKELY GETS ALL THE ACTIONS CONFIGURED
 	//zbx_dc_config_history_sync_get_actions_eval(&actions, ZBX_ACTION_OPCLASS_NORMAL | ZBX_ACTION_OPCLASS_RECOVERY);
 	//prepare_actions_conditions_eval(&actions, uniq_conditions);

//FORMAL CHECK IF THE event HAS proper attributes to be able to create an event
 	//get_escalation_events(events, esc_events);


//OK, ITERATING ON ALL sames soucre actions conditions on each problem
// 	um_handle = zbx_dc_open_user_macros();

// 	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
// 	{
// 		if (0 == esc_events[i].values_num)
// 			continue;

// 		zbx_vector_ptr_sort(&esc_events[i], compare_events);

// 		zbx_hashset_iter_reset(&uniq_conditions[i], &iter);

// 		while (NULL != (condition = (zbx_condition_t *)zbx_hashset_iter_next(&iter)))
  
  //HERE IS A PROBLEM! EXTENSIVE SQL USAGE FOR CONDITIONS CHECK!
 		//	check_events_condition(&esc_events[i], i, condition);
// 	}

// 	zbx_dc_close_user_macros(um_handle);



// 	/* 1. All event sources: match PROBLEM events to action conditions, add them to 'new_escalations' list.      */
// 	/* 2. EVENT_SOURCE_DISCOVERY, EVENT_SOURCE_AUTOREGISTRATION: execute operations (except command and message  */
// 	/*    operations) for events that match action conditions.                                                   */
// 	for (i = 0; i < events->values_num; i++)
// 	{
// 		int		j;
// 		const ZBX_DB_EVENT	*event;

// 		if (FAIL == is_escalation_event((event = (const ZBX_DB_EVENT *)events->values[i])))
// 			continue;

// 		for (j = 0; j < actions.values_num; j++)
// 		{
// 			zbx_action_eval_t	*action = (zbx_action_eval_t *)actions.values[j];

// 			if (action->eventsource != event->source)
// 				continue;

// 			if (SUCCEED == check_action_conditions(event->eventid, action))
// 			{
// 				zbx_escalation_new_t	*new_escalation;
// 				/* command and message operations handled by escalators even for    */
// 				/* EVENT_SOURCE_DISCOVERY and EVENT_SOURCE_AUTOREGISTRATION events  */
// 				new_escalation = (zbx_escalation_new_t *)zbx_malloc(NULL, sizeof(zbx_escalation_new_t));
// 				new_escalation->actionid = action->actionid;
// 				new_escalation->event = event;
// 				zbx_vector_ptr_append(&new_escalations, new_escalation);

// 				if (EVENT_SOURCE_DISCOVERY == event->source ||
// 						EVENT_SOURCE_AUTOREGISTRATION == event->source)
// 				{
// 					execute_operations(event, action->actionid);
// 				}
// 			}
// 		}
// 	}

// 	for (i = 0; i < EVENT_SOURCE_COUNT; i++)
// 	{
// 		zbx_vector_ptr_destroy(&esc_events[i]);
// 		conditions_eval_clean(&uniq_conditions[i]);
// 		zbx_hashset_destroy(&uniq_conditions[i]);
// 	}

// 	zbx_vector_ptr_clear_ext(&actions, (zbx_clean_func_t)zbx_action_eval_free);
// 	zbx_vector_ptr_destroy(&actions);

	
// 	/* 3. Find recovered escalations and store escalationids in 'rec_escalation' by OK eventids. */
// 	if (0 != closed_events->values_num)
// 	{
// 		char			*sql = NULL;
// 		size_t			sql_alloc = 0, sql_offset = 0;
// 		zbx_vector_uint64_t	eventids;
// 		DB_ROW			row;
// 		DB_RESULT		result;
// 		int			j, index;

// 		zbx_vector_uint64_create(&eventids);

// 		/* 3.1. Store PROBLEM eventids of recovered events in 'eventids'. */
// 		for (j = 0; j < closed_events->values_num; j++)
// 			zbx_vector_uint64_append(&eventids, closed_events->values[j].first);

// 		/* 3.2. Select escalations that must be recovered. */
// 		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// 		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
// 				"select eventid,escalationid"
// 				" from escalations"
// 				" where");

// 		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
// 		result = DBselect("%s", sql);

// 		zbx_vector_uint64_pair_reserve(&rec_escalations, eventids.values_num);

// 		/* 3.3. Store the escalationids corresponding to the OK events in 'rec_escalations'. */
// 		while (NULL != (row = DBfetch(result)))
// 		{
// 			zbx_uint64_pair_t	pair;

// 			ZBX_STR2UINT64(pair.first, row[0]);

// 			if (FAIL == (index = zbx_vector_uint64_pair_bsearch(closed_events, pair,
// 					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
// 			{
// 				THIS_SHOULD_NEVER_HAPPEN;
// 				continue;
// 			}

// 			pair.second = closed_events->values[index].second;
// 			ZBX_DBROW2UINT64(pair.first, row[1]);
// 			zbx_vector_uint64_pair_append(&rec_escalations, pair);
// 		}

// 		zbx_db_free_result(result);
// 		zbx_free(sql);
// 		zbx_vector_uint64_destroy(&eventids);
// 	}
	
// 	/* 4. Create new escalations in DB. */
// 	if (0 != new_escalations.values_num)
// 	{
	
// 		zbx_db_insert_t	db_insert;
// 		int		j;

// 		zbx_db_insert_prepare(&db_insert, "escalations", "escalationid", "actionid", "status", "triggerid",
// 					"itemid", "eventid", "r_eventid", "acknowledgeid", NULL);

// 		for (j = 0; j < new_escalations.values_num; j++)
// 		{
// 			zbx_uint64_t		triggerid = 0, itemid = 0;
// 			zbx_escalation_new_t	*new_escalation;

// 			new_escalation = (zbx_escalation_new_t *)new_escalations.values[j];

// 			switch (new_escalation->event->object)
// 			{
// 				case EVENT_OBJECT_TRIGGER:
// 					triggerid = new_escalation->event->objectid;
// 					DEBUG_TRIGGER(triggerid, "Adding new escalation")
// 					break;
// 				case EVENT_OBJECT_ITEM:
// 				case EVENT_OBJECT_LLDRULE:
// 					itemid = new_escalation->event->objectid;
// 					break;
// 			}

// 			zbx_db_insert_add_values(&db_insert, __UINT64_C(0), new_escalation->actionid,
// 					(int)ESCALATION_STATUS_ACTIVE, triggerid, itemid,
// 					new_escalation->event->eventid, __UINT64_C(0), __UINT64_C(0));

// 			zbx_free(new_escalation);
// 		}

// 		zbx_db_insert_autoincrement(&db_insert, "escalationid");
// 		zbx_db_insert_execute(&db_insert);
// 		zbx_db_insert_clean(&db_insert);
// 	}
	
// 	/* 5. Modify recovered escalations in DB. */
// 	if (0 != rec_escalations.values_num)
// 	{
// 		char	*sql = NULL;
// 		size_t	sql_alloc = 0, sql_offset = 0;
// 		int	j;

// 		zbx_vector_uint64_pair_sort(&rec_escalations, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

// 		zbx_DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

// 		for (j = 0; j < rec_escalations.values_num; j++)
// 		{
// 			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
// 					"update escalations set r_eventid=" ZBX_FS_UI64 ",nextcheck=0"
// 					" where escalationid=" ZBX_FS_UI64 ";\n",
// 					rec_escalations.values[j].second, rec_escalations.values[j].first);

// 			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
// 		}

// 		zbx_DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

// 		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
// 			DBexecute("%s", sql);

// 		zbx_free(sql);
// 	}

// 	zbx_vector_uint64_pair_destroy(&rec_escalations);
// 	zbx_vector_ptr_destroy(&new_escalations);

// 	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
//

}
int glb_actions_process_problem_recovery() {
//    HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_by_acknowledgments() {
    //HALT_HERE("Not implemented yet: %s", __func__ );
}
int glb_actions_process_discovery_service() {
//     HALT_HERE("Not implemented yet: %s", __func__ );
}
