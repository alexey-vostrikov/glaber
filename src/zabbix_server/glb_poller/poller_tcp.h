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
#ifndef GLABER_TCP_POLL_H
#define GLABER_TCP_POLL_H

#include "glb_poller.h"
#define DEFAULT_TCP_HOST_CONTENTION 4

typedef void *(*tcp_poll_item_init_func)(poller_item_t *poller_item, DC_ITEM *dc_item);
typedef void (*tcp_poll_type_destroy_func)(void * proto_data);
typedef	unsigned char (*tcp_poll_connect_cb_func)(poller_item_t *poller_item, void *proto_data);
typedef unsigned char (*tcp_poll_response_cb_func)(poller_item_t *poller_item, void *proto_data, const char *response, size_t response_size);
typedef void	(*tcp_poll_create_request_cb_func)(poller_item_t *poller_item, void *ctx_data, void **buffer, size_t *buf_size);
typedef	void    (*tcp_poll_timeout_cb_func)(poller_item_t *poller_item, void *proto_data);
typedef	void    (*tcp_poll_fail_cb_func)(poller_item_t *poller_item, void *proto_data,const char *error_reason);

typedef enum {
	ASYNC_TCP_POLL_TYPE_AGENT = 1,
	ASYNC_TCP_POLL_SIMPLE_HTTP_TYPE,
	ASYNC_TCP_POLL_TYPE_COUNT
} tcp_poll_type_t;

#define ASYNC_IO_TCP_PROC_FINISH	1
#define ASYNC_IO_TCP_PROC_CONTINUE	2

typedef struct {
	tcp_poll_type_t type;
	tcp_poll_item_init_func	item_init;
	tcp_poll_type_destroy_func	item_destroy;
	tcp_poll_create_request_cb_func	create_request;
	tcp_poll_response_cb_func	response_cb;
	tcp_poll_timeout_cb_func	timeout_cb;
	tcp_poll_fail_cb_func		conn_fail_cb;
	tcp_poll_connect_cb_func connect_cb;
} tcp_poll_type_procs_t ;

int     glb_tcp_init();

#endif