/*
** Zabbix
** Copyright (C) 2001-2023 GLABER JSC
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

#ifndef GLB_STATE_H
#define GLB_STATE_H

int     glb_state_host_register_ip(const char *addr, u_int64_t hostid);
void    glb_state_hosts_release_ip(const char *addr);
u_int64_t   glb_state_host_find_by_ip(const char *addr);

#endif
