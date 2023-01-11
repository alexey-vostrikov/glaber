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

#define _GNU_SOURCE

#include "../../../include/zbxcommon.h"
#include "worker.h"
#include "log.h"
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include "dbcache.h"
#include "zbxipcservice.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#if (__FreeBSD_version) > 700000
#define ENODATA 245
#endif

//how many reqeusts process by a runner befeore terminate it and start a new one
#define GLB_DEFAULT_WORKER_MAX_CALLS 1000000000
#define WORKER_CHANGE_TIMEOUT 30

struct glb_worker_t {
	char *path;       //path to executable to run
	char *args[128];
    //char *params;     //params
    pid_t pid;              //pid of the script
    int calls;              //number of requests processed
    int timeout;            //how much time to wait for result until consider the worker is dead or stuck
    int max_calls;          //how many calls per run before worker restart
	int mode_to_worker;		//how to terminate output to worker
	int mode_from_worker;	//which termination to expect from the worker when parsing returned data 
	int pipe_from_worker;	//communication pipes
	int pipe_to_worker;
	//unsigned char async_mode; //worker is working in async mode - we don't wait it for the answer
	int last_start;
    int last_change;
    int last_change_check;
    struct evbuffer *buffer; 
};

extern int CONFIG_TIMEOUT;

#define DEFAULT_PIPE_SIZE 65536

int file_is_executable(const char *path) {
    
    struct stat sb;
    if (stat(path, &sb) == 0 && 
        (sb.st_mode & S_IXUSR) ) 
        return SUCCEED;
    return FAIL;
}

int file_get_lastchange(const char *full_path)
{
    struct stat filestat;
    time_t a;

    if (-1  == stat( full_path ,&filestat))
        return -1;
    
    if (filestat.st_mtime > filestat.st_ctime)
        return filestat.st_ctime;
    
}

static size_t get_max_pipe_buff_size() 
{
    long max_pipe_size = 0L;

    FILE* fp = NULL;
    if ( (fp = fopen("/proc/sys/fs/pipe-max-size", "r" )) == NULL ) {
        LOG_WRN("Couldn't get max worker pipe size, cannot open proc file, will use 65k bytes as a safe default. Warn: might reduce performance on heavy loaded systems");
        return DEFAULT_PIPE_SIZE;      /* Can't open, default will work on on most systems */
    }
    
    if ( fscanf( fp, "%ld", &max_pipe_size ) != 1 )
    {
        fclose( fp );
        LOG_WRN("Couldn't get max worker pipe size, cannot read proc file, will use 65k bytes as a safe default. Warn: might perform poor on heavy loaded systems");
        return DEFAULT_PIPE_SIZE;      /* Can't read? */
    }
    
    fclose( fp );
    return (size_t)max_pipe_size;
}

int worker_process_args(glb_worker_t *worker, const char *params_buf) {
    
    LOG_DBG("%s: parsing params: '%s'", __func__, params_buf);
    
    if ( NULL!= params_buf && strlen(params_buf) >0 )   {

        char prevchar = 0, *params=NULL;
        int i=0, args_num=2;
        
        worker->args[0]=worker->path;
        worker->args[1]=zbx_strdup(NULL, params_buf);

        params = worker->args[1];

        while (  0 != params[i] && args_num < GLB_WORKER_ARGS_MAX) {
            if ( ' ' == params[i] && '\\' != prevchar ) {
                params[i]=0;
                worker->args[args_num++]=params+i+1;
            }
        
            prevchar = params[i];
            i++;
        }
        worker->args[args_num]=NULL;
    } else {
        worker->args[0] = worker->path;
        worker->args[1] = NULL;
    }
}

glb_worker_t *glb_worker_init(const char *path, const char* args, int timeout, int max_calls, 
        glb_worker_termination_type_t mode_to_worker, glb_worker_termination_type_t mode_from_worker)
{
    glb_worker_t *worker = zbx_calloc(NULL, 0, sizeof(glb_worker_t));
    
    if (NULL == worker || NULL == path)
        return NULL;
    
    if (FAIL == file_is_executable(path)) 
        return NULL;

    worker->path = zbx_strdup(NULL, path);
    worker->max_calls = max_calls;
    worker->timeout = timeout;
    
    worker->mode_from_worker = mode_from_worker;
    worker->mode_to_worker = mode_to_worker;

    if (NULL != args)
        worker_process_args(worker, args);   

    if ( 0 == timeout )   
        worker->timeout = CONFIG_TIMEOUT;

    if (0 == worker->max_calls)
        worker->max_calls = GLB_DEFAULT_WORKER_MAX_CALLS;

    if (GLB_WORKER_MODE_DEFAULT == worker->mode_to_worker) 
        worker->mode_to_worker = GLB_WORKER_MODE_NEWLINE;

    if (GLB_WORKER_MODE_DEFAULT == worker->mode_from_worker) 
        worker->mode_from_worker = GLB_WORKER_MODE_NEWLINE;

    worker->buffer = evbuffer_new();
  
    return worker;
}

int glb_worker_restart(glb_worker_t *worker)
{
    int i;
    
    LOG_DBG("In %s()", __func__);

    static unsigned int count_rst_time=0, restarts=0;
    unsigned int now=time(NULL);
    
    if (worker->last_start + CONFIG_TIMEOUT > now) 
        return FAIL;
   

    LOG_INF("Restarting worker %s pid %d", worker->path, worker->pid);
    
    if (worker->pipe_from_worker)
        close(worker->pipe_from_worker);
    if (worker->pipe_to_worker)
        close(worker->pipe_to_worker);

    if ( worker->pid > 0 ) 
    {
        int exitstatus;
        
        LOG_INF( "Killing old worker instance pid %d", worker->pid);
        
        if (0 == kill(worker->pid, 0))
            kill(worker->pid, SIGKILL);

        waitpid(worker->pid, &exitstatus, WNOHANG);
        
        LOG_INF("Waitpid returned %d", exitstatus);
       // worker->pid = 0;
    }

    int from_child[2];
    int to_child[2];

    if (0 != pipe(from_child))
        return -1;
 
    if (0 != pipe(to_child))
    {
        close((from_child)[0]);
        close((from_child)[1]);
        return FAIL;
    }
    
    worker->pid = fork();

    if (0 > worker->pid)
    {   //fork has failed
        close((from_child)[0]);
        close((from_child)[1]);
        close((to_child)[0]);
        close((to_child)[1]);
        worker->pid = 0;
        return FAIL;
    }

    //this is a children
    if (0 == worker->pid)
    {
        sigset_t	mask, orig_mask;
	
        sigemptyset(&mask);
	    sigaddset(&mask, SIGINT);
	    sigaddset(&mask, SIGQUIT);
    
	    if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		    zbx_error("cannot set sigprocmask to block the user signal");
     
        if (0 > dup2(from_child[1], 1) || 0 > dup2(to_child[0], 0))
        {
            _Exit(127);
        }

        close((from_child)[0]);
        close((from_child)[1]);
        close((to_child)[0]);
        close((to_child)[1]);

      
        LOG_DBG("Starting worker %s", worker->path);

        for (i=0; NULL != worker->args[i]; i++) {
            zabbix_log(LOG_LEVEL_DEBUG,"Starting with arg[%d]=%s",i,worker->args[i]);
        }
        
        if ( -1 == execv(worker->path, worker->args ))  {
            zabbix_log(LOG_LEVEL_WARNING,"Couldn't start %s command: errno is %d",worker->path,errno);
        }

        _Exit(127);
    }

    //only the parent gets here, let's close unnecessary fds
    close(to_child[0]);
    close(from_child[1]);
    
    worker->pipe_to_worker = to_child[1];
    worker->pipe_from_worker = from_child[0];
    
    int max_pipe_buff_size = get_max_pipe_buff_size();

    fcntl(worker->pipe_from_worker, F_SETPIPE_SZ, max_pipe_buff_size);
    fcntl(worker->pipe_to_worker, F_SETPIPE_SZ, max_pipe_buff_size);

    worker->calls = 0;
   
    zabbix_log(LOG_LEVEL_INFORMATION, "Started worker '%s' new pid is %d", worker->path, worker->pid);
    zabbix_log(LOG_LEVEL_DEBUG, "Ended %s()", __func__);
    
    worker->last_start = time(NULL);

    int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
    fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);
        
    evbuffer_free(worker->buffer);
    worker->buffer = evbuffer_new();

    return SUCCEED;
}

int glb_worker_is_alive(glb_worker_t *worker)
{
    int status;
    if (0 == worker->pid) {
        LOG_INF("Worker pid is zero, not alive");
        return FAIL;
    }
    
    if (worker->last_start +1 >= time(NULL)) 
        return SUCCEED;

    LOG_INF("%s: Worker pid is %d", __func__, worker->pid);

    waitpid(worker->pid, &status, WNOHANG);
    
    if (WIFEXITED(status)) { //|| WIFSIGNALED(status)) {
        LOG_INF("Alive check: the process pid %d has stopped", worker->pid);
        worker->pid = 0; 
        return FAIL;
    }
    
//    if (0 != kill(worker->pid, 0) )     
//      return FAIL;

    return SUCCEED;
}

//static int worker_is_changed(glb_worker_t *worker) {
//    return FAIL;
//}

static void worker_cleanup(glb_worker_t *worker)
{
    char *buffer[MAX_STRING_LEN];
    int read_num = 0, bytes = 0;

    while (0 < (read_num = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN)))
        bytes += read_num;
    
}

int glb_worker_send_request(glb_worker_t *worker, const char * request) {
 
    int worker_fail = 0;
    int request_len = 0;
    int write_fail = 0;
    int i;
    int wr_len = 0;
   // char *eol = "\n";
    
    if (worker->calls++ >= worker->max_calls)
    {
        LOG_INF("%s worker %s pid %d exceeded number of requests (%d), restarting it",
                   __func__, worker->path, worker->pid, worker->max_calls);
        
        if (SUCCEED != glb_worker_restart(worker)) 
            return FAIL;
        
        LOG_INF("%s: worker restarted, new pid is %d", __func__, worker->pid);
    };

    if (SUCCEED != glb_worker_is_alive(worker))
    {
      
        if (SUCCEED != glb_worker_restart(worker)) 
            return FAIL;
        
        zabbix_log(LOG_LEVEL_WARNING, "%s: worker %s has been (re)started", __func__, worker->path);
    }

    LOG_DBG("%s : worker %s is alive, pid is %d, served requests %d out of %d",
                   __func__, worker->path, worker->pid, worker->calls, worker->max_calls);
    
    if (request)
        request_len = strlen(request);
    else
    {
        LOG_WRN("%s: Request must not be NULL", __func__);
        THIS_SHOULD_NEVER_HAPPEN;
        return FAIL;
    };

    if (!request_len)
    {
        zabbix_log(LOG_LEVEL_WARNING, "Got zero length request while mode in SINGLE LINE, switch to SILENT");
        return FAIL;
    }

    if (NULL != strstr(request, "\n")) {
        LOG_WRN("New line marker shouldn't be inside the request '%s' this is a bug", request);
        THIS_SHOULD_NEVER_HAPPEN;
        return FAIL;
    }

    zbx_alarm_on(worker->timeout);
    
    wr_len = write(worker->pipe_to_worker, request, strlen(request));

    if (0 > wr_len)
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't write to the script's stdin: %d", errno);
        write_fail = 1;
    }
    
    if (!write_fail) 
        if (0 > write(worker->pipe_to_worker, "\n", strlen("\n"))) {
            zabbix_log(LOG_LEVEL_WARNING, "Couldn't write eol marker to the script's stdin: %d", errno);
            write_fail = 1;
        }

    zbx_alarm_off();

    if (wr_len != strlen(request))
    {
        zabbix_log(LOG_LEVEL_WARNING, "WARNING: wrote less bytes then buffer size: %d of %ld, consider decreasing amount of data or increase write timeout or worker has died", wr_len, strlen(request));
        glb_worker_restart(worker);
        return FAIL;
    }

    if (SUCCEED == zbx_alarm_timed_out() || 1 == write_fail)
    {
        zabbix_log(LOG_LEVEL_WARNING, "%s: FAIL: script %s took too long to read input data, it will be restarted", __func__, worker->path);
        glb_worker_restart(worker);
        return FAIL;
    }
    
    return SUCCEED;
};

int worker_get_fd_from_worker(glb_worker_t *worker) {
    return worker->pipe_from_worker;
}


int glb_worker_get_async_buffered_responce(glb_worker_t *worker,  char **response) {

    //if there are data ready - just return it, don't care if worker is running (it maybe dead already, but sent us data)
    if ( NULL != ( *response = evbuffer_readln(worker->buffer, NULL, EVBUFFER_EOL_CRLF)))
        return SUCCEED;

    //need to read from the worker, need it alive
    if ( SUCCEED != glb_worker_is_alive(worker)) {
        glb_worker_restart(worker); 
        //worker will need some time to start, so next time we read from it
        return SUCCEED; 
    }
    
    //reading whaterever is there in the pipe ready
    while (0 < evbuffer_read(worker->buffer, worker->pipe_from_worker, -1)); 
    
    *response = evbuffer_readln(worker->buffer, NULL, EVBUFFER_EOL_CRLF);
    return SUCCEED;
}


int glb_worker_get_sync_response(glb_worker_t *worker,  char ** response) {
    *response = NULL;

    //sync response will either wait till data comes or timeout ends
    int start = time(NULL);

    while (start + worker->timeout > time(NULL)) {
     
        if (FAIL == glb_worker_get_async_buffered_responce(worker, response)) {
            LOG_INF("Worker failed, restated, waiting for start");
            usleep(500000);
            continue;
        }

        if (NULL != *response) 
            return SUCCEED;

        usleep(10000);
    }

    return FAIL;
}

int glb_process_worker_request(glb_worker_t *worker, const char *request, char **response)
{
    worker_cleanup(worker);
    
    if (SUCCEED != glb_worker_send_request(worker, request) ||
        SUCCEED != glb_worker_get_sync_response(worker, response) )
        
        return FAIL;
    
    zabbix_log(LOG_LEVEL_DEBUG, "Finished %s", __func__);
    return SUCCEED;
}

int glb_worker_escape_string(char *in_string, char *out_buffer)
{
    int in = 0, out = 0;
    while (in_string[in] != '\0')
    {
        switch (in_string[in]) {
        case '\"':
            out_buffer[out++] = '\\';
            out_buffer[out++] = in_string[in];
            break;
        case 10:
            out_buffer[out++] = '\\';
            out_buffer[out++] = 'n';
            break;
        default:
            out_buffer[out++] = in_string[in];
        }
        in++;
    }
    out_buffer[out] = '\0';
    return out;
}

void glb_worker_destroy(glb_worker_t *worker)
{   
    int exitstatus;
    
    if (SUCCEED == glb_worker_is_alive(worker)) {
        kill(worker->pid, SIGINT);
        sleep(1);
        waitpid(worker->pid, &exitstatus, WNOHANG);
    }

    evbuffer_free(worker->buffer);
    zbx_free(worker->path);
    zbx_free(worker);
}

const char *worker_get_path(glb_worker_t *worker) {
    return worker->args[0];
}

void worker_set_mode_from_worker(glb_worker_t *worker, unsigned char mode) {
    worker->mode_from_worker = mode;
}

void worker_set_mode_to_worker(glb_worker_t *worker, unsigned char mode) {
     worker->mode_to_worker = mode;
}

const char *glb_worker_get_path(glb_worker_t *worker) {
    return worker->path;
}

int glb_worker_get_pid(glb_worker_t *worker) {
    return worker->pid;
}