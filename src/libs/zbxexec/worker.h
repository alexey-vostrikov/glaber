//#ifndef _GLABER_RUNNER
//#define _GLABER_RUNNER

#include <stdio.h>
#include <sys/types.h>
#include "dbcache.h"

typedef struct glb_worker_t glb_worker_t; 

typedef enum {
    GLB_WORKER_MODE_DEFAULT =   0, 
    GLB_WORKER_MODE_SILENT  =   1,  // no communication is expected
    GLB_WORKER_MODE_NEWLINE,        // line by line, new line treated as the end of input/data
} glb_worker_termination_type_t;


const char *worker_get_path(glb_worker_t *worker);

void worker_set_mode_from_worker(glb_worker_t *worker, unsigned char mode);
void worker_set_mode_to_worker(glb_worker_t *worker, unsigned char mode);

glb_worker_t *worker_init( const char* path, unsigned int max_calls, 
            unsigned char async_mode, unsigned char mode_to_worker, unsigned char mode_from_worker, int timeout );


int glb_process_worker_request(glb_worker_t *worker, const char * request, char **responce);

//theese two are for async communication or situations when there is no input or output expected
int glb_worker_send_request(glb_worker_t *worker, const char * request);
int glb_worker_get_sync_response(glb_worker_t *worker, char ** responce);
int glb_worker_get_async_buffered_responce(glb_worker_t *worker,  char **response);
int glb_worker_is_alive(glb_worker_t *worker);

//glb_worker_t* glb_worker_init(char *params);
glb_worker_t *glb_worker_init(const char *path, const char* params, int timeout, int max_calls, 
                    glb_worker_termination_type_t mode_to_worker, glb_worker_termination_type_t mode_from_worker);

void    glb_worker_destroy(glb_worker_t *worker);
int     glb_worker_restart(glb_worker_t *worker, char *reason);
int     glb_worker_escape_string(char *in_string, char *out_buffer);
int     glb_worker_process_params(glb_worker_t *worker, char *params_buf);

int worker_get_fd_from_worker(glb_worker_t *worker);
int     glb_worker_get_pid(glb_worker_t *worker);

glb_worker_t* glb_get_worker_script(char *cmd);
const char  *glb_worker_get_path(glb_worker_t *worker);
