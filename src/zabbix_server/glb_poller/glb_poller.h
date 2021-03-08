/*
** Glaber
** Copyright (C) 2001-2020 Glaber JSC
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

#ifndef GLABER_POLLER_H
#define GLABER_POLLER_H

#include "threads.h"

#define GLB_ASYNC_POLLING_MAX_ITERATIONS 10000000
#define GLB_EVENT_ITEM_POLL 1
#define GLB_EVENT_NEW_ITEMS_CHECK 2
#define GLB_EVENT_AGING 3

//#define GLB_GET_MAX_ITEMS 256 //do not make it too big! - arrays of this size are allocated from stack

#define GLB_ITEM_STATE_NEW 1
#define GLB_ITEM_STATE_POLLING 2
#define GLB_ITEM_STATE_QUEUED 3


//#define GLB_MAX_ITEM_PARAMS 16 //so far SNMP needs 7 params, maybe other's will need more, increase!
#define GLB_AGING_PERIOD 62	 //how often to clean up the items

#define GLB_MAX_FAILS 6 //how many times in a row items should fail to mark host as unreachable and pause polling for CONFIG_UREACHABLE_PERIOD

#define GLB_PROTO_ITEMID "itemid"
#define GLB_PROTO_VALUE "value"
#define GLB_PROTO_ERRCODE "errcode"
#define GLB_PROTO_ERROR "error"

typedef struct
{

	zbx_uint64_t hostid;
	unsigned int poll_items;
	unsigned int items;
	unsigned char fails;
	time_t disabled_till;

} GLB_POLLER_HOST;

typedef struct
{

	unsigned int time;
	char type;
	zbx_uint64_t id; //using weak linking assuming events might be outdated

} GLB_POLLER_EVENT;

typedef struct
{
	zbx_uint64_t itemid;
	zbx_uint64_t hostid;
	char state;
	unsigned char value_type;
	unsigned int ttl;
	const char *delay;
	unsigned char item_type;
	unsigned char flags;
	unsigned int lastpolltime;
	void *itemdata;		 //item type specific data
} GLB_POLLER_ITEM;

typedef struct {
	const char *oid;
	unsigned char snmp_version;
	const char		*interface_addr;
	unsigned char	useip;
	unsigned short	interface_port;
	unsigned char	snmpv3_securitylevel;
	unsigned char	snmpv3_authprotocol;
	unsigned char	snmpv3_privprotocol;
	const char *community;
	const char *snmpv3_securityname;
	const char *snmpv3_contextname;
	const char *snmpv3_authpassphrase;
	const char *snmpv3_privpassphrase;

} GLB_SNMP_ITEM;



void add_host_fail(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
int host_is_failed(zbx_hashset_t *hosts, zbx_uint64_t hostid, int now);
int glb_create_item(zbx_binary_heap_t *events, zbx_hashset_t *hosts, zbx_hashset_t *items, DC_ITEM *dc_item, void *poll_engine);


ZBX_THREAD_ENTRY(glbpoller_thread, args);

GLB_POLLER_ITEM *glb_get_poller_item(zbx_uint64_t itemid);

#endif
