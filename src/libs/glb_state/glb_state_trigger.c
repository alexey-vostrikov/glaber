
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

#include "glb_common.h"
#include "glb_state_triggers.h"
#include "glb_state_problems.h"
#include "zbxcacheconfig.h"
//TODO: move trigger constatnts to GLB_CODE to ease sources upgrade
#include "zbx_trigger_constants.h"
#include "../glb_events_log/glb_events_log.h"
#include "zbxcacheconfig.h"
//NOTE: look at db_trigger_expand_macros for macro expansion in conditions

char* glb_trigger_get_severity_name(unsigned char priority)
{
	static zbx_config_t	cfg = {0};

	if (TRIGGER_SEVERITY_COUNT <= priority)
		return NULL;

	//this will init once and never free cfg, but we most likely reuse it often
	if ( NULL == cfg.severity_name) { 
		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_SEVERITY_NAME);
	}
	//zbx_config_clean(&cfg);
	return cfg.severity_name[priority];
}

char      *glb_trigger_get_admin_state_name(unsigned char admin_state)
 {
 	switch (admin_state)
 	{
 		case TRIGGER_STATUS_ENABLED:
			return "Enabled";
		case TRIGGER_STATUS_DISABLED:
 			return "Disabled";
 	}
	HALT_HERE("Unknown admin state passed %d", admin_state)
}

static void trigger_check_dependency(calc_trigger_t *trigger) {
	if (FAIL == glb_check_trigger_has_value_ok_masters(trigger->triggerid)) {
		zbx_free(trigger->new_error);
		trigger->new_error = zbx_strdup(NULL,"There are master trigger(s) in PROBLEM or UNKNOWN state");
		trigger->value = TRIGGER_VALUE_UNKNOWN;
		DEBUG_TRIGGER(trigger->triggerid, "Dependency check found master triggers in PROBLEM or UNKNOWN state, trigger is set to UNKNWON value");
		return;
	}
	DEBUG_TRIGGER(trigger->triggerid, "Dependency check not found master triggers in PROBLEM or UNKNOWN state, trigger value is not changed %d", trigger->value);
}

static int trigger_error_changed(calc_trigger_t *trigger) {
	if (NULL == trigger->error && NULL != trigger->new_error)
		return SUCCEED;
	if (NULL == trigger->new_error && NULL != trigger->error)
		return SUCCEED;
	
	return strcmp(trigger->error, trigger->new_error);
}

static void trigger_write_events(calc_trigger_t *trigger) {

	if (trigger->value != trigger->new_value) {
		DEBUG_TRIGGER(trigger->triggerid, "Will write value change %d->%d to events log", trigger->value, trigger->new_value  );
		glb_state_trigger_set_value(trigger->triggerid, trigger->new_value, 0);
		write_event_log_attr_int_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "value", trigger->value, trigger->new_value );
	}

	if (trigger_error_changed(trigger)) {
		DEBUG_TRIGGER(trigger->triggerid, "Will write error change '%s'->'%s' to events log", trigger->error, trigger->new_error  );
		write_event_log_attr_str_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "error", trigger->error, trigger->new_error);
		
		if (NULL != trigger->new_error && *trigger->new_error != '\0') 
			glb_state_trigger_set_error(trigger->triggerid, trigger->new_error);
	}
}

// int glb_trigger_register_calculation(calc_trigger_t *trigger) {
// 	DEBUG_TRIGGER(trigger->triggerid, "Processing trigger value %d", trigger->new_value);
// 	trigger_check_dependency(trigger);
// 	trigger_write_events(trigger);
// 	glb_state_problems_process_trigger_value(trigger);
// }

