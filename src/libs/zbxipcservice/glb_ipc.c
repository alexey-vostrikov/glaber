/****** the copyright ***************/
/* read carefully
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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

#define GLB_IPC_UNLOCKED 		0
#define GLB_IPC_LOCKED 			1

#define GLB_IPC_MODE_STRICT 	1
#define GLB_IPC_MODE_NONSTRICT	2
#define GLB_IPC_MODE_ALL 		3

//local buffer options - timeout and maximum number of items
#define GLB_IPC_MAX_CACHE_LOCAL 16384
#define GLB_IPC_MAX_CACHE_TIMEOUT 1

#define GLB_IPC_BUFFERS 65536
#define GLB_IPC_MAX_DATA_SZIE 2*1024*1024 //1Meg is enough for any data

extern size_t CONFIG_IPC_BUFFER_SIZE;
			  

static  zbx_mem_info_t	*ipc_mem;
ZBX_MEM_FUNC_IMPL(__ipc, ipc_mem)

//limiting number of each ipc elements by it's queue elems
typedef struct glb_ipc_queue_item_t glb_ipc_queue_item_t;
struct glb_ipc_queue_item_t{
	 glb_ipc_buffer_t *buffer;
     glb_ipc_queue_item_t *next;
}; 

typedef struct {
	glb_ipc_queue_item_t *first;
	glb_ipc_queue_item_t *last;
	unsigned int count;
	pthread_mutex_t p_lock;
} glb_ipc_queue_t;

typedef struct {
	glb_ipc_buffer_t *first;
	pthread_mutex_t q_lock;
	unsigned int count;
} glb_ipc_buffer_queue_t;


/* ipc per-type data, need one buffer for the each communication type */
#define GLB_IPC_MAX_CONSUMERS 64

//ipc type to handle all IPC work
typedef struct
{
	/* the number of transfered items per IPC type, used for statistics */
	zbx_uint64_t		sent;
	glb_ipc_queue_t		free;   //queue of free queue_items
    glb_ipc_queue_t		queued[GLB_IPC_MAX_CONSUMERS]; //array of queues for items ready for processing, should be allocated in shm
 	glb_ipc_type_cfg_t	conf;
} glb_ipc_type_t;

//finally, IPC struct that describes all the IPC
typedef struct {
	glb_ipc_type_t ipc[GLB_IPC_TYPE_COUNT];
	glb_ipc_buffer_queue_t free_buffers; //this queue will hold free queue_items to handle buffers
	glb_ipc_buffer_queue_t buffers[GLB_IPC_BUFFERS];
	pthread_mutex_t mem_lock;
	char test_string[MAX_BUFFER_LEN];
	unsigned int  buffers_count;
} glb_ipc_t;

//has to be allocated in shm
glb_ipc_t *glb_ipc;

//for IPC clients, holds internal queues
static glb_ipc_type_t local_ipc[GLB_IPC_TYPE_COUNT];
/**********************************************************
 * fetches one item from the queue. Ineficcient
 * //TODO: mass fetch
***********************************************************/
glb_ipc_queue_item_t *glb_ipc_get_item(glb_ipc_queue_t *queue) {
	glb_ipc_queue_item_t *elem = NULL;
	
	glb_lock_block(&queue->p_lock);

	if (NULL == queue->first) {
		glb_lock_unlock(&queue->p_lock);
		return NULL;
	} 

	elem = queue->first;
	queue->first = elem->next;

	if (NULL == elem) {
		queue->last = NULL;
	}
	
	queue->count--;

	glb_lock_unlock(&queue->p_lock);
	return elem;
}
/*****************************************************************
* adds a queue of items to the existing queue
* ***************************************************************/
unsigned int glb_ipc_put_items(glb_ipc_queue_t *queue,  glb_ipc_queue_item_t *item,  glb_ipc_queue_item_t *new_last, unsigned int count) {

	glb_ipc_queue_item_t *old_last = NULL;
	
	glb_lock_block(&queue->p_lock);
	
	if (NULL == new_last ) 
		new_last = item;
	
	new_last->next = NULL;

	if ( NULL == queue->first) {
		queue->last = new_last;
		queue->count = count;
		queue->first = item;
	} else {
		if (NULL == queue->last) {
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
		}
		queue->last->next = item;
		queue->last = new_last;
		queue->count += count;
	}
	
	glb_lock_unlock(&queue->p_lock);

	return SUCCEED;
}


/*************************************************************
 * Fetches one item from the end of the queue 
 * unless it's the last item, doesn't locks, uses CAS
 * to fetch. Allthow it seems safe, i only recommend to 
 * use it with one consumer and use locked version 
 * when there are many consumers
**************************************************************/
/*
static  glb_ipc_queue_item_t *glb_ipc_get_item_nolock1( glb_ipc_queue_t *queue) {
	int i=0;
	 glb_ipc_queue_item_t *old_head = queue->first, *new_head = NULL;
	
	do {
		if (i++ > 10) usleep(1);

		//no data or locked - go away
	//	if (queue->p_lock > 0)  {
	//		zabbix_log(LOG_LEVEL_INFORMATION, "Queue is locked, cannot fetch");
	//		return NULL;
	//	}
		
		if (NULL == queue->first || NULL == queue->last)  {
			//zabbix_log(LOG_LEVEL_INFORMATION, "Queue is emtpy, nothing to fetch");
			return NULL;
		}
		//leave 1 space element, this way we don't deal with last element change
		//which a) doubles the work and so b) increases chances 
		//TODO: get rid of this, at least, by locking,  or make sure that each newly created queue holds an element
		
		//we stop requiring that there was a spacer, BUT 
		//while we updating the first element, it's possible that another thread will add something to 
		//the element... so after successufull first->null change we check if last has a new value now,
		//and if so, setting first 
		old_head = queue->first;

		if (NULL != old_head) 
			new_head = old_head->next;
		else 
			return NULL; //no way to get the item - head has become null (someone has changed it)

		if (queue->first == queue->last) {
			//the special case: there is only one element 
			//CAS last first and only after - first to prevent adding new data to the removed element 
			if (__sync_bool_compare_and_swap(&queue->last, old_head, NULL)) {
				//we succeed, so no items will be added after the current item, they'll get an empty last and reset last and first
				//but before we continue, lets try to sawp first to null, but only if it's still the old value
				__sync_bool_compare_and_swap(&queue->first, old_head, NULL);
				//event if we failed, this means writer has altered first already due to knowning last is null, which is fine
				return old_head;

			} else 	//last has changed before us, probably, addition of new data completed or someone succeded on retrieving the item
					//on it's way, we have to repeat all over again, starting from resetting varaibles
				continue;
		}
	} while (!__sync_bool_compare_and_swap(&queue->first, old_head, new_head));
	
	return old_head;
}
*/


/************************************************
 * Global init, should be called before forks	*
************************************************/
int glb_ipc_init(glb_ipc_type_cfg_t *comm_types) {
    
	int ipc_type = 0;
	int i;
    char *error = NULL;
	
	
	zabbix_log(LOG_LEVEL_INFORMATION,"Allocating shared memory for IPC size %d",CONFIG_IPC_BUFFER_SIZE);

	//SHM
	if (SUCCEED != zbx_mem_create(&ipc_mem, CONFIG_IPC_BUFFER_SIZE, "IPC cache size", "IPCsize", 1, &error))
		return FAIL;

	//IPC struct 
	if (NULL == (glb_ipc = (glb_ipc_t *)__ipc_mem_malloc_func(NULL, sizeof(glb_ipc_t)))) {	
		zabbix_log(LOG_LEVEL_CRIT,"Cannot allocate IPC structures, exiting");
		return FAIL;
	}

	memset((void *)glb_ipc, 0, sizeof(glb_ipc_t));
	
	//locks
	for (i=0; i<GLB_IPC_BUFFERS; i++) {
		glb_lock_init(&glb_ipc->buffers[i].q_lock);	
	}

	glb_lock_init(&glb_ipc->mem_lock);
	
	for ( ipc_type = 0; GLB_IPC_NONE != comm_types[ipc_type].type; ipc_type++ ) {
		void *data;
		
		glb_ipc_type_cfg_t *cfg = &comm_types[ipc_type];

		zabbix_log(LOG_LEVEL_INFORMATION, "Creating IPC mem and structures for IPC type %d, elements: %d", 
				cfg->type, cfg->elems_count);

		glb_ipc->ipc[ipc_type].conf.consumers = comm_types[ipc_type].consumers;
		glb_ipc->ipc[ipc_type].conf.elems_count = comm_types[ipc_type].elems_count;

		for (i = 0; i < GLB_IPC_MAX_CONSUMERS; i++ ) {

			glb_ipc->ipc[ipc_type].queued[i].count = 0;
			glb_ipc->ipc[ipc_type].queued[i].first = NULL;
			glb_ipc->ipc[ipc_type].queued[i].last = NULL;
			glb_lock_init(&glb_ipc->ipc[ipc_type].queued[i].p_lock);
		}	
		
		glb_lock_init(&glb_ipc->ipc[ipc_type].free.p_lock);
			
		glb_ipc_queue_item_t *prev_elem = NULL;
		//TODO: return mass addition of queue elements - they aren't expected to be free at all
		for ( i = 0 ; i < cfg->elems_count; i++ ) {
			glb_ipc_queue_item_t *q_elem;
			
			if (NULL == (q_elem = __ipc_mem_malloc_func(NULL,sizeof(glb_ipc_queue_item_t)))) {
				zabbix_log(LOG_LEVEL_WARNING, "Cannot do init for type %d, element %d, exiting", ipc_type, i);
				exit(-1);
			}

			q_elem->buffer = NULL;

			if (0 == i) {
				glb_ipc->ipc[ipc_type].free.first = q_elem;
			} else {
				prev_elem->next = q_elem;
			}
			prev_elem = q_elem;
		}
		prev_elem->next = NULL;
		glb_ipc->ipc[ipc_type].free.last = prev_elem;

	}
	zabbix_log(LOG_LEVEL_INFORMATION, "%s:finished", __func__);
	return SUCCEED;
}


/*************************************************************
 * Fetches all items from the queue 
 **************************************************************/
 /*
 glb_ipc_queue_item_t *glb_ipc_get_items_nolock( glb_ipc_queue_t *queue) {

	int i=0;
	 glb_ipc_queue_item_t *old_head = queue->first, *old_last, *new_head = NULL;

	do {
		if (i++ > 10) usleep(1);

		//no data or locked - go away
		//if (queue->lock > 0 || NULL == queue->first || NULL == queue->last) 
		//	return NULL;
		//leave 1 space element, this way we don't deal with last element change
		//which a) doubles the work and so b) increases chances 
		//TODO: get rid of this, at least, by locking,  or make sure that each newly created queue holds an element
		
		//we stop requiring that there was a spacer, BUT 
		//while we updating the first element, it's possible that another thread will add something to 
		//the element... so after successufull first->null change we check if last has a new value now,
		//and if so, setting first 
		old_head = queue->first;
		old_last = queue->last;

		if (NULL != old_head) 
			new_head = NULL;
		else 
			return NULL; //no way to fetch any item - head has become null (someone has changed it)

		if (old_head == old_last) {
			//the special case: there is only one element 
			//CAS last and only after it - first to prevent adding new data to the removed element 
			if (__sync_bool_compare_and_swap(&queue->last, old_head, NULL)) {
				//we succeed, so no items will be added after the current item, they'll get an empty last and reset last and first
				//but before we continue, lets try to sawp first to null, but only if it's still the old value
				__sync_bool_compare_and_swap(&queue->first, old_head, NULL);
				//event if we failed, this means writer has altered first already due to knowning last is null, which is fine
				return old_head;

			} else 	//last has changed before us, probably, addition of new data completed or someone succeded on retrieving the item
					//on it's way, we have to repeat all over again, starting from resetting varaibles
				continue;
		}
	} while (!__sync_bool_compare_and_swap(&queue->first, old_head, NULL));
	
	//when first point to NULL, no one will read our data anymore
	//but someone might be adding new data to the tail right now
	//it's not a big problem, we just get some more data and will 
	//get new queue->last. 
	do {
		old_last = queue->last;
	 } while (!__sync_bool_compare_and_swap(&queue->last, old_last, NULL));

	//by this moment we've got a new last item, the old one is pointing to the our end
	//closing the list
	if (old_last) 
		old_last->next = NULL;
	
	return old_head;
}
*/
/*************************************************************
 * Adds the item or items to the queue's tail 
 * if Item has been added, returns SUCCEED, after that
 * added items should only be processed in any way only atfer 
 * fetching from the queue. new_last must point to the end of queue
 * it might be NULL, so only one item will be added
**************************************************************/
/*
static unsigned int glb_ipc_put_items_nolock1( glb_ipc_queue_t *queue,  glb_ipc_queue_item_t *item,  glb_ipc_queue_item_t *new_last, unsigned int count) {
	 glb_ipc_queue_item_t *old_last = NULL;

	zabbix_log(LOG_LEVEL_INFORMATION, "ipc mem address is %ld",ipc_mem);

	if (NULL == new_last ) 
		new_last = item;
	
	//if (queue->lock > 0 )  
	//	return FAIL;

	//wait till the queue in the consistent state
	while ( (NULL != queue->last && NULL == queue->first) || 
			(NULL == queue->last && NULL != queue->first) ) {
		usleep(1);
	}
	
	//just for safety
	new_last->next = NULL;

	do {
		old_last = queue->last;
	} while (!__sync_bool_compare_and_swap(&queue->last, old_last, new_last));
	//now, if we happened to change last from NULL to an addr (we where the first to succeed data add)
	//we have to set first to the first item
	if (NULL == old_last) 
		queue->first = item;
	else 
		old_last->next = item;

	queue->count += count;
	//ok now there is no problem to set the last item from the queue
	//pointing to our new head - the item will remain in the queue until we do that
	
	return SUCCEED;
}
*/

/********************************************************
 *  Using atomics for lock/unlock						*
 *******************************************************/
//static void glb_ipc_lock(pthread_mutex_t *lock) {
//	zabbix_log(LOG_LEVEL_INFORMATION, "In %s: started", __func__);
//	zabbix_log(LOG_LEVEL_INFORMATION, "In %s: started, lock addr is %ld, lock val is %d", __func__, lock, *lock);
//	while(1) {
//		zabbix_log(LOG_LEVEL_INFORMATION, "In %s: lock went to 0, trying to lock ", __func__);
	//	usleep(50);
//		if( __sync_bool_compare_and_swap(lock, GLB_IPC_UNLOCKED, GLB_IPC_LOCKED)) {
//			zabbix_log(LOG_LEVEL_INFORMATION, "In %s: finished", __func__);
//			return;
//		}
//		usleep(50);
//	}
//	pthread_mutex_lock(lock);	
	
	
	//return;
//}

//static void glb_ipc_unlock(pthread_mutex_t *lock) {
	
//	pthread_mutex_unlock(lock);
	//*lock = GLB_IPC_UNLOCKED;
//	zabbix_log(LOG_LEVEL_INFORMATION, "In %s: finished", __func__);
//}



/******************************************************
 * allocates a buffer in the shm 					  *
 * ****************************************************/
void *glb_ipc_malloc(size_t size) {
	void *ret;
	int num;
	zabbix_log(LOG_LEVEL_INFORMATION,"IPC: locking memory");
	
	glb_lock_block(&glb_ipc->mem_lock);
	
//	zabbix_log(LOG_LEVEL_INFORMATION,"IPC: locked mem, calling zbx_mem_malloc");
//	zabbix_log(LOG_LEVEL_INFORMATION,"Memory stats: total %ld, used %ld, free %ld%%", ipc_mem->total_size, ipc_mem->used_size, (ipc_mem->free_size*100)/ipc_mem->total_size);

	ret = zbx_mem_malloc(ipc_mem, NULL,size);
	

	glb_lock_unlock(&glb_ipc->mem_lock);
	
	zabbix_log(LOG_LEVEL_INFORMATION,"IPC: unlocked memory");
	return ret;
}

//TODO: add calc of either size, not only index to fully utilize the buffers
#define GLB_IPC_BUFFER_MIN_SIZE 24
#define GLB_IPC_GRANULAR_SIZE 32768
#define HI_RES_GRAN 8
#define LO_RES_GRAN 256

static unsigned int glb_ipc_calc_buf_idx(size_t data_size) {
	unsigned int buf_idx;
	if (data_size < GLB_IPC_BUFFER_MIN_SIZE)
	 	data_size = GLB_IPC_BUFFER_MIN_SIZE;

	data_size +=sizeof(glb_ipc_buffer_t);
	
	if (data_size <  GLB_IPC_GRANULAR_SIZE) { 
		buf_idx = (data_size - GLB_IPC_BUFFER_MIN_SIZE) / HI_RES_GRAN;
		data_size = (buf_idx + 1) * HI_RES_GRAN + GLB_IPC_BUFFER_MIN_SIZE;
	} else {
		buf_idx = ( GLB_IPC_GRANULAR_SIZE - GLB_IPC_BUFFER_MIN_SIZE) / HI_RES_GRAN + (data_size - GLB_IPC_GRANULAR_SIZE ) / LO_RES_GRAN;
	}
	
	if (buf_idx >= GLB_IPC_BUFFERS) {
		zabbix_log(LOG_LEVEL_INFORMATION,
				"Requested buffer of %ld size, thats exceeding maximum IPC size %d, this is bug",
				GLB_IPC_GRANULAR_SIZE  + LO_RES_GRAN * (GLB_IPC_BUFFERS - GLB_IPC_GRANULAR_SIZE /HI_RES_GRAN ) );
		
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}
  	return buf_idx;
}



/****************************************************************
 * buffer allocation - if exists,  retrieved from the cache
 * if not, then new buffer is allocated from the free IPC memory
****************************************************************/
 glb_ipc_buffer_t *glb_ipc_get_buffer(size_t raw_size) {
	glb_ipc_buffer_t *buffer = NULL;
	size_t total_size = raw_size - 1 + sizeof (glb_ipc_buffer_t);


	unsigned int buf_idx = glb_ipc_calc_buf_idx(total_size);

		
	zabbix_log(LOG_LEVEL_INFORMATION,"In %s: allocating %d bytes, index is %d", __func__, total_size, buf_idx);
	
	//do minimal length of GLB_IPC_MIN_BUFF_SIZE
	
	zabbix_log(LOG_LEVEL_INFORMATION,"In %s: buffer index is %d", __func__, buf_idx);
	//try to fetch an item from the buffers
	glb_lock_block(&glb_ipc->buffers[buf_idx].q_lock);
	
	if ( NULL != glb_ipc->buffers[buf_idx].first ) {
		buffer = glb_ipc->buffers[buf_idx].first;
		glb_ipc->buffers[buf_idx].first = buffer->next;
		 
	}
	glb_lock_unlock(&glb_ipc->buffers[buf_idx].q_lock);	
	
	zabbix_log(LOG_LEVEL_INFORMATION,"In %s: Allocated from buffer queue %d", __func__, buf_idx);

	if (NULL == buffer )  {
		zabbix_log(LOG_LEVEL_INFORMATION,"In %s: Allocating via zbx_mem_alloc %d", __func__, total_size);
		buffer = (glb_ipc_buffer_t *)glb_ipc_malloc(total_size);
		buffer->magic = 89234234;
		
		buffer->buf_idx = buf_idx;
		zabbix_log(LOG_LEVEL_INFORMATION,"In %s: Allocated from malloc", __func__);
	}

	return buffer;
}

//buffer allocation to be called from the external 
//places 
void *glb_ipc_alloc(size_t size) {
	glb_ipc_buffer_t *buffer = glb_ipc_get_buffer(size);		

	return (void *)buffer->data;
}

/******************************************************
 * allocates buffer and retruns data part for it in 
 * IPC shm, respect NULLS
 * ****************************************************/
char *glb_ipc_alloc_str(char *string) {
	glb_ipc_buffer_t *buffer;
	
	if (NULL == string)
		return NULL;

	//strlen doesn't retrun 0 terminator, but we need it
	size_t len = strlen(string)+1;
	
	buffer = glb_ipc_get_buffer(len);		
	if (buffer->magic != 89234234 ) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Non magical buffer retruned in string allocation, bad times");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}
	zbx_strlcpy(buffer->data,string,len);

	return buffer->data;
}
/***********************************************************
 * Places buffer back to buffers queue
************************************************************/
//TODO: make a single call to safely allocate or fetch from buffers
void glb_ipc_release_buffer(glb_ipc_buffer_t *buffer) {

	zabbix_log(LOG_LEVEL_INFORMATION, "In %s: started", __func__);
	
	if (buffer->magic != 89234234 ) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Magic is wrong, wrong buffer pointer has been retruned!");
		exit(-1);
	}

	buffer->lastuse = time(NULL);
	zabbix_log(LOG_LEVEL_INFORMATION, " %s IPC:clean Putting  buffer item back to free buffers", __func__);

	glb_lock_block(&glb_ipc->buffers[buffer->buf_idx].q_lock);

	buffer->next = glb_ipc->buffers[buffer->buf_idx].first;
	glb_ipc->buffers[buffer->buf_idx].first = buffer;
	
	glb_lock_unlock(&glb_ipc->buffers[buffer->buf_idx].q_lock);

	zabbix_log(LOG_LEVEL_INFORMATION, "In %s: finished", __func__);
	}


/*******************************************************
 * deallocates the buffer referenced by data ptr
 *  it's expected that 
 * the address is part of the buffer structure
 * if glb_ipc_buffer_t changes, this should be reflected 
 * here
 * *****************************************************/
void glb_ipc_free(void *ptr) {
	//the trick is thar ptr point to the buffer part wich is prepended by
	//size_t and unsigned long 
	if (NULL == ptr) 
		return;
	
	glb_ipc_buffer_t *buffer = (void*) ptr - offsetof(glb_ipc_buffer_t, data);
	
	if (buffer->magic != 89234234 ) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Magic is wrong (got %ld,%ld,%ld,%ld, %ld), wrong buffer pointer has been retruned", buffer->magic, buffer->buf_idx, buffer->lastuse, buffer->next, buffer->data);
		exit(-1);
	}
	//now we have the buffer, we can retrun it
	glb_ipc_release_buffer(buffer);
}


void glb_ipc_dump_metric( GLB_METRIC *metric) {
	zabbix_log(LOG_LEVEL_INFORMATION,"Metric dump: \n	addr:%ld,\n	itemid:%ld,\n	hostid:%ld,\n	vector_id_ptr:%ld,\n	host_ptr:%ld", 
						metric, metric->itemid, metric->hostid, metric->vector_id, metric->hostname);
}

/*********************************************************
* inits local heap structures for being able to buffer and
* send messages
* for proc_type. If several types is used, them should
* be called several time - once per type
*********************************************************/

int  glb_ipc_init_sender(int consumers, int ipc_type) {
	
	memset( (void *)local_ipc[ipc_type].queued, 0,  sizeof(glb_ipc_queue_t)*consumers);
	local_ipc[ipc_type].sent = 0 ;
	local_ipc[ipc_type].conf.consumers = consumers;
	
	return SUCCEED;
} 

#define GLB_IPC_MAX_LOCAL_COUNT 128

void glb_ipc_add_data(int ipc_type, int q_num,  void* buffer) {
	
	glb_ipc_queue_item_t *q_item;
	zabbix_log(LOG_LEVEL_INFORMATION, "IPC:Put local Getting new SHM queue item for type %d");

	while (NULL ==(q_item = glb_ipc_get_item(&glb_ipc->ipc[ipc_type].free))) {
		sleep(1);
		zabbix_log(LOG_LEVEL_INFORMATION, "Failed to got a new SHM queue item, either increase  elements or check for slowdowns");
	}
	
	q_item->buffer = buffer;
	q_item->next = NULL;

	glb_ipc_put_items(&glb_ipc->ipc[ipc_type].queued[q_num],q_item,q_item,1);

}
/*
void glb_ipc_buffer_localy(int ipc_type, int q_num, ) {
	//fetch or create queue item to hold the data
	 glb_ipc_queue_item_t *q_item;
	//TODO: a safe and debuggable way of repetitive calls	
	zabbix_log(LOG_LEVEL_INFORMATION, "IPC:Put local Getting new SHM queue item for type %d");
	while (NULL ==(q_item = glb_ipc_get_item(&glb_ipc->ipc[ipc_type].free))) {
		sleep(1);
		zabbix_log(LOG_LEVEL_INFORMATION, "Failed to got a new SHM queue item, either increase  elements or check for slowdowns");
	}
	
	q_item->buffer= buffer;
	q_item->next = NULL;

	zabbix_log(LOG_LEVEL_INFORMATION, "Got new SHM queue item");

	glb_ipc_queue_t *queue = &local_ipc[ipc_type].queued[q_num];

	//sanity check
	 glb_ipc_buffer_t *buff =(glb_ipc_buffer_t*) data - sizeof(glb_ipc_buffer_t);
	if (buff->magic != 89234234) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Magic is screwed on sending, fall off");
		THIS_SHOULD_NEVER_HAPPEN;
		exit(-1);
	}

	if (NULL == queue->first )  {
		queue->first =  q_item;
		queue->last =  q_item;

	} else {
		queue->last->next = q_item;
		queue->last = q_item;
	}
	
	queue->count++;
	zabbix_log(LOG_LEVEL_INFORMATION,"Metric added, total %d metrics in the local queue %d, ipc addr is %ld, queue addr is %ld", 
	     queue->count, q_num, glb_ipc, &glb_ipc->ipc[GLB_IPC_PROCESSING].queued);
	//caller shouldn't forget to flush data periodically
	//it's quite inexpensive - any amount is sent without locks
	//glb_ipc_flush(ipc_type);
	//return;
}
*/
/*********************************************************
 * buffers the data localy by hash calculated based on 
 * number of consimers (same idx will go to same consumer)
 * ******************************************************/
/*
void *glb_ipc_save_localy_hashed(int ipc_type, u_int64_t idx,  void* data) {
	static unsigned long lastflush = 0;

	unsigned int hash = idx % local_ipc[ipc_type].conf.consumers;
	zabbix_log(LOG_LEVEL_INFORMATION,"IPC:Put Queueing item %ld (hash %d) to queue %d", idx, hash, ipc_type );

	glb_ipc_buffer_localy(ipc_type, hash, data);
	
	if (lastflush != time(NULL)) {
		glb_ipc_flush(ipc_type);
		lastflush = time(NULL);
	}

}
*/	
/******************************************************
 * flushes data to IPC memory from the "local" buffer *
 * actually, it's expected that "local" data is in the*
 * IPC either 										  *
 * ****************************************************/
void glb_ipc_flush(int ipc_type) {
	int i;
	 glb_ipc_queue_t *q_to;
	 glb_ipc_queue_t *q_from;
	 glb_ipc_queue_item_t *tmp_i = NULL;


	for ( i=0; i<local_ipc[ipc_type].conf.consumers ; i++) {
		q_from = &local_ipc[ipc_type].queued[i];
		
		zabbix_log(LOG_LEVEL_INFORMATION, "Dumping metrics before attaching to the SHM");
		tmp_i = q_from->first;
		while (tmp_i) {
			glb_ipc_dump_metric((GLB_METRIC *)tmp_i->buffer->data);
			tmp_i=tmp_i->next;
		}
		zabbix_log(LOG_LEVEL_INFORMATION, "Finished dumping metrics");

		if (NULL == q_from->first)
			continue;
		
		q_to = &glb_ipc[ipc_type].ipc->queued[i];
		zabbix_log(LOG_LEVEL_INFORMATION, "Adding to %d metrics to IPC_type %d queue num %d , ipc_type addr is %ld  queue addr is %ld",
							q_from->count, ipc_type, i, &glb_ipc[ipc_type].ipc, &glb_ipc[ipc_type].ipc->queued[i]);
		glb_ipc_put_items(q_to, q_from->first, q_from->last, q_from->count);
		zabbix_log(LOG_LEVEL_INFORMATION, "Added  %d metrics to IPC_type %d queue num %d",q_from->count, ipc_type, i);

		memset((void *)q_from, 0, sizeof(glb_ipc_queue_t));
	}
}


/**************************************************
 * Reciever API                           		  *
 * ************************************************/



/********************************************************
 * Gets single data items, queue item retruned to 
 * the buffer, if possible better use mass version
 * ******************************************************/
void* glb_ipc_get_data(int ipc_type, int q_num) {
	
	//zabbix_log(LOG_LEVELINFORMATION, "ipc mem address is %ld",ipc_mem);
	 void *buffer = NULL;
	 glb_ipc_queue_item_t *q_item = NULL;
	 

	//zabbix_log(LOG_LEVEL_INFORMATION, "IPC:Read data from IPC type %d queue num %d, ipc addr is %ld, ipc type addr is %ld, queue addr is %ld",
	 //		 ipc_type, q_num, glb_ipc, &glb_ipc->ipc[ipc_type], &glb_ipc->ipc[ipc_type].queued);
	
	q_item = glb_ipc_get_item(&glb_ipc->ipc[ipc_type].queued[q_num]);
		
	if (NULL != q_item ) {
//		zabbix_log(LOG_LEVEL_INFORMATION, "IPC:Read got data from IPC type %d queue num %d ptr is %ld", ipc_type, q_num, q_item);
		buffer = q_item->buffer;
		q_item->buffer = NULL;
		glb_ipc_put_items(&glb_ipc->ipc[ipc_type].free,q_item,q_item,1);
	} else {
		buffer = NULL;
	}
	
	return buffer;
}
/*************************************************************
 * mass version of the data retrieval 
 * puts all the data to the vector, returns 
 * number of items fetched
 * ***********************************************************/
/*
//TODO: after succesifull payload test, do this version, 
int glb_ipc_get_mass_data(int ipc_type, int q_num, zbx_vector_ptr_t *data) {

}
*/
int glb_ipc_dump_queue_stats() {
	int i,j;
	zabbix_log(LOG_LEVEL_INFORMATION,"*************************************************");
	zabbix_log(LOG_LEVEL_INFORMATION,"* IPC queues dump                               *");
	zabbix_log(LOG_LEVEL_INFORMATION,"*************************************************");
	zabbix_log(LOG_LEVEL_INFORMATION,"Global data:");
	zabbix_log(LOG_LEVEL_INFORMATION,"	Buffers: total: %d, free queue items: %d\n",glb_ipc->buffers_count,glb_ipc->free_buffers);
	zabbix_log(LOG_LEVEL_INFORMATION,"	Buffers stat:");
	
	//for (i =0 ; i < GLB_IPC_BUFFERS; i++) {
	//	zabbix_log(LOG_LEVEL_INFORMATION, "		Buffer[%d]: %d items",i, glb_ipc->buffers[i].count);
	//}
	zabbix_log(LOG_LEVEL_INFORMATION, "\n");
	zabbix_log(LOG_LEVEL_INFORMATION, "	Statistics by IPC TYPE:");
	for (i = 0; i < GLB_IPC_TYPE_COUNT; i++) {
		zabbix_log(LOG_LEVEL_INFORMATION, "		Queue: %d, free item buffers: %d, queues: %d", i, glb_ipc->ipc[i].free.count,glb_ipc->ipc[j].conf.consumers);
		for ( j = 0; j < glb_ipc->ipc[i].conf.consumers; j++ ) {
			zabbix_log(LOG_LEVEL_INFORMATION, "				Queue: %d, items: %d", glb_ipc->ipc[i].queued[j].count);
		}
	}
	zabbix_log(LOG_LEVEL_INFORMATION,"*************************************************");
	zabbix_log(LOG_LEVEL_INFORMATION,"* end of IPC queues dump                        *");
	zabbix_log(LOG_LEVEL_INFORMATION,"*************************************************");
};

//that's pretty strange destroy proc yet
void	glb_ipc_destroy(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
    //there is no reason to go and waste time on 
    //destroying all the objects, on existing
    //shared mem will get freed immediately

    //however to make things right, this proc is here
    //TODO:implement full objects destroy sequence
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


//p.s. my intention was to do a simple IPC handling in less then 500 lines. 
//However i failed this, too many comments by now, so far 730 lines 
//(but this also actually includes simple memory management)
//update - by now, even with moving part of structures and code to the header file, have 847 lines....


#endif