/*
** Copyright Glaber 2018-2023
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

#ifndef GLB_SERIAL_BUFFER_H
#define GLB_SERIAL_BUFFER_H

typedef struct serial_buffer_t serial_buffer_t;
typedef void (*serial_buffer_proc_func_t)(void *buffer, void *item, void *ctx_data, int i);

serial_buffer_t *serial_buffer_init(size_t init_size);
void    serial_buffer_destroy(serial_buffer_t *buf);
void    serial_buffer_clean(serial_buffer_t *buf);
void    serial_buffer_reset(serial_buffer_t *buf);

void*   serial_buffer_add_item(serial_buffer_t *buf, const void *item, size_t len);
void*   serial_buffer_add_data(serial_buffer_t *buf, void *data, size_t len);
void*   serial_buffer_get_real_addr(void *buffer, u_int64_t offset);

int    serial_buffer_process(void *buffer, serial_buffer_proc_func_t proc_func, void *ctx_data);

void*  serial_buffer_get_buffer(serial_buffer_t *buf);
size_t  serial_buffer_get_size(serial_buffer_t *buf);
size_t  serial_buffer_get_used(serial_buffer_t *buf);
int     serial_buffer_get_items_count(serial_buffer_t *buf);
int serial_buffer_get_time_created(serial_buffer_t *buf);
#endif