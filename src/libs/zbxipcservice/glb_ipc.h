/***********The cipyright **********/
//TODO: fill the copy left


#include "zbxvariant.h"

typedef struct
{
	u_int64_t itemid;
	u_int64_t hostid;
	zbx_timespec_t ts;
	
	char *vector_id; //if metric belongs to a vector, should be set
	
	int logeventid;         //log needs theese.
	unsigned char severity;	//fields either
							//there is also a source field, but it's quite strange
							//to keep it as source is identified by host/key items

	char *hostname;	//host (service)
	char *key;	//and key names are the primary metric identifiers

	unsigned int flags;	//GLB_METRIC_FLAG_*
	unsigned int code;	//GLB_METRIC_CODE_*
	
	struct zbx_variant value;

} GLB_METRIC;



//all buffers keep their size in the first
//4 bytes
//it maybe worth of adding timastap when it was returned to the queue for cleaning
//OMG, i am writting own memeory management system here, stupid and simple

typedef enum
{
	GLB_IPC_PROCESSING =0 ,  //buffer to send items from pollers to preprocessing
	GLB_IPC_TYPE_COUNT,  //these too should always be the last ones
	GLB_IPC_NONE 
} glb_ipc_types_enum;

//config options for an IPC type
typedef struct {
	unsigned char   type;
	unsigned int    elems_count; //IPC elements for the type
	unsigned char   consumers; //how many IPC consumer queues to create
} glb_ipc_type_cfg_t;

typedef struct glb_ipc_buffer_t glb_ipc_buffer_t;

struct glb_ipc_buffer_t {
	unsigned int buf_idx;
	unsigned int magic;
	unsigned int lastuse;
	glb_ipc_buffer_t *next;
	char data[32];
};

void* glb_ipc_save_localy_hashed(int ipc_type, u_int64_t idx,  void* data);
void  glb_ipc_flush(int ipc_type);

void* glb_ipc_alloc(size_t size);
void  glb_ipc_free(void *ptr);
char* glb_ipc_alloc_str(char *string);

int  glb_ipc_init_sender(int consumers, int ipc_type);
int glb_ipc_init(glb_ipc_type_cfg_t *comm_types);

void* glb_ipc_get_data(int ipc_type, int q_num);
void glb_ipc_add_data(int ipc_type, int q_num,  void* buffer);

void glb_ipc_dump_metric( GLB_METRIC *metric);
int glb_ipc_dump_queue_stats();
glb_ipc_buffer_t *glb_ipc_get_buffer(size_t raw_size);
void glb_ipc_release_buffer(glb_ipc_buffer_t *buffer);
void	glb_ipc_destroy(void);
