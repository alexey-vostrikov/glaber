/*
** Copyright Glaber
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

#ifndef GLB_IPC_SERVICE_H
#define GLB_IPC_SERVICE_H

#include "common.h"
#include "log.h"
#include "memalloc.h"
#include "zbxalgo.h"
#include "glb_ipc.h"
#include "glb_lock.h"
#include <pthread.h>

//for debugging only to have metric 
#include "../glb_process/process.h"
#define IPC_BULK_COUNT 256

typedef struct ipc_element_t ipc_element_t;

struct ipc_element_t {
	ipc_element_t *next;
	u_int64_t data;
} ;

typedef struct
{
	ipc_element_t *first;
	ipc_element_t *last;
	int count;
	pthread_mutex_t lock;
	u_int64_t sent;
} ipc_queue_t;

typedef struct {
	zbx_mem_info_t shm;
	unsigned char   type;
	unsigned int    elems_count; //per consumer
	size_t 			elem_size;
	unsigned int    consumers; 	
	mem_funcs_t 	memf;
	
	ipc_data_create_cb_t create_cb; //functions to properly place and free
//	glb_ipc_release_cb release_cb; //ipc data to the ipc queue and ipc mem
	ipc_data_free_cb_t free_cb;

	ipc_queue_t free_queue; //init in the SHM
	ipc_queue_t *queues; //init in the SHM
	ipc_mode_t mode; //ipc_bulk or ipc_fast

} ipc_conf_t;

typedef struct 
{
	ipc_queue_t *send_queues; //queue for buffering messages before they being send 
	ipc_queue_t rcv_queue; //queue for local buffering of messages before they get processed
	ipc_queue_t free_rcv_queue; //for buffering elements after they got free on recieve side
	ipc_queue_t free_snd_queue; //for buffering free elements on the sender side
} ipc_local_queues_t;

static ipc_local_queues_t *local_queues = NULL;

#define MOVE_ALL_ELEMENTS 0

static int alloc_local_queues() {
	if (NULL != local_queues)
		return SUCCEED;

	if(NULL == (local_queues = zbx_malloc(NULL, sizeof(ipc_local_queues_t) * GLB_IPC_TYPE_COUNT))) {
		LOG_WRN("Cannot allocate heap memory for local ipc structures");
		return FAIL;
	}

	memset(local_queues, 0 , sizeof(ipc_local_queues_t) * GLB_IPC_TYPE_COUNT);
	return SUCCEED;
}

int  glb_ipc_init_sender(unsigned char ipc_type, int consumers) {
	int i;

	if (SUCCEED != alloc_local_queues())
		return FAIL;

//	LOG_INF("Doing init of %d local queues for type %d", consumers, ipc_type);
	local_queues[ipc_type].send_queues = zbx_malloc(NULL, sizeof(ipc_queue_t) * consumers);

//	LOG_INF("local snd queue 0 addr is %p", local_queues[ipc_type].send_queues);
	memset(local_queues[ipc_type].send_queues, 0 , sizeof(ipc_queue_t) * consumers);
	
	for (i = 0; i < consumers; i++ ) {
		glb_lock_init(&local_queues[ipc_type].send_queues[i].lock);
	}
	
	glb_lock_init(&local_queues[ipc_type].free_snd_queue.lock);

	return SUCCEED;
}

int glb_ipc_init_reciever(unsigned char ipc_type) {
	
	if (SUCCEED != alloc_local_queues())
		return FAIL;
	
	glb_lock_init(&local_queues[ipc_type].free_rcv_queue.lock);
	glb_lock_init(&local_queues[ipc_type].rcv_queue.lock);
}

void glb_ipc_client_destroy() {
	int i;
	for (i = 0; i < GLB_IPC_TYPE_COUNT; i++) {
		if (NULL != local_queues[i].send_queues)
			zbx_free(local_queues[i].send_queues);
	}

	zbx_free(local_queues);
}

static int move_all_elements(ipc_queue_t *src, ipc_queue_t *dst) {

	glb_lock_block(&src->lock);

	if (src->count == 0) {
		glb_lock_unlock(&src->lock);
		return FAIL;	
	}
	
	glb_lock_block(&dst->lock);	

	if (dst->first == NULL) {

		dst->first = src->first;
		dst->last = src->last;
		dst->count = src->count;
		
	} else {

		dst->last->next = src->first;
		dst->last = src->last;
		dst->count += src->count;

	}

	src->first = src->last = NULL;
	src->count = 0;

	glb_lock_unlock(&dst->lock);
	glb_lock_unlock(&src->lock);

	return SUCCEED;
}
static void dump_queue(ipc_queue_t *q, char *name) {
//	LOG_INF("Queue '%s' size %d, start: %p, end: %p", name, q->count, q->first, q->last);

	if (0 == q->count && (NULL != q->first || NULL != q->last)) {
		LOG_INF("FAIL: count is 0 but start/end isn't null");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}

	if (0 < q->count && (NULL == q->first || NULL == q->last)) {
		LOG_INF("FAIL: count is not 0 but start/end is Null");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}

	if (NULL != q->last && NULL != q->last->next) {
		LOG_INF("FAIL: last element doesn't points to 0, exiting");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}
	int i=0;
	ipc_element_t *elem = q->first;
}

/*note: the function is intended to local non blocking use only */
static int move_one_element(ipc_queue_t *src, ipc_queue_t *dst, char *move_name) {
	int ret = SUCCEED;

	//glb_lock_block(&src->lock);

	if (src->count == 0) {
	//	glb_lock_unlock(&src->lock);
		return FAIL;	
	}

	//glb_lock_block(&dst->lock);
//	LOG_INF("Unlocked, dumping before move (%s):",move_name);
//	dump_queue(src, "Src");
//	dump_queue(dst, "Dst");

	ipc_element_t *elem = src->first;
//	LOG_INF("Moving elemem's address is %p", elem);

	if (NULL == dst->first) {
		//LOG_INF("Dst queue is empty, init from first");
		dst->first = elem;
		dst->last = elem;
	} else {
//		LOG_INF("Dst queue is non empty, has %d items moving elems to tail", dst->count);
		dst->last->next = elem;
		dst->last = elem;
	}
		
	src->first = elem->next;
	elem->next = NULL;

	if (NULL == src->first)  {
		src->last = NULL;
		src->count = 0;
	} else {
		src->count --;
	}

	dst->count += 1;	
		
//	LOG_INF("Unlocking, dump after move (%s):", move_name);
//	dump_queue(src, "Src");
//	dump_queue(dst, "Dst");

//	glb_lock_unlock(&dst->lock);
//	glb_lock_unlock(&src->lock);

//	LOG_INF("Finished");
	return SUCCEED;
}
//note: maybe it's worth to disble extra locking on local queues
//to save a few CPU ticks

static int move_n_elements(ipc_queue_t *src, ipc_queue_t *dst, int count, char *move_name) {
	int ret = SUCCEED;

	if (0 == count ) {
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}

	glb_lock_block(&src->lock);

	if (src->count == 0) {
		glb_lock_unlock(&src->lock);
		return FAIL;	
	}

	glb_lock_block(&dst->lock);

	ipc_element_t *new_last = src->first;
	int i = 1;
		
//	LOG_INF("Moving %d items", count);
		
	while (count  > i  && NULL != new_last->next ) {
//		LOG_INF("I is %d, new last is %p", i, new_last);
		new_last = new_last->next;
		i++;
	}

	if (NULL == dst->first) {
		dst->first = src->first;
	} else {
		dst->last->next = src->first;
	}

	dst->last = new_last;
	src->first = new_last->next;
			
	if (NULL == src->first)  {
		src->last = NULL;
		src->count = 0;
	} else {
		src->count -= i;
	}

	dst->count += i;	
	new_last->next = NULL;

	glb_lock_unlock(&dst->lock);
	glb_lock_unlock(&src->lock);
//	LOG_INF("Finished");
	return SUCCEED;
}

static void flush_queues(ipc_conf_t *ipc) {
	int i;
	static u_int64_t last_flush = 0;
	u_int64_t now = glb_ms_time();
	
	if (now == last_flush) 
		return;
	
	last_flush = now;

	for (i =0 ; i < ipc->consumers; i ++ ) {
		if (IPC_LOW_LATENCY == ipc->mode || local_queues[ipc->type].send_queues[i].count > IPC_BULK_COUNT)
			move_all_elements(&local_queues[ipc->type].send_queues[i], &ipc->queues[i]);
	}
	
}
int glb_ipc_flush_all(void *ipc_conf) {
	ipc_conf_t *ipc = ipc_conf;
	int i = 0;

	for (i = 0 ; i < ipc->consumers; i ++ ) {
		if (IPC_LOW_LATENCY == ipc->mode || local_queues[ipc->type].send_queues[i].count > IPC_BULK_COUNT)
			move_all_elements(&local_queues[ipc->type].send_queues[i], &ipc->queues[i]);
	}
}

static int get_free_queue_items(ipc_conf_t *ipc, ipc_queue_t *local_free_queue, unsigned char lock) {
	static int laststat = 0;
	
	while (local_free_queue->count == 0 ) {
		if (FAIL == move_n_elements(&ipc->free_queue, local_free_queue, IPC_BULK_COUNT, "ipc_free -> local free")) {
			if (0 != lock) {
			//	LOG_INF("ipc Lock wait");
				usleep(13300);
				continue;
			}
			
			return FAIL;
		};
	}
	return SUCCEED;
}

int glb_ipc_send(void *ipc_conf, int queue_num, void* send_data, unsigned char lock ) {
	ipc_conf_t *ipc = ipc_conf;

	ipc_queue_t *local_free_queue = &local_queues[ipc->type].free_snd_queue,
				*local_send_queue = &local_queues[ipc->type].send_queues[queue_num];

	ipc_element_t  *element = NULL;

	//LOG_INF("There are %d items in the local free queue", local_free_queue->count);
	if (FAIL == get_free_queue_items(ipc, local_free_queue, lock))
		return FAIL;
	
	//LOG_INF("Moving element from the local free queue to local send queue, queue num is %d, addr is %p", queue_num, local_send_queue);
	if (FAIL == move_one_element(local_free_queue, local_send_queue, "local_free -> local_send")) 
		return FAIL;
	//LOG_INF("Getting ptr to the item to send");
	element = local_send_queue->last;
	//LOG_INF("Calling callback");
	ipc->create_cb(&ipc->memf, (void *)(&element->data), send_data);
	
	//LOG_INF("Flushing local send queues to global queues");
	flush_queues(ipc);
	//LOG_INF("Finished");
	return SUCCEED;
}

/* callback based interface to process incoming data without need 
to pop or copy data, returns number of the processed messages */
int  glb_ipc_process(void *ipc_conf, int consumerid, ipc_data_process_cb_t cb_func, void *cb_data, int max_count) {
	int i = 0;
	ipc_element_t *element;
	ipc_conf_t *ipc = ipc_conf;
	static u_int64_t last_rcv_check = 0;
	u_int64_t now = glb_ms_time();

	ipc_queue_t *local_rcv_queue = &local_queues[ipc->type].rcv_queue,
				*rcv_queue = &ipc->queues[consumerid],
				*local_free_queue = &local_queues[ipc->type].free_rcv_queue;
	

	while (i < max_count ) {

//	dump_queue(rcv_queue, "Rcv global queue");
//	dump_queue(local_rcv_queue, "Rcv local queue");

		if (local_rcv_queue->count == 0 ) 
		{
			if (now == last_rcv_check)
				break; /*no reason to hummer queue more then once a millisecind */ 

			last_rcv_check = now;

			if (FAIL == move_all_elements(rcv_queue, local_rcv_queue)) {
				//LOG_INF("No data arrived yet, local and global rcv queue is empty");
				break;
			}
		}
		//LOG_INF("Got %d elements in the rcv queue", local_rcv_queue->count);
		element = local_rcv_queue->first;
		
		//LOG_INF("Filling the buffer/releasing IPC mem, element addr is %p", element);
		cb_func(&ipc->memf, i, (void *)(&element->data), cb_data);
		
		if (NULL != ipc->free_cb)
			ipc->free_cb(&ipc->memf, (void *)(&element->data));	

		//LOG_INF("Returning to the local free queue, queue size is %d", local_free_queue->count);
		move_one_element(local_rcv_queue, local_free_queue, "local_recieve -> local_free");
		
		//	LOG_INF("Moved one element to the free buffer");
		if (local_free_queue->count >= 256 ) {
			//LOG_INF("Returning to the global free queue");
			move_all_elements(local_free_queue, &ipc->free_queue);
		}
		i++;
	}
	
	return i;
}

void* glb_ipc_init(unsigned char ipc_type, size_t mem_size, char *name, int elems_count, int elem_size, int consumers, mem_funcs_t *memf,
			ipc_data_create_cb_t create_cb, ipc_data_free_cb_t free_cb, ipc_mode_t mode) {
    
	ipc_conf_t *conf;
	int i;
    char *error = NULL;
	ipc_element_t *elements;
	
	//LOG_INF("Allocating memory for ipc config, func addr is %p, need to allocate %ld bytes ", memf->malloc_func, sizeof(ipc_conf_t));
	if (NULL == (conf = memf->malloc_func(NULL, sizeof(ipc_conf_t)))) {	
		LOG_WRN("Cannot allocate IPC structures for IPC %s, exiting", name);
		return NULL;
	}
	
	memset((void *)conf, 0, sizeof(ipc_conf_t));

	conf->consumers = consumers;
	conf->type = ipc_type;
	conf->memf = *memf;
	//LOG_INF("Allocating memory for queues %d", consumers);
	//queues init

	if (NULL == (conf->queues = memf->malloc_func(NULL, sizeof(ipc_queue_t) * consumers))) {
		LOG_WRN("Couldn't allocate %ld bytes for queues ", sizeof(ipc_queue_t) * consumers);
		return NULL;
	}
	memset( (void *)conf->queues, 0, sizeof(ipc_queue_t) * consumers);
	
	for (i=0; i< consumers; i++) {
		glb_lock_init(&conf->queues[i].lock);
	}
	
	glb_lock_init(&conf->free_queue.lock);

	size_t full_elem_size = elem_size + sizeof (ipc_element_t *);
	if (NULL == (elements = memf->malloc_func(NULL, full_elem_size * elems_count) ) )  {
		LOG_WRN("Couldn't allocate %ld bytes for elements", full_elem_size * elems_count);
		return NULL;
	}
	//making queue out of elements
	void * ptr = elements;
	ipc_element_t *elem;
	for (i = 0; i < elems_count-1; i++) {
	 	elem = ptr;
		elem->next = ptr + full_elem_size;
		ptr = ptr + full_elem_size;
	}

	elem = (void *) elements + full_elem_size * (elems_count-1);
	elem->next = NULL;

	
	conf->free_queue.first = elements;
	conf->free_queue.last =  elem;
	conf->free_queue.count = elems_count;
	
	conf->create_cb = create_cb;
	conf->free_cb = free_cb;
	
	LOG_DBG("%s:finished", __func__);
	return (void *)conf;
}

//since all the data is in shm, the best way to destrow - release the shm
void glb_ipc_destroy() {

}

void 	glb_ipc_dump_sender_queues(void *ipc_data, char *name) {
	ipc_conf_t *ipc = ipc_data;
	LOG_INF("QUEUES dump at %s: local_free_queue: %d, global_free_queue: %d, local_send_queue: %d, global_snd_queue: %d, local_rcv_queue: %d", 
		name, local_queues[ipc->type].free_snd_queue.count, ipc->free_queue.count, local_queues[ipc->type].send_queues[0].count, 
			ipc->queues[0].count, local_queues[ipc->type].rcv_queue.count);
//	dump_queue(&ipc->queues[0],"IPC send queue");
}

void 	glb_ipc_dump_reciever_queues(void *ipc_data, char *name, int proc_num) {
	ipc_conf_t *ipc = ipc_data;
	LOG_INF("QUEUES dump at %s: local_free_queue: %d, global_free_queue: %d, global_snd_queue: %d, local_rcv_queue: %d", 
		name, local_queues[ipc->type].free_rcv_queue.count, ipc->free_queue.count, ipc->queues[proc_num-1].count, local_queues[ipc->type].rcv_queue.count);
//	dump_queue(&ipc->queues[0],"IPC send queue");
}

mem_funcs_t *glb_ipc_get_memf_funcs(void *ipc_data) {
	ipc_conf_t *ipc = ipc_data;
	return &ipc->memf;
}
#endif