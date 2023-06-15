
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
//#include "comms.h"
#include "log.h"
#include "glb_poller.h"
#include "poller_tcp.h"
#include "tcp_agent_proto.h"


typedef struct {
    void*   request;
    size_t  request_size;
    char*   buffer;
    size_t  buffsize;
	size_t 	buff_allocated;
} poller_agent_proto_t;

#define ZBX_TCP_HEADER_DATA	"ZBXD"
#define ZBX_TCP_HEADER_LEN	ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA)
#define ZBX_TLS_MAX_REC_LEN	16384
#define AGENT_HEADERS_SIZE 13  

/*
Agent unencrypted and uncompressed proto is:
bytes : reason
0-3: ZBXD | 4: flag = ZBX_TCP_PROTOCOL(1) | 5-8: length of payload | 9-12: reserved(0) | 13 - length of payload + 13: payload
*/

static void agent_proto_create_request(char *key, void **request, size_t* request_size) {
    /* packet gen taken from zbx_tcp_send */
	unsigned char flags = ZBX_TCP_PROTOCOL;
		
	size_t len  = strlen(key);
	size_t offset, send_len = len;

	*request = zbx_malloc(NULL, send_len + AGENT_HEADERS_SIZE);

  	memcpy(*request, ZBX_TCP_HEADER_DATA, ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA));
	offset = ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA);

	((char *)*request)[offset++] = flags;

	zbx_uint32_t	len32_le;

	len32_le = zbx_htole_uint32((zbx_uint32_t)send_len);
	memcpy(*request + offset, &len32_le, sizeof(len32_le));
	offset += sizeof(len32_le);
	
	len32_le = zbx_htole_uint32((zbx_uint32_t)0);
	memcpy(*request + offset, &len32_le, sizeof(len32_le));
	offset += sizeof(len32_le);
	
	memcpy(*request + offset, key, send_len);
	*request_size = offset + send_len;
}

/* Request is always the same, so it's created once on item creation*/
static void* item_init(poller_item_t *poller_item,DC_ITEM *dc_item) {
    poller_agent_proto_t *agent_data;
 
    if (NULL == (agent_data = zbx_calloc(NULL, 0, sizeof(*agent_data))))
        return NULL;
    
    agent_proto_create_request(dc_item->key, &agent_data->request, &agent_data->request_size);
 
    return agent_data;   
}

static void free_buffer(poller_agent_proto_t *agent_data) {
    if (0 < agent_data->buff_allocated) {
        zbx_free(agent_data->buffer);
        agent_data->buffsize = 0;
        agent_data->buffer = NULL;
        agent_data->buff_allocated = 0;
    }
}

static void item_destroy(void *proto_ctx) {
	poller_agent_proto_t *agent_data = proto_ctx;

    zbx_free(agent_data->request);
    free_buffer(agent_data);
    zbx_free(agent_data);
}

static void fail_cb(poller_item_t *poller_item, void *proto_ctx, const char *error) {
    poller_preprocess_error(poller_item, error);
}

static void  timeout_cb(poller_item_t *poller_item, void *proto_ctx) {
    fail_cb(poller_item, proto_ctx, "Timeout while waiting for response");
} 

static void create_request(poller_item_t *poller_item, void *proto_ctx, void **buffer, size_t *buffsize) {
	poller_agent_proto_t *agent_data = proto_ctx;

    *buffsize = agent_data->request_size;
    *buffer = agent_data->request;

    return;
}


static void process_payload_response(poller_item_t *poller_item, const char* buffer, size_t buffer_size) {
    #define MAX_STATIC_BUFFER 8192
   
    char *value;
    char buff[MAX_STATIC_BUFFER];
    
    if (buffer_size < MAX_STATIC_BUFFER)
        value = buff;
    else 
        value = zbx_malloc(NULL, buffer_size +1);
    
    memcpy(value, buffer, buffer_size);
    value[buffer_size] = 0;

    zbx_rtrim(value, " \r\n");
	zbx_ltrim(value, " ");

    if (0 == strcmp(value, ZBX_NOTSUPPORTED) ||
        0 == strcmp(value, ZBX_ERROR) ) {
        poller_preprocess_error(poller_item, value);
        return;
    }

    poller_preprocess_str(poller_item, NULL, value);
    
	DEBUG_ITEM(poller_get_item_id(poller_item), "Arrived agent response: %s", value);

    if (buffer_size >= MAX_STATIC_BUFFER)
        zbx_free(value);
}

/*returns success if buffer has been processed and there is no need to wait for more data*/
static int process_response(poller_item_t *poller_item, const char *buffer, size_t buffer_size) {
    #define PAYLOAD_OFFSET 5 
    
    if (buffer_size < AGENT_HEADERS_SIZE) 
        return FAIL;

    if (0 != strncmp(buffer, ZBX_TCP_HEADER_DATA, 4 )) {
        DEBUG_ITEM(poller_get_item_id(poller_item),"Response doesn't start from ZBXD prefix");
        poller_preprocess_error(poller_item, "Response doesn't start from ZBXD prefix");
        return SUCCEED;
    }

    unsigned char flags = buffer[4];
    
    if (ZBX_TCP_PROTOCOL != flags) {
        poller_preprocess_error(poller_item, "Response has improper flag value, '0x01' is expected");
        return SUCCEED;
    }
    
    u_int32_t payload_len;
    memcpy(&payload_len, buffer + PAYLOAD_OFFSET, sizeof(payload_len));
    payload_len= zbx_letoh_uint32(payload_len);

    DEBUG_ITEM(poller_get_item_id(poller_item), "Got payload of size %d", payload_len);
    
    if (payload_len + AGENT_HEADERS_SIZE > buffer_size) {
        return FAIL; 
    }

    process_payload_response(poller_item, buffer + AGENT_HEADERS_SIZE, payload_len);
    
    return SUCCEED;
}

void extend_buffer(poller_agent_proto_t *agent_data, size_t need_more) {
    void *new_buff = zbx_malloc(NULL, agent_data-> buff_allocated + need_more);
    
    memcpy(new_buff, agent_data->buffer, agent_data->buffsize);
    zbx_free(agent_data->buffer);
    
    agent_data->buff_allocated += need_more;
    agent_data->buffer = new_buff;
}

static void add_data_to_buffer(poller_agent_proto_t *agent_data, const char *buffer, size_t buffer_len) {
 
    if (agent_data->buff_allocated - agent_data->buffsize < buffer_len)
        extend_buffer (agent_data, buffer_len);
    memcpy(agent_data->buffer + agent_data->buffsize, buffer, buffer_len);
    
    agent_data->buffsize+=buffer_len;

    return ;
}

static void clean_buffer (poller_agent_proto_t *agent_data) {
#define MAX_REASONABLE_BUFFER_LENGTH ZBX_MEBIBYTE
    static int lastcheck = 0;

    if (agent_data->buff_allocated > MAX_REASONABLE_BUFFER_LENGTH && ( lastcheck + 30 < time(NULL)) ) {
        lastcheck = time(NULL);
        free_buffer(agent_data);
        return;
    }

    agent_data->buffsize = 0;
}

/*response arrive chunked by ~4096 bytes*/
static unsigned char response_cb(poller_item_t *poller_item, void *proto_ctx, const char *response, size_t response_size) {
    poller_agent_proto_t *agent_data = proto_ctx;
    
    if (0 == agent_data->buff_allocated) {
        if (SUCCEED == process_response(poller_item, response, response_size)) {
            poller_inc_responses();
            return ASYNC_IO_TCP_PROC_FINISH;
        }
    }

    add_data_to_buffer(agent_data, response, response_size);
    
    if (SUCCEED == process_response(poller_item, agent_data->buffer, agent_data->buffsize)) {
        poller_inc_responses();
        clean_buffer(agent_data);
        return ASYNC_IO_TCP_PROC_FINISH;
    }
    
    return ASYNC_IO_TCP_PROC_CONTINUE; 
}

void tcp_agent_proto_init(tcp_poll_type_procs_t *procs) {
    procs->fail_cb = fail_cb;
    procs->item_destroy = item_destroy;
    procs->item_init = item_init;
    procs->response_cb = response_cb;
    procs->create_request = create_request;
    procs->timeout_cb = timeout_cb;
}
