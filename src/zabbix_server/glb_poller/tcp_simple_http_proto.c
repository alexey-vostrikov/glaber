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

#include "zbxcommon.h"
#include "zbxcomms.h"
#include "zbxsysinfo.h"
#include "log.h"
#include "glb_poller.h"
#include "poller_tcp.h"
#include "tcp_simple_http_proto.h"

static void* item_init(poller_item_t* poller_item, DC_ITEM *dc_item) {

    AGENT_REQUEST	request;
    zbx_init_agent_request(&request);
    u_int32_t port;
    char *ret = (char *)1;

	if (SUCCEED != zbx_parse_item_key(dc_item->key, &request))
	{
		poller_preprocess_error(poller_item,"Invalid item key format.");
        ret = NULL;
	}

    if (3 < request.nparam || 2 > request.nparam)
	{
		poller_preprocess_error(poller_item,"Invalid number of parameters in key");
        ret = NULL;
	}

	char *ip_str = get_rparam(&request, 1);
	char *port_str = get_rparam(&request, 2);
    
    DEBUG_ITEM(dc_item->itemid, "Setting new interface to %s, old was %s", ip_str, dc_item->interface.addr);
	if (NULL != ip_str && '\0' != *ip_str) {
    	zbx_snprintf(dc_item->interface.dns_orig, ZBX_INTERFACE_DNS_LEN_MAX, "%s", ip_str);
        dc_item->interface.addr = dc_item->interface.dns_orig;
        dc_item->interface.useip = 0;
    }

    DEBUG_ITEM(dc_item->itemid, "Set new interface to %s:%s", ip_str, port_str);

	if (NULL == port_str || '\0' == *port_str || SUCCEED != zbx_is_ushort(port_str, &dc_item->interface.port))
	{
		poller_preprocess_error(poller_item,"Invalid number of parameters in key");
        ret = NULL;
	}

    zbx_free_agent_request(&request);
    return ret;   
}

static void conn_fail_cb(poller_item_t *poller_item, void *proto_ctx, const char *error) {
    poller_preprocess_uint64(poller_item, NULL, 0, poller_get_item_type(poller_item));

}

static void  timeout_cb(poller_item_t *poller_item, void *proto_ctx) {
    conn_fail_cb(poller_item, proto_ctx, "Timeout while waiting for response");
    poller_iface_register_timeout(poller_item);
} 

static unsigned char connect_cb(poller_item_t *poller_item, void *proto_ctx) {
    poller_preprocess_uint64(poller_item, NULL, 1, poller_get_item_type(poller_item));
    poller_inc_responses();
    return ASYNC_IO_TCP_PROC_FINISH;
}

static unsigned char response_cb(poller_item_t *poller_item, void *proto_ctx, const char *response, size_t response_size) {
    poller_preprocess_uint64(poller_item, NULL,  1, poller_get_item_type(poller_item));
    return ASYNC_IO_TCP_PROC_FINISH;
}

void tcp_simple_http_proto_init(tcp_poll_type_procs_t *procs) {
    procs->item_init = item_init;
    procs->response_cb = response_cb;
    procs->timeout_cb = timeout_cb;
    procs->connect_cb = connect_cb;
    procs->conn_fail_cb  = conn_fail_cb;
}