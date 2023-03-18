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

/* set of of functions to fetch and return object's data*/

#include "zbxcommon.h"
#include "zbxcacheconfig.h"
#include "glb_macro_defs.h"


int recognize_macro_type_and_object_type(macro_proc_data_t *macro_proc);

int glb_macro_builtin_expand_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid);
int glb_macro_builtin_expand_by_host(macro_proc_data_t *macro_proc, DC_HOST *host);

int glb_macro_builtin_expand_by_itemid(macro_proc_data_t *macro_proc, u_int64_t itemid);
int glb_macro_builtin_expand_by_item(macro_proc_data_t *macro_proc, DC_ITEM *item);

int glb_macro_builtin_expand_by_trigger(macro_proc_data_t *macro_proc, calc_trigger_t *tr);