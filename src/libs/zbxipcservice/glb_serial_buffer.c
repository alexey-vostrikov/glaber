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
#include "glb_serial_buffer.h"
#include "log.h"

#define MIN_BUFFER_GROW_PERCENT 20
#define MIN_BUFFER_GROW_BYTES 16384

#define MAX_SERIAL_BUFFER_SIZE ZBX_GIBIBYTE

typedef struct serial_buffer_item_t serial_buffer_item_t;


struct serial_buffer_item_t {
    int next;
};

struct  serial_buffer_t {
    void *data;
    size_t used;
    size_t size;
    u_int64_t last;
    int items_count;
    size_t init_size;
    int time_created; //for flushing
}; 

typedef struct  {
    int first_item;
} serial_buff_header_t;

void serial_buffer_reset(serial_buffer_t *buf) {
    zbx_free(buf->data);
    buf->data = zbx_malloc(NULL, buf->init_size);
    buf->items_count = 0;
    buf->last = 0;
    buf->size = buf->init_size;
    buf->used =  sizeof(serial_buffer_t);
    buf->time_created  = time(NULL);
}


void serial_buffer_clean(serial_buffer_t *buf) {
    buf->used = sizeof(serial_buffer_t);;
    buf->items_count = 0;
    buf->last = 0;
    buf->time_created = time(NULL);
}


#define MIN_SBUFF_SIZE sizeof(serial_buff_header_t)
serial_buffer_t *serial_buffer_init(size_t init_size) {
    serial_buffer_t *buf;
    if (init_size < MIN_SBUFF_SIZE) 
        init_size = MIN_SBUFF_SIZE;
    //LOG_INF("Doing serial buffer init");
    if (init_size > MAX_SERIAL_BUFFER_SIZE)
        return NULL;

    if (NULL ==(buf = zbx_calloc(NULL, 0, sizeof(serial_buffer_t))))
        HALT_HERE("Not enough memory in heap, cannot allocate either ipc buffer or it's data segment");

    serial_buffer_reset(buf);
    return buf;
};

void serial_buffer_destroy(serial_buffer_t *buf) {
    LOG_INF("Freeing data");
    zbx_free(buf->data);
    LOG_INF("Freeing data2");
    zbx_free(buf);
}

static void serial_buffer_grow(serial_buffer_t *buf, size_t need_more) {
    int new_len;

    new_len = MAX ( (buf->size + buf->size/ MIN_BUFFER_GROW_PERCENT) , buf->used + need_more);
    new_len = MAX ( new_len, MIN_BUFFER_GROW_BYTES);

    if (NULL == (buf->data = zbx_realloc(buf->data, new_len)))
        HALT_HERE("Cannot reallocate IPC buffer in the heap, not enough memory");
    buf->size = new_len;

    return;
}

void *serial_buffer_add_item(serial_buffer_t *buf, const void *item, size_t len) {
    u_int64_t full_len = len + sizeof(serial_buffer_item_t);
    
    if (buf->used + full_len > buf->size) {
       // LOG_INF("Will grow buffer");
        serial_buffer_grow(buf, full_len);
       // LOG_INF("New buffer len is %d", serial_buffer_get_size(buf));
    }
    
    serial_buffer_item_t *new_item = buf->data + buf->used;

    void *data_ptr = buf->data + buf->used + sizeof(serial_buffer_item_t);
    new_item->next = 0;
    
    if (0 != buf->last) {
        serial_buffer_item_t *last_item = buf->data + buf->last;
        last_item->next = buf->used; //relative offset is saved in the buffer
    } else {
        //setting header
        serial_buff_header_t *hdr = buf->data;
        hdr->first_item = buf->used;
    }
    
    buf->last = buf->used; //keeping offset either as buffer might be reallocated
    //LOG_INF("Set new last offset to %d",buf->last);

    memcpy(data_ptr, item, len);
    buf->used += full_len;
    buf->items_count++;

    return data_ptr;
}

void *serial_buffer_add_data(serial_buffer_t *buf, void *data, size_t len) {
    u_int64_t offset = buf->used;
    u_int64_t full_len = len;
     

    if (buf->used + full_len > buf->size) {
      //  LOG_INF("Will grow buffer");
        serial_buffer_grow(buf, full_len);
      //  LOG_INF("New buffer len is %d", serial_buffer_get_size(buf));
    }
    void *data_ptr = buf->data + buf->used;
    
   // LOG_INF("Do memcpy");
    memcpy(data_ptr, data, len);
   // LOG_INF("Data is saved to addr %lld", (void *)buf->data + buf->used );
    buf->used += full_len;
   // LOG_INF("Finished");
    return (void *)offset;
}

void *serial_buffer_get_real_addr(void *buffer, u_int64_t offset) {
    return (void *)(buffer + offset);
}

int serial_buffer_process(void *buffer, serial_buffer_proc_func_t proc_func, void *ctx_data) {
    serial_buff_header_t *header = buffer;
    serial_buffer_item_t *item = buffer + header->first_item;
    
    int i = 0;
 //   LOG_INF("Buffer is %p, next is %d", buffer, item->next);

    while (1) {
      //  LOG_INF("Calling processing func");
        proc_func(buffer, (void *)item + sizeof(serial_buffer_item_t), ctx_data, i);
   //     LOG_INF("Processing func exited");
        if (0 == item->next)
            break;
        item = (void *)buffer + item->next;
        i++;
    }
    
    return i;
}

size_t serial_buffer_get_size(serial_buffer_t *buf) {
    return buf->size;
}

size_t serial_buffer_get_used(serial_buffer_t *buf) {
    return buf->used;
}

void*  serial_buffer_get_buffer(serial_buffer_t *buf) {
    return buf->data;
}

int serial_buffer_get_items_count(serial_buffer_t *buf) {
    return buf->items_count;
}

int serial_buffer_get_time_created(serial_buffer_t *buf) {
    return buf->time_created;
}