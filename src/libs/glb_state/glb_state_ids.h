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

#ifndef GLB_STATE_IDGEN_H
#define GLB_STATE_IDGEN_H

#include "zbxcommon.h"

typedef enum {
    GLB_ID_TYPE_PROBLEMS = 1,
} glb_id_type_t;

int glb_state_ids_init(int proc_num);

u_int64_t glb_state_id_gen_new();
u_int64_t glb_state_id_get_time(u_int64_t id);

#endif