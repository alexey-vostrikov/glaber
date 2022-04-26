
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

#include "common.h"
#include "zbxalgo.h"
#include "zbxdb.h"

int config_index_init(mem_funcs_t *memf);

//item -> trigger index
int conf_index_items_to_triggers_del_trigger(u_int64_t triggerid);
int conf_index_items_to_triggers_del_item(u_int64_t itemid);
int conf_index_items_to_triggers_add(u_int64_t itemid, u_int64_t triggerid);
int conf_index_items_to_triggers_get_triggers(u_int64_t itemid, zbx_vector_uint64_t *triggers);
int conf_index_items_to_triggers_get_items(u_int64_t triggerid, zbx_vector_uint64_t *items);
int conf_index_items_to_triggers_dump();

//dependant trigger -> trigger index
int conf_index_deptrigger_to_trigger_add(u_int64_t deptriggerid, u_int64_t triggerid);
int conf_index_deptrigger_to_trigger_get_triggers(u_int64_t deptriggerid, zbx_vector_uint64_t *triggers);
int conf_index_deptrigger_to_trigger_get_deptriggers(u_int64_t triggerid, zbx_vector_uint64_t *deptriggers);
int conf_index_deptrigger_to_trigger_del_trigger(u_int64_t triggerid);

int conf_index_deptrigger_sync_from_db(unsigned char sync_mode, DB_RESULT result);
int conf_index_trigger_to_deptrigger_dump();

