//#ifndef _GLABER_RUNNER
//#define _GLABER_RUNNER

#include <stdio.h>
#include <sys/types.h>
#include "dbcache.h"

typedef struct ext_worker_t ext_worker_t; 

ext_worker_t* glb_init_worker(char *params);
const char *worker_get_path(ext_worker_t *worker);

void worker_set_mode_from_worker(ext_worker_t *worker, unsigned char mode);
void worker_set_mode_to_worker(ext_worker_t *worker, unsigned char mode);
void worker_set_async_mode(ext_worker_t *worker, unsigned char mode);
ext_worker_t *worker_init( const char* path, unsigned int max_calls, 
            unsigned char async_mode, unsigned char mode_to_worker, unsigned char mode_from_worker, int timeout );


int glb_process_worker_request(ext_worker_t *runner, const char * request, char **responce);

//theese two are for async communication or situations when there is no input or output expected
int glb_worker_request(ext_worker_t *worker, const char * request);
int glb_worker_responce(ext_worker_t *worker, char ** responce);
int async_buffered_responce(ext_worker_t *worker,  char **response);
int worker_is_alive(ext_worker_t *worker);
int glb_start_worker(ext_worker_t *worker);

void glb_destroy_worker(ext_worker_t *runner);
int glb_escape_worker_string(char *in_string, char *out_buffer);
int glb_init_external_workers(char **workers_cfg, char *scriptdir);
int glb_process_worker_params(ext_worker_t *worker, char *params_buf);

int worker_get_fd_from_worker(ext_worker_t *worker);
int worker_get_pid(ext_worker_t *worker);

ext_worker_t* glb_get_worker_script(char *cmd);

//this will not (yet) work as workers rely on FD which is different for each proc
//zbx_uint64_t zbx_dc_get_ext_worker(ext_worker_t *worker, char *path);
//int zbx_dc_add_ext_worker(char *path, char *params, int max_calls, int timeout, int mode_to_writer, int mode_from_writer);
//zbx_uint64_t zbx_dc_return_ext_worker(ext_worker_t *worker);

//how much time to wait for a runner till it execs before try to restart it
#define GLB_DEFAULT_WORKER_TIMEOUT 10

//how many reqeusts process by a runner befeore terminate it and start a new one
#define GLB_DEFAULT_WORKER_MAX_CALLS 1000000000

//how to treat input/output
#define GLB_WORKER_MODE_SILENT      1   // no communication is expected
#define GLB_WORKER_MODE_NEWLINE     2   // line by line, new line treated as the end of input/data
#define GLB_WORKER_MODE_EMPTYLINE   3   // multiline, an empty line is treated as end of input/data

#define GLB_DEFAULT_MODE_TO_WORKER GLB_WORKER_MODE_EMPTYLINE
#define GLB_DEFAULT_MODE_FROM_WORKER GLB_WORKER_MODE_EMPTYLINE
