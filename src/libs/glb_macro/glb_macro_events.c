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


convert_dc_trigger_2_event(ZBX_DB_EVENT *event, DC_TRIGGER *trigger) {
	
}

int glb_macro_translate_event_name(DC_TRIGGER *trigger, char *result, int max_len) {
	char err[MAX_STRING_LEN];
		
	//	zbx_substitute_simple_macros(NULL, trigger, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	//								 &event->trigger.correlation_tag, MACRO_TYPE_TRIGGER_TAG, err, sizeof(err));

	ZBX_DB_EVENT event={0};
	convert_dc_trigger_2_event(&event, trigger);

	zbx_substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			 &trigger->event_name, MACRO_TYPE_EVENT_NAME, err, sizeof(err));

    free_event(&event);

	LOG_INF("Standalone version of translate: translat");
	HALT_HERE("Not implemented");
}
