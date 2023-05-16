/*
** Glaber
** Copyright (C) 2001-2023 Glaber
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

#include "zbxalgo.h"

typedef enum {
     GLB_CONF_API_ACTIONS_OPERATIONS = 0,
     GLB_CONF_API_OBJECTS_MAX
} glb_conf_json_object_t;

int       glb_conf_api_sync_init(mem_funcs_t *memf);
void      glb_conf_api_sync_destroy();

void      glb_conf_set_json_data_table(char *buffer, int table);
char*     glb_conf_get_json_data_table(int table);

int       glb_api_sync_operations();