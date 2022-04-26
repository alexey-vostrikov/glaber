/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#ifndef ZABBIX_EVENTS_H
#define ZABBIX_EVENTS_H

#include "../libs/glb_state/state_triggers.h"
#include "../libs/glb_conf/conf_triggers.h"


void	zbx_initialize_events(void);
void	zbx_uninitialize_events(void);
DB_EVENT	*zbx_add_event(unsigned char source, unsigned char object, zbx_uint64_t objectid,
		const zbx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const zbx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *event_name, const char *error);

int create_event(DB_EVENT *event, unsigned char source, unsigned char object, zbx_uint64_t objectid,
		const zbx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const zbx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *event_name, const char *error);


int	zbx_close_problem(zbx_uint64_t triggerid, zbx_uint64_t eventid, zbx_uint64_t userid);

int	zbx_process_events(zbx_vector_ptr_t *trigger_diff, zbx_vector_uint64_t *triggerids_lock);

//int process_trigger_event(DB_EVENT *event, trigger_conf_t *conf, trigger_state_t *state);
int	process_trigger_change(trigger_conf_t *conf, trigger_state_t *state, unsigned char old_vlaue, zbx_timespec_t *ts);

//int	process_event(DB_EVENT *event, trigger_conf_t *conf);

void	zbx_clean_events(void);
void	zbx_reset_event_recovery(void);
void	zbx_export_events(void);

#endif
