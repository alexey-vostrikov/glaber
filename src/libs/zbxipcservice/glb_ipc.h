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


typedef enum
{
	IPC_PRE_PROCESSING_TYPE =0 ,  //buffer to send items from pollers to preprocessing
	IPC_PROCESSING,	//processing queue to send to processors
	IPC_PROCESSING_NOTIFY,	//processing queue to notify processors about config changes
	GLB_IPC_TYPE_COUNT,  //these too should always be the last ones
	GLB_IPC_NONE 
} glb_ipc_types_enum;

typedef void (*ipc_data_create_cb_t)(mem_funcs_t *memf, void *ipc_data, void *buffer);
typedef void (*ipc_data_free_cb_t)(mem_funcs_t *memf, void *ipc_data);
typedef void (*ipc_data_process_cb_t)(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data);

#define IPC_CREATE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data, void *local_data)

//#define IPC_RELEASE_CB(name) \
//		static void name(mem_funcs_t *memf, void *ipc_data, void *local_data)

#define IPC_FREE_CB(name) \
		static void name(mem_funcs_t *memf, void *ipc_data)

#define IPC_PROCESS_CB(name) \
		static void name(mem_funcs_t *memf, int i, void *ipc_data, void *cb_data)


typedef struct glb_ipc_buffer_t glb_ipc_buffer_t;

//typical init staff
void	*glb_ipc_init(unsigned char ipc_type, size_t mem_size, char *name, int elems_count, int elem_size, int consumers, mem_funcs_t *memf,
			ipc_data_create_cb_t create_func, ipc_data_free_cb_t free_func);
void	glb_ipc_destroy();

//this will copy data to the ipc mem and put to local queue and try to flush 
int		glb_ipc_send(void *ipc_conf, int queue_num , void *data, unsigned char lock);

int 	glb_ipc_process(void *ipc_conf, int consumerid, ipc_data_process_cb_t cb_func, void *cb_data, int max_count);
void	glb_ipc_flush(void *ipc);

//sender/reciever should be called once for each type if several IPCs are in use
int  	glb_ipc_init_sender(unsigned char ipc_type, int consumers);
int 	glb_ipc_init_reciever(unsigned char ipc_type);

//should be called just once per fork for cleanup
void	glb_ipc_client_destroy();

//for sides that need to cleanup/malloc data in ipc 
mem_funcs_t *glb_ipc_get_memf_funcs(void *ipc_data);

//to flush remaining in case of fast processing
int glb_ipc_flush_all(void *ipc_conf);

void 	glb_ipc_dump_reciever_queues(void *ipc_data, char *name, int queue_num);
void 	glb_ipc_dump_sender_queues(void *ipc_data, char *name);