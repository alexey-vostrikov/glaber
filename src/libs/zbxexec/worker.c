/***************************************
* This is a good place for a copyright - here it is. The copyright.
*
* An external worker is a long - running script 
* which is capable of processing simple and 
* similar tasks lots of times
* Runners are solution to have functionality added
* to server wihtout digging into the sources
* and having good performance at the same time
***************************************************/
#include "../../../include/common.h"
#include "worker.h"
#include "log.h"
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include "dbcache.h"
#include "zbxipcservice.h"

extern int CONFIG_TIMEOUT;

//todo: look if there is proc in the dbconfig.c
//workers are yet process-specific since they use FD , as soom
//as they'll be more universal (perhaps, will use unix domains or tcp addr for
//communitcation, then they could be global
//int zbx_dc_add_ext_worker(char *path, char *params, int max_calls, int timeout, int mode_to_writer, int mode_from_writer);
static zbx_hashset_t *workers = NULL;

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
        params[MAX_STRING_LEN],
        buff[MAX_STRING_LEN];

    int i = 0;
    zbx_json_type_t type;

    int timeout = GLB_DEFAULT_WORKER_TIMEOUT;
    int max_calls = GLB_DEFAULT_WORKER_MAX_CALLS;
    int mode_to_worker = GLB_WORKER_MODE_NEWLINE;
    int mode_from_worker = GLB_WORKER_MODE_EMPTYLINE;

    GLB_EXT_WORKER *worker = NULL;

    path[0] = 0;
    params[0] = 0;
    buff[0] = 0;

    struct zbx_json_parse jp, jp_config;

    zabbix_log(LOG_LEVEL_DEBUG, "%s: got config: '%s'", __func__, config_line);
    if (NULL == (worker = zbx_malloc(NULL, sizeof(GLB_EXT_WORKER))))
    {
        return NULL;
    }

    bzero(worker, sizeof(GLB_EXT_WORKER));

    //we expect JSON as a config, let's parse it
    if (SUCCEED != zbx_json_open(config_line, &jp_config))
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: '%s', most likely not a valid JSON", config_line);
        zbx_free(worker);
        return NULL;
    }

    if (SUCCEED != zbx_json_value_by_name(&jp_config, "path", path, MAX_STRING_LEN, &type))
    {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: couldn't find 'path' parameter");
        zbx_free(worker);
        return NULL;
    }
    else
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed worker path: '%s'", __func__, path);
        worker->path = zbx_strdup(NULL, path);
    }

    if (SUCCEED == zbx_json_value_by_name(&jp_config, "params", buff, MAX_STRING_LEN, &type))
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed params: '%s'", __func__, buff);
        worker->params = zbx_strdup(NULL, buff);
    }
    else
        worker->params = NULL;
    
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

zbx_hash_t glb_worker_hash_func(const void *data)
{
    GLB_EXT_WORKER *w = (GLB_EXT_WORKER *)data;
    return ZBX_DEFAULT_STRING_HASH_ALGO(w->path, strlen((const char *)w->path), ZBX_DEFAULT_HASH_SEED);
}

int glb_worker_compare_func(const void *d1, const void *d2)
{
    GLB_EXT_WORKER *w1 = (GLB_EXT_WORKER *)d1, *w2 = (GLB_EXT_WORKER *)d2;

    return strcmp(w1->path, w2->path);
}

int glb_init_external_workers(char **workers_cfg, char *scriptdir)
{
    char **worker_cfg;
    int ret = SUCCEED;
    GLB_EXT_WORKER *worker;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
    zabbix_log(LOG_LEVEL_INFORMATION, "Doing workers init");

    if (NULL == (workers = zbx_malloc(NULL, sizeof(zbx_hashset_t))))
        return FAIL;

    zbx_hashset_create(workers, 5, glb_worker_hash_func, glb_worker_compare_func);

    if (NULL == *workers_cfg)
        goto out;

    for (worker_cfg = workers_cfg; NULL != *worker_cfg; worker_cfg++)
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "Init worker cfg %s", *worker_cfg);

        if (NULL == (worker = glb_init_worker(*worker_cfg)))
        {
            goto out;
            ret = FAIL;
        }
        zabbix_log(LOG_LEVEL_INFORMATION, "Created worker %s", worker->path);
        //adding worker to the hash
        zbx_hashset_insert(workers, worker, sizeof(GLB_EXT_WORKER));
        zabbix_log(LOG_LEVEL_INFORMATION, "Worker %s dded to the hash of workers", worker->path);
    }
    ret = SUCCEED;
out:
    return ret;
}

//static 
int restart_worker(GLB_EXT_WORKER *worker)
{
    #define RST_ACCOUNT_PERIOD  20
    #define RST_MAX_RESTARTS    5
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

    zabbix_log(LOG_LEVEL_DEBUG, "Restarting worker %s %s pid %d", worker->path, worker->params, worker->pid);
    //closing pipework
    if (worker->pipe_from_worker)
        close(worker->pipe_from_worker);
    if (worker->pipe_to_worker)
        close(worker->pipe_to_worker);

    //first of all, killing the old one if exists
    if ( 0 != worker->pid && !kill(worker->pid, 0))
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "Killing old worker instance pid %d", worker->pid);
        //bye the process, you (probably) served us well
        kill(worker->pid, SIGINT);
        //that's enough for shutdown
        usleep(10000);
        if (!kill(worker->pid, 0))
        {
            //worker must be unable to process sigterm, lets do a sigkill
            zabbix_log(LOG_LEVEL_INFORMATION, "Stil alive, killing old worker instance pid with SIGKILL %d", worker->pid);
            kill(worker->pid, SIGKILL);
        }
    }
    
    //the new worker's live starts here
    int from_child[2];
    int to_child[2];

    //making piping
    if (0 != pipe(from_child))
    {
        return -1;
    }
    //checking
    if (0 != pipe(to_child))
    {
        close((from_child)[0]);
        close((from_child)[1]);
        return FAIL;
    }
    
    //ok, it;s a fork(burn) time
    worker->pid = fork();

    if (0 > worker->pid)
    {
        //fork has failed
        close((from_child)[0]);
        close((from_child)[1]);
        close((to_child)[0]);
        close((to_child)[1]);
        return FAIL;
    }

    //this is a children
    if (0 == worker->pid)
    {

        if (0 > dup2(from_child[1], 1) || 0 > dup2(to_child[0], 0))
        {
            _Exit(127);
        }

        //don't need pipe's fds anymore, stdin and stdout already connected to them
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
    
        execl("/bin/sh", "sh", "-c", worker->path, worker->params, (char *)NULL);
        //if we are here, then execl has failed.
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

    //resetting run count
    worker->calls = 0;
    zabbix_log(LOG_LEVEL_DEBUG, "New worker instance pid is %d", worker->pid);
    zabbix_log(LOG_LEVEL_DEBUG, "Ended %s()", __func__);
    return SUCCEED;
}

static int worker_is_alive(GLB_EXT_WORKER *worker)
{

    if (!worker->pid)
    {
        zabbix_log(LOG_LEVEL_INFORMATION, "Worker hasn't been running before");
        return FAIL;
    }
    if (kill(worker->pid, 0))
    {
        zabbix_log(LOG_LEVEL_DEBUG, "kill to worker's pid has returned non 0");
        return FAIL;
    }
    return SUCCEED;
}

//makes sure worker is ready to answer requests
//starts the script if needed, restarts after max_calls
//kills on timeout
static int worker_cleanup(GLB_EXT_WORKER *worker)
{

    char *buffer[MAX_STRING_LEN];
    int read_num = 0;
    int bytes = 0;
    zabbix_log(LOG_LEVEL_DEBUG, "%s, Doing worker %s cleanup ", __func__, worker->path);

    //will set fd from the script to non-blicking and will read till there is a data

    /* Save the current flags */
    int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
    if (flags == -1)
        return 0;

    fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);

    while (0 < (read_num = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN)))
    {
        bytes += read_num;
    }

    if (bytes > 0)
        zabbix_log(LOG_LEVEL_INFORMATION, "GARBAGE DETECTED  %d bytes of garbage from %s: %s", bytes, worker->path, *buffer);

    //restore the flags
    fcntl(worker->pipe_from_worker, F_SETFL, flags);
}

GLB_EXT_WORKER *glb_get_worker_script(char *cmd)
{
    return zbx_hashset_search(workers, cmd);
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
        zabbix_log(LOG_LEVEL_WARNING, "%s: Request must not be NULL", __func__);
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
            //for (int i= request_len -10; i < request_len; i++) {
            //    zabbix_log(LOG_LEVEL_WARNING,"%c",request[i]);
            // }
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
                zabbix_log(LOG_LEVEL_WARNING, "%d %d %d", request_len, i, request[i]);
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

    int empty_line = 0;
    int continue_read = 1;
    int read_len = 0;
    int worker_fail = 0;

    zbx_alarm_on(worker->timeout);
    wait_start = zbx_time();

    if (0 != worker->async_mode) {
        int flags = fcntl(worker->pipe_from_worker, F_GETFL, 0);
        fcntl(worker->pipe_from_worker, F_SETFL, flags | O_NONBLOCK);
    }

    while (FAIL == zbx_alarm_timed_out() && continue_read)
    {
        //usleep(10000);
        char buffer[MAX_STRING_LEN*10];
        buffer[0] = 0;

        //doing non-blocking read. Checking if we eneded up with new line or
        //just a line to understand that all the data has been recieved
        read_len = read(worker->pipe_from_worker, buffer, MAX_STRING_LEN*10);
        buffer[read_len] = 0;

        if (-1 == read_len )
        {
            //this was supposed to be used in non-blocking mode, but
            //since in raw descriptor mode it works fine even in blocking mode + alaram
            //this code isn't needed so far.
            //todo: either remove or switch scripting to the sync mode
            if (EAGAIN == errno || EWOULDBLOCK == errno || ENODATA == errno)
            {
                //we've reached end of input, it's ok
                //but if this happens not fisrt time, lets sleep a bit to save CPU
                //while waitng for some new data to appear
                
                if (wait_count++ > 1 && worker->async_mode == 0)
                {
                    usleep(1000);
                   // zabbix_log(LOG_LEVEL_DEBUG, "Waiting for new data for SYNC responce from the worker");
                } else {
                   // zabbix_log(LOG_LEVEL_INFORMATION, "Not waiting for new data from the worker due to ASYNC mode");
                    continue_read = 0;
                }
            }
            else
            {
                //this might happen if script dies
                continue_read = 0;

                zabbix_log(LOG_LEVEL_INFORMATION, "Socket read failed errno is %d", errno);
            }
        }
   //     else
     //   {
       //     zabbix_log(LOG_LEVEL_INFORMATION, "Read %d bytes from the worker", read_len);
     //   }

        //zabbix_log(LOG_LEVEL_INFORMATION, "read len check");
        if (0 == read_len)
        {
            //we've got nothing from the worker, which is most likely means that worker has died

            if (SUCCEED != worker_is_alive(worker) || (zbx_time() - wait_start > worker->timeout))
            {
                zabbix_log(LOG_LEVEL_INFORMATION, "Worker %s has died during request process or not responding", worker->path);
                continue_read = 0;
                worker_fail = 1;
            }
            else
                usleep(10000); //whatever else is is it's good to time to take a nap to save some CPU heat
        }
        size_t old_offset = rbuffoffset;

        //todo: get rid of dynamic allocations here
        //we've got a line, lets put it to the buffer

        zbx_snprintf_alloc(&resp_buffer, &rbuflen, &rbuffoffset, "%s", buffer);
        
        switch (worker->mode_from_worker)
        {
        case GLB_WORKER_MODE_NEWLINE:
            if (rbuflen > 1 && NULL != strstr(buffer, "\n"))
            {
                continue_read = 0;
                zabbix_log(LOG_LEVEL_DEBUG, "Found newline char in SINLE mode, finishing reading");
            }
            break;
        case GLB_WORKER_MODE_EMPTYLINE:
            if ((!strcmp(buffer, "\n")) || (NULL != strstr(buffer, "\n\n")))
            {
                zabbix_log(LOG_LEVEL_DEBUG, "Found empty line char in MULTI mode, finishing reading");
                continue_read = 0;
            }
            break;
        }
    }
    
    //lets see if actually has red something or it's a timeout has happened
    if (SUCCEED == zbx_alarm_timed_out() || 1 == continue_read || 1 == worker_fail)
    {
        zabbix_log(LOG_LEVEL_WARNING,
                   "%s: FAIL: script %s failed or took too long to respond or may be there was no newline/empty line in the output, or it has simply died. Will be restarted",
                   __func__, worker->path);

        sleep(1);
        int resp_len = strlen(resp_buffer);
    
        restart_worker(worker);
        
        zbx_free(resp_buffer);
        return FAIL;
    }

    zbx_alarm_off();
    //setting the responce buffer
    //it's the caller's business to free it
   // zabbix_log(LOG_LEVEL_INFORMATION,"In %s: setting responce", __func__);
    *responce = resp_buffer;
    //zabbix_log(LOG_LEVEL_INFORMATION,"In %s: finished", __func__);
    if ( 1 > read_len) return POLL_NODATA;
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

//this is supposed to be used to escape data before sending to workers
//it does two things: escapes single unscaped quotes to make the string JSON valid
//and replaces newlines with \n to make them worker - valid for line by line processing
int glb_escape_worker_string(char *in_string, char *out_buffer)
{

    int in = 0, out = 0;
    while (in_string[in] != '\0')
    {
        //we don't want some special chars to appear in the json

        if (in_string[in] == '\"')
        {

            if (0 < in && in_string[in - 1] != '\\')
            {
                out_buffer[out++] = '\\';
            }
            out_buffer[out++] = in_string[in++];
        }
        else if (in_string[in] == 10)
        {
            out_buffer[out++] = '\\';
            out_buffer[out++] = 'n';
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

    //let's kill the worker first
    if (worker_is_alive(worker))
        kill(worker->pid, SIGINT);

    zbx_free(worker->path);
    zbx_free(worker->params);
    zbx_free(worker);
}
//ok, now the logic how we work with the worker modules:
//1. Generally it's interactive mode, when a worker
//finishes processing, it should give us empty new line
//1.1. It might be feasible to run a module in a "consuming" mode when
//it just accept data and we don't expect any output to happen from it
//it's much easier for things like history uploading - we just send info, and when we cannot, we fail

//then, it might be a good idea to implement a worker back-off timer - whenever worker has failed,
//the module will fail quickly without doing an expencive job like restarting a caller all the time

//2. we must write to a pipe in async mode: have to be sure nothing will block us
//3. then the module keeps waiting for a new line till timeout exceeds, and this process
//must be quite efficient (perhaps a poll with timeout or something), just banging each 1ms
//into a pipe might be either not fast enough or non efficicent in terms of CPU usage

//4. new lines logic: it might be many approaches. It's definetly very easy to do a line by line processing
//for both scripts and output from a script.
//however it might be usefull and convinient for scripts to produce multiline output.
//so generally it's three modes - single line, multiline and silent. Its all applies to input data and
//output data.
//however if "silent" input mode is seleted, then to signal a runnur to feed us a data, we'll send one empty line to it

//for example: a history syncer which writes data might be silent for output, which means we don't expect
//anything on his stdout pipe, we will clean (and complain) whatever happens there  (actually perhaps it's a good idea to
// handle sterr separatly - perhaps, translate it to the log)

//a history syncer working in the reader mode is a good example of multiline output (we probably be getting)
//JSON data or some preformatted CSV out of it

//it might be a history syncer which works in value cache prefill mode, this one might get all
//the params it needs through the configuration

//so, it's three modes, GLB_MODULE_SILENT, GLB_MODULE_SINGLELINE GLB_MODULE_MULTILINE
//and the defaults: to module: GLB_MODULE_SINGLELINE
//                  from module: GLB_MODULE_MULTILINE

//so we treat sending and recieving data from and to the module differently according to the mode
//in single line operations we don't expect newline characters to appear in the request
//as well as emptylines in the multiple mode
//we send nothing in silent mode
//and whatever output happens in the silent mode we ignore it, but complain in the logs
//when processing output, we wait till timeout for getting a newline character and ignore whatever data
//fillows new line
//in multiline we wait for an empty string to show up