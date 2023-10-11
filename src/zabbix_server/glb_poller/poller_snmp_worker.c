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
#include "poller_snmp_worker_discovery.h"
#include "poller_snmp_worker_get.h"
#include "poller_snmp_worker_walk.h"
#include "poller_contention.h"

extern int CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];
#define SNMP_MAX_OID_LEN 128

static void process_result(const char *response);

typedef struct
{
    glb_worker_t *snmp_worker;
    poller_event_t *worker_event;
} conf_t;

static conf_t conf;

extern char *CONFIG_SNMP_WORKER_LOCATION;
extern char *CONFIG_SOURCE_IP;

static void read_worker_results_cb(poller_item_t *poller_item, void *data)
{
    char *worker_response = NULL;
    //  LOG_INF("Reading data from the worker");
    while (SUCCEED == glb_worker_get_async_buffered_responce(conf.snmp_worker, &worker_response))
    {

        if (NULL == worker_response) // read succesifull, no data yet
            break;
        // LOG_INF("Got response: %s",worker_response);

        process_result(worker_response);
        //  LOG_INF("Response was processed");
        zbx_free(worker_response);
    }
}

static int subscribe_worker_fd()
{

    // TODO: could this be persistant?
    if (NULL != conf.worker_event)
    {
        poller_destroy_event(conf.worker_event);
    }

    conf.worker_event = poller_create_event(NULL, read_worker_results_cb,
                                            worker_get_fd_from_worker(conf.snmp_worker), NULL, 1);
    poller_run_fd_event(conf.worker_event);
}

static void snmp_worker_finish_poll(poller_item_t *poller_item)
{

    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    poller_contention_remove_session(snmp_item->address);
    poller_disable_event(snmp_item->timeout_event);
    poller_return_item_to_queue(poller_item);
}

int snmp_worker_send_request(poller_item_t *poller_item, const char *request)
{

    static int last_worker_pid = 0;

    DEBUG_ITEM(poller_item_get_id(poller_item), "Sending item poll request to the worker: '%s'", request);

    if (SUCCEED != glb_worker_send_request(conf.snmp_worker, request))
    {
        LOG_INF("Couldn't send request %s", request);
        DEBUG_ITEM(poller_item_get_id(poller_item), "Couldn't send request for snmp: %s", request);
        poller_preprocess_error(poller_item, "Cannot send snmp request");
        snmp_worker_finish_poll(poller_item);
        return FAIL;
    }

    if (last_worker_pid != glb_worker_get_pid(conf.snmp_worker))
    {
        LOG_INF("glb_snmp_worker worker's PID (%d) is changed, subscibing the new FD (%d)", last_worker_pid, glb_worker_get_pid(conf.snmp_worker));
        last_worker_pid = glb_worker_get_pid(conf.snmp_worker);
        subscribe_worker_fd(worker_get_fd_from_worker(conf.snmp_worker));
    }
    poller_inc_requests();

    return SUCCEED;
}

static void process_result(const char *response)
{
    struct zbx_json_parse jp_resp;
    u_int64_t itemid, code;
    poller_item_t *poller_item;
    zbx_json_type_t type;
    size_t alloc = 0;

    poller_inc_responses();

    if (SUCCEED != zbx_json_open(response, &jp_resp))
    {
        LOG_INF("Couldn't open JSON response from glb_snmp_worker: '%s'", response);
        return;
    }

    if (SUCCEED != glb_json_get_uint64_value_by_name(&jp_resp, "id", &itemid) ||
        SUCCEED != glb_json_get_uint64_value_by_name(&jp_resp, "code", &code))
    {
        LOG_INF("Cannot parse glb_snmp_worker response: either id or code is missing: '%s'", response);
        return;
    }

    DEBUG_ITEM(itemid, "Received response from worker: '%s'", response);

    if (NULL == (poller_item = poller_get_poller_item(itemid)))
    {
        DEBUG_ITEM(itemid, "Got response from glb_snmp_worker for non-existing itemid");
        return;
    }

    DEBUG_ITEM(itemid, "Parsed itemid result code %lld ", code);

    if (200 == code)
    {
        snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

        switch (snmp_item->request_type)
        {
        case SNMP_REQUEST_GET:
            snmp_worker_process_get_response(poller_item, &jp_resp);
            break;

        case SNMP_REQUEST_DISCOVERY:
            snmp_worker_process_discovery_response(poller_item, &jp_resp);

            if (SUCCEED == snmp_worker_discovery_need_more_data(snmp_item))
            {
                DEBUG_ITEM(poller_item_get_id(poller_item), "Item poll isn't finished, continue next oid");
                snmp_worker_start_discovery_next_walk(poller_item, NULL);
                return;
            }

            snmp_worker_clean_discovery_request(snmp_item);
            break;

        case SNMP_REQUEST_WALK:
            snmp_worker_process_walk_response(poller_item, &jp_resp);
            break;
        }
        DEBUG_ITEM(poller_item_get_id(poller_item), "Item poll is finished");
        snmp_worker_finish_poll(poller_item);
        return;
    }

    if (408 == code || 503 == code)
    {
        char err_buffer[MAX_STRING_LEN];
        zbx_json_value_by_name(&jp_resp, "error", err_buffer, MAX_STRING_LEN, &type);

        DEBUG_ITEM(itemid, "Got error %s", err_buffer);
        poller_preprocess_error(poller_item, err_buffer);
        poller_iface_register_timeout(poller_item);

        snmp_worker_finish_poll(poller_item);
        return;
    }

    LOG_INF("Warning: unsupported code from glb_snmp_worker: %d", code);
}

static void timeout_cb(poller_item_t *poller_item, void *data)
{
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    DEBUG_ITEM(poller_item_get_id(poller_item), "In item timeout handler, submitting timeout");

    switch (snmp_item->request_type)
    {
    case SNMP_REQUEST_GET:
    case SNMP_REQUEST_WALK:
        break;

    case SNMP_REQUEST_DISCOVERY:
        snmp_worker_clean_discovery_request(snmp_item);
        break;
    }

    poller_iface_register_timeout(poller_item);
    snmp_worker_finish_poll(poller_item);
}

static int isdigital_oid(const char *oid)
{
    int i;
    for (i = 0; i < strlen(oid); i++)
    {
        if ('.' != oid[i] && 0 == isdigit(oid[i]))
            return FAIL;
    }
    return SUCCEED;
}

const char *snmp_worker_parse_oid(const char *in_oid)
{
    int i;
    oid p_oid[MAX_OID_LEN];
    static char buffer[MAX_OID_LEN * 4];

    size_t oid_len = MAX_OID_LEN, pos = 0;

    if (SUCCEED == isdigital_oid(in_oid))
        return in_oid;

    if (NULL == snmp_parse_oid(in_oid, p_oid, &oid_len))
        return NULL;

    for (i = 0; i < oid_len; i++)
        pos += zbx_snprintf(buffer + pos, MAX_OID_LEN - pos, ".%d", p_oid[i]);

    return buffer;
}

static void free_item(poller_item_t *poller_item)
{
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    switch (snmp_item->request_type)
    {
    case SNMP_REQUEST_GET:
        // LOG_INF("Freeng normal item");
        snmp_worker_free_get_item(poller_item);
        break;
    case SNMP_REQUEST_WALK:
        snmp_worker_free_walk_item(poller_item);
        break;
    case SNMP_REQUEST_DISCOVERY:
        // LOG_INF("Freeng discovery item");
        snmp_worker_free_discovery_item(poller_item);
        break;
    default:
        LOG_INF("Unknown snmp item type %d", snmp_item->request_type);
        zbx_backtrace();
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }
    // LOG_INF("doing coommon free");
    poller_strpool_free(snmp_item->address);
    // LOG_INF("free addr");
    poller_strpool_free(snmp_item->iface_json_info);
    // LOG_INF("destroy event");
    poller_destroy_event(snmp_item->timeout_event);
    // LOG_INF("freeing item %p", snmp_item);
    zbx_free(snmp_item);
    // LOG_INF("Fisnished");
}

char *snmv3_security_level_name(unsigned char level)
{
    switch (level)
    {
    case ZBX_ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV:
        return "no";
    case ZBX_ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV:
        return "authnopriv";
    case ZBX_ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV:
        return "authpriv";
    default:
        HALT_HERE("Unknown SNMP security level %d", level);
    }
}

char *snmpv3_auth_proto_name(unsigned char auth_proto)
{
    switch (auth_proto)
    {
    case ITEM_SNMPV3_AUTHPROTOCOL_MD5:
        return "MD5";
    case ITEM_SNMPV3_AUTHPROTOCOL_SHA1:
        return "SHA1";
    case ITEM_SNMPV3_AUTHPROTOCOL_SHA224:
        return "SHA224";
    case ITEM_SNMPV3_AUTHPROTOCOL_SHA256:
        return "SHA256";
    case ITEM_SNMPV3_AUTHPROTOCOL_SHA384:
        return "SHA384";
    case ITEM_SNMPV3_AUTHPROTOCOL_SHA512:
        return "SHA512";
    default:
        HALT_HERE("Unknown SNMP auth proto %d", auth_proto);
    }
}

char *snmpv3_priv_proto_name(unsigned char priv_proto)
{
    switch (priv_proto)
    {
    case ITEM_SNMPV3_PRIVPROTOCOL_DES:
        return "DES";
    case ITEM_SNMPV3_PRIVPROTOCOL_AES128:
        return "AES128";
    case ITEM_SNMPV3_PRIVPROTOCOL_AES192:
        return "AES192";
    case ITEM_SNMPV3_PRIVPROTOCOL_AES256:
        return "AES256";
    case ITEM_SNMPV3_PRIVPROTOCOL_AES192C:
        return "AES192C";
    case ITEM_SNMPV3_PRIVPROTOCOL_AES256C:
        return "AES256C";
    default:
        HALT_HERE("Unknown SNMP priv proto %d", priv_proto);
    }
}

static int init_item(DC_ITEM *dc_item, poller_item_t *poller_item)
{

    char buffer[MAX_STRING_LEN];
    char *addr;
    snmp_worker_item_t *snmp_item;
    int ret = FAIL;
    char *error = NULL;

    if (NULL == dc_item->snmp_oid ||
        dc_item->snmp_oid[0] == '\0')
    {
        DEBUG_ITEM(dc_item->itemid, "Empty oid for item, item will not be polled until OID is set ");
        poller_preprocess_error(poller_item, "Error: empty OID, item will not be polled until OID is set");
        LOG_INF("Item with an empty oid");
        return FAIL;
    }

    if (NULL == (snmp_item = zbx_calloc(NULL, 0, sizeof(snmp_worker_item_t))))
    {
        LOG_WRN("Cannot allocate memory for snmp item, not enough HEAP memory, exiting");
        exit(-1);
    };

    poller_set_item_specific_data(poller_item, snmp_item);

    if (strstr(dc_item->snmp_oid, "walk["))
        ret = snmp_worker_init_walk_item(poller_item, dc_item->snmp_oid);
    else if (strstr(dc_item->snmp_oid, "discovery["))
        ret = snmp_worker_init_discovery_item(poller_item, dc_item->snmp_oid);
    else
        ret = snmp_worker_init_get_item(poller_item, dc_item->snmp_oid);

    DEBUG_ITEM(dc_item->itemid, "Result of init key %s is %d", dc_item->snmp_oid, ret);

    if (FAIL == ret)
    {
        DEBUG_ITEM(dc_item->itemid, "Couldn't init item, wrong oid value: '%s'", dc_item->snmp_oid);
        free_item(poller_item);
        return FAIL;
    }

    int version;

    switch (dc_item->snmp_version)
    { // cannot rely on constants, do a "recoding"
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

    // TODO: bulk param support at least, for walks, normal items will need grouping and aggregating
    int len = zbx_snprintf(buffer, MAX_STRING_LEN, "\"port\":%d, \"community\":\"%s\", \"version\":%d",
                           dc_item->interface.port, dc_item->snmp_community, version);

    if (3 == version)
    {
        len += zbx_snprintf(buffer + len, MAX_STRING_LEN - len, ",\"context\":\"%s\",\"security_name\":\"%s\",\"security_level\":\"%s\"",
                            dc_item->snmpv3_contextname, dc_item->snmpv3_securityname,
                            snmv3_security_level_name(dc_item->snmpv3_securitylevel));

        if (ZBX_ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV == dc_item->snmpv3_securitylevel ||
            ZBX_ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV == dc_item->snmpv3_securitylevel)
            len += zbx_snprintf(buffer + len, MAX_STRING_LEN - len, ",\"auth_proto\":\"%s\",\"auth_pass\":\"%s\" ",
                                snmpv3_auth_proto_name(dc_item->snmpv3_authprotocol), dc_item->snmpv3_authpassphrase);

        if (ZBX_ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV == dc_item->snmpv3_securitylevel)
            len += zbx_snprintf(buffer + len, MAX_STRING_LEN - len, ",\"priv_proto\":\"%s\",\"priv_pass\":\"%s\"",
                                snmpv3_priv_proto_name(dc_item->snmpv3_privprotocol),
                                dc_item->snmpv3_privpassphrase);
    }

    if (dc_item->interface.useip)
    {
        addr = dc_item->interface.ip_orig;
        snmp_item->need_resolve = 0;
    }
    else
    {
        addr = dc_item->interface.dns_orig;
        snmp_item->need_resolve = 1;
    }

    snmp_item->iface_json_info = poller_strpool_add(buffer);
    snmp_item->address = poller_strpool_add(addr);
    snmp_item->timeout_event = poller_create_event(poller_item, timeout_cb, 0, NULL, 0);

    return SUCCEED;
}

static void start_snmp_poll(poller_item_t *poller_item, const char *resolved_address)
{
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);

    DEBUG_ITEM(poller_item_get_id(poller_item), "Starting to poll item, type %d to ip %s", snmp_item->request_type, resolved_address);

    poller_contention_add_session(snmp_item->address);

    switch (snmp_item->request_type)
    {
    case SNMP_REQUEST_GET:
        snmp_worker_start_get_request(poller_item, resolved_address);
        break;
    case SNMP_REQUEST_DISCOVERY:
        snmp_worker_start_discovery_next_walk(poller_item, resolved_address);
        break;
    case SNMP_REQUEST_WALK:
        snmp_worker_start_walk_request(poller_item, resolved_address);
        break;
    default:
        LOG_INF("Unknown snmp item request type %d", snmp_item->request_type);
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }
}

static void resolved_callback(poller_item_t *poller_item, const char *resolved_address)
{
    start_snmp_poll(poller_item, resolved_address);
}

static void resolve_fail_callback(poller_item_t *poller_item)
{
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    poller_preprocess_error(poller_item, "Failed to resolve item's hostname");
    snmp_worker_finish_poll(poller_item);
}

static int start_poll(poller_item_t *poller_item)
{
    snmp_worker_item_t *snmp_item = poller_item_get_specific_data(poller_item);
    int n;

    if (SNMP_MAX_CONTENTION <= (n = poller_contention_get_sessions(snmp_item->address)))
    {
        DEBUG_ITEM(poller_item_get_id(poller_item),
                   "There are already %d connections for the %s host, delaying poll", n, snmp_item->address);
        return POLL_NEED_DELAY;
    }

    if (snmp_item->need_resolve)
    {
        DEBUG_ITEM(poller_item_get_id(poller_item), "Start resolving polling item");
        poller_async_resolve(poller_item, snmp_item->address);
        return POLL_STARTED_FAIL;
    }

    start_snmp_poll(poller_item, snmp_item->address);
    return POLL_STARTED_OK;
}

static void handle_async_io(void)
{
    RUN_ONCE_IN(30);

    poller_contention_housekeep();

    if (FAIL == glb_worker_is_alive(conf.snmp_worker))
        glb_worker_restart(conf.snmp_worker, "Restarted due to fail in periodic check");
    subscribe_worker_fd();
}

static int forks_count(void)
{
    return CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP_WORKER];
}

static void snmp_worker_shutdown(void)
{
}

void glb_snmp_worker_init(void)
{

    char args[MAX_STRING_LEN];

    bzero(&conf, sizeof(conf_t));
    args[0] = '\0';

    // TODO: get rid of snmp translation in library, move to worker either
    init_snmp(progname);

    poller_set_poller_callbacks(init_item, free_item, handle_async_io, start_poll, snmp_worker_shutdown,
                                forks_count, resolved_callback, resolve_fail_callback, "snmp", 0, 1);

    if (-1 == access(CONFIG_SNMP_WORKER_LOCATION, X_OK))
    {
        LOG_INF("Couldn't find glb_snmp_worker at the path: %s or it isn't set to be executable: %s",
                CONFIG_SNMP_WORKER_LOCATION, zbx_strerror(errno));
        exit(-1);
    };

    if (NULL != CONFIG_SOURCE_IP)
    {
        zbx_snprintf(args, MAX_STRING_LEN, "-S %s ", CONFIG_SOURCE_IP);
    }
    LOG_INF("Will run SNMP  worker %s", CONFIG_SNMP_WORKER_LOCATION);
    conf.snmp_worker = glb_worker_init(CONFIG_SNMP_WORKER_LOCATION, args, 30, 0, 0, 0);

    if (NULL == conf.snmp_worker)
    {
        LOG_INF("Cannot create SNMP worker, check the coniguration: path '%s', args %s", CONFIG_SNMP_WORKER_LOCATION, args);
        exit(-1);
    }

    worker_set_mode_to_worker(conf.snmp_worker, GLB_WORKER_MODE_NEWLINE);
    worker_set_mode_from_worker(conf.snmp_worker, GLB_WORKER_MODE_NEWLINE);

    LOG_DBG("In %s: Ended", __func__);
}
