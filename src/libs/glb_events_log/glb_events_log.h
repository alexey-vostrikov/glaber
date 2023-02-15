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
#ifndef GLB_EVENT_LOG
#define GLB_EVENT_LOG

typedef enum {
    GLB_EVENT_LOG_SOURCE_ITEMS = 0,
    GLB_EVENT_LOG_SOURCE_TRIGGER,
    GLB_EVENT_LOG_SOURCES_COUNT
} glb_event_log_source_t;


void write_event_log_str(u_int64_t objectid, u_int64_t eventid, glb_event_log_source_t source, char *log);

#define write_event_log(OBJECTID, EVENTID, SOURCE, FORMAT, ...) { char *str_line = zbx_dsprintf(NULL, FORMAT, ##__VA_ARGS__); \
                  write_event_log_str(OBJECTID, EVENTID, SOURCE, str_line); \
                  zbx_free(str_line); }

void write_event_log_attr_int_change(u_int64_t objectid, u_int64_t eventid, glb_event_log_source_t source, const char *attr_name, int old_value, int new_value);
void write_event_log_attr_str_change(u_int64_t objectid, u_int64_t eventid, glb_event_log_source_t source, const char *attr_name, const char *old_value, const char *new_value);

#endif