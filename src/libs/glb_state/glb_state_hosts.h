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
#ifndef GLB_STATE_HOSTS_H
#define GLB_STATE_HOSTS_H

#include "glb_state.h"
#include "zbxjson.h"

typedef enum {
    HOST_HEARTBEAT_UNKNOWN = 0,
    HOST_HEARTBEAT_ALIVE, 
    HOST_HEARTBEAT_DOWN
} glb_host_heartbeat_status_t;


typedef enum {
    INTERFACE_AVAILABLE_UNKNOWN = 0,
    INTERFACE_AVAILABLE_TRUE = 1,
    INTERFACE_AVAILABLE_FALSE = 2
} glb_interface_avail_t;

int glb_state_hosts_init(mem_funcs_t *memf);
int glb_state_hosts_destroy();

void glb_state_host_delete(u_int64_t hostid);

void glb_state_host_set_id_interface_avail(u_int64_t hostid, u_int64_t interfaceid, int avail_state, const char *error);
void glb_state_host_set_name_interface_avail(u_int64_t hostid, char *ifname, int avail_state, const char *error);

void glb_state_host_process_heartbeat(u_int64_t hostid, int freq);
void glb_state_host_reset_heartbeat(u_int64_t hostid);
int  glb_state_host_get_heartbeat_alive_status(u_int64_t hostid); 
void glb_state_host_reset(u_int64_t hostid);

int glb_state_host_get_interfaces_avail_json(u_int64_t hostid, struct zbx_json *j);
int glb_state_hosts_get_interfaces_avail_json(zbx_vector_uint64_t *hostids, struct zbx_json *j); 
int glb_state_hosts_get_changed_ifaces_json(int timestamp, struct zbx_json *j);

int glb_state_host_get_interface_avail_by_type(u_int64_t hostid, int iface_type, const char *if_name);


int glb_state_host_is_name_interface_pollable(u_int64_t hostid, char *ifname, int *disabled_till);
int glb_state_host_is_id_interface_pollable(u_int64_t hostid, u_int64_t interfaceid, int *disabled_till);

int glb_state_host_set_interfaces_from_json(struct zbx_json_parse *jp);
void glb_state_hosts_process_heartbeat(u_int64_t hostid, int freq);

int     glb_state_host_register_ip(const char *addr, u_int64_t hostid);
void    glb_state_hosts_release_ip(const char *addr);
u_int64_t   glb_state_host_find_by_ip(const char *addr);

#endif