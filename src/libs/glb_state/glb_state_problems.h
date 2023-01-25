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

#ifndef GLB_STATE_PROBLEMS_H
#define GLB_STATE_PROBLEMS_H

#include "glb_state.h"




int glb_state_problems_init(mem_funcs_t *memf);
int glb_state_problems_destroy();

typedef struct {
     char *problem;
     unsigned char avail;
     int lastchange;
 } glb_state_problem_info_t;


int    glb_state_create_problem(u_int64_t interfaceid, const char *error); 
int    glb_state_update_problem(u_int64_t interfaceid, const char *error); 
glb_state_problem_info_t *glb_state_get_problem(u_int64_t id);

int     glb_state_get_problems_json(zbx_vector_uint64_t *ids, struct zbx_json *json);

#endif