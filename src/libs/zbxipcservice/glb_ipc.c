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

typedef struct 
{
	ipc_queue_t *send_queues; //queue for buffering messages before they being send 
	ipc_queue_t rcv_queue; //queue for local buffering of messages before they get processed
	ipc_queue_t free_rcv_queue; //for buffering elements after they got free on recieve side
	ipc_queue_t free_snd_queue; //for buffering free elements on the sender side
} ipc_local_queues_t;

struct  ipc_conf_t {

	unsigned int    consumers; 	
	mem_funcs_t 	memf;
	
	ipc_data_create_cb_t create_cb; //functions to properly place and free
	ipc_data_free_cb_t free_cb;

	ipc_queue_t free_queue; //init in the SHM for interprocess io
 	ipc_queue_t *queues; //init in the SHM for interprocess io
	
	ipc_local_queues_t local_queues; //local send and recieve, allocate in heap

	ipc_mode_t mode; //ipc_bulk or ipc_fast

};



//static ipc_local_queues_t *local_queues = NULL;

#define MOVE_ALL_ELEMENTS 0

static int  glb_ipc_init_sender(ipc_conf_t *ipc) {
	int i;

	ipc->local_queues.send_queues = zbx_malloc(NULL, sizeof(ipc_queue_t) * ipc->consumers);

	bzero(ipc->local_queues.send_queues, sizeof(ipc_queue_t) * ipc->consumers);
	for (i = 0; i < ipc->consumers; i++ ) {
		glb_lock_init(&ipc->local_queues.send_queues[i].lock);
	}
	
	glb_lock_init(&ipc->local_queues.free_snd_queue.lock);

	return SUCCEED;
}

void glb_ipc_local_destroy(ipc_conf_t *ipc) {
	int i;
	
	if (NULL != ipc->local_queues.send_queues)
			zbx_free(ipc->local_queues.send_queues);

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

	if (src->count == 0) {
		return FAIL;	
	}

	ipc_element_t *elem = src->first;

	if (NULL == dst->first) {
		dst->first = elem;
		dst->last = elem;
	} else {
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
		
	return SUCCEED;
}

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

		
	while (count  > i  && NULL != new_last->next ) {
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
		if (IPC_LOW_LATENCY == ipc->mode || ipc->local_queues.send_queues[i].count > IPC_BULK_COUNT) 
			move_all_elements(&ipc->local_queues.send_queues[i], &ipc->queues[i]);
	}
}

int glb_ipc_flush_all(void *ipc_conf) {
	ipc_conf_t *ipc = ipc_conf;
	int i = 0;

	for (i = 0 ; i < ipc->consumers; i ++ ) {
		if (IPC_LOW_LATENCY == ipc->mode || ipc->local_queues.send_queues[i].count > IPC_BULK_COUNT)
			move_all_elements(&ipc->local_queues.send_queues[i], &ipc->queues[i]);
	}
}

static int get_free_queue_items(ipc_conf_t *ipc, ipc_queue_t *local_free_queue, unsigned char lock) {
	static int laststat = 0;
	
	while (local_free_queue->count == 0 ) {
		if (FAIL == move_n_elements(&ipc->free_queue, local_free_queue, IPC_BULK_COUNT, "ipc_free -> local free")) {
			if (0 != lock) {
				usleep(13300);
				continue;
			}
			
			return FAIL;
		};
	}
	return SUCCEED;
}

int glb_ipc_send(ipc_conf_t *ipc, int queue_num, void* send_data, unsigned char lock ) {
	
	ipc_queue_t *local_free_queue = &ipc->local_queues.free_snd_queue;
	ipc_queue_t *local_send_queue = &ipc->local_queues.send_queues[queue_num];
	ipc_element_t  *element = NULL;

	if (FAIL == get_free_queue_items(ipc, local_free_queue, lock)) {

		return FAIL;
	}


	if (FAIL == move_one_element(local_free_queue, local_send_queue, "local_free -> local_send")) 
		return FAIL;
	

	element = local_send_queue->last;

	ipc->create_cb(&ipc->memf, (void *)(&element->data), send_data);
	
	flush_queues(ipc);
	return SUCCEED;
}

int  glb_ipc_process(ipc_conf_t *ipc, int consumerid, ipc_data_process_cb_t cb_func, void *cb_data, int max_count) {
	int i = 0;
	ipc_element_t *element;
	static u_int64_t last_rcv_check = 0;
	u_int64_t now = glb_ms_time();


	ipc_queue_t *local_rcv_queue = &ipc->local_queues.rcv_queue,
				*rcv_queue = &ipc->queues[consumerid],
				*local_free_queue = &ipc->local_queues.free_rcv_queue;
	
	if (consumerid < 0) {
		LOG_WRN("got consumer ipc number less then 0, this is a bug");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}	

	while (i < max_count || 0 == max_count ) {

		if (local_rcv_queue->count == 0 ) 
		{
			if (now == last_rcv_check)
				break; /*no reason to hummer queue more then once a millisecind */ 

			last_rcv_check = now;

			if (FAIL == move_all_elements(rcv_queue, local_rcv_queue)) 
				break;
			
		}

		element = local_rcv_queue->first;
		
		cb_func(&ipc->memf, i, (void *)(&element->data), cb_data);
		
		if (NULL != ipc->free_cb)
			ipc->free_cb(&ipc->memf, (void *)(&element->data));	

		move_one_element(local_rcv_queue, local_free_queue, "local_recieve -> local_free");
		
		if (local_free_queue->count >= 256 ) 
			move_all_elements(local_free_queue, &ipc->free_queue);
		
		i++;
	}
	
	return i;
}

ipc_conf_t* glb_ipc_init(int elems_count, int elem_size, int consumers, mem_funcs_t *memf,
			ipc_data_create_cb_t create_cb, ipc_data_free_cb_t free_cb, ipc_mode_t mode) {
    
	ipc_conf_t *ipc;
	int i;
    char *error = NULL;
	ipc_element_t *elements;
	

	if (NULL == (ipc = memf->malloc_func(NULL, sizeof(ipc_conf_t)))) {	
		LOG_WRN("Cannot allocate IPC structures for IPC, exiting");
		return NULL;
	}
	
	memset((void *)ipc, 0, sizeof(ipc_conf_t));

	ipc->consumers = consumers;
	ipc->memf = *memf;

	if (NULL == (ipc->queues = memf->malloc_func(NULL, sizeof(ipc_queue_t) * consumers))) {
		LOG_WRN("Couldn't allocate %ld bytes for queues ", sizeof(ipc_queue_t) * consumers);
		return NULL;
	}

	memset( (void *)ipc->queues, 0, sizeof(ipc_queue_t) * consumers);
	
	for (i=0; i< consumers; i++) {
		glb_lock_init(&ipc->queues[i].lock);
	}
	
	glb_lock_init(&ipc->free_queue.lock);

	size_t full_elem_size = elem_size + sizeof (ipc_element_t);

	if (NULL == (elements = memf->malloc_func(NULL, full_elem_size * elems_count) ) )  {
		LOG_WRN("Couldn't allocate %ld bytes for elements", full_elem_size * elems_count);
		return NULL;
	}

	void * ptr = elements;
	ipc_element_t *elem;
	for (i = 0; i < elems_count-1; i++) {
	 	elem = ptr;
		elem->next = ptr + full_elem_size;
		ptr = ptr + full_elem_size;
	}

	elem = (void *) elements + full_elem_size * (elems_count-1);
	elem->next = NULL;

	
	ipc->free_queue.first = elements;
	ipc->free_queue.last =  elem;
	ipc->free_queue.count = elems_count;
	
	ipc->create_cb = create_cb;
	ipc->free_cb = free_cb;
	ipc->mode = mode;

	glb_lock_init(&ipc->local_queues.free_rcv_queue.lock);
	glb_lock_init(&ipc->local_queues.rcv_queue.lock);

	glb_ipc_init_sender(ipc);

	LOG_DBG("%s:finished", __func__);
	return (void *)ipc;
}

void glb_ipc_destroy(ipc_conf_t *ipc) {

}

void 	glb_ipc_dump_sender_queues(ipc_conf_t *ipc, char *name) {
	LOG_INF("QUEUE sender dump at %s: local_free_queue: %d, global_free_queue: %d, local_send_queue: %d, global_snd_queue: %d, local_rcv_queue: %d", 
		name, ipc->local_queues.free_snd_queue.count, ipc->free_queue.count, ipc->local_queues.send_queues[0].count, 
			ipc->queues[0].count, ipc->local_queues.rcv_queue.count);
}

void 	glb_ipc_dump_reciever_queues(ipc_conf_t *ipc, char *name, int proc_num) {
	LOG_INF("QUEUE reciever dump at %s: local_free_queue: %d, global_free_queue: %d, global_snd_queue: %d, local_rcv_queue: %d", 
		name, ipc->local_queues.free_rcv_queue.count, ipc->free_queue.count, ipc->queues[proc_num-1].count, 
		ipc->local_queues.rcv_queue.count);
}

typedef struct {
	size_t values_num;
	u_int64_t *data;
} ipc_vector_t;

IPC_CREATE_CB(ipc_vector_uint64_create_cb) {
	zbx_vector_uint64_t *vec = local_data;
	ipc_vector_t *ipc_arr = ipc_data;

	ipc_arr->values_num = vec->values_num;

	if (0 == vec->values_num)
		return;

	if (NULL == (ipc_arr->data = memf->malloc_func(NULL, sizeof(u_int64_t) * vec->values_num))){
		LOG_WRN("Cannot allocate IPC mem");
		return;
	}

	memcpy(ipc_arr->data, vec->values,  sizeof(u_int64_t)*ipc_arr->values_num);
}

IPC_PROCESS_CB(ipc_vector_uint64_process_cb) {
	zbx_vector_uint64_t *vec = cb_data;
	ipc_vector_t *ipc_arr = ipc_data;

	zbx_vector_uint64_append_array(vec, ipc_arr->data, ipc_arr->values_num );
	
	if (NULL != ipc_arr->data )
		memf->free_func(ipc_arr->data);
}

ipc_conf_t *ipc_vector_uint64_init(int elems_count, int consumers, int mode, mem_funcs_t *memf) {

	return  glb_ipc_init(elems_count, sizeof(ipc_vector_t), consumers, memf, 
						ipc_vector_uint64_create_cb, NULL, mode);
}

int ipc_vector_uint64_recieve(ipc_conf_t *ipc, int consumerid, zbx_vector_uint64_t * vector, int max_count) {
	return glb_ipc_process(ipc, consumerid, ipc_vector_uint64_process_cb, vector, max_count );
}

int ipc_vector_uint64_send(ipc_conf_t *ipc, zbx_vector_uint64_pair_t *vector, unsigned char lock ) {
	zbx_vector_uint64_t *snd = zbx_calloc(NULL, 0, sizeof(zbx_vector_uint64_t)*ipc->consumers);
	int i;

	for (i=0; i<ipc->consumers; i++)
		zbx_vector_uint64_create(&snd[i]);
	
	for (i=0; i<vector->values_num; i++) {
		//LOG_INF("Addimg value to consumer %d", vector->values[i].first % ipc->consumers);
		zbx_vector_uint64_append(&snd[vector->values[i].first % ipc->consumers], vector->values[i].second);
	}
	
	for(i = 0; i < ipc->consumers; i++) {
//		LOG_INF("Sending %d values to consumer %d", snd[i].values_num, i);
		glb_ipc_send(ipc, i, &snd[i], lock);
		zbx_vector_uint64_destroy(&snd[i]);
	}

	zbx_free(snd);
}

void ipc_vector_uint64_destroy(ipc_conf_t *ipc) {
	glb_ipc_destroy(ipc);
}

#endif