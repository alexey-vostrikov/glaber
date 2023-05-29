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
#include "zbxalgo.h"

typedef struct glb_conf_script_t glb_conf_script_t;

//int glb_conf_script_create_update_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *data);
glb_conf_script_t *glb_conf_script_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool);
void glb_conf_script_clear(glb_conf_script_t *script, mem_funcs_t *memf, strpool_t *strpool);
