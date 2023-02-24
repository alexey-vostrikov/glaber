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

/* reasoning: make macro calc optimal, avoid db usage, reuse objects already
having data needed */

#include "zbxcommon.h"
#include "zbxcacheconfig.h"
#include "glb_macro_defs.h"
#include "log.h"
#include "zbxserver.h"

//a good example in static int	db_trigger_expand_macros(const ZBX_DB_TRIGGER *trigger, zbx_eval_context_t *ctx)

// void convert_dc_trigger_2_event(ZBX_DB_EVENT *event, CALC_TRIGGER *trigger) {
	
// 	event->value = trigger->value;
// 	event->object = EVENT_OBJECT_TRIGGER;

// 	event->trigger.correlation_mode = trigger->correlation_mode;
// 	event->trigger.correlation_tag = trigger->correlation_tag;
// 	event->trigger.description = trigger->description;
// 	event->trigger.event_name = trigger->event_name;
// 	event->trigger.expression = trigger->expression;
// 	event->trigger.opdata = trigger->opdata;
// 	event->trigger.priority = trigger->priority;
// 	event->trigger.recovery_expression = trigger->recovery_expression;
// 	event->trigger.recovery_mode = trigger->recovery_mode;
// 	event->trigger.triggerid = trigger->triggerid;
// 	event->trigger.type = trigger->type;
// 	event->trigger.value = trigger->new_value;
// 	//event->trigger.cache
// }



// int glb_macro_translate_event_name(CALC_TRIGGER *trigger, char **event_name, int max_len) {
// 	char err[MAX_STRING_LEN];
		
// 	//zbx_substitute_simple_macros(NULL, trigger, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
// 	//								 &event->trigger.correlation_tag, MACRO_TYPE_TRIGGER_TAG, err, sizeof(err));

// 	ZBX_DB_EVENT event={0};
// 	convert_dc_trigger_2_event(&event, trigger);

// 	LOG_INF("Standalone version of translate: translating trigger event from '%s'", *event_name);
// 	zbx_substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
// 			event_name, MACRO_TYPE_EVENT_NAME, err, sizeof(err));
// 	LOG_INF("Standalone version of translate: translated trigger event to '%s'", *event_name);
// 	//HALT_HERE("Not implemented");
// }






