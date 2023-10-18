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
#include "zbxsysinfo.h"
#include "poller_snmp_worker_discovery.h"
#include "poller_snmp_worker.h"

#define MAX_OID_LEN 128

struct snmp_worker_walk_t {
    const char *address;
    char *oids;
    int count;
    int current_oid;
    char *buffer;
    char *result;
    size_t alloc;
    size_t offset;
}; 

int snmp_worker_walk_need_more_data(snmp_worker_item_t *snmp_item) {
       
    if ( snmp_item->request_data.walk_data->current_oid < 
            ( snmp_item->request_data.walk_data->count - 1) )
        return SUCCEED;

    return FAIL;
}

void snmp_worker_clean_walk_request(snmp_worker_item_t *snmp_item) {
    snmp_worker_walk_t *ddata = snmp_item->request_data.walk_data;
    
    zbx_free(ddata->result);
    ddata->result = NULL;
    ddata->alloc = 0;
    ddata->offset = 0;
    ddata->current_oid = -1;
}

int  snmp_worker_process_walk_response(poller_item_t *poller_item, struct zbx_json_parse *jp) {
    struct zbx_json_parse jp_values, jp_value;
    char oid[MAX_OID_LEN *4], value[MAX_STRING_LEN];
    zbx_json_type_t type;
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    
    snmp_worker_walk_t *ddata = snmp_item->request_data.walk_data;
    
    DEBUG_ITEM(poller_item_get_id(poller_item), "Walk response data is %s", jp->start);
    
    if (FAIL == zbx_json_brackets_by_name(jp, "values", &jp_values))
        return FAIL;

    const char *p = NULL;
    
    while (p = zbx_json_next(&jp_values, p)) {
        if (FAIL == zbx_json_brackets_open(p, &jp_value) ||
            FAIL == zbx_json_value_by_name(&jp_value, "oid", oid, MAX_OID_LEN*4, &type) || 
            FAIL == zbx_json_value_by_name(&jp_value, "value", value, MAX_OID_LEN*4, &type) )
            continue;
       
        zbx_snprintf_alloc(&ddata->result, &ddata->alloc, &ddata->offset, "%s = STRING: \"%s\"\n", oid, value);
    }    

    if (ddata->current_oid + 1 == ddata->count) {
        poller_preprocess_str(poller_item, NULL, ddata->result);
        snmp_worker_clean_walk_request(snmp_item);
        poller_return_item_to_queue(poller_item);
    }

    return SUCCEED;
}

static void snmp_worker_free_walk_data(snmp_worker_walk_t *ddata) {
    poller_strpool_free(ddata->address); 
    zbx_free(ddata->buffer);
    zbx_free(ddata->result);
    zbx_free(ddata->oids);
}

int snmp_worker_init_walk_item(poller_item_t *poller_item, const char *key) {

    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    AGENT_REQUEST req;
    int i, j, printed_count;

    snmp_item->request_type = SNMP_REQUEST_WALK;
    
    zbx_init_agent_request(&req);
    
    char *key_ptrs;
    char *oid_ptrs;
        
    if (SUCCEED != zbx_parse_item_key(key, &req))
	{
		poller_preprocess_error(poller_item, "Invalid SNMP OID: cannot parse expression.");
        zbx_free_agent_request(&req); 
		return FAIL;
	}

    if ( 0 >= req.nparam) {
        poller_preprocess_error(poller_item, "Invalid parameters, should be one or more");
		zbx_free_agent_request(&req); 
		return FAIL;
    }

    for (i = 1; i < req.nparam; i ++)
	{
		for (j = 0; j < i; j ++)
		{
			if (0 == strcmp(req.params[i], req.params[j]))
			{
				poller_preprocess_error(poller_item,  "Invalid SNMP OID: unique oids are expected");
				zbx_free_agent_request(&req);
				return FAIL;
			}
		}
	}

    snmp_worker_walk_t *ddata = zbx_calloc(NULL, 0 , sizeof(snmp_worker_walk_t));
    snmp_item->request_data.walk_data = ddata;

    ddata->count = req.nparam;
    ddata->oids = zbx_malloc(NULL, sizeof(char *) * ddata->count);
    ddata->current_oid = -1;    
    ddata->result = NULL;
    ddata->alloc = 0;
    ddata->offset = 0;

    size_t alloc = 0, offset = 0;

    for (i = 0; i < req.nparam; i ++) {
        const char *oid_str;
   
        ddata->oids[i] = offset;    
        
        if (NULL == ( oid_str = snmp_worker_parse_oid(req.params[i]))) {
            poller_preprocess_error(poller_item,  "Cannot translate one of discovery oids");
            zbx_free_agent_request(&req);
            return FAIL;
        }

        zbx_snprintf_alloc(&ddata->buffer, &alloc, &offset, "%s", oid_str);    
        offset++;
    }
    
    zbx_free_agent_request(&req);
    return SUCCEED;
}

void snmp_worker_start_walk_next_walk(poller_item_t *poller_item, const char*addr) {
    
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    snmp_worker_walk_t *ddata = snmp_item->request_data.walk_data;
      
    if ( -1 == ddata->current_oid && NULL == addr ) //not in polling state and not the firtst call
        return;

    DEBUG_ITEM(poller_item_get_id(poller_item),
        "About to start new discovery, prev oid idx %d count is %d", ddata->current_oid, ddata->count);
   

    if (ddata->current_oid == -1 && NULL != addr) {
        if (NULL != ddata->address) {
             if (0 != strcmp(ddata->address, addr)) {
                poller_strpool_free(ddata->address);
                ddata->address = poller_strpool_add(addr);
            }
        } else 
            ddata->address = poller_strpool_add(addr);
    }
    ddata->current_oid++;
 
    if (ddata->current_oid >= ddata->count) {
        snmp_worker_clean_walk_request(snmp_item);
        poller_return_item_to_queue(poller_item);
 
        return;
    }
 
    char request[MAX_STRING_LEN];

    zbx_snprintf(request, MAX_STRING_LEN, "{\"id\":%lld, \"ip\":\"%s\", \"oid\":\"%s\", \"type\":\"walk\", %s}", 
                poller_item_get_id(poller_item),
                ddata->address, 
                ddata->buffer + ddata->oids[0],
                snmp_item->iface_json_info );
    
    DEBUG_ITEM(poller_item_get_id(poller_item), "Will do discovery request: '%s'", request);
    snmp_worker_send_request(poller_item, request);
}

void snmp_worker_free_walk_item(poller_item_t *poller_item) {
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    if (NULL == snmp_item->request_data.walk_data)
        return;
    
    snmp_worker_clean_walk_request(snmp_item);

    poller_strpool_free(snmp_item->request_data.walk_data->address);
    zbx_free(snmp_item->request_data.walk_data->buffer);
    zbx_free(snmp_item->request_data.walk_data->oids);
}
