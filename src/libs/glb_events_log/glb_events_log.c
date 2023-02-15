
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
#include "zbxcommon.h"
#include "glb_history.h"
#include "glb_events_log.h"

static char *event_source_names[GLB_EVENT_LOG_SOURCES_COUNT] =
     {"", //normal items have no source info
      "trigger"};
 
void write_event_log_str(u_int64_t objectid, u_int64_t eventid,  glb_event_log_source_t source, char *log) {
    
    ZBX_DC_HISTORY h; 
    zbx_log_value_t log_val;
    
    bzero(&h, sizeof(h));
    bzero(&log_val, sizeof(log_val));
    
    h.itemid = objectid;
    h.value_type = ITEM_VALUE_TYPE_LOG;
    h.hostid = objectid;
    h.ts.sec = time(NULL);
    h.value.log = &log_val;

    log_val.logeventid = eventid;
    log_val.source = event_source_names[source];
    
  //  LOG_INF("Abbout to write log %s", log);
    log_val.value = zbx_strdup(NULL, log);
    
    glb_history_add_history(&h, 1);
}

void write_event_log_attr_int_change(u_int64_t objectid, u_int64_t eventid, glb_event_log_source_t source, const char *attr_name, int old_value, int new_value) {
    write_event_log(objectid, eventid, source, "Attribute '%s' changed %d -> %d", attr_name, old_value, new_value);
}

void write_event_log_attr_str_change(u_int64_t objectid, u_int64_t eventid, glb_event_log_source_t source, const char *attr_name, const char *old_value, const char *new_value) {
    write_event_log(objectid, eventid, source, "Attribute '%s' changed '%s' -> '%s'", attr_name, old_value, new_value);
}
