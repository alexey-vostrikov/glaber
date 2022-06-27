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
#include "zbxalgo.h"
#include "glb_poller.h"
#include "csnmp.h"
#include "snmp.h"
#include "poller_async_io.h"
#include "snmp_util.h"

int snmp_get_send_request(poller_item_t *poller_item) {
	csnmp_pdu_t pdu;
	asn1_oid_t oid;
	
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	snmp_fill_pdu_header(poller_item, &pdu, SNMP_CMD_GET);
		
	if (FAIL == snmp_item_oid_to_asn(snmp_item->oid, &oid)) {
	 	poller_preprocess_error(poller_item, "Cannot parse oid");
	 	return FAIL;
	}
    
    csnmp_add_var(&pdu, oid, SNMP_TP_NULL, NULL);
	snmp_send_packet(poller_item, &pdu);

	csnmp_free_pdu(&pdu);
    return SUCCEED;
}

void snmp_get_timeout(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	DEBUG_ITEM(poller_get_item_id(poller_item),"Registered timeout for the item, try %d of %d", snmp_item->retries + 1 , SNMP_MAX_RETRIES);

	if (snmp_item->retries >= SNMP_MAX_RETRIES) {
		DEBUG_ITEM(poller_get_item_id(poller_item), "Item timed out after try %d",  snmp_item->retries );
		poller_return_item_to_queue(poller_item);
		poller_preprocess_error(poller_item, "Timeout waiting for the responce");
		return;
	} else {
		snmp_item->retries++;
		DEBUG_ITEM(poller_get_item_id(poller_item),"Sending item, try %d",  snmp_item->retries);
		snmp_get_send_request(poller_item);
	}
}

void snmp_get_process_result(poller_item_t *poller_item, const csnmp_pdu_t* pdu) {
	u_int64_t mstime=glb_ms_time();
 	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	poller_return_item_to_queue(poller_item);
	
	if (SNMP_ERR_OK == pdu->error_status) {  
		int i;

		for (i = 0; i < pdu->vars_len; i++) {

			AGENT_RESULT result = {0};
			init_result(&result);

 			if (SUCCEED == snmp_set_result(poller_item, &pdu->vars[i], &result)) {
				DEBUG_ITEM(poller_get_item_id(poller_item),"Async SNMP SUCCEED RESULT processing for the item, type is %d", result.type);
				poller_preprocess_value(poller_item, &result ,mstime, ITEM_STATE_NORMAL, NULL);
				poller_register_item_succeed(poller_item);
			} else {
				DEBUG_ITEM(poller_get_item_id(poller_item), "Async SNMP FAILED RESULT processing for the item: %s", result.msg );
				poller_preprocess_value(poller_item, NULL, mstime,ITEM_STATE_NOTSUPPORTED, result.msg);
 			}

			free_result(&result);
		}	
 	} else 
 			poller_preprocess_value(poller_item, NULL, mstime, ITEM_STATE_NOTSUPPORTED, "Got responce PDU with error indication");
}