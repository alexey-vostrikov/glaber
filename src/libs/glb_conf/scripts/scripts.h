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

#include "zbxcommon.h"
#include "script.h"

int glb_conf_scripts_init(mem_funcs_t *memf);

void    glb_conf_script_free(glb_conf_script_t *vm, mem_funcs_t *memf, strpool_t *strpool);
glb_conf_script_t*  glb_conf_script_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool);
int     glb_conf_scripts_set_data(char *json_buffer);
//todo: interface to fetch and use script data should be here