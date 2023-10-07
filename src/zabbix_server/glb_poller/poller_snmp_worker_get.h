
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

#ifndef GLB_POLLER_SNMP_WORKER_GET_H
#define GLB_POLLER_SNMP_WORKER_GET_H
#include "glb_poller.h"
#include "poller_async_io.h"

void snmp_worker_process_get_response(poller_item_t *poller_item,  struct zbx_json_parse *jp);
void snmp_worker_clean_get_request(snmp_worker_item_t *snmp_item);
void snmp_worker_free_get_item(poller_item_t *poller_item);
int snmp_worker_init_get_item(poller_item_t *poller_item, const char *key);
void snmp_worker_start_get_request(poller_item_t *poller_item, const char *ip_addr);


#endif