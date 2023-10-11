/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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

#ifndef GLB_POLLER_SNMP_WORKER_H
#define GLB_POLLER_SNMP_WORKER_H
#include "glb_poller.h"
#include "poller_async_io.h"

typedef struct snmp_worker_discovery_t snmp_worker_discovery_t;
typedef enum {
    SNMP_REQUEST_GET = 1,
    SNMP_REQUEST_WALK,
    SNMP_REQUEST_DISCOVERY,
    SNMP_REQUEST_MAX
} snmp_request_type_t;

typedef struct {
    const char *oid;
} snmp_worker_one_oid_t;

typedef union 
{
    const char *get_oid;
    const char *walk_oid;
    snmp_worker_discovery_t *discovery_data;
} snmp_worker_request_data_t;

typedef struct {
    snmp_worker_request_data_t request_data;
    const char *iface_json_info; 
    const char *address;
    unsigned char need_resolve;
    snmp_request_type_t request_type;
    poller_event_t *timeout_event;
} snmp_worker_item_t;
#define SNMP_MAX_CONTENTION 4

void glb_snmp_worker_init();

//might be feasible to move to a util file
const char*   snmp_worker_parse_oid(const char *in_oid);
int     snmp_worker_send_request(poller_item_t *poller_item, const char *request);
#endif
