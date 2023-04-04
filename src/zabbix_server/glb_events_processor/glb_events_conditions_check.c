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
#include "glb_events_processor.h"
#include "glb_events_actions.h"
#include "zbxdbhigh.h"
#include "zbxexpr.h"
#include "../../libs/glb_state/glb_state_problems.h"
#include "../../libs/glb_macro/glb_macro.h"
#include "../../libs/glb_conf/tags/tags.h"

static void check_problem_belongs_to_template(u_int64_t problemid, condition_t *condition) {
	u_int64_t templateid;
	zbx_vector_uint64_t	problem_hostids, template_ids;
	int i, ret;

	ZBX_STR2UINT64(templateid, condition->value);
	
	zbx_vector_uint64_create(&problem_hostids);
	
	if ( (ZBX_CONDITION_OPERATOR_EQUAL != condition->op && ZBX_CONDITION_OPERATOR_NOT_EQUAL != condition->op) ||
		 FAIL == glb_state_problems_get_hostids(problemid, &problem_hostids)) 
	{
		zbx_vector_uint64_destroy(&problem_hostids);
		condition->result = NOTSUPPORTED;
		return;	
	}

	zbx_vector_uint64_create(&template_ids);
	zbx_dc_get_hosts_templateids(&problem_hostids, &template_ids);

	zbx_vector_uint64_uniq(&template_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(&template_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	ret = FAIL;

	for (i = 0; i < problem_hostids.values_num && FAIL == ret; i++ ) 
		ret = zbx_vector_uint64_bsearch(&template_ids, problem_hostids.values[i], ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	if ((FAIL == ret && ZBX_CONDITION_OPERATOR_NOT_EQUAL == condition->op) || 
		(SUCCEED ==ret && ZBX_CONDITION_OPERATOR_EQUAL == condition->op) 
	)
		condition->result = SUCCEED;
	else 
		condition->result = FAIL;		

	zbx_vector_uint64_destroy(&template_ids);
	zbx_vector_uint64_destroy(&problem_hostids);
}

static void check_problem_belongs_to_host_group(u_int64_t problemid, condition_t *condition) {
	int i, ret;

	zbx_vector_uint64_t	problem_hostids, gr_hostids;
	zbx_uint64_t		group_id;

	ZBX_STR2UINT64(group_id, condition->value);

	zbx_vector_uint64_create(&problem_hostids);
	
	if ( (ZBX_CONDITION_OPERATOR_EQUAL != condition->op && ZBX_CONDITION_OPERATOR_NOT_EQUAL != condition->op) ||
		 FAIL == glb_state_problems_get_hostids(problemid, &problem_hostids)) 
	{
		zbx_vector_uint64_destroy(&problem_hostids);
		condition->result = NOTSUPPORTED;
		return;	
	}
	
	zbx_vector_uint64_create(&gr_hostids);
	zbx_dc_get_nested_group_hostids(group_id, &gr_hostids);
	
	zbx_vector_uint64_uniq(&gr_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(&gr_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	ret = FAIL;

	for (i = 0; i < problem_hostids.values_num && FAIL == ret; i++ ) 
		ret = zbx_vector_uint64_bsearch(&gr_hostids, problem_hostids.values[i], ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		
	if ((FAIL == ret && ZBX_CONDITION_OPERATOR_NOT_EQUAL == condition->op) || 
		(SUCCEED ==ret && ZBX_CONDITION_OPERATOR_EQUAL == condition->op) 
	)
		condition->result = SUCCEED;
	else 
		condition->result = FAIL;		

	zbx_vector_uint64_destroy(&gr_hostids);
	zbx_vector_uint64_destroy(&problem_hostids);
}

static void	check_problem_tag_name(u_int64_t problemid, condition_t *condition)
{
	int result;
	tag_t tag = {.tag = condition->value, .value = condition->value2};

	if (FAIL == glb_state_problems_check_tag_value(problemid, &tag, condition->op, &result)) {
		condition->result = NOTSUPPORTED;
		return;
	}
	
	condition->result = result;
}

static void	check_problem_tag_value(u_int64_t problemid, condition_t *condition)
{
	int result;
	tag_t tag = {.value = condition->value, .tag = condition->value2};

	if (FAIL == glb_state_problems_check_tag_name(problemid, &tag, condition->op, &result)) {
		condition->result = NOTSUPPORTED;
		return;
	}
	
	condition->result = result;
}

static void check_problem_name(u_int64_t problemid, condition_t *condition) {
	char event_name[MAX_STRING_LEN];
	
	if ( (ZBX_CONDITION_OPERATOR_LIKE != condition->op && ZBX_CONDITION_OPERATOR_NOT_LIKE != condition->op) || 
		  FAIL == glb_state_problems_get_name(problemid, event_name, MAX_STRING_LEN) )
	{
		condition->result = NOTSUPPORTED;
		return;
	}

	if ( ( ZBX_CONDITION_OPERATOR_LIKE == condition->op 	&& NULL != strstr(event_name, condition->value) ) || 
		 ( ZBX_CONDITION_OPERATOR_NOT_LIKE == condition->op && NULL == strstr(event_name, condition->value) ) ) 
	{
		 condition->result = SUCCEED;
		 return;
	}
	
	condition->result = FAIL;
}

static void check_problem_time_period(u_int64_t problemid, condition_t *condition)
{

	char *period;
	int res, p_clock;

	if (ZBX_CONDITION_OPERATOR_IN != condition->op && ZBX_CONDITION_OPERATOR_NOT_IN != condition->op) {
		condition->result = NOTSUPPORTED;
		return;
	}

	if (FAIL == glb_state_problems_get_clock(problemid, &p_clock))
	{
		condition->result = NOTSUPPORTED;
		return;
	}

	period = zbx_strdup(NULL, condition->value);
	glb_macro_expand_common(&period, NULL, 0);

	if (SUCCEED == zbx_check_time_period(period, (time_t)p_clock, NULL, &res))
	{
		if ((SUCCEED == res && ZBX_CONDITION_OPERATOR_IN == condition->op) ||
			(FAIL == res && ZBX_CONDITION_OPERATOR_NOT_IN == condition->op))
			condition->result = SUCCEED;
		else
			condition->result = FAIL;
	}
	else
		condition->result = NOTSUPPORTED;

	zbx_free(period);
}

static void check_problem_is_suppressed(u_int64_t problemid, condition_t *condition)
{

	glb_problem_suppress_state_t is_suppressed;
	
	if (FAIL == glb_state_problems_get_suppressed(problemid, &is_suppressed))
	{
		condition->result = NOTSUPPORTED;
		return;
	}

	if ((SUCCEED == is_suppressed && GLB_PROBLEM_SUPPRESSED_TRUE == condition->op) ||
		(FAIL == is_suppressed && GLB_PROBLEM_SUPPRESSED_FALSE == condition->op) )
		{
			condition->result = SUCCEED;
			return;
		}
	condition->result = FAIL;
}

static void check_problem_is_acknowledged(u_int64_t problemid, condition_t *condition) {
	if ( condition->op != ZBX_CONDITION_OPERATOR_EQUAL || 
		 FAIL == glb_state_problems_is_acknowledged(problemid, &condition->result) )
		
		condition->result = NOTSUPPORTED;
}

static void	check_problem_has_severity(u_int64_t problemid, condition_t *condition) {
	int severity;
	unsigned char	condition_value;

	if (FAIL == glb_state_problems_get_severity(problemid, &severity)) {
		condition->result = NOTSUPPORTED;
		return;
	}

	condition_value = (unsigned char)atoi(condition->value);
	condition->result = SUCCEED;

	switch (condition->op)
	{
		case ZBX_CONDITION_OPERATOR_EQUAL:
			if (severity == condition_value)
				return;
			break;
		case ZBX_CONDITION_OPERATOR_NOT_EQUAL:
			if (severity != condition_value)
				return;
			break;
		case ZBX_CONDITION_OPERATOR_MORE_EQUAL:
			if (severity >= condition_value)
				return;
			break;
		case ZBX_CONDITION_OPERATOR_LESS_EQUAL:
			if (severity <= condition_value)
				return;
			break;
		default:
			condition->result = NOTSUPPORTED;
			return;
	}
	condition->result = FAIL;
}


static void check_problem_has_trigger_id(u_int64_t problemid, condition_t *condition) {
	zbx_uint64_t			condition_value;
	zbx_uint64_t			trigger_id;

	ZBX_STR2UINT64(condition_value, condition->value);
	if (ZBX_CONDITION_OPERATOR_EQUAL != condition->op && 
	    ZBX_CONDITION_OPERATOR_NOT_EQUAL != condition->op) {
		condition->result = NOTSUPPORTED;
		return;
	}
	
	if (FAIL == glb_state_problems_get_triggerid(problemid, &trigger_id)) {
		condition->result = NOTSUPPORTED;
		return;
	}
	
	if ( (trigger_id == condition_value &&	ZBX_CONDITION_OPERATOR_EQUAL == condition->op) ||
		 (trigger_id != condition_value &&	ZBX_CONDITION_OPERATOR_NOT_EQUAL == condition->op) ) {
		condition->result = SUCCEED;
		return;
		}

	condition->result = FAIL;
}

static void check_problem_belongs_to_host(u_int64_t problemid, condition_t *condition) {
	zbx_vector_uint64_t host_ids;
	zbx_vector_uint64_create(&host_ids);
	int ret = FAIL;
	
	u_int64_t hostid;
		
	ZBX_STR2UINT64(hostid, condition->value);
	
	if (SUCCEED == glb_state_problems_get_hostids(problemid, &host_ids)) {
		
		zbx_vector_uint64_sort(&host_ids, ZBX_DEFAULT_UINT64_PAIR_COMPARE_FUNC);
		
		if (FAIL != zbx_vector_uint64_bsearch(&host_ids, hostid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			ret = SUCCEED;
	}
	
	zbx_vector_uint64_destroy(&host_ids);

	if ( (ZBX_CONDITION_OPERATOR_EQUAL == condition->op && SUCCEED == ret) ||
		 (ZBX_CONDITION_OPERATOR_NOT_EQUAL == condition->op && FAIL == ret)	) {
		condition->result = SUCCEED;
		return;
	}
	
	condition->result = FAIL;
}



static void	check_problem_condition(u_int64_t problemid, condition_t *condition)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	switch (condition->conditiontype)
	{
		case ZBX_CONDITION_TYPE_HOST_GROUP:
			check_problem_belongs_to_host_group(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_HOST_TEMPLATE:
			check_problem_belongs_to_template(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_HOST:
			check_problem_belongs_to_host(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_TRIGGER:
			check_problem_has_trigger_id(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_TRIGGER_NAME:
			check_problem_name(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_TRIGGER_SEVERITY:
			check_problem_has_severity(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_TIME_PERIOD:
			check_problem_time_period(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_SUPPRESSED:
			check_problem_is_suppressed(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_EVENT_ACKNOWLEDGED:
			check_problem_is_acknowledged(problemid, condition);
			break;
		case ZBX_CONDITION_TYPE_EVENT_TAG:
			check_problem_tag_name(problemid, condition);
			ret = SUCCEED;
			break;
		case ZBX_CONDITION_TYPE_EVENT_TAG_VALUE:
			check_problem_tag_value(problemid, condition);
			ret = SUCCEED;
			break;
		default:
			HALT_HERE("unsupported operator [%d] for condition id [" ZBX_FS_UI64 "]",
				(int)condition->conditiontype, condition->conditionid);

	}
}

void	glb_event_condition_calc(events_processor_event_t *event, condition_t *condition)
{

	 switch (event->event_source)
	 {
	 	case EVENT_SOURCE_PROBLEM:
			check_problem_condition(event->object_id, condition);
			return;
	// 	case EVENT_SOURCE_DISCOVERY:
	// 		check_discovery_condition(esc_events, condition);
	// 		break;
	// 	case EVENT_SOURCE_AUTOREGISTRATION:
	// 		check_autoregistration_condition(esc_events, condition);
	// 		break;
	// 	case EVENT_SOURCE_INTERNAL:
	// 		check_internal_condition(esc_events, condition);
	// 		break;
	 	default:
	 		LOG_INF("unsupported event source [%d] for condition id [" ZBX_FS_UI64 "]",
	 				event->event_type, condition->conditionid);
			//HALT_HERE("");					
	 }

	// zbx_vector_uint64_sort(&condition->eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	// zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
