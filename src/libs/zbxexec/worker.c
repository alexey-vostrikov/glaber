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

#include "../../../include/common.h"
#include "worker.h"
#include "log.h"
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include "dbcache.h"
#include "zbxipcservice.h"

#if (__FreeBSD_version) > 700000
#define ENODATA 245
#endif

extern int CONFIG_TIMEOUT;

//todo: look if there is proc in the dbconfig.c
//workers are yet process-specific since they use FD , as soom
//as they'll be more universal (perhaps, will use unix domains or tcp addr for
//communitcation, then they could be global
//int zbx_dc_add_ext_worker(char *path, char *params, int max_calls, int timeout, int mode_to_writer, int mode_from_writer);
//static zbx_hashset_t *workers = NULL;

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

GLB_EXT_WORKER *glb_init_worker(char *config_line)
{
    char path[MAX_STRING_LEN],
         buff[MAX_STRING_LEN];

    int i = 0;
    zbx_json_type_t type;

    int timeout = GLB_DEFAULT_WORKER_TIMEOUT;
    int max_calls = GLB_DEFAULT_WORKER_MAX_CALLS;
    int mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    int mode_from_worker = GLB_WORKER_MODE_EMPTYLINE;

    GLB_EXT_WORKER *worker = NULL;

    path[0] = 0;
 
    buff[0] = 0;

    struct zbx_json_parse jp, jp_config;

    zabbix_log(LOG_LEVEL_DEBUG, "%s: got config: '%s'", __func__, config_line);
    if (NULL == (worker = zbx_malloc(NULL, sizeof(GLB_EXT_WORKER))))
    {
        return NULL;
    }

    bzero(worker, sizeof(GLB_EXT_WORKER));

    if (SUCCEED != zbx_json_open(config_line, &jp_config))
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configuration: '%s', most likely not a valid JSON", config_line);
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
        zabbix_log(LOG_LEVEL_DEBUG, "%s: parsed params: '%s'", __func__, buff);
        
        char prevchar = 0, *params=NULL;
        int i=0, args_num=2;
        
        worker->args[0]=worker->path;
        worker->args[1]=zbx_strdup(NULL, buff);

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
        
    }
    else {
        worker->args[0] = worker->path;
        worker->args[1] = NULL;
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

    return worker;
}

static int restart_worker(GLB_EXT_WORKER *worker)
{
    #define RST_ACCOUNT_PERIOD  20
    #define RST_MAX_RESTARTS    5
    int i;
    
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s()", __func__);

    static unsigned int count_rst_time=0, restarts=0;
    unsigned int now=time(NULL);

    if (now > count_rst_time) {
        zabbix_log(LOG_LEVEL_DEBUG,"Zeroing restart limit");
        count_rst_time=now + RST_ACCOUNT_PERIOD;
        restarts = 0;
    }
     
    if (restarts++ > RST_MAX_RESTARTS ) {
        zabbix_log(LOG_LEVEL_INFORMATION,"Restart of worker %s delayed in %d seconds to avoid system hammering", worker->path, count_rst_time - now);
        return FAIL;
    }

    zabbix_log(LOG_LEVEL_DEBUG, "Restarting worker %s pid %d", worker->path, worker->pid);
    
    if (worker->pipe_from_worker)
        close(worker->pipe_from_worker);
    if (worker->pipe_to_worker)
        close(worker->pipe_to_worker);

    if ( worker->pid > 0  && !kill(worker->pid, 0))
    {
        int exitstatus;
        zabbix_log(LOG_LEVEL_INFORMATION, "Killing old worker instance pid %d", worker->pid);
        kill(worker->pid, SIGINT);
        waitpid(worker->pid, &exitstatus,WNOHANG);
        zabbix_log(LOG_LEVEL_INFORMATION, "Waitpid returned %d", exitstatus);
        
    }
    
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

        /* set the child as the process group leader, otherwise orphans may be left after timeout */
        //this is from original zabbix code, was trying to find defunct processes with it
        //but it's somenthing else, for a while, i don't see really much use for it here

        //if (-1 == setpgid(0, 0))
        // {
        //   zabbix_log(LOG_LEVEL_ERR, "%s(): failed to create a process group: %s", __func__, zbx_strerror(errno));
        //   exit(EXIT_FAILURE);
        // }

        //it might be a good idea to conform a security considerations here
        //like close unnecessary file handles before we'll get replaced with the worker
        //but..... hey, would you run an untrusted worker on your monitoring system?
        //i don't think so... Even if someone will, than the real problem is the
        // person's stupidity and not lack of security in the first place...
        //And it's not something i can fix here in the code... (sure i mean the stupidity)

        //ok, let the worker go
        //this params processing code must be moved to the worker init 
        
        
        //execl("/bin/sh", "sh", "-c", worker->path, worker->params, (char *)NULL);
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

    //setting to ignore sigchild which in trun let kernel know that
    //it's ok to shutdown childs completely
    //signal(SIGCHLD, SIG_IGN);

    //and make file handles to the ends we need
    worker->pipe_to_worker = to_child[1];
    worker->pipe_from_worker = from_child[0];
    //zabbix_log(LOG_LEVEL_INFORMATION,"Set worker pid to %d", worker->pid);
    //resetting run count
    worker->calls = 0;
    if (worker->async_mode) {
        //to prevent bombing with other async requests in the same process in async mode
        //having a nap
        sleep(2);
    }
    zabbix_log(LOG_LEVEL_INFORMATION, "Started worker '%s' pid is %d", worker->path, worker->pid);
    zabbix_log(LOG_LEVEL_DEBUG, "Ended %s()", __func__);
    return SUCCEED;
}

int glb_start_worker(GLB_EXT_WORKER *worker) {
    return restart_worker(worker);
}

int worker_is_alive(GLB_EXT_WORKER *worker)
{
    if (!worker->pid)
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "Worker hasn't been running before");
        return FAIL;
    }
    if (kill(worker->pid, 0))
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "sending kill to worker's pid %d has returned non 0",worker->pid);
        return FAIL;
    }
    return SUCCEED;
}

static void worker_cleanup(GLB_EXT_WORKER *worker)
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

    if (bytes > 0)
        zabbix_log(LOG_LEVEL_INFORMATION, "GARBAGE DETECTED  %d bytes of garbage from %s: %s", bytes, worker->path, *buffer);

    fcntl(worker->pipe_from_worker, F_SETFL, flags);
    zabbix_log(LOG_LEVEL_DEBUG, "%s, finished", __func__);

}

int glb_worker_request(GLB_EXT_WORKER *worker, const char * request) {
 
    int worker_fail = 0;
    int request_len = 0;
    int write_fail = 0;
    int i;

    if (worker->calls++ >= worker->max_calls)
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "%s worker %s pid %d exceeded number of requests (%d), restarting it",
                   __func__, worker->path, worker->pid, worker->max_calls);
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
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s : worker %s is alive, pid is %d, served requests %d out of %d",
                   __func__, worker->path, worker->pid, worker->calls, worker->max_calls);
    }

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

    // zabbix_log(LOG_LEVEL_INFORMATION,"Communicating to script, to_script:%d, from script:%d",worker->mode_to_worker,worker->mode_from_worker);
    //checking of there are no impoper end of line chars
    switch (worker->mode_to_worker)
    {
    case GLB_WORKER_MODE_NEWLINE:
        zabbix_log(LOG_LEVEL_DEBUG, "Single line request processing");
        //checking that the only place we see a new line is the end of request, that's probably internal code fail
        //as this request might only come from glaber code
        if (strstr(request, "\n") != request + request_len - 1)
        {
            THIS_SHOULD_NEVER_HAPPEN;
            zabbix_log(LOG_LEVEL_INFORMATION, "No new line characer or it is inside the request %s", request);
            return FAIL;
        }
        break;

    case GLB_WORKER_MODE_EMPTYLINE:
        zabbix_log(LOG_LEVEL_INFORMATION, "Empty line request processing");
        //make sure that all the request is just an empty line or end of line is in the end of request
        if (!strcmp(request, "\n") || strstr(request, "\n\n") != request + request_len - 2)
        {
            zabbix_log(LOG_LEVEL_WARNING, "Request FAIL: no empty line or it's inside the request: '%s'", request);
         
            for (i = request_len - 10; i < request_len; i++)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "%d %d %d", request_len, i, request[i]);
            }

            return FAIL;
        }
        break;
    }
    
    zbx_alarm_on(worker->timeout);
    
    //so, lets write to the worker's pipe
    int wr_len = write(worker->pipe_to_worker, request, strlen(request));

    if (0 > wr_len)
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't write to the script's stdin: %d", errno);
        write_fail = 1;
    };

    if (!write_fail)
        zbx_alarm_off();

    if (wr_len != strlen(request))
    {
        zabbix_log(LOG_LEVEL_WARNING, "WARNING: wrote less bytes then buffer size: %d of %ld, consider decreasing amount of data or increase write timeout or worker has died", wr_len, strlen(request));
        restart_worker(worker);
        return FAIL;
    }

    //now, let's see if we completed the write in time
    if (SUCCEED == zbx_alarm_timed_out() || 1 == write_fail)
    {
        zabbix_log(LOG_LEVEL_WARNING, "%s: FAIL: script %s took too long to read input data, it will be restarted", __func__, worker->path);
        restart_worker(worker);
        return FAIL;
    }
    
    return SUCCEED;
};
/*

/****************************************************************
* to assist async workeres and to split data by responses 
* 
****************************************************************/
int async_buffered_responce(GLB_EXT_WORKER *worker,  char **response) {
    //char *read_buffer=NULL; //we read from worker here 
    static char *circle_buffer=NULL; //this is constant circle buffer we use to retrun responses
                                     //since we know the maximim reponce returned from worker has 
    static size_t buffoffset = 0, bufsize = 0;                                     
    static size_t start,  //points to the first character of the string
                  end; //points to \0 at the end of last data chunk
    char *delim, *request_end=NULL;
    char *wresp;
    
    static char datapresent = 0;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    switch  (worker->mode_from_worker) {
        case GLB_WORKER_MODE_NEWLINE: 
            delim = "\n";
            break;
        case GLB_WORKER_MODE_EMPTYLINE:
            delim = "\n\n";
            break;
    }
    
    while (1) { //we might need to repeat data 
    
        //if have any complete data redy in the buffer, send it right away
        if ( datapresent ) {
            //zabbix_log(LOG_LEVEL_INFORMATION, "In %s() there are data buffered ", __func__);
            if ( NULL != (request_end = strstr(circle_buffer + start, delim) )) {
                //ok, there is complete request data in the buffer, returning it
                //zabbix_log(LOG_LEVEL_INFORMATION, "In %s() found some data ready to send ", __func__);
                *response = circle_buffer + start;
                request_end[0]=0; //setting 0 at the end of responce 
                zabbix_log(LOG_LEVEL_DEBUG, "In %s() will respond with: %s", __func__, *response);
         
                start = request_end - circle_buffer + strlen(delim); //setting start to the begining of the new responce
                //zabbix_log(LOG_LEVEL_INFORMATION, "In %s(): buffer remainings '%s' strlen is %d", __func__, circle_buffer + start, strlen(circle_buffer + start));
                if (0 == circle_buffer[start] ) { //reaching 0 means evth has been sent from the buffer
                        datapresent = 0;
                        start = 0;
                }
                //zabbix_log(LOG_LEVEL_INFORMATION, "In %s() datapresent is set to %d ", __func__,datapresent);
                return SUCCEED;
            } else { //there is an incomplete data sitting in the buffer which starts as start marker
                zabbix_log(LOG_LEVEL_INFORMATION, "In %s() found incomplete data: %s at %d", __func__, circle_buffer + start, start);
                char *tmp_buff = zbx_malloc(NULL, MAX_STRING_LEN);
                size_t offset = 0, allocated = MAX_STRING_LEN;
                zbx_strcpy_alloc(&tmp_buff,&allocated, &offset, circle_buffer+start);
                //and copying it all back to the begining of the circle_buffer
                buffoffset = 0;
                
                zabbix_log(LOG_LEVEL_DEBUG, "In %s() copying to the start of the buffer ", __func__);
                zbx_strcpy_alloc(&circle_buffer,&bufsize,&buffoffset,tmp_buff);
                zbx_free(tmp_buff);
            }
        } 
        zabbix_log(LOG_LEVEL_DEBUG, "In %s() finished buffer operations, will read from worker ", __func__);
        //ok, there is either no data in the buffer or it's incomplete, it's aleady in the start of the buffer
        //reading next piece of data from the worker
        
        if (SUCCEED == glb_worker_responce(worker,&wresp)) {
           // zabbix_log(LOG_LEVEL_INFORMATION, "In %s() Adding worker response '%s' at %d ", __func__,wresp, start);
            if (datapresent) 
                buffoffset = strlen(circle_buffer);
            else 
                buffoffset = 0;
            zbx_strcpy_alloc(&circle_buffer, &bufsize, &buffoffset, wresp);
            datapresent = 1;
            //zabbix_log(LOG_LEVEL_INFORMATION, "In %s() new buffer is '%s'", __func__,circle_buffer);
            zbx_free(wresp); 
        } else { //nothing retruned yet, exiting, no reason to burn CPU here
            *response = NULL;
            return FAIL;
        }
    } 
    zabbix_log(LOG_LEVEL_INFORMATION, "End of %s()", __func__);
}


int glb_worker_responce(GLB_EXT_WORKER *worker,  char ** responce) {
    
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
        //zabbix_log(LOG_LEVEL_INFORMATION,"Set operation to non blocking mode");
        int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
        fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);
    }
    
   
    while (FAIL == zbx_alarm_timed_out() && continue_read)
    {
        char buffer[MAX_STRING_LEN*10];
        buffer[0] = 0;

        //doing non-blocking read. Checking if we eneded up with new line or
        //just a line to understand that all the data has been recieved
        //zabbix_log(LOG_LEVEL_INFORMATION,"Calling read");
        read_len = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN*10);
        //zabbix_log(LOG_LEVEL_INFORMATION,"finished read");
        
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
                        usleep(1000000);
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
                    usleep(1000000); //whatever else is is it's good to time to take a nap to save some CPU heat
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
        zabbix_log(LOG_LEVEL_INFORMATION,"Continue read: %d, worker_fail: %d, Hisread:%s",continue_read,worker_fail,resp_buffer);
        sleep(1);
        int resp_len = strlen(resp_buffer);
    
        restart_worker(worker);
        
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


int glb_process_worker_request(GLB_EXT_WORKER *worker, const char *request, char **responce)
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

void glb_destroy_worker(GLB_EXT_WORKER *worker)
{

    if (worker_is_alive(worker))
        kill(worker->pid, SIGINT);

    zbx_free(worker->path);
    zbx_free(worker->args[0]);
    zbx_free(worker);
}
