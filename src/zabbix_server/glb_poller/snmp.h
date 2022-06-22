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

#ifndef POLL_SNMP_H
#define POLL_SNMP_H
#include "glb_poller.h"
#include "csnmp.h"

#include "poller_async_io.h"

#define SNMP_MAX_RETRIES 2

typedef struct  {
	const char *oid;
	const char		*interface_addr;
	const char		*ipaddr;
		
	poller_event_t	*tm_event;
	u_int32_t		sessid;
	unsigned char	retries;
	void 			*data;
	
	unsigned char	snmp_version;
	unsigned char	useip;
	unsigned short	interface_port;
	const char 		*community;
	unsigned char	snmp_req_type;

} snmp_item_t;


void 	snmp_async_init(void);
void	snmp_async_shutdown(void);

int		snmp_set_result(poller_item_t *poller_item, csnmp_var_t *var, AGENT_RESULT *result);
void 	snmp_send_packet(poller_item_t *poller_item, csnmp_pdu_t *pdu);
int 	snmp_fill_pdu_header(poller_item_t *poller_item, csnmp_pdu_t *pdu, int command);

#endif