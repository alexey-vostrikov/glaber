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


int glb_macro_translate_event_name(DC_TRIGGER *trigger, char *result, int max_len) {

    ZBX_DB_EVENT event={0};
    char err[MAX_STRING_LEN];
    
    event.eventid = 0;
	//event.history_idx = history_idx;
	event.source = EVENT_SOURCE_TRIGGERS;
	event.object = EVENT_OBJECT_TRIGGER;
	event.objectid = trigger->triggerid;
	event.name = NULL;
	event.clock = time(NULL);
	event.ns = 0;
	event->value = trigger->new_value;
	event->acknowledged = EVENT_NOT_ACKNOWLEDGED;
	event->flags = ZBX_FLAGS_DB_EVENT_CREATE;
	event->severity = TRIGGER_SEVERITY_NOT_CLASSIFIED;
	event->suppressed = ZBX_PROBLEM_SUPPRESSED_FALSE;

	if (EVENT_SOURCE_TRIGGERS == source)
	{
		char err[256];
		zbx_dc_um_handle_t *um_handle;

		um_handle = zbx_dc_open_user_macros();

		if (TRIGGER_VALUE_PROBLEM == value)
			event->severity = trigger_priority;

		event->trigger.triggerid = objectid;
		event->trigger.description = zbx_strdup(NULL, trigger_description);
		event->trigger.expression = zbx_strdup(NULL, trigger_expression);
		event->trigger.recovery_expression = zbx_strdup(NULL, trigger_recovery_expression);
		event->trigger.priority = trigger_priority;
		event->trigger.type = trigger_type;
		event->trigger.correlation_mode = trigger_correlation_mode;
		event->trigger.correlation_tag = zbx_strdup(NULL, trigger_correlation_tag);
		event->trigger.value = trigger_value;
		event->trigger.opdata = zbx_strdup(NULL, trigger_opdata);
		event->trigger.event_name = ((NULL != event_name && event_name[0] != '\0') ? zbx_strdup(NULL, event_name) : NULL);
		event->name = zbx_strdup(NULL, ((NULL != event_name && event_name[0] != '\0') ? event_name : trigger_description));
		event->trigger.cache = NULL;
		event->trigger.url = NULL;
		event->trigger.url_name = NULL;
		event->trigger.comments = NULL;

		zbx_substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
									 &event->trigger.correlation_tag, MACRO_TYPE_TRIGGER_TAG, err, sizeof(err));

		zbx_substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
									 &event->name, MACRO_TYPE_EVENT_NAME, err, sizeof(err));

		zbx_vector_ptr_create(&event->tags);

		if (NULL != trigger_tags)
		{
			for (i = 0; i < trigger_tags->values_num; i++)
				process_trigger_tag(event, (const zbx_tag_t *)trigger_tags->values[i]);
		}

		zbx_vector_ptr_create(&item_tags);
		get_item_tags_by_expression(&event->trigger, &item_tags);

		for (i = 0; i < item_tags.values_num; i++)
		{
			process_item_tag(event, (const zbx_item_tag_t *)item_tags.values[i]);
			zbx_free_item_tag(item_tags.values[i]);
		}

		zbx_vector_ptr_destroy(&item_tags);

		zbx_dc_close_user_macros(um_handle);
	}






    zbx_substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    					 &event.name, MACRO_TYPE_EVENT_NAME, err, sizeof(err));
     
	LOG_INF("Standalone version of translate: translat");
	HALT_HERE("Not implemented");
}
