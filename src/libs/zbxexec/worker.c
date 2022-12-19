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

#include "../../../include/common.h"
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

#define WORKER_CHANGE_TIMEOUT 30

struct ext_worker_t {
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
	unsigned char async_mode; //worker is working in async mode - we don't wait it for the answer
	int last_start;
    int last_change;
    int last_change_check;
    struct evbuffer *buffer; 
};

extern int CONFIG_TIMEOUT;

static int get_worker_mode(char *mode)
{
    if (!strcmp(mode, "new_line"))
        return GLB_WORKER_MODE_NEWLINE;
    if (!strcmp(mode, "double_new_line"))
        return GLB_WORKER_MODE_EMPTYLINE;
    if (!strcmp(mode, "silent"))
        return GLB_WORKER_MODE_SILENT;
    return FAIL;
}

#define DEFAULT_PIPE_SIZE 65536

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
        LOG_WRN("Couldn't get max worker pipe size, cannot read proc file, will use 65k bytes as a safe default. Warn: might reduce performance on heavy loaded systems");
        return DEFAULT_PIPE_SIZE;      /* Can't read? */
    }
    
    fclose( fp );
    return (size_t)max_pipe_size;
}


int glb_process_worker_params(ext_worker_t *worker, char *params_buf) {
    
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

ext_worker_t *glb_init_worker(char *config_line)
{
    char path[MAX_STRING_LEN], buff[MAX_STRING_LEN];

    int i = 0;
    zbx_json_type_t type;
    ext_worker_t *worker = NULL;

    path[0] = 0;
     buff[0] = 0;

    struct zbx_json_parse jp, jp_config;

    zabbix_log(LOG_LEVEL_DEBUG, "%s: got config: '%s'", __func__, config_line);
    if (NULL == (worker = zbx_calloc(NULL, 0, sizeof(ext_worker_t))))
    {
        return NULL;
    }

    if (SUCCEED != zbx_json_open(config_line, &jp_config))
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configuration: '%s', not a valid JSON", config_line);
        zbx_free(worker);
        return NULL;
    }

    if (SUCCEED != zbx_json_value_by_name(&jp_config, "path", path, MAX_STRING_LEN, &type))
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configuration: couldn't find 'path' parameter");
        zbx_free(worker);
        return NULL;
    }
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s: parsed worker path: '%s'", __func__, path);
        worker->path = zbx_strdup(NULL, path);
    }

    if (SUCCEED == zbx_json_value_by_name(&jp_config, "params", buff, MAX_STRING_LEN, &type))
    {
        glb_process_worker_params(worker, buff);    
    }    
    
    if (SUCCEED == zbx_json_value_by_name(&jp_config, "timeout", buff, MAX_STRING_LEN, &type))
    {
        worker->timeout = strtol(buff, NULL, 10);
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed timeout: '%d'", __func__, worker->timeout);
    }
    else
        worker->timeout = CONFIG_TIMEOUT;

    worker->max_calls = GLB_DEFAULT_WORKER_MAX_CALLS;
    
    if (SUCCEED == zbx_json_value_by_name(&jp_config, "max_calls", buff, MAX_ID_LEN, &type))
    {
        worker->max_calls = strtol(buff, NULL, 10);
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed max_calls: '%d'", __func__, worker->max_calls);
    }

    if ((SUCCEED != zbx_json_value_by_name(&jp_config, "mode_to_worker", buff, MAX_ID_LEN, &type)) ||
        (FAIL == (worker->mode_to_worker = get_worker_mode(buff))))
    {
        worker->mode_to_worker = GLB_DEFAULT_MODE_TO_WORKER;
    }

    if ((SUCCEED != zbx_json_value_by_name(&jp_config, "mode_from_worker", buff, MAX_ID_LEN, &type)) ||
        (FAIL == (worker->mode_from_worker = get_worker_mode(buff))))
    {
        worker->mode_from_worker = GLB_DEFAULT_MODE_FROM_WORKER;
    }

    worker->async_mode = 0;
    worker->buffer = evbuffer_new();

    return worker;
}

static int restart_worker(ext_worker_t *worker)
{
    #define RST_ACCOUNT_PERIOD  20
    #define RST_MAX_RESTARTS    5
    int i;
    
    LOG_DBG("In %s()", __func__);

    static unsigned int count_rst_time=0, restarts=0;
    unsigned int now=time(NULL);

    if (worker->last_start + CONFIG_TIMEOUT > now) {
        LOG_INF("Not restarting, waiting for %d seconds till restart", now - (CONFIG_TIMEOUT + worker->last_start));
        return FAIL;
    }

    if (now > count_rst_time) {
        LOG_DBG("Zeroing restart limit");
        count_rst_time=now + RST_ACCOUNT_PERIOD;
        restarts = 0;
    }
     
    if (restarts++ > RST_MAX_RESTARTS ) {
        LOG_INF("Restart of worker %s delayed in %d seconds to avoid system hammering", worker->path, count_rst_time - now);
        return FAIL;
    }

    LOG_INF("Restarting worker %s pid %d", worker->path, worker->pid);
    
    if (worker->pipe_from_worker)
        close(worker->pipe_from_worker);
    if (worker->pipe_to_worker)
        close(worker->pipe_to_worker);

    if ( worker->pid > 0  && !kill(worker->pid, 0))
    {
        int exitstatus;
        
        LOG_INF( "Killing old worker instance pid %d", worker->pid);
        
        kill(worker->pid, SIGINT);
        waitpid(worker->pid, &exitstatus, WNOHANG);

        zabbix_log(LOG_LEVEL_INFORMATION, "Waitpid returned %d", exitstatus);
        
    }
    
    worker->pid = 0;

    int from_child[2];
    int to_child[2];

    if (0 != pipe(from_child))
    {
        return -1;
    }
 
    if (0 != pipe(to_child))
    {
        close((from_child)[0]);
        close((from_child)[1]);
        return FAIL;
    }
    
    worker->pid = fork();

    if (0 > worker->pid)
    {
        //fork has failed
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

        //There is nothing to do, the child is useless by now, goodbye
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
   
    zabbix_log(LOG_LEVEL_INFORMATION, "Started worker '%s' pid is %d", worker->path, worker->pid);
    zabbix_log(LOG_LEVEL_DEBUG, "Ended %s()", __func__);
    worker->last_start = time(NULL);
    
    if (0 != worker->async_mode) {
      //  LOG_INF("Worker is async");
        int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
        fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);
        
        evbuffer_free(worker->buffer);
        worker->buffer = evbuffer_new();
    }

    return SUCCEED;
}

int glb_start_worker(ext_worker_t *worker) {
    return restart_worker(worker);
}

int worker_is_alive(ext_worker_t *worker)
{
    if (0 == worker->pid) 
        return FAIL;
    
    if (0 != waitpid(worker->pid, NULL, WNOHANG )) 
        return FAIL;
    
    return SUCCEED;
}

static int worker_is_changed(ext_worker_t *worker) {

 //   if (time(NULL) - WORKER_CHANGE_TIMEOUT > worker->last_change_check && 
//         worker->last_change != file_get_lastchange(worker->path)) 
//    {
 //       worker->last_change_check = time(NULL);
 //       return SUCCEED;
 //   }
    
    return FAIL;
}

static void worker_cleanup(ext_worker_t *worker)
{

    char *buffer[MAX_STRING_LEN];
    int read_num = 0;
    int bytes = 0;
    zabbix_log(LOG_LEVEL_DEBUG, "%s, Doing worker %s cleanup ", __func__, worker->path);

    int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
    
    if (flags == -1)
        return;

    fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);

    while (0 < (read_num = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN)))
    {
        bytes += read_num;
    }

    fcntl(worker->pipe_from_worker, F_SETFL, flags);
    zabbix_log(LOG_LEVEL_DEBUG, "%s, finished", __func__);

}

int glb_worker_request(ext_worker_t *worker, const char * request) {
 
    int worker_fail = 0;
    int request_len = 0;
    int write_fail = 0;
    int i;
    int wr_len = 0, eol_len;
    char *eol = "\n";
    
    if (worker->calls++ >= worker->max_calls)
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "%s worker %s pid %d exceeded number of requests (%d), restarting it",
                   __func__, worker->path, worker->pid, worker->max_calls);
        
        worker->calls = 0;
        if (SUCCEED != restart_worker(worker)) 
            return FAIL;
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: worker restarted, new pid is %d", __func__, worker->pid);
   
    };

    if (SUCCEED != worker_is_alive(worker))
    {
        zabbix_log(LOG_LEVEL_WARNING, "%s: worker %s is not running, starting", __func__, worker->path);
        if (SUCCEED != restart_worker(worker)) 
            return FAIL;
    }

    if (SUCCEED == worker_is_changed(worker)) 
    {
         zabbix_log(LOG_LEVEL_WARNING, "%s: worker %s file has changed, restarting", __func__, worker->path);
         if (SUCCEED != restart_worker(worker)) 
             return FAIL;
    }

    zabbix_log(LOG_LEVEL_DEBUG, "%s : worker %s is alive, pid is %d, served requests %d out of %d",
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
        zabbix_log(LOG_LEVEL_WARNING, "Got zero length request while mode is SINGLE LINE, switch to SILENT");
        return FAIL;
    }

    if (GLB_WORKER_MODE_EMPTYLINE ==  worker->mode_to_worker) {
        eol="\n\n";
        eol_len = strlen(eol);
    }

    if (NULL != strstr(request, eol)) {
        THIS_SHOULD_NEVER_HAPPEN;
         zabbix_log(LOG_LEVEL_INFORMATION, "New line marker inside the request %s, shouldn't be, this is a bug", request);
         exit(-1);
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
        if (0 >(eol_len = write(worker->pipe_to_worker, eol, strlen(eol)))) {
            zabbix_log(LOG_LEVEL_WARNING, "Couldn't write eol marker to the script's stdin: %d", errno);
            write_fail = 1;
        }

    zbx_alarm_off();

    if (wr_len != strlen(request))
    {
        zabbix_log(LOG_LEVEL_WARNING, "WARNING: wrote less bytes then buffer size: %d of %ld, consider decreasing amount of data or increase write timeout or worker has died", wr_len, strlen(request));
        restart_worker(worker);
        return FAIL;
    }

    if (SUCCEED == zbx_alarm_timed_out() || 1 == write_fail)
    {
        zabbix_log(LOG_LEVEL_WARNING, "%s: FAIL: script %s took too long to read input data, it will be restarted", __func__, worker->path);
        restart_worker(worker);
        return FAIL;
    }
    
    return SUCCEED;
};

int worker_get_fd_from_worker(ext_worker_t *worker) {
    return worker->pipe_from_worker;
}
int worker_get_pid(ext_worker_t *worker) {
    return worker->pid;
}


#define MAX_WORKER_BUFF_LEN 32 * ZBX_MEBIBYTE
/*
/****************************************************************
* to assist async workeres and to split data by responses 
* 
****************************************************************/
int async_buffered_responce(ext_worker_t *worker,  char **response) {
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
    int read_data = SUCCEED;

    if ( 0 == worker->pid ) 
        return FAIL;
    
    if ( SUCCEED != worker_is_alive(worker)) {
        LOG_INF("Worker %s is dead, need restart", worker_get_path(worker));
        restart_worker(worker);
        return FAIL;
    }
    
    *response = evbuffer_readln(worker->buffer, NULL, EVBUFFER_EOL_CRLF);

    if (evbuffer_get_length(worker->buffer) > MAX_WORKER_BUFF_LEN) 
        return SUCCEED;
    
    while (0 < evbuffer_read(worker->buffer, worker->pipe_from_worker, -1) &&
               evbuffer_get_length(worker->buffer) > MAX_WORKER_BUFF_LEN );
    
   // zabbix_log(LOG_LEVEL_INFORMATION, "End of %s()", __func__);
    return SUCCEED;
}


int glb_worker_responce(ext_worker_t *worker,  char ** responce) {
    
    zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
    
    if (GLB_WORKER_MODE_SILENT == worker->mode_from_worker)
    {
        //no need to read anything from the script, we've done
        *responce = NULL;
        return SUCCEED;
    }

    int wait_count = 0;
    char *resp_buffer = NULL;
    size_t rbuflen = 0, rbuffoffset = 0;
     
    double wait_start;

    int continue_read = 1;
    int read_len = 0, total_len = 0;
    int worker_fail = 0;

    zbx_alarm_on(worker->timeout);
    wait_start = zbx_time();

    if (0 != worker->async_mode) {
        int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
        fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);
    }
   
    while (FAIL == zbx_alarm_timed_out() && continue_read)
    {
        char buffer[MAX_STRING_LEN*10];
        buffer[0] = 0;

        //doing non-blocking read. Checking if we eneded up with new line or
        //just a line to understand that all the data has been recieved
        LOG_DBG("Calling read");
        read_len = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN * 10);
        LOG_DBG("finished read");
        
        switch (read_len) {
            case -1: 
                //this was supposed to be used in non-blocking mode, but
                //since in raw descriptor mode it works fine even in blocking mode + alaram
                //this code isn't needed so far.
                //todo: either remove or switch scripting to the sync mode
                if (EAGAIN == errno || EWOULDBLOCK == errno || ENODATA == errno)
                {
                    //we've reached end of input, it's ok
                    //but if this happens not fisrt time, lets sleep a bit to save CPU
                    //while waitng for some new data to appear
                
                    if (wait_count++ > 1 && worker->async_mode == 0) {
                        usleep(1000);
                        zabbix_log(LOG_LEVEL_DEBUG, "Waiting for new data for SYNC responce from the worker");
                    } else {
                        zabbix_log(LOG_LEVEL_DEBUG, "Not waiting for new data from the worker due to ASYNC mode");
                        continue_read = 0;
                    }
                } else {
                    //this might happen if script dies
                    continue_read = 0;
                    zabbix_log(LOG_LEVEL_INFORMATION, "Socket read failed errno is %d", errno);
                }
                break;
            case 0: //we've got nothing from the worker, which is most likely means that worker has died
                //but it's ok there is nothing from async worker
               // if (worker->async_mode) 
               //     break;
                if (SUCCEED != worker_is_alive(worker) || (zbx_time() - wait_start > worker->timeout)) {
                    zabbix_log(LOG_LEVEL_INFORMATION, "Worker %s has died during request process or not responding", worker->path);
                    continue_read = 0;
                    worker_fail = 1;
                } else
                    usleep(1000); //whatever else is is it's good to time to take a nap to save some CPU heat
                break;
            
            default: //succesifull read
                buffer[read_len] = 0;
                total_len += read_len;
                //todo: get rid of dynamic allocations here
                //we've got a line, lets put it to the buffer
                zabbix_log(LOG_LEVEL_DEBUG, "Adding %s to response buffer",buffer);
                zbx_snprintf_alloc(&resp_buffer, &rbuflen, &rbuffoffset, "%s", buffer);
        
                //if this is sync worker, then checking we've got the responce to stop
                //for async ones, we just reading whatever ready in the pipe, not bothering about endings
                if ( 0 == worker->async_mode ) {
                    switch (worker->mode_from_worker) {
                        case GLB_WORKER_MODE_NEWLINE:
                            if (rbuflen > 1 && NULL != strstr(buffer, "\n")) {       
                                continue_read = 0;
                                zabbix_log(LOG_LEVEL_DEBUG, "Found newline char in SINLE mode, finishing reading");
                            }
                            break;
                        case GLB_WORKER_MODE_EMPTYLINE:
                            if ((!strcmp(buffer, "\n")) || (NULL != strstr(buffer, "\n\n"))) {
                                zabbix_log(LOG_LEVEL_DEBUG, "Found empty line char in MULTI mode, finishing reading");
                                continue_read = 0;
                            }
                            break;
                    }
                }
        }
    } //read data loop was here

    //lets see if actuallyred something or it's a timeout has happened
    if (SUCCEED == zbx_alarm_timed_out() || 1 == worker_fail)
    {
        zabbix_log(LOG_LEVEL_WARNING,
                   "%s: FAIL: script %s failed or took too long to respond or may be there was no newline/empty line in the output, or it has simply died. Will be restarted",
                   __func__, worker->path);
        LOG_DBG("Continue read: %d, worker_fail: %d, Hisread:%s",continue_read,worker_fail,resp_buffer);
        
        if (worker->pid != 0) {
            //worker->last_fail = time(NULL);
            //worker->pid = 0;
        }
        
        //sleep(1);
        int resp_len = strlen(resp_buffer);
        LOG_INF("Restarting the worker, current pid is %d", worker->pid);

        restart_worker(worker);

        LOG_INF("Worker has been restarted");
        
        zbx_free(resp_buffer);
        return FAIL;
    }

    zbx_alarm_off();
    //setting the responce buffer
    *responce = resp_buffer;
     zabbix_log(LOG_LEVEL_DEBUG,"In %s: setting responce: %s, read len is %d", __func__,*responce, total_len);
    //zabbix_log(LOG_LEVEL_INFORMATION,"In %s: finished", __func__);
    if ( 1 > total_len ) return POLL_NODATA;
    return SUCCEED;
};


int glb_process_worker_request(ext_worker_t *worker, const char *request, char **responce)
{

    zabbix_log(LOG_LEVEL_DEBUG, "Started %s", __func__);
    
    worker_cleanup(worker);
    
    if (SUCCEED != glb_worker_request(worker,request) ||
        SUCCEED != glb_worker_responce(worker, responce) )
        
        return FAIL;
    
    zabbix_log(LOG_LEVEL_DEBUG, "Finished %s", __func__);
    return SUCCEED;
}


int glb_escape_worker_string(char *in_string, char *out_buffer)
{
    
    int in = 0, out = 0;
    while (in_string[in] != '\0')
    {
        //we don't want some special chars to appear in the json

        if (in_string[in] == '\"')
        {
            out_buffer[out++] = '\\';
            out_buffer[out++] = in_string[in++];
        }
        else if (in_string[in] == 10)
        {
            out_buffer[out++] = '\\';
            out_buffer[out++] = 'n';
            in++;
        }
        else if (in_string[in] == '"')
        {
            out_buffer[out++] = '\\';
            out_buffer[out++] = '"';
            in++;
        }
        else
            out_buffer[out++] = in_string[in++];
    }
    out_buffer[out] = '\0';

    return out;
}

void glb_destroy_worker(ext_worker_t *worker)
{   
    int exitstatus;
    
    if (SUCCEED == worker_is_alive(worker)) {
        kill(worker->pid, SIGINT);
        sleep(1);
        waitpid(worker->pid, &exitstatus,WNOHANG);
    }
    
   // if (worker->async_mode)
    evbuffer_free(worker->buffer);

    zbx_free(worker->path);
    zbx_free(worker);
       
    //LOG_INF("Worker destroy completed");
}

const char *worker_get_path(ext_worker_t *worker) {
    return worker->args[0];
}

void worker_set_mode_from_worker(ext_worker_t *worker, unsigned char mode) {
    worker->mode_from_worker = mode;
}

void worker_set_mode_to_worker(ext_worker_t *worker, unsigned char mode) {
     worker->mode_to_worker = mode;
}

void worker_set_async_mode(ext_worker_t *worker, unsigned char mode) {
    worker->async_mode = mode;
}

ext_worker_t *worker_init(const char* path, unsigned int max_calls, 
            unsigned char async_mode, unsigned char mode_to_worker, unsigned char mode_from_worker, int timeout ) {
    
    ext_worker_t *worker = zbx_calloc(NULL, 0, sizeof(struct ext_worker_t));

    worker->path = zbx_strdup(NULL, path);
    worker->async_mode = async_mode;
    worker->max_calls = max_calls;
    worker->mode_from_worker = mode_from_worker;
    worker->mode_to_worker = mode_to_worker;
    worker->timeout = timeout;
    worker->last_change = file_get_lastchange(path);
    worker->last_change_check = time(NULL);
    
    //if (worker->async_mode) {
    worker->buffer = evbuffer_new();
    //assert(NULL != worker->buffer);
    //}  
    
    
    return worker;
}