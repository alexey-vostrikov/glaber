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


int     glb_workers_init(char **workers_cfg, char *scriptdir);
void    glb_worker_destroy(ext_worker_t *runner);
int     glb_worker_restart(ext_worker_t *worker);
int     glb_worker_escape_string(char *in_string, char *out_buffer);
int     glb_worker_process_params(ext_worker_t *worker, char *params_buf);

int worker_get_fd_from_worker(ext_worker_t *worker);
int worker_get_pid(ext_worker_t *worker);

ext_worker_t* glb_get_worker_script(char *cmd);
const char *glb_worker_get_path(ext_worker_t *worker);

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
