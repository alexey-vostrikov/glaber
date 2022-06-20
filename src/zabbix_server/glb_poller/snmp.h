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
//#include <net-snmp/net-snmp-config.h>
//#include <net-snmp/net-snmp-includes.h>

#include "poller_async_io.h"

#define SNMP_MAX_RETRIES 2

typedef struct  {
	const char *oid;
	const char*		ipaddr;
	unsigned char	snmp_req_type;
	void 			*data;
	
	poller_event_t* tm_event;
	u_int32_t		sessid;
	
	unsigned char	retries;

	unsigned char	snmp_version;
	const char		*interface_addr;
	unsigned char	useip;
	unsigned short	interface_port;
	const char 		*community;

} snmp_item_t;


void async_snmp_init(void);

//typedef struct snmp_item_t snmp_item_t;

int snmp_item_oid_to_asn(const char *str, asn1_oid_t* coid);
int	snmp_set_result(poller_item_t *poller_item, csnmp_var_t *var, AGENT_RESULT *result);
void snmp_send_packet(poller_item_t *poller_item, csnmp_pdu_t *pdu);
int snmp_fill_pdu_header(poller_item_t *poller_item, csnmp_pdu_t *pdu, int command);

char	*snmp_err_to_text(unsigned char type);
void	csnmp_oid_2_netsnmp(csnmp_oid_t *c_oid, unsigned long *n_oid);

#endif