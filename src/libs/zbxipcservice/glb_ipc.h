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

//#include "zbxvariant.h"

//IPC is just a simple single-linked list mechanic brought on top of the shm

/*to avoid contention the following logix is used:
1. "local buffering and de-buffering"
	
	when a sender want to send a data local buffer of N messages is allocated
	transparently to the sender. When sendere adds new messages, only ofter the 
	buffer gets full, it's sent to the IPC
	
	same is true when a reciever recieves data, it's M messages are detached locally
	so they can be processed without addtionall locks

2. Messages are basically anything (at least 8 bytes or u_int64_t value)
	if complex structures has to be passed or when messaging is used to pipeline
	metrics, it's better to take special measures to avoid extra memory contention:
		- allocate structures at init time then each time when needed
3. To decrease contention, it's a 'try' paradigm used to send and recieve messages
	- if queue is locked, sender or reciever doesn't wait and return on next try
	however if needed, waiting of lock might be used
	This may lead to growing buffers at sender or reciever side. Which is pretty much OK, 
	and on exchausting MAX buffer count messaging will stop which whould allow reciever 
	to process all the buffered data
	- sender might use two strategies: drop data on FAIL or lock till some buffers will 
	be free, it's up to the particular implementation 
4. it's better to create separate SHM for the each type of communication: this way
	it will be much less contention on allocating the new data. However it might be opposite 
	in cases when same data traverses several steps to avoid extra memory copyings
5.  sender might use flush function to force immediate sending
	*/
#ifndef GLB_IPC_H
#define GLB_IPC_H

#define IPC_BULK_COUNT 512
#define IPC_LOW_LATENCY_COUNT 4

typedef enum 
{
	IPC_HIGH_VOLUME = 8, //for bufferd high volume traffic
	IPC_LOW_LATENCY //to send local messages, with immediate send, but will produce more locks
} ipc_mode_t;

typedef enum 
{
	IPC_LOCK_NOWAIT = 0, //abort waiting if all buffer elements is busy
	IPC_LOCK_WAIT //wait for free buffer
} ipc_lock_mode_t;

#define IPC_PROCESS_ALL 0

typedef void (*ipc_data_create_cb_t)(mem_funcs_t *memf, void *ipc_data, void *buffer);
typedef void (*ipc_data_free_cb_t)(mem_funcs_t *memf, void *ipc_data);
typedef void (*ipc_data_process_cb_t)(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data);

#define IPC_CREATE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data, void *local_data)

#define IPC_FREE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data)

#define IPC_PROCESS_CB(name) \
		static void name(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data)

typedef struct glb_ipc_buffer_t glb_ipc_buffer_t;
typedef struct ipc_conf_t ipc_conf_t;

ipc_conf_t	*glb_ipc_init(size_t elems_count, size_t elem_size, int consumers, mem_funcs_t *memf,
			ipc_data_create_cb_t create_func, ipc_data_free_cb_t free_func, ipc_mode_t mode);
void		glb_ipc_destroy(ipc_conf_t* ipc);

int		glb_ipc_send(ipc_conf_t *ipc_conf, int queue_num , void *data, unsigned char lock_wait);
int 	glb_ipc_process(ipc_conf_t *ipc_conf, int consumerid, ipc_data_process_cb_t cb_func, void *cb_data, int max_count);

int		glb_ipc_flush(ipc_conf_t *ipc_conf);
int 	glb_ipc_force_flush(ipc_conf_t *ipc);  

void 	glb_ipc_dump_reciever_queues(ipc_conf_t *ipc_data, char *name, int queue_num);
void 	glb_ipc_dump_sender_queues(ipc_conf_t *ipc_data, char *name);


u_int64_t glb_ipc_get_sent(ipc_conf_t *ipc);
double glb_ipc_get_free_pcnt(ipc_conf_t *ipc);
u_int64_t glb_ipc_get_queue(ipc_conf_t *ipc);


/* vector specific ipc functions to pass zbx_vector_uint64_t arrays */
typedef void (*ipc_data_vector_uint64_cb_t)(mem_funcs_t *memf, int i, zbx_vector_uint64_t *vector, void *data);

ipc_conf_t*	ipc_vector_uint64_init(int elems_count, int consumers, int mode, mem_funcs_t *memf);

int 	ipc_vector_uint64_recieve(ipc_conf_t *ipc, int consumerid, zbx_vector_uint64_t * vector, int max_count);
int 	ipc_vector_uint64_send(ipc_conf_t *ipc, zbx_vector_uint64_pair_t *vector, unsigned char lock);

void 		ipc_vector_uint64_destroy(ipc_conf_t *ipc);
#endif