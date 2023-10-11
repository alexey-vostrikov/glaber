
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

typedef struct
{
	char	*index;
	char	**values;
}
snmp_dobject_t;

struct snmp_worker_discovery_t {
    const char *address;
    char *macro_names;
    char *oids;
    int count;
    int current_oid;
    char *buffer;
    zbx_hashset_t results;
}; 

int snmp_worker_discovery_need_more_data(snmp_worker_item_t *snmp_item) {
       
    if ( snmp_item->request_data.discovery_data->current_oid < 
            ( snmp_item->request_data.discovery_data->count - 1) )
        return SUCCEED;

    return FAIL;
}

void	snmp_walk_save_result_value(snmp_worker_discovery_t *ddata, const char *index, const char *value)
{
    snmp_dobject_t	*obj;
	
	if (NULL == (obj = (snmp_dobject_t *)zbx_hashset_search(&ddata->results, &index)))
	{
		snmp_dobject_t	new_obj;

		new_obj.index = zbx_strdup(NULL, index);
		new_obj.values = (char **)zbx_malloc(NULL, sizeof(char *) * ddata->count );
		memset(new_obj.values, 0, sizeof(char *) * ddata->count );

		obj = (snmp_dobject_t *)zbx_hashset_insert(&ddata->results, &new_obj, sizeof(new_obj));
	}
	obj->values[ddata->current_oid] = zbx_strdup(NULL, value);
}

static void snmp_worker_discovery_submit_result(poller_item_t *poller_item) {
//    HALT_HERE("Result submission isn't implemented");
    zbx_hashset_iter_t iter;
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    snmp_worker_discovery_t *ddata = snmp_item->request_data.discovery_data;
    

    snmp_dobject_t *obj;
    struct zbx_json js;
    int i;

    zbx_json_initarray(&js, ZBX_JSON_STAT_BUF_LEN);
    zbx_hashset_iter_reset(&ddata->results, &iter);
 	
    while (NULL != (obj = zbx_hashset_iter_next(&iter))) {
    
        zbx_json_addobject(&js, NULL);
	    zbx_json_addstring(&js, "{#SNMPINDEX}", obj->index, ZBX_JSON_TYPE_STRING);

        for (i = 0; i < ddata->count; i++)
	    {
	   			if (NULL == obj->values[i])
				continue;

			zbx_json_addstring(&js, ddata->buffer+ ddata->macro_names[i], obj->values[i], ZBX_JSON_TYPE_STRING);
		}
		zbx_json_close(&js);
	}

	
	zbx_json_close(&js);
    DEBUG_ITEM(poller_item_get_id(poller_item),"Finished walk for the item, result is %s", js.buffer);
    
 //   LOG_INF("Finished walk");
 //  LOG_INF("Finished walk for the item %lld, oid is %s result is %s", poller_item_get_id(poller_item),
 //       snmp_item->request_data.discovery_data->buffer + snmp_item->request_data.discovery_data->oids[snmp_item->request_data.discovery_data->current_oid],
 //       js.buffer);
 // LOG_INF("Finished walk1");
	poller_preprocess_str(poller_item, NULL, js.buffer);
  //    LOG_INF("Finished walk2");
    zbx_json_free(&js);
 // LOG_INF("Finished walk3");
//	poller_iface_register_succeed(poller_item);
//  zbx_hashset_iter_reset(&snmp_item->request_data.discovery_data->results, &iter);

}

int  snmp_worker_process_discovery_response(poller_item_t *poller_item, struct zbx_json_parse *jp) {
    struct zbx_json_parse jp_values, jp_value;
    char oid[MAX_OID_LEN *4], value[MAX_STRING_LEN];
    zbx_json_type_t type;
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    
    snmp_worker_discovery_t *ddata = snmp_item->request_data.discovery_data;
    
    DEBUG_ITEM(poller_item_get_id(poller_item), "Discovery response data is %s", jp->start);

    const char *walk_oid = ddata->buffer + ddata->oids[ddata->current_oid];
    
    const char *macro = ddata->buffer + ddata->macro_names[ddata->current_oid];

    
    DEBUG_ITEM(poller_item_get_id(poller_item),"Walk oid is %s", walk_oid);
    
    if (FAIL == zbx_json_brackets_by_name(jp, "values", &jp_values))
        return FAIL;

    const char *p = NULL;
    
    while (p = zbx_json_next(&jp_values, p)) {
        if (FAIL == zbx_json_brackets_open(p, &jp_value) ||
            FAIL == zbx_json_value_by_name(&jp_value, "oid", oid, MAX_OID_LEN*4, &type) || 
            FAIL == zbx_json_value_by_name(&jp_value, "value", value, MAX_OID_LEN*4, &type) )
            continue;
        
        const char *idx;
        if (0 != strncmp(walk_oid, oid, strlen(walk_oid))) {
           // LOG_INF("Returned oid(%s) isn't withing index of walk(%s), skipping", oid, walk_oid);
                      
            //handling missing '.' at the oid start in the config
            if (walk_oid[0] == '.' || oid[0] != '.') 
                continue;
            
            if (0 != strncmp(walk_oid, oid + 1, strlen(walk_oid)))
                continue;
            
            idx = oid + strlen(walk_oid) + 2;
        } else 
            idx = oid + strlen(walk_oid) + 1;

        snmp_walk_save_result_value(ddata, idx, value);        
    }    

    if (ddata->current_oid + 1 == ddata->count) {
        snmp_worker_discovery_submit_result(poller_item);
        snmp_worker_clean_discovery_request(snmp_item);
    }

    return SUCCEED;
}

static void snmp_worker_free_discovery_data(snmp_worker_discovery_t *ddata) {
    poller_strpool_free(ddata->address); 
    zbx_free(ddata->buffer);
    zbx_free(ddata->macro_names);
    zbx_free(ddata->oids);
}

/* discovery objects hashset support */
static zbx_hash_t	zbx_snmp_dobject_hash(const void *data)
{
	const char	*index = *(const char **)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(index, strlen(index), ZBX_DEFAULT_HASH_SEED);
}

static int	zbx_snmp_dobject_compare(const void *d1, const void *d2)
{
	const char	*i1 = *(const char **)d1;
	const char	*i2 = *(const char **)d2;

	return strcmp(i1, i2);
}

int snmp_worker_init_discovery_item(poller_item_t *poller_item, const char *key) {

    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    AGENT_REQUEST req;
    int i, j, printed_count;

    snmp_item->request_type = SNMP_REQUEST_DISCOVERY;
    
    zbx_init_agent_request(&req);
    
    char *key_ptrs;
    char *oid_ptrs;
        
    if (SUCCEED != zbx_parse_item_key(key, &req))
	{
		poller_preprocess_error(poller_item, "Invalid SNMP OID: cannot parse expression.");
        zbx_free_agent_request(&req); 
		return FAIL;
	}

    if ( 1 >= req.nparam) {
        poller_preprocess_error(poller_item, "Invalid parameters, should be two or more");
		zbx_free_agent_request(&req); 
		return FAIL;
    }

    for (i = 0; i < req.nparam; i += 2)
	{
		if (SUCCEED != zbx_is_discovery_macro(req.params[i]))
		{
			poller_preprocess_error(poller_item, "Invalid SNMP OID: macro is invalid");
			zbx_free_agent_request(&req);
			return FAIL;
		}

		if (0 == strcmp(req.params[i], "{#SNMPINDEX}"))
		{
			poller_preprocess_error(poller_item,  "Invalid SNMP OID: macro \"{#SNMPINDEX}\" is not allowed.");
			zbx_free_agent_request(&req);
			return FAIL;
		}
	}
    
	for (i = 2; i < req.nparam; i += 2)
	{
		for (j = 0; j < i; j += 2)
		{
			if (0 == strcmp(req.params[i], req.params[j]))
			{
				poller_preprocess_error(poller_item,  "Invalid SNMP OID: unique macros are expected.");
				zbx_free_agent_request(&req);
				return FAIL;
			}
		}
	}

    snmp_worker_discovery_t *ddata = zbx_calloc(NULL, 0 , sizeof(snmp_worker_discovery_t));
    snmp_item->request_data.discovery_data = ddata;

    ddata->count = req.nparam/2;;
    ddata->macro_names = zbx_malloc(NULL, sizeof(char *) * ddata->count);
    ddata->oids = zbx_malloc(NULL, sizeof(char *) * ddata->count);
    ddata->current_oid = -1;    

    size_t alloc = 0, offset = 0;

    for (i = 0; i < req.nparam; i += 2) {
        const char *oid_str;

        ddata->macro_names[i/2] = offset;
        zbx_snprintf_alloc(&ddata->buffer, &alloc, &offset, "%s", req.params[i]);
        
        offset++;
        ddata->oids[i/2] = offset;    
        
        if (NULL == ( oid_str = snmp_worker_parse_oid(req.params[i+1]))) {
            poller_preprocess_error(poller_item,  "Cannot translate one of discovery oids");
            zbx_free_agent_request(&req);
            return FAIL;
        }

        zbx_snprintf_alloc(&ddata->buffer, &alloc, &offset, "%s", oid_str);    
        offset++;
    }
    
   
    zbx_free_agent_request(&req);
    zbx_hashset_create(&ddata->results, 10, zbx_snmp_dobject_hash, zbx_snmp_dobject_compare);

    return SUCCEED;
}

void snmp_worker_start_discovery_next_walk(poller_item_t *poller_item, const char*addr) {
    
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    snmp_worker_discovery_t *ddata = snmp_item->request_data.discovery_data;
    
    
    if ( -1 == ddata->current_oid && NULL == addr ) //not in polling state and not the firtst call
        return;

    DEBUG_ITEM(poller_item_get_id(poller_item),"About to start new discovery, prev oid idx %d count is %d", ddata->current_oid, ddata->count);
   // LOG_INF("Stage wegwe1");

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
     //   snmp_worker_discovery_submit_result(poller_item);
    //    LOG_INF("Stage wegwe31");
        snmp_worker_clean_discovery_request(snmp_item);
    //    LOG_INF("Stage wegwe32");
        poller_return_item_to_queue(poller_item);
    //    LOG_INF("Stage wegwe33");
        return;
    }
 //   LOG_INF("Stage wegwe4");
    char request[MAX_STRING_LEN];

    zbx_snprintf(request, MAX_STRING_LEN, "{\"id\":%lld, \"ip\":\"%s\", \"oid\":\"%s\", \"type\":\"walk\", %s}", 
                poller_item_get_id(poller_item),
                ddata->address, 
                ddata->buffer + ddata->oids[0],
                snmp_item->iface_json_info );
    
    DEBUG_ITEM(poller_item_get_id(poller_item), "Will do discovery request: '%s'", request);
    snmp_worker_send_request(poller_item, request);

}

void snmp_worker_clean_discovery_request(snmp_worker_item_t *snmp_item) {
    snmp_worker_discovery_t *ddata = snmp_item->request_data.discovery_data;
    snmp_dobject_t *obj;
    zbx_hashset_iter_t iter;
    int i;
   
    zbx_hashset_iter_reset(&ddata->results, &iter);
	
    while (NULL != (obj = zbx_hashset_iter_next(&iter))) {
        for (i = 0; i < ddata->count; i++)
	    	if (NULL != obj->values[i])
    			zbx_free(obj->values[i]);

		zbx_free(obj->index);
		zbx_free(obj->values);
	}
	zbx_hashset_clear(&ddata->results);
    snmp_item->request_data.discovery_data->current_oid = -1;
}

void snmp_worker_free_discovery_item(poller_item_t *poller_item) {
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    if (NULL == snmp_item->request_data.discovery_data)
        return;
    
    snmp_worker_clean_discovery_request(snmp_item);

    poller_strpool_free(snmp_item->request_data.discovery_data->address);
    zbx_free(snmp_item->request_data.discovery_data->buffer);
    zbx_free(snmp_item->request_data.discovery_data->oids);
    zbx_free(snmp_item->request_data.discovery_data->macro_names);
}