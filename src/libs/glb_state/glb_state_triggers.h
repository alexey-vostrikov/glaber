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

#ifndef GLB_STATE_TRIGGERS_H
#define GLB_STATE_TRIGGERS_H
#include "glb_state.h"

typedef struct {
    u_int64_t id; 
    unsigned char value;
    int lastchange; //when value changed last time
    int lastcalc;   //when the last trigger recalc has happen
    unsigned char flags;
    char *error;
} state_trigger_info_t;

#define STATE_GET_TRIGGER_ERROR 1

int glb_state_triggers_init(mem_funcs_t *memf);
int glb_state_trigger_get_info(state_trigger_info_t *info);
int glb_state_trigger_set_info(state_trigger_info_t *info);

unsigned char glb_state_trigger_get_value(u_int64_t id);
unsigned char glb_state_trigger_get_value_lastchange(u_int64_t id, unsigned char *value, int* lastchange);
void glb_state_trigger_set_value(u_int64_t id, unsigned char value, int lastcalc); //lastcalc might be 0 - then current time is used

void glb_state_triggers_apply_diffs(zbx_vector_ptr_t *trigger_diff);

int glb_state_triggers_destroy();
#endif