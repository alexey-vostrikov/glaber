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
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include "csnmp.h"
#include "glb_poller.h"
#include "snmp.h"
#include "../poller/checks_snmp.h"

/* accepts translated oid i.e where only digits are present: .1.2.3.4.5.6 */
int snmp_item_oid_to_asn(const char *str_oid, asn1_oid_t* coid) {
	char tmp_oid[MAX_OID_LEN];
	char *p;
	int i = 0;
	oid p_oid[MAX_OID_LEN];
	size_t oid_len = MAX_OID_LEN;
	
	if (NULL == snmp_parse_oid(str_oid, p_oid, &oid_len)) {
		return FAIL;
	};

	coid->b = zbx_malloc(NULL, sizeof(int) * oid_len);

	for (i = 0; i< oid_len; i++) {
		coid->b[i] = p_oid[i];
	}

	coid->len = oid_len;

	return SUCCEED;
}

int snmp_fill_pdu_header(poller_item_t *poller_item, csnmp_pdu_t *pdu, int command) {
	//static asn1_oid_t oid;
	struct sockaddr_in server;

	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	bzero(pdu, sizeof(csnmp_pdu_t));
	bzero((char*)&server, sizeof(server));
    
	server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(snmp_item->ipaddr);
    server.sin_port = htons(snmp_item->interface_port);
		
	memcpy(&pdu->addr, &server, sizeof(struct sockaddr_in));
	pdu->addr_len = sizeof(struct sockaddr_in);
	
	//pdu->req_id = poller_get_item_id(poller_item);
	pdu->req_id = snmp_item->sessid;

	/*note: pdu->command should be set in the type-specific handler */
	pdu->community.b = zbx_strdup(NULL, snmp_item->community);
	pdu->community.len = strlen(snmp_item->community);
	pdu->command = command;
		
	switch (snmp_item->snmp_version)
	{
		case ZBX_IF_SNMP_VERSION_1:
			pdu->version = SNMP_VERSION_1;
		break;
		case ZBX_IF_SNMP_VERSION_2:
			pdu->version = SNMP_VERSION_2c;
		break;
		default:
			LOG_INF("Unsuppoerted SNMP version in async code");
			
	}

}

void csnmp_oid_2_netsnmp(csnmp_oid_t *c_oid, oid *n_oid) {
	int i;
	for (i =0; i<c_oid->len; i++) {
		n_oid[i] = c_oid->b[i];
	}
}


const char* get_octet_string(csnmp_var_t *cvar, unsigned char *string_type)
{
	const char	*hint;
	static char	buffer[MAX_BUFFER_LEN];
	char		*strval_dyn = NULL;
	struct tree	*subtree;
	unsigned char	type;
	struct variable_list var = {0};
	
	asn1_str_t *val_str = cvar->value;
	var.name = var.name_loc;

	csnmp_oid_2_netsnmp(&cvar->oid, var.name);
	var.name_length = cvar->oid.len;

	subtree = get_tree(var.name, var.name_length, get_tree_head());
	hint = (NULL != subtree ? subtree->hint : NULL);

	var.type = cvar->type;
	var.val.string = val_str->b;
	var.val_len = val_str->len;

	var.val.bitstring = val_str->b;

	/* we will decide if we want the value from var->val or what snprint_value() returned later */
	if (-1 == snprint_value(buffer, sizeof(buffer), var.name, var.name_length, &var))
		return NULL;

	if (0 == strncmp(buffer, "Hex-STRING: ", 12))
	{
		strval_dyn = buffer + 12;
		type = ZBX_SNMP_STR_HEX;
	}
	else if (NULL != hint && 0 == strncmp(buffer, "STRING: ", 8))
	{
		strval_dyn = buffer + 8;
		type = ZBX_SNMP_STR_STRING;
	}
	else if (0 == strncmp(buffer, "OID: ", 5))
	{
		strval_dyn = buffer + 5;
		type = ZBX_SNMP_STR_OID;
	}
	else if (0 == strncmp(buffer, "BITS: ", 6))
	{
	//	strval_dyn = zbx_strdup(strval_dyn, buffer + 6);
		strval_dyn = buffer + 6;
		type = ZBX_SNMP_STR_BITS;
	}
	else
	{
		strval_dyn = buffer;
		memcpy(buffer, val_str->b, val_str->len);
		buffer[val_str->len] = '\0';
		type = ZBX_SNMP_STR_ASCII;
	}

	if (NULL != string_type)
		*string_type = type;

	return strval_dyn;
}

/*note: copied from checks_snmp.c */
char	*snmp_err_to_text(unsigned char type)
{
	switch (type)
	{
		case SNMP_TP_NO_SUCH_INSTANCE:
			return zbx_strdup(NULL, "No Such Object available on this agent at this OID");
		case SNMP_TP_NO_SUCH_OBJ:
			return zbx_strdup(NULL, "No Such Instance currently exists at this OID");
		case SNMP_TP_END_OF_MIB_VIEW:
			return zbx_strdup(NULL, "No more variables left in this MIB View"
					" (it is past the end of the MIB tree)");
		default:
			return zbx_dsprintf(NULL, "Value has unknown type 0x%02X", (unsigned int)type);
	}
}

/*todo: switch to variant support when preprocessing is ready */
int	snmp_set_result(poller_item_t *poller_item, csnmp_var_t *var, AGENT_RESULT *result)
{
	char		*strval_dyn;
	int		ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%d", __func__, (int)var->type);

	switch (var->type) {
		case SNMP_TP_COUNTER64:
    	case SNMP_TP_INT64:
    	case SNMP_TP_UINT64:
    	case SNMP_TP_TIMETICKS: {
			DEBUG_ITEM(poller_get_item_id(poller_item),"Arrived 64bit unsigned value on smnp: %llu",*(u_int64_t*)var->value);
			result->type = AR_UINT64;
			result->ui64 = *(u_int64_t*)var->value;
			
			return SUCCEED;
			break;
		}
		case SNMP_TP_BOOL:
    	case SNMP_TP_INT:
    	case SNMP_TP_COUNTER:
    	case SNMP_TP_GAUGE:
 			if (*(int*)var->value >= 0) {
				result->type = AR_UINT64;
				result->ui64 = *(int*)var->value;
				DEBUG_ITEM(poller_get_item_id(poller_item), "Converted item int to uint type %llu", result->ui64);
			} else {
				result->type = AR_DOUBLE;
				result->dbl = (double)*(int*)var->value;
				DEBUG_ITEM(poller_get_item_id(poller_item), "Converted item int to double type %f", result->dbl);
			}
			
			return SUCCEED;
			
	       	break;
		case SNMP_TP_BIT_STR:
    	case SNMP_TP_OCT_STR: {
			unsigned char str_type = ZBX_SNMP_STR_UNDEFINED;
			const char *parsed_str = get_octet_string(var, &str_type);
			result->type = AR_TEXT;	
			result->text = zbx_strdup(NULL, parsed_str);
			break;
		}
	 	case SNMP_TP_IP_ADDR: {
			asn1_str_t *str = (asn1_str_t *)var->value;

    		DEBUG_ITEM(poller_get_item_id(poller_item), "Processing as an ip addr type");
			SET_STR_RESULT(result, zbx_dsprintf(NULL, "%hhu.%hhu.%hhu.%hhu",
				(unsigned char)str->b[0],
				(unsigned char)str->b[1],
				(unsigned char)str->b[2],
				(unsigned char)str->b[3]));
			break;
		 }
		case SNMP_TP_FLOAT: 
			DEBUG_ITEM(poller_get_item_id(poller_item), "Processing as a float type");
			SET_DBL_RESULT(result, *(float*)var->value);
			break;
		case SNMP_TP_DOUBLE: 
			DEBUG_ITEM(poller_get_item_id(poller_item), "Processing as a double type");
			SET_DBL_RESULT(result, *(double*)var->value);
			break;
		case SNMP_TP_NO_SUCH_OBJ:
			DEBUG_ITEM(poller_get_item_id(poller_item), "Item not supported type or no such object: %s", snmp_err_to_text(var->type));
			SET_MSG_RESULT(result, snmp_err_to_text(var->type));
			ret = NOTSUPPORTED;
			break;
		default: 
			DEBUG_ITEM(poller_get_item_id(poller_item), "Item not supported type: %s", snmp_err_to_text(var->type));
			SET_MSG_RESULT(result, snmp_err_to_text(var->type));
			ret = NOTSUPPORTED;
	}

	return ret;
}
