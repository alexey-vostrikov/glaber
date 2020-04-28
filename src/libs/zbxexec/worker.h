//#ifndef _GLABER_RUNNER
//#define _GLABER_RUNNER

#include <stdio.h>
#include <sys/types.h>
#include "dbcache.h"

//typedef struct DC_EXT_WORKER DC_EXT_WORKER;

DC_EXT_WORKER*  glb_init_worker(char *params);
int glb_process_worker_request(DC_EXT_WORKER *runner, const char * request, char **responce) ;
void glb_destroy_worker(DC_EXT_WORKER *runner);
int glb_escape_worker_string(char *in_string, char *out_buffer);
int glb_init_external_workers(char **workers_cfg, char *scriptdir);

DC_EXT_WORKER* glb_get_worker_script(char *cmd);

//this will not (yet) work as workers rely on FD which is different for each proc
//zbx_uint64_t zbx_dc_get_ext_worker(DC_EXT_WORKER *worker, char *path);
//int zbx_dc_add_ext_worker(char *path, char *params, int max_calls, int timeout, int mode_to_writer, int mode_from_writer);
//zbx_uint64_t zbx_dc_return_ext_worker(DC_EXT_WORKER *worker);

//how much time to wait for a runner till it execs before try to restart it
#define GLB_DEFAULT_WORKER_TIMEOUT 10

//how many reqeusts process by a runner befeore terminate it and start a new one
#define GLB_DEFAULT_WORKER_MAX_CALLS 1000

//how to treat input/output
#define GLB_WORKER_MODE_SILENT      1   // no communication is expected
#define GLB_WORKER_MODE_NEWLINE     2   // line by line, new line treated as the end of input/data
#define GLB_WORKER_MODE_EMPTYLINE   3   // multiline, an empty line is treated as end of input/data

#define GLB_DEFAULT_MODE_TO_WORKER GLB_WORKER_MODE_EMPTYLINE
#define GLB_DEFAULT_MODE_FROM_WORKER GLB_WORKER_MODE_EMPTYLINE