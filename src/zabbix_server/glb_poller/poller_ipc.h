/*
** Glaber
** Copyright (C)  Glaber
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
#include "zbxcommon.h"

int poller_notify_ipc_init(size_t mem_size);

int poller_item_notify_init();
void poller_item_notify_flush();

int poller_item_add_notify(int item_type, const char *key, u_int64_t itemid, u_int64_t hostid, int snmp_version);
int poller_ipc_notify_rcv(int value_type, int consumer, zbx_vector_uint64_t* changed_items);