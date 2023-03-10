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

#include "log.h"
#include "zbxalgo.h"
#include "conf_items_notify.h"

void DCget_host_items(u_int64_t hostid, zbx_vector_uint64_t *items) ;
int DCget_host_name(u_int64_t hostid, char **name);
int DCget_host_host(u_int64_t hostid, char **name);

static void conf_hosts_get_host_items(u_int64_t hostid, zbx_vector_uint64_t *changed_items) {
    DCget_host_items(hostid, changed_items);
}

void conf_hosts_notify_changes(zbx_vector_uint64_t *changed_hosts) {
    zbx_vector_uint64_t changed_items;
    int i;

    //zbx_vector_uint64_uniq(changed_hosts, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_vector_uint64_create(&changed_items);

    for( i = 0; i < changed_hosts->values_num; i++) {
        conf_hosts_get_host_items(changed_hosts->values[i], &changed_items);
    }
    
    conf_items_notify_changes(&changed_items);
    zbx_vector_uint64_destroy(&changed_items);
}

int glb_conf_host_get_host(u_int64_t hostid, char **host) {
    return DCget_host_host(hostid, host);
}

int glb_conf_host_get_name(u_int64_t hostid, char **name) {
    return DCget_host_name(hostid, name);
}