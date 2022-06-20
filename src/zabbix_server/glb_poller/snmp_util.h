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

//#include "glb_poller.h"
#include "log.h"
#include "common.h"
#include "csnmp.h"
#include "glb_poller.h"
#include "snmp.h"


int snmp_item_oid_to_asn(const char *str_oid, asn1_oid_t* coid);
int snmp_fill_pdu_header(poller_item_t *poller_item, csnmp_pdu_t *pdu, int command);
const char* get_octet_string(csnmp_var_t *cvar, unsigned char *string_type);
char	*snmp_err_to_text(unsigned char type);
int	snmp_set_result(poller_item_t *poller_item, csnmp_var_t *var, AGENT_RESULT *result);