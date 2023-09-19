
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
#include "zbxserver.h"
#include "zbx_item_constants.h"
#include "../../libs/zbxexec/worker.h"
#include "../poller/checks_snmp.h"

#include "module.h"
#include "preproc.h"
#include "zbxjson.h"
#include "poller_async_io.h"
#include "zbxsysinfo.h"
#include "zbxip.h"

extern int  CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];
#define SNMP_MAX_OID_LEN    128

typedef struct
{
	u_int64_t time;
	u_int64_t itemid;
} pinger_event_t;


typedef enum {
    SNMP_REQUEST_GET = 1,
    SNMP_REQUEST_WALK,
    SNMP_REQUEST_DISCOVERY
} snmp_request_type_t;

typedef struct {
    const char *oid;
} snmp_worker_one_oid_t;

typedef struct {
    char *macro_names;
    char *oids;
    int count;
} snmp_worker_multi_oid_t;

typedef union 
{
    snmp_worker_one_oid_t get_data;
    snmp_worker_one_oid_t walk_data;
    snmp_worker_multi_oid_t discovery_data;
} snmp_worker_request_data_t;

typedef struct {
    snmp_worker_request_data_t request_data;
    const char *iface_json_info; 
    const char *address;
    unsigned char need_resolve;
    snmp_request_type_t request_type;
    poller_event_t *timeout_event;
} snmp_item_t;

typedef struct {
    glb_worker_t *snmp_worker;
    poller_event_t *worker_event;
} conf_t;

static conf_t conf;

extern char *CONFIG_SNMP_WORKER_LOCATION;
extern char *CONFIG_SOURCE_IP;

void snmp_worker_process_result(poller_item_t *poller_item, int result_code, const char *value) {
    
    if (TIMEOUT_ERROR == result_code || CONFIG_ERROR == result_code) {
         poller_register_item_iface_timeout(poller_item);
    } else 
        poller_register_item_iface_succeed(poller_item);
    
    if (SUCCEED == result_code) {
        poller_preprocess_str(poller_item, NULL, value);
        return;
    }
    poller_preprocess_error(poller_item, value);
}

void finish_snmp_poll(poller_item_t *poller_item, int result_code, const char *result) {
    
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    
    poller_disable_event(snmp_item->timeout_event);
    poller_return_item_to_queue(poller_item);
    snmp_worker_process_result(poller_item, result_code, result);
}

static void process_result(const char *response) {
    struct zbx_json_parse jp_resp;
    u_int64_t itemid, code;
    poller_item_t *poller_item;
    char *buffer = NULL;
    zbx_json_type_t type;
    size_t alloc = 0;

    poller_inc_responses();

    if (SUCCEED != zbx_json_open(response, &jp_resp)) {
		LOG_INF("Couldn't open JSON response from glb_snmp_worker: '%s'", response);
        return;
	}
            
    if (SUCCEED != glb_json_get_uint64_value_by_name(&jp_resp, "id", &itemid ) ||
        SUCCEED != glb_json_get_uint64_value_by_name(&jp_resp, "code", &code))    
    {
        LOG_INF("Cannot parse glb_snmp_worker response: either id or code is missing: '%s'",response);
        return;
    }
    DEBUG_ITEM(itemid, "Received response from worker: '%s'", response);

    if (NULL == (poller_item = poller_get_poller_item(itemid))) {
        LOG_INF("Got response from glb_snmp_worker for non-existing itemid %lld",itemid);
        return;
    }
    
    // if (FAIL == poller_item_is_polled(poller_item)) {
    //     LOG_INF("Arrived response for item %lld which isn't in polling, not processing", itemid);
    //     DEBUG_ITEM(itemid,"Arrived response for item  which isn't in polling, not processing" );
    //     return;
    // }

    switch (code) {
        case 200: 
            if (SUCCEED != zbx_json_value_by_name_dyn(&jp_resp, "value", &buffer, &alloc, &type)){
                LOG_INF("Got response from glb_snmp_worker with missing value: '%s'", response);
                return;
            }
            finish_snmp_poll(poller_item, SUCCEED, buffer);
            break;
        case 408:
            buffer[0]='\0';
            zbx_json_value_by_name_dyn(&jp_resp, "error", &buffer, &alloc, &type);
            finish_snmp_poll(poller_item, TIMEOUT_ERROR, buffer);
            break;

        default:
            LOG_INF("Warning: unsupported code from glb_snmp_worker: %d", code);
            return;
    }
    zbx_free(buffer);
}

void read_worker_results_cb(poller_item_t *poller_item, void *data) {
    char *worker_response = NULL;
    
    while (SUCCEED == glb_worker_get_async_buffered_responce(conf.snmp_worker, &worker_response)) {
        
        if ( NULL == worker_response) //read succesifull, no data yet
            break;
       // LOG_INF("Got response: %s",worker_response);

        process_result(worker_response);
        zbx_free(worker_response);
    }
}

static int subscribe_worker_fd() {
    
    //TODO: could this be persistant?
    if (NULL != conf.worker_event) {
        poller_destroy_event(conf.worker_event);
    }

    conf.worker_event = poller_create_event(NULL, read_worker_results_cb,  
                                worker_get_fd_from_worker(conf.snmp_worker),  NULL, 1);
    poller_run_fd_event(conf.worker_event);
}



void timeout_cb(poller_item_t *poller_item, void *data) {
    DEBUG_ITEM(poller_get_item_id(poller_item), "In item timeout handler, submitting timeout");
    finish_snmp_poll(poller_item, TIMEOUT_ERROR, "Timout connecting to the host");
}

static int isdigital_oid(const char *oid) {
    int i;
    for (i = 0; i < strlen(oid); i++) {
        if ('.' != oid[i] && 0 == isdigit(oid[i]))
            return FAIL;
    }
    return SUCCEED;
}

static int snmp_worker_parse_oid(const char **out_oid, char *in_oid) {
    int i;
	oid p_oid[MAX_OID_LEN];
    char buffer[MAX_OID_LEN *4];

	size_t oid_len = MAX_OID_LEN, pos = 0;

    if (SUCCEED == isdigital_oid(in_oid)) {
        *out_oid = poller_strpool_add(in_oid);
        return SUCCEED;
    }
    
    //TODO: move all net-snmp related thing out to the worker
    if (NULL == snmp_parse_oid(in_oid, p_oid, &oid_len)) {
		return FAIL;
	};

    for (i = 0; i < oid_len; i++)
        pos += zbx_snprintf(buffer + pos, MAX_OID_LEN - pos, ".%d", p_oid[i]);
    
    *out_oid = poller_strpool_add(buffer);
    
    return SUCCEED;
}

void snmp_worker_free_get_request(snmp_worker_request_data_t *request){
    poller_strpool_free(request->get_data.oid);
}

void snmp_worker_free_walk_request(snmp_worker_request_data_t *request) {
     poller_strpool_free(request->walk_data.oid);
}

void snmp_worker_free_discovery_request(snmp_worker_request_data_t *request){
    //HALT_HERE("Not implemented");
    //LOG_INF("Implement discovery item cleaunup");
}
static void free_item(poller_item_t *poller_item ) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

    switch (snmp_item->request_type) {
        case SNMP_REQUEST_GET:
            snmp_worker_free_get_request(&snmp_item->request_data);
            break;
        case SNMP_REQUEST_WALK:
            snmp_worker_free_walk_request(&snmp_item->request_data);
            break;
        case SNMP_REQUEST_DISCOVERY:
            snmp_worker_free_discovery_request(&snmp_item->request_data);
            break;
        default:
            LOG_INF("Trying to free item of request type %d",snmp_item->request_type);
            zbx_backtrace();
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);
    }

    poller_strpool_free(snmp_item->address);
    poller_strpool_free(snmp_item->iface_json_info);

    poller_destroy_event(snmp_item->timeout_event);

    zbx_free(snmp_item);
}

int snmp_worker_init_walk(snmp_item_t *snmp_item, DC_ITEM *dc_item) {
    //walk items has only one oid just as get items but they are used for walks
    snmp_item->request_type = SNMP_REQUEST_WALK;
    if (FAIL == snmp_worker_parse_oid(&snmp_item->request_data.walk_data.oid, dc_item->snmp_oid))
        return FAIL;
    
    
    return SUCCEED;
}

int snmp_worker_init_get(snmp_item_t *snmp_item, DC_ITEM *dc_item) {
    //walk items has only one oid just as get items but they are used for walks
    snmp_item->request_type = SNMP_REQUEST_GET;
    if (FAIL == snmp_worker_parse_oid(&snmp_item->request_data.get_data.oid, dc_item->snmp_oid))
        return FAIL;
    
    
    return SUCCEED;
}

int snmp_worker_init_discovery(snmp_item_t *snmp_item, DC_ITEM *dc_item) {
    //for discovery items: parsing oid into single string with null separated macros and values
    //count the params, creating results array where we'll keep the walk results and 
    //will do the iteration
    snmp_item->request_type = SNMP_REQUEST_DISCOVERY;
    //it's reasonable to do this after first succesifull v2 tests
     
    //HALT_HERE("Discovery init isn't ready yet");
    return FAIL;
}

static int init_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
    //char translated_oid[4 * SNMP_MAX_OID_LEN];
 	char buffer[MAX_STRING_LEN];
    char *addr;
    snmp_item_t *snmp_item;
    int ret = FAIL;

    // 	/* todo: if possible, do oid resolve without net-snmp*/
    if (NULL == dc_item->snmp_oid || 
        dc_item->snmp_oid[0] == '\0' )
        //FAIL == snmp_worker_parse_oid(translated_oid, dc_item->snmp_oid))
 	{
 		DEBUG_ITEM(dc_item->itemid, "Empty oid for item, item will not be polled until OID is set ");
 		poller_preprocess_error(poller_item, "Error: empty OID, item will not be polled until OID is set");
        LOG_INF("Item with an empty oid");
 		return FAIL;
 	}
    
    if (NULL == (snmp_item = zbx_calloc(NULL, 0, sizeof(snmp_item_t)))) {
        LOG_WRN("Cannot allocate memory for snmp item, not enough RSS memory, exiting");
        exit(-1);
    };

    poller_set_item_specific_data(poller_item, snmp_item);

    if ( strstr(dc_item->snmp_oid,"walk[") )
        ret = snmp_worker_init_walk(snmp_item, dc_item);
    else 
    if ( strstr(dc_item->snmp_oid,"discovery["))
        ret = snmp_worker_init_discovery(snmp_item, dc_item);
    else 
        ret = snmp_worker_init_get(snmp_item, dc_item);
    



    if (FAIL == ret) {
        DEBUG_ITEM(dc_item->itemid, "Couldn't init item, wrong oid value: '%s'", dc_item->snmp_oid);
        free_item(poller_item);
        return FAIL;
    }

    int version;

 	switch (dc_item->snmp_version) { //cannot rely on constants, do a "recoding"
        case ZBX_IF_SNMP_VERSION_1:
            version = 1;
            break;
        case ZBX_IF_SNMP_VERSION_2:
            version = 2;
            break;
        case ZBX_IF_SNMP_VERSION_3:
            version = 3;
            break;
    }
	    
    //TODO: bulk param support at least, for walks, normal items will need grouping and aggregating

    zbx_snprintf(buffer, MAX_STRING_LEN, "\"port\":%d, \"community\":\"%s\", \"version\":%d",
                                      dc_item->interface.port, dc_item->snmp_community, version);
	
    if (dc_item->interface.useip) {
        addr = dc_item->interface.ip_orig;
        snmp_item->need_resolve = 0;
    } else {
        addr = dc_item->interface.dns_orig;
        snmp_item->need_resolve = 1;
    }

    snmp_item->iface_json_info = poller_strpool_add(buffer); 
    snmp_item->address = poller_strpool_add(addr);
    snmp_item->timeout_event = poller_create_event(poller_item, timeout_cb, 0, NULL, 0);

    return SUCCEED;
}

int snmp_worker_send_request(poller_item_t *poller_item, const char *request) {

    static int last_worker_pid = 0;
    
    DEBUG_ITEM(poller_get_item_id(poller_item), "Sending item poll request to the worker: '%s'", request);

    if (SUCCEED != glb_worker_send_request(conf.snmp_worker, request) ) {
        LOG_INF("Couldn't send request %s", request);
        DEBUG_ITEM(poller_get_item_id(poller_item), "Couldn't send request for snmp: %s",request);
        finish_snmp_poll(poller_item, CONFIG_ERROR ,"Couldn't start or pass request to glb_snmp_worker");
        return FAIL;
    }
//    LOG_INF("Worker's pid is %d", last_worker_pid);

    if (last_worker_pid != glb_worker_get_pid(conf.snmp_worker)) {
        LOG_INF("glb_snmp_worker worker's PID (%d) is changed, subscibing the new FD (%d)", last_worker_pid, glb_worker_get_pid(conf.snmp_worker));
        last_worker_pid = glb_worker_get_pid(conf.snmp_worker);
        subscribe_worker_fd(worker_get_fd_from_worker(conf.snmp_worker));
    }
    poller_inc_requests();
    return SUCCEED;
}

void snmp_worker_start_get_request(poller_item_t *poller_item, const char *ipaddr) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

    char request[MAX_STRING_LEN];

    zbx_snprintf(request, MAX_STRING_LEN, "{\"id\":%lld, \"ip\":\"%s\", \"oid\":\"%s\", \"type\":\"get\", %s}", 
                poller_get_item_id(poller_item),
                ipaddr, snmp_item->request_data.get_data.oid, snmp_item->iface_json_info );
    
    //LOG_INF("Will do request: '%s'", request);

    snmp_worker_send_request(poller_item, request);
}

static void start_snmp_poll(poller_item_t *poller_item, const char *resolved_address) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
  //  LOG_INF("Addr is %s, Net data is %s",resolved_address, snmp_item->iface_json_info);
    
    switch (snmp_item->request_type) {
        case SNMP_REQUEST_GET:
            snmp_worker_start_get_request( poller_item, resolved_address);
            return;
            //break;
     //   case SNMP_DISCOVERY_WALK:
     //       snmp_worker_start_discovery_walk(resolved_address, snmp_item);
     //       break;
     //   case SNMP_WALK:
     //       snmp_worker_start_walk(resolved_address, snmp_item);
     //       break;
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);
    }
    
    HALT_HERE("Need to implement starting and processing of the get, walk, and discovery walk");
    HALT_HERE("Also, parsing and translating of the walk and discovery items needs to be implemented either");
    HALT_HERE("OMG ! we are polling now item %lld", poller_get_item_id(poller_item));    

}

static void resolved_callback(poller_item_t *poller_item, const char *resolved_address) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
   // LOG_INF("Name %s resolved to %s", snmp_item->address, resolved_address);
    start_snmp_poll(poller_item, resolved_address);
}

static void resolve_fail_callback(poller_item_t *poller_item) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
    finish_snmp_poll(poller_item, CONFIG_ERROR, "Failed to resolve host name");
}

static void   start_poll(poller_item_t *poller_item) {
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
        
    if (snmp_item->need_resolve) {
        poller_async_resolve(poller_item, snmp_item->address);
    }
    start_snmp_poll(poller_item, snmp_item->address);
}

static void handle_async_io(void)
{ 
//TODO: make it possible to ommit handle_async_io func at all
    RUN_ONCE_IN(30);
    if (FAIL == glb_worker_is_alive(conf.snmp_worker))
        glb_worker_restart(conf.snmp_worker, "Restarted due to fail in periodic check");
    subscribe_worker_fd();
}

static int forks_count(void) {
	return CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP_WORKER];
}

void snmp_worker_shutdown(void) {
	
}

void glb_snmp_worker_init(void) {

	char args[MAX_STRING_LEN];

    bzero(&conf, sizeof(conf_t));
    args[0] = '\0';

    //TODO: get rid of snmp translation in library, move to a worker
    init_snmp(progname);

    poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_poll, snmp_worker_shutdown, 
                                         forks_count,  resolved_callback, resolve_fail_callback, "snmp", 0);    
 
    if (-1 == access(CONFIG_SNMP_WORKER_LOCATION, X_OK) )
 	{
        LOG_INF("Couldn't find glb_snmp_worker at the path: %s or it isn't set to be executable: %s", 
                CONFIG_SNMP_WORKER_LOCATION, zbx_strerror(errno));
 		exit(-1);
 	};

    if ( NULL != CONFIG_SOURCE_IP) {
        zbx_snprintf(args, MAX_STRING_LEN,"-S %s ",CONFIG_SOURCE_IP);
    } 
    LOG_INF("Will run SNMP  worker %s",CONFIG_SNMP_WORKER_LOCATION );
    conf.snmp_worker = glb_worker_init(CONFIG_SNMP_WORKER_LOCATION, args, 30, 0, 0, 0);
    
    if (NULL == conf.snmp_worker) {
        LOG_INF("Cannot create SNMP woker, check the coniguration: path '%s', args %s", CONFIG_SNMP_WORKER_LOCATION, args);
         exit(-1);
    }

    worker_set_mode_to_worker(conf.snmp_worker, GLB_WORKER_MODE_NEWLINE);
    worker_set_mode_from_worker(conf.snmp_worker, GLB_WORKER_MODE_NEWLINE);

 
    LOG_DBG("In %s: Ended", __func__);
}