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
#include "log.h"
#include "zbxshmem.h"
#include "glb_lock.h"
#include "glb_ipc2.h"

typedef struct ipc2_chunk_t ipc2_chunk_t;

//#define MAGIK 23473456

#define IPC_WAIT_TIMEOUT 10056
#define IPC_LOWMEM_TIMEOUT 20072

struct ipc2_chunk_t {
	ipc2_chunk_t *next;
	int size;
	int items;
//	int magik; used in betta for basic data integrity test
} ;

typedef struct
{
	ipc2_chunk_t *first; //FIFO: chunks processed from first
	ipc2_chunk_t *last;  //and added after last
	int count;
	int items;
	pthread_mutex_t lock; //to lock for fetch/add
	u_int64_t sent;
	u_int64_t items_sent;
} ipc2_chunk_queue_t;

struct  ipc2_conf_t {
	zbx_shmem_info_t *mem_info;
	u_int64_t		low_mem_watermark;
	mem_funcs_t 	memf;
	unsigned int    consumers; 	
	char name[64];
 	ipc2_chunk_queue_t *queues; //init in the SHM for interprocess io
	ipc2_rcv_conf_t *rcv;
};

struct ipc2_rcv_conf_t {
	ipc2_chunk_t *rcv_queue;
};

static ipc2_chunk_t *allocate_chunk(ipc2_conf_t *ipc, size_t len, int items_count, int alloc_priority) {
	ipc2_chunk_t *chunk;
	static int last_error = 0;

	while ( ALLOC_PRIORITY_HIGH != alloc_priority &&
		    ipc->mem_info->free_size < ipc->low_mem_watermark ) {
		
		usleep(IPC_LOWMEM_TIMEOUT);

		if (time(NULL) != last_error) {
			last_error = time(NULL);
			LOG_INF("Cannot send to queue '%s', memory, low watermark %d reached, waiting", ipc->name, ipc->low_mem_watermark);
			ipc2_dump_queues(ipc);
		}
	}

	while (NULL == (chunk = ipc->memf.malloc_func(NULL, len + sizeof(ipc2_chunk_t)))) {
		usleep(IPC_WAIT_TIMEOUT);
		
		if (time(NULL) != last_error) {
			last_error = time(NULL);
			LOG_INF("Cannot send to queue '%s', out of memory, waiting", ipc->name);
			ipc2_dump_queues(ipc);
		}
	}
	chunk->size = len;
	chunk->next = 0;
	chunk->items = items_count;

	return chunk;
}


static void add_chunk_to_queue(ipc2_chunk_queue_t *q, ipc2_chunk_t *chunk, int items_count) {
	
//	chunk->magik = MAGIK;
	glb_lock_block(&q->lock);
	
	if (NULL == (q->first)) {
		q->first = chunk;
		q->last = chunk;
		q->count = 1;
		q->items = chunk->items;
	} else {
		q->last->next = chunk;
		q->last = chunk;
		q->count++;
		q->items += chunk->items;
	}
	
	q->sent++;
	q->items_sent += items_count;
	glb_lock_unlock(&q->lock);
}

void ipc2_send_by_cb_fill(ipc2_conf_t *ipc, int consumerid, int items_count, size_t len, ipc2_receive_cb_t cb_func, void *ctx_data, int priority) {
	
	ipc2_chunk_t *chunk = allocate_chunk(ipc, len, items_count, priority);
	void *data_ptr = (void *)chunk + sizeof(ipc2_chunk_t);

	cb_func(data_ptr, ctx_data);
	
	add_chunk_to_queue(&ipc->queues[consumerid], chunk, items_count);
} 


void ipc2_send_chunk(ipc2_conf_t *ipc, int consumerid, int items_count, void *data, size_t len, int priority) {
	ipc2_chunk_t *chunk;
	
	if (0 == items_count)
		return;
	
	if (consumerid >= ipc->consumers)
		HALT_HERE("Wrong consumerid %d, this is bug", consumerid);

	chunk = allocate_chunk(ipc, len, items_count, priority);
	
	void *data_ptr = (void *)chunk + sizeof(ipc2_chunk_t);

	memcpy ( data_ptr, data, len);
	add_chunk_to_queue(&ipc->queues[consumerid], chunk, items_count);
}

static int ensure_chunks_in_local_queue(ipc2_conf_t *ipc, ipc2_rcv_conf_t *rcv, int consumerid) {

	if (NULL != rcv->rcv_queue) 
		return SUCCEED;

	glb_lock_block(&ipc->queues[consumerid].lock);
	rcv->rcv_queue = ipc->queues[consumerid].first;
	ipc->queues[consumerid].first = NULL;
	ipc->queues[consumerid].last = NULL;
	ipc->queues[consumerid].count = 0;
	glb_lock_unlock(&ipc->queues[consumerid].lock);

	if (NULL == rcv->rcv_queue)
		return FAIL;
	
	return SUCCEED;
}

int  ipc2_receive_one_chunk(ipc2_conf_t *ipc, ipc2_rcv_conf_t *rcv_in,  int consumerid, ipc2_receive_cb_t receive_func, void *ctx_data) {

	ipc2_chunk_t *chunk;
	ipc2_rcv_conf_t *rcv = ipc->rcv;
	
	if (NULL != rcv_in)
		rcv = rcv_in;

	if (consumerid >= ipc->consumers)
		HALT_HERE("Wrong consumerid %d, this is bug", consumerid);

	if (FAIL == ensure_chunks_in_local_queue(ipc, rcv, consumerid))
		return 0;

	chunk = rcv->rcv_queue;
	rcv->rcv_queue = rcv->rcv_queue->next;
	
	void *data_ptr = (void *)chunk + sizeof(ipc2_chunk_t); 

	// if (chunk->magik != MAGIK) {
	// 	HALT_HERE("Received broken chunk addr %lld", chunk);
	// }

	receive_func(data_ptr, ctx_data);
	
	int items = chunk->items;
	ipc->memf.free_func(chunk);
	
	return items;
}

ipc2_conf_t* ipc2_init(int consumers, mem_funcs_t *memf, char *name, zbx_shmem_info_t *mem_info) {
    
	ipc2_conf_t *ipc;
	int i;
		
	if (NULL == (ipc = memf->malloc_func(NULL, sizeof(ipc2_conf_t)))) {	
		LOG_WRN("Cannot allocate IPC structures for IPC, exiting");
		return NULL;
	}

	if ( NULL == name || 0 == strcmp(name, "")) 
		HALT_HERE("IPC name cannot be null or emty");

	memset((void *)ipc, 0, sizeof(ipc2_conf_t));
	zbx_strlcpy(ipc->name, name, strlen(name) + 1);
	
	ipc->consumers = consumers;
	ipc->memf = *memf;
	ipc->mem_info = mem_info;
	ipc->low_mem_watermark = mem_info->total_size/3;

	if (NULL == (ipc->queues = memf->malloc_func(NULL, sizeof(ipc2_chunk_queue_t) * consumers))) {
		LOG_WRN("Couldn't allocate %ld bytes for queues ", sizeof(ipc2_chunk_queue_t) * consumers);
		return NULL;
	}

	memset( (void *)ipc->queues, 0, sizeof(ipc2_chunk_queue_t) * consumers);
	
	for (i=0; i< consumers; i++) {
		glb_lock_init(&ipc->queues[i].lock);
	}
	
	ipc->rcv = zbx_calloc(NULL,0, sizeof(ipc2_rcv_conf_t));
	return ipc;
}

 ipc2_rcv_conf_t *ipc2_init_receiver() {
	ipc2_rcv_conf_t *rcv= zbx_calloc(NULL,0, sizeof(ipc2_rcv_conf_t));
	return rcv;
 }

void ipc2_deinit_receiver(ipc2_rcv_conf_t *ipc_rcv) {
	zbx_free(ipc_rcv);
}

void ipc2_destroy(ipc2_conf_t *ipc) {
	ipc->memf.free_func(ipc->queues);
	ipc->memf.free_func(ipc);
}

u_int64_t ipc2_get_sent_chunks(ipc2_conf_t *ipc) {
	int i;
	u_int64_t sent = 0;
	
	for (i = 0; i < ipc->consumers; i++) 
		sent += ipc->queues[i].sent;
	
	return sent;
}

u_int64_t ipc2_get_sent_items(ipc2_conf_t *ipc) {
	int i;
	u_int64_t sent = 0;
	
	for (i = 0; i < ipc->consumers; i++) 
		sent += ipc->queues[i].items_sent;
	
	return sent;
}

u_int64_t ipc2_get_queue_size(ipc2_conf_t *ipc) {
	int i;
	u_int64_t total = 0;
	
	for (i = 0; i < ipc->consumers; i++) 
		total += ipc->queues[i].items;
	
	return total;
}


int ipc2_get_consumers(ipc2_conf_t *ipc) {
	return ipc->consumers;
}

void 	ipc2_dump_queues(ipc2_conf_t *ipc) {
	int i;

	LOG_INF("Dump for IPC '%s'", ipc->name);
	
	for (i = 0; i < ipc->consumers; i++) {
		int chunks = 0, items = 0, size = 0;
		
		glb_lock_block(&ipc->queues[i].lock);
		ipc2_chunk_t *chunk = ipc->queues[i].first;

		while (NULL != chunk ) {
			
			chunks ++;
			size += chunk->size;
			items += chunk->items;
		
			chunk = chunk->next; //should be last!
		}
		glb_lock_unlock(&ipc->queues[i].lock);

		LOG_INF("IPC consumer %d: chunks %d, items %d, size %d", i , chunks, items, size);
	}
}
