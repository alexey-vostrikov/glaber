

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

/*each time a trigger processed and calculated, the new value should be processed to handle 
problems state and to create event log changes*/
/* trigger values */

#include "zbxcommon.h"

#ifndef GLB_TRIGGER_H
#define GLB_TRIGGER_H




int     glb_trigger_register_calculation(CALC_TRIGGER *trigger);
void	glb_trigger_get_functions_ids(CALC_TRIGGER *trigger, zbx_vector_uint64_t *functions_ids);

char*   glb_trigger_get_severity_name(unsigned char priority);
char*   glb_trigger_get_admin_state_name(unsigned char admin_state);
#endif