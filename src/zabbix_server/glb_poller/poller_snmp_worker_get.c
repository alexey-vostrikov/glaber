/*
** Glaber
** Copyright (C) 2001-2028 Glaber JSC
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
#include "glb_poller.h"
#include "poller_snmp_worker.h"

void snmp_worker_process_get_response(poller_item_t *poller_item,  struct zbx_json_parse *jp) {
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item); 
    char value[MAX_STRING_LEN];
    zbx_json_type_t type;

    poller_disable_event(snmp_item->timeout_event);
    poller_return_item_to_queue(poller_item);

    //TODO: add timestamps to responces

    if (FAIL == zbx_json_value_by_name(jp, "value", value, MAX_STRING_LEN, &type)) 
        poller_preprocess_error(poller_item, "There was no 'value' field in the response from worker");
    else 
        poller_preprocess_str(poller_item, NULL, value);

}

void snmp_worker_start_get_request(poller_item_t *poller_item, const char *ip_addr) {
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    char request[MAX_STRING_LEN];

    zbx_snprintf(request, MAX_STRING_LEN, "{\"id\":%lld, \"ip\":\"%s\", \"oid\":\"%s\", \"type\":\"get\", %s}", 
                poller_item_get_id(poller_item),
                ip_addr, snmp_item->request_data.get_oid, snmp_item->iface_json_info );
    
    DEBUG_ITEM(poller_item_get_id(poller_item), "Sending get request to snmp worker: '%s'", request);
    snmp_worker_send_request(poller_item, request);
}

int snmp_worker_init_get_item(poller_item_t *poller_item, const char *oid) {
    const char *translated_oid;
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    snmp_item->request_type = SNMP_REQUEST_GET;

    if (NULL == (translated_oid = snmp_worker_parse_oid(oid))) {
        poller_preprocess_error(poller_item, "Cannot translate item OID to digital form, item will not be polled");
        return FAIL;
    }

    snmp_item->request_data.get_oid = poller_strpool_add(translated_oid);

    poller_set_item_specific_data(poller_item, snmp_item);
    return SUCCEED;
}

void snmp_worker_free_get_item(poller_item_t *poller_item) {
    
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    if (NULL == snmp_item) 
        return;

    poller_strpool_free(snmp_item->request_data.get_oid);
}