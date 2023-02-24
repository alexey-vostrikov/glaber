/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "zbxdbwrap.h"

#include "zbxnum.h"
#include "zbxexpr.h"
#include "../../libs/glb_state/glb_state_triggers.h"

/******************************************************************************
 *                                                                            *
 * Purpose: get events and flags that indicate what was filled in             *
 *           ZBX_DB_EVENT structure                                           *
 *                                                                            *
 * Parameters: eventids   - [IN] requested event ids                          *
 *             events     - [OUT] the array of events                         *
 *                                                                            *
 * Comments: use 'zbx_db_free_event' function to release allocated memory     *
 *                                                                            *
 ******************************************************************************/
// void	zbx_db_get_events_by_eventids(zbx_vector_uint64_t *eventids, zbx_vector_ptr_t *events)
// {
// 	DB_RESULT		result;
// 	DB_ROW			row;
// 	char			*sql = NULL;
// 	size_t			sql_alloc = 0, sql_offset = 0;
// 	zbx_vector_uint64_t	tagged_eventids, triggerids;
// 	int			i, index;

// 	zbx_vector_uint64_create(&tagged_eventids);
// 	zbx_vector_uint64_create(&triggerids);

// 	zbx_vector_uint64_sort(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// 	zbx_vector_uint64_uniq(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

// 	/* read event data */

// 	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
// 			"select eventid,source,object,objectid,clock,value,acknowledged,ns,name,severity"
// 			" from events"
// 			" where");
// 	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);
// 	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by eventid");

// 	result = DBselect("%s", sql);

// 	while (NULL != (row = DBfetch(result)))
// 	{
// 		ZBX_DB_EVENT	*event = NULL;

// 		event = (ZBX_DB_EVENT *)zbx_malloc(event, sizeof(ZBX_DB_EVENT));
// 		ZBX_STR2UINT64(event->eventid, row[0]);
// 		event->source = atoi(row[1]);
// 		event->object = atoi(row[2]);
// 		ZBX_STR2UINT64(event->objectid, row[3]);
// 		event->clock = atoi(row[4]);
// 		event->value = atoi(row[5]);
// 		event->acknowledged = atoi(row[6]);
// 		event->ns = atoi(row[7]);
// 		event->name = zbx_strdup(NULL, row[8]);
// 		event->severity = atoi(row[9]);
// 		event->suppressed = ZBX_PROBLEM_SUPPRESSED_FALSE;

// 		event->trigger.triggerid = 0;

// 		if (EVENT_SOURCE_TRIGGERS == event->source || EVENT_SOURCE_INTERNAL == event->source ||
// 				EVENT_SOURCE_SERVICE == event->source)
// 		{
// 			zbx_vector_ptr_create(&event->tags);
// 			zbx_vector_uint64_append(&tagged_eventids, event->eventid);
// 		}

// 		if (EVENT_OBJECT_TRIGGER == event->object)
// 			zbx_vector_uint64_append(&triggerids, event->objectid);

// 		zbx_vector_ptr_append(events, event);
// 	}
// 	zbx_db_free_result(result);

// 	zbx_vector_ptr_sort(events, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

// 	/* read event_suppress data */

// 	sql_offset = 0;
// 	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct eventid from event_suppress where");
// 	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);

// 	result = DBselect("%s", sql);

// 	while (NULL != (row = DBfetch(result)))
// 	{
// 		ZBX_DB_EVENT	*event;
// 		zbx_uint64_t	eventid;

// 		ZBX_STR2UINT64(eventid, row[0]);
// 		if (FAIL == (index = zbx_vector_ptr_bsearch(events, &eventid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
// 		{
// 			THIS_SHOULD_NEVER_HAPPEN;
// 			continue;
// 		}

// 		event = (ZBX_DB_EVENT *)events->values[index];
// 		event->suppressed = ZBX_PROBLEM_SUPPRESSED_TRUE;
// 	}
// 	zbx_db_free_result(result);

// 	/* EVENT_SOURCE_TRIGGERS || EVENT_SOURCE_INTERNAL || EVENT_SOURCE_SERVICE */
// 	if (0 != tagged_eventids.values_num)
// 	{
// 		ZBX_DB_EVENT	*event = NULL;

// 		sql_offset = 0;
// 		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", tagged_eventids.values,
// 				tagged_eventids.values_num);

// 		result = DBselect("select eventid,tag,value from event_tag where%s order by eventid", sql);

// 		while (NULL != (row = DBfetch(result)))
// 		{
// 			zbx_uint64_t	eventid;
// 			zbx_tag_t	*tag;

// 			ZBX_STR2UINT64(eventid, row[0]);

// 			if (NULL == event || eventid != event->eventid)
// 			{
// 				if (FAIL == (index = zbx_vector_ptr_bsearch(events, &eventid,
// 						ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
// 				{
// 					THIS_SHOULD_NEVER_HAPPEN;
// 					continue;
// 				}

// 				event = (ZBX_DB_EVENT *)events->values[index];
// 			}

// 			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
// 			tag->tag = zbx_strdup(NULL, row[1]);
// 			tag->value = zbx_strdup(NULL, row[2]);
// 			zbx_vector_ptr_append(&event->tags, tag);
// 		}
// 		zbx_db_free_result(result);
// 	}

// 	if (0 != triggerids.values_num)	/* EVENT_OBJECT_TRIGGER */
// 	{
// 		zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// 		zbx_vector_uint64_uniq(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

// 		sql_offset = 0;
// 		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids.values,
// 				triggerids.values_num);

// 		result = DBselect(
// 				"select triggerid,description,expression,priority,comments,url,url_name,recovery_expression,"
// 					"recovery_mode,null,opdata,event_name"
// 				" from triggers"
// 				" where%s",
// 				sql);

// 		while (NULL != (row = DBfetch(result)))
// 		{
// 			zbx_uint64_t	triggerid;

// 			ZBX_STR2UINT64(triggerid, row[0]);

// 			for (i = 0; i < events->values_num; i++)
// 			{
// 				ZBX_DB_EVENT	*event = (ZBX_DB_EVENT *)events->values[i];

// 				if (EVENT_OBJECT_TRIGGER != event->object)
// 					continue;

// 				if (triggerid == event->objectid)
// 				{
// 					event->trigger.triggerid = triggerid;
// 					event->trigger.description = zbx_strdup(NULL, row[1]);
// 					event->trigger.expression = zbx_strdup(NULL, row[2]);
// 					ZBX_STR2UCHAR(event->trigger.priority, row[3]);
// 					event->trigger.comments = zbx_strdup(NULL, row[4]);
// 					event->trigger.url = zbx_strdup(NULL, row[5]);
// 					event->trigger.url_name = zbx_strdup(NULL, row[6]);
// 					event->trigger.recovery_expression = zbx_strdup(NULL, row[7]);
// 					ZBX_STR2UCHAR(event->trigger.recovery_mode, row[8]);
// 					event->trigger.value = glb_state_trigger_get_value(triggerid);
// 					event->trigger.opdata = zbx_strdup(NULL, row[10]);
// 					event->trigger.event_name = ('\0' != *row[11] ? zbx_strdup(NULL, row[11]) :
// 							NULL);
// 					event->trigger.cache = NULL;
// 				}
// 			}
// 		}
// 		zbx_db_free_result(result);
// 	}

// 	zbx_free(sql);

// 	zbx_vector_uint64_destroy(&tagged_eventids);
// 	zbx_vector_uint64_destroy(&triggerids);
// }

/******************************************************************************
 *                                                                            *
 * Purpose: free the event with its resources                                 *
 *                                                                            *
 * Parameters: event - [IN] event data                                        *
 *                                                                            *
 ******************************************************************************/
// void	zbx_db_free_event(ZBX_DB_EVENT *event)
// {
// 	if (EVENT_SOURCE_TRIGGERS == event->source || EVENT_SOURCE_INTERNAL == event->source ||
// 			EVENT_SOURCE_SERVICE == event->source)
// 	{
// 		zbx_vector_ptr_clear_ext(&event->tags, (zbx_clean_func_t)zbx_free_tag);
// 		zbx_vector_ptr_destroy(&event->tags);
// 	}

// 	if (0 != event->trigger.triggerid)
// 		zbx_db_trigger_clean(&event->trigger);

// 	zbx_free(event->name);
// 	zbx_free(event);
// }

// /******************************************************************************
//  *                                                                            *
//  * Purpose: get recovery event IDs by event IDs then map them together also   *
//  *          additional create a separate array of recovery event IDs          *
//  *                                                                            *
//  * Parameters: eventids    - [IN] requested event IDs                         *
//  *             event_pairs - [OUT] the array of event ID and recovery event   *
//  *                                 pairs                                      *
//  *             r_eventids  - [OUT] array of recovery event IDs                *
//  *                                                                            *
//  ******************************************************************************/
// void	zbx_db_get_eventid_r_eventid_pairs(zbx_vector_uint64_t *eventids, zbx_vector_uint64_pair_t *event_pairs,
// 		zbx_vector_uint64_t *r_eventids)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;
// 	char		*filter = NULL;
// 	size_t		filter_alloc = 0, filter_offset = 0;

// 	DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, "eventid", eventids->values,
// 			eventids->values_num);

// 	result = DBselect("select eventid,r_eventid"
// 			" from event_recovery"
// 			" where%s order by eventid",
// 			filter);

// 	while (NULL != (row = DBfetch(result)))
// 	{
// 		zbx_uint64_pair_t	r_event;

// 		ZBX_STR2UINT64(r_event.first, row[0]);
// 		ZBX_STR2UINT64(r_event.second, row[1]);

// 		zbx_vector_uint64_pair_append(event_pairs, r_event);
// 		zbx_vector_uint64_append(r_eventids, r_event.second);
// 	}
// 	zbx_db_free_result(result);

// 	zbx_free(filter);
// }

// /******************************************************************************
//  *                                                                            *
//  * Purpose: allocate memory for event                                         *
//  *                                                                            *
//  * Parameters: eventid   - [IN] requested event id                            *
//  *             event     - [OUT]                                              *
//  *                                                                            *
//  * Comments: use 'zbx_db_free_event' function to release allocated memory     *
//  *                                                                            *
//  ******************************************************************************/
// void	zbx_db_prepare_empty_event(zbx_uint64_t eventid, ZBX_DB_EVENT **event)
// {
// 	ZBX_DB_EVENT	*evt = NULL;

// 	evt = (ZBX_DB_EVENT*)zbx_malloc(evt, sizeof(ZBX_DB_EVENT));
// 	evt->eventid = eventid;
// 	evt->name = NULL;
// 	zbx_vector_ptr_create(&evt->tags);

// 	evt->source = EVENT_SOURCE_TRIGGERS;
// 	memset(&evt->trigger, 0, sizeof(ZBX_DB_TRIGGER));

// 	evt->flags = ZBX_FLAGS_DB_EVENT_UNSET;

// 	*event = evt;
// }

// /******************************************************************************
//  *                                                                            *
//  * Purpose: get event data from events table, if it's not obtained already    *
//  *                                                                            *
//  * Parameters: event     - [IN/OUT]                                           *
//  *                                                                            *
//  ******************************************************************************/
// void	zbx_db_get_event_data_core(ZBX_DB_EVENT *event)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;

// 	if (0 != (ZBX_FLAGS_DB_EVENT_RETRIEVED_CORE & event->flags))
// 		return;

// 	result = DBselect("select source,object,objectid,clock,value,acknowledged,ns,name,severity"
// 			" from events"
// 			" where eventid=" ZBX_FS_UI64, event->eventid);

// 	if (NULL != (row = DBfetch(result)))
// 	{
// 		event->source = atoi(row[0]);
// 		event->object = atoi(row[1]);
// 		ZBX_STR2UINT64(event->objectid, row[2]);
// 		event->clock = atoi(row[3]);
// 		event->value = atoi(row[4]);
// 		event->acknowledged = atoi(row[5]);
// 		event->ns = atoi(row[6]);
// 		event->name = zbx_strdup(NULL, row[7]);
// 		event->severity = atoi(row[8]);
// 		event->suppressed = ZBX_PROBLEM_SUPPRESSED_FALSE;

// 		event->flags |= ZBX_FLAGS_DB_EVENT_RETRIEVED_CORE;
// 	}
// 	zbx_db_free_result(result);
// }

/******************************************************************************
 *                                                                            *
 * Purpose: get event tag data from event_tag table, if it's not obtained     *
 *          already                                                           *
 *                                                                            *
 * Parameters: event   - [IN/OUT] event data                                  *
 *                                                                            *
 ******************************************************************************/
// void	zbx_db_get_event_data_tags(ZBX_DB_EVENT *event)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;

// 	if (0 != (ZBX_FLAGS_DB_EVENT_RETRIEVED_TAGS & event->flags) || (EVENT_SOURCE_TRIGGERS != event->source &&
// 			EVENT_SOURCE_INTERNAL != event->source && EVENT_SOURCE_SERVICE != event->source))
// 	{
// 		return;
// 	}

// 	result = DBselect("select tag,value from event_tag where eventid=" ZBX_FS_UI64, event->eventid);

// 	while (NULL != (row = DBfetch(result)))
// 	{
// 		zbx_tag_t	*tag;

// 		tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
// 		tag->tag = zbx_strdup(NULL, row[0]);
// 		tag->value = zbx_strdup(NULL, row[1]);
// 		zbx_vector_ptr_append(&event->tags, tag);
// 	}
// 	zbx_db_free_result(result);

// 	if (0 != event->tags.values_num)
// 		event->flags |= ZBX_FLAGS_DB_EVENT_RETRIEVED_TAGS;
// }

/******************************************************************************
 *                                                                            *
 * Purpose: get event trigger data from triggers table, if it's not obtained  *
 *          already                                                           *
 *                                                                            *
 * Parameters: event   - [IN/OUT] event data                                  *
 *                                                                            *
 ******************************************************************************/
// void	zbx_db_get_event_data_triggers(ZBX_DB_EVENT *event)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;

// 	if (0 != (ZBX_FLAGS_DB_EVENT_RETRIEVED_TRIGGERS & event->flags) || EVENT_OBJECT_TRIGGER != event->object)
// 		return;

// 	result = DBselect("select description,expression,priority,comments,url,url_name,recovery_expression,"
// 			"recovery_mode,value,opdata,event_name"
// 			" from triggers"
// 			" where triggerid=" ZBX_FS_UI64, event->objectid);

// 	if (NULL != (row = DBfetch(result)))
// 	{
// 		event->trigger.triggerid = event->objectid;
// 		event->trigger.description = zbx_strdup(NULL, row[0]);
// 		event->trigger.expression = zbx_strdup(NULL, row[1]);
// 		ZBX_STR2UCHAR(event->trigger.priority, row[2]);
// 		event->trigger.comments = zbx_strdup(NULL, row[3]);
// 		event->trigger.url = zbx_strdup(NULL, row[4]);
// 		event->trigger.url_name = zbx_strdup(NULL, row[5]);
// 		event->trigger.recovery_expression = zbx_strdup(NULL, row[6]);
// 		ZBX_STR2UCHAR(event->trigger.recovery_mode, row[7]);
// 		ZBX_STR2UCHAR(event->trigger.value, row[8]);
// 		event->trigger.opdata = zbx_strdup(NULL, row[9]);
// 		event->trigger.event_name = ('\0' != *row[10] ? zbx_strdup(NULL, row[10]) : NULL);
// 		event->trigger.cache = NULL;

// 		event->flags |= ZBX_FLAGS_DB_EVENT_RETRIEVED_TRIGGERS;
// 	}
// 	zbx_db_free_result(result);
// }

/******************************************************************************
 *                                                                            *
 * Purpose: select symptom event IDs                                          *
 *                                                                            *
 * Parameters: eventids          - [IN] events to be evaluated                *
 *             symptom_eventids  - [OUT] array of symptom event IDs           *
 *                                                                            *
 ******************************************************************************/
// void	zbx_db_select_symptom_eventids(zbx_vector_uint64_t *eventids, zbx_vector_uint64_t *symptom_eventids)
// {
// 	char		*sql = NULL;
// 	size_t		sql_alloc = 0, sql_offset = 0;
// 	zbx_uint64_t	s_eventid;
// 	DB_RESULT	result;
// 	DB_ROW		row;

// 	zbx_vector_uint64_sort(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// 	zbx_vector_uint64_uniq(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

// 	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values,
// 			eventids->values_num);

// 	result = DBselect("select eventid from event_symptom where%s", sql);

// 	while (NULL != (row = DBfetch(result)))
// 	{
// 		ZBX_STR2UINT64(s_eventid, row[0]);
// 		zbx_vector_uint64_append(symptom_eventids, s_eventid);
// 	}
// 	zbx_db_free_result(result);

// 	zbx_free(sql);
// }

/******************************************************************************
 *                                                                            *
 * Purpose: get cause event id                                                *
 *                                                                            *
 * Parameters: eventid   - [IN] event id of the symptom                       *
 *                                                                            *
 * Return value: cause event id, or '0' if no cause event id found            *
 *                                                                            *
 ******************************************************************************/
// zbx_uint64_t	zbx_db_get_cause_eventid(zbx_uint64_t eventid)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;
// 	zbx_uint64_t	cause_eventid;

// 	result = DBselect("select cause_eventid from event_symptom where eventid=" ZBX_FS_UI64, eventid);

// 	if (NULL != (row = DBfetch(result)))
// 		ZBX_STR2UINT64(cause_eventid, row[0]);
// 	else
// 		cause_eventid = 0;

// 	zbx_db_free_result(result);

// 	return cause_eventid;
// }

/******************************************************************************
 *                                                                            *
 * Purpose: get id of the object, which generated this event                  *
 *                                                                            *
 * Parameters: eventid - [IN]                                                 *
 *                                                                            *
 * Return value: object id, or '0' if object no object id found               *
 *                                                                            *
 ******************************************************************************/
// zbx_uint64_t	zbx_get_objectid_by_eventid(zbx_uint64_t eventid)
// {
// 	DB_RESULT	result;
// 	DB_ROW		row;
// 	zbx_uint64_t	objectid;

// 	result = DBselect("select objectid from events where eventid=" ZBX_FS_UI64, eventid);

// 	if (NULL != (row = DBfetch(result)))
// 		ZBX_STR2UINT64(objectid, row[0]);
// 	else
// 		objectid = 0;

// 	zbx_db_free_result(result);

// 	return objectid;
// }
