/*
** Glaber
** Copyright (C) 2001-2038 Glaber 
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
#include "common.h"
#include "glb_poller.h"

#define SNMP_WALK_MAX_OIDS 100000

void snmp_walk_destroy_item(poller_item_t *poller_item);
void snmp_walk_timeout(poller_item_t *poller_item);

int snmp_walk_send_first_request(poller_item_t *poller_item);
void snmp_walk_process_result(poller_item_t *poller_item, const csnmp_pdu_t *pdu);
