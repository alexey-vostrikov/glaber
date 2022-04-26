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
#ifndef CONF_TRIGGERS_H
#define CONF_TRIGGERS_H

#include "common.h"
#include "zbxalgo.h"
#include "../glb_state/state_triggers.h"
#include "../glb_process/proc_triggers.h"

#define TRIGGER_TIMER_DEFAULT		0x00
#define TRIGGER_TIMER_EXPRESSION		0x01
#define TRIGGER_TIMER_RECOVERY_EXPRESSION	0x02



int     conf_trigger_get_trigger_conf_data(u_int64_t triggerid, trigger_conf_t *tr_conf);
void    conf_trigger_free_trigger( trigger_conf_t *trigger);

int get_triggers_by_triggerids(zbx_vector_uint64_t ids, zbx_vector_ptr_t *triggers);
void conf_trigger_get_functionids(zbx_vector_uint64_t *functionids, trigger_conf_t *tr);

//int conf_triggers_init(mem_funcs_t *memf);

#endif