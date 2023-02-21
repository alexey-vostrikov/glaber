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
#include "../../zabbix_server/glb_poller/poller_ipc.h"

void DCget_host_items(u_int64_t hostid, zbx_vector_uint64_t *items) ;

static void conf_hosts_get_host_items(u_int64_t hostid, zbx_vector_uint64_t *changed_items) {
    DCget_host_items(hostid, changed_items);
}


static void host_notify_changes(u_int64_t hostid) {
    zbx_vector_uint64_t changed_items;
    
    zbx_vector_uint64_create(&changed_items);   
    
    conf_hosts_get_host_items(hostid, &changed_items);
    conf_items_notify_changes(&changed_items);
    
    zbx_vector_uint64_destroy(&changed_items);
}

void conf_host_notify_changes(u_int64_t hostid) {
    poller_item_notify_init();
    host_notify_changes(hostid);
    poller_item_notify_flush();
}

void conf_hosts_notify_changes(zbx_vector_uint64_t *changed_hosts) {
    int i;
    poller_item_notify_init();

    zbx_vector_uint64_uniq(changed_hosts, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    for( i = 0; i < changed_hosts->values_num; i++) 
         host_notify_changes(changed_hosts->values[i]);
    
    poller_item_notify_flush();
}
