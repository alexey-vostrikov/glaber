

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

#include "glb_poller.h"
#include "poller_async_io.h"
#include "poller_snmp_worker.h"

void snmp_worker_free_walk_item(poller_item_t *poller_item) {
    
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    if (NULL == snmp_item) 
        return;

    poller_strpool_free(snmp_item->request_data.walk_oid);
}

void snmp_worker_process_walk_response(poller_item_t *poller_item,  struct zbx_json_parse *jp) {
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item); 
    char value[MAX_STRING_LEN];
    zbx_json_type_t type;

    poller_disable_event(snmp_item->timeout_event);
    poller_return_item_to_queue(poller_item);

    //TODO: add timestamps to responces

    // if (FAIL == zbx_json_value_by_name(jp, "value", value, MAX_STRING_LEN, &type)) 
    //     poller_preprocess_error(poller_item, "There was no 'value' field in the response from worker");
    // else 
    //     poller_preprocess_str(poller_item, NULL, value);
    HALT_HERE("Walk result processing is not implemented");

}
int  snmp_worker_init_walk_item(poller_item_t *poller_item, const char *key) {
    HALT_HERE("Not implemented");
    //snmp_item->request_type = SNMP_REQUEST_WALK;
}

void snmp_worker_start_walk_request(poller_item_t *poller_item, const char *resolved_address) {
    HALT_HERE("Not implemented");
}