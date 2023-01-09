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

#ifndef GLB_CACHE_INTERFACES_H
#define GLB_CACHE_INTERFACES_H

#include "glb_state.h"




int glb_state_interfaces_init(mem_funcs_t *memf);
int glb_state_interfaces_destroy();

/* ip -> host indexing */
/*consider moving theese to a host state whenever host state appears */
int         glb_state_interfaces_register_ip(const char *addr, u_int64_t hostid);
u_int64_t   glb_state_interfaces_find_host_by_ip(const char *addr);
void        glb_state_interfaces_release_ip(const char *addr);

typedef struct {
    char *error;
    unsigned char avail;
} glb_state_interface_info_t;

void    glb_state_interfaces_register_if_fail(u_int64_t id, char *error);
void    glb_state_interfaces_register_if_ok(u_int64_t id);
int     glb_state_interfaces_register_get_if_avail(glb_state_interface_info_t *info);

#endif