/*
** Glaber
** Copyright (C) 2001-2038 Glaber 
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

#include "log.h"
#include "zbxcommon.h"
#include "../../libs/zbxexec/worker.h"
#include "glb_server.h"
#include "glb_poller.h"
#include "module.h"
#include "zbxjson.h"

extern int CONFIG_EXT_SERVER_FORKS;

#define WORKER_SILENCE_TIMEOUT 60

typedef struct
{
    int last_heard;
    glb_worker_t *worker;
    int responses;
    int delay;
} worker_t;

extern char *CONFIG_WORKERS_DIR;
extern int CONFIG_CONFSYNCER_FREQUENCY;

typedef struct
{
    u_int64_t hash;
    u_int64_t itemid;
    u_int64_t hostid;
    unsigned int expire;
    unsigned char flags;
    unsigned char value_type;
} GLB_SERVER_IDX_T;

static int init_item(DC_ITEM *dcitem, poller_item_t *poller_item)
{
    worker_t *worker;
    char *args = NULL;
    struct zbx_json_parse jp;
    char full_path[MAX_STRING_LEN], params[MAX_STRING_LEN], tmp_str[MAX_STRING_LEN];
    zbx_json_type_t type;
    char *path = NULL;

 	zbx_custom_interval_t *custom_intervals;
    char *error;

    worker = (worker_t *)zbx_calloc(NULL, 0, sizeof(worker_t));

    if (SUCCEED != zbx_interval_preproc(dcitem->delay, &worker->delay, &custom_intervals, &error))
	{
		LOG_INF("Worker itemd %ld has wrong delay time set :%s", dcitem->itemid, dcitem->delay);
		return FAIL;
	}

    DEBUG_ITEM(dcitem->itemid, "Set timeout of %d seconds for worker %s",worker->delay, dcitem->key);

    if (NULL == dcitem->params)
    {
        LOG_DBG("Cannot run a server worker with an empty path");
        DEBUG_ITEM(dcitem->itemid, "Cannot run a server worker with empty path");

        return FAIL;
    }

    if ('/' != dcitem->params[0])
    {
        zbx_snprintf(full_path, MAX_STRING_LEN, "%s/%s", CONFIG_WORKERS_DIR, dcitem->params);
        path = full_path;
    }
    else
        path = dcitem->params;

    if (NULL != (args = strchr(path, ' ')))
    {
        args[0] = 0;
        args++;
    }
    //it's a potential flaw here - until item updated server worker won't start
    if (NULL == (worker->worker = glb_worker_init(path, args, worker->delay, 
                GLB_SERVER_MAXCALLS, GLB_WORKER_MODE_NEWLINE, GLB_WORKER_MODE_NEWLINE))) {
        zbx_free(worker);
        poller_preprocess_error(poller_item, "Couldn't strart server worker, check if file exist");
        return FAIL;
    }
    worker->last_heard = time(NULL);
   
    poller_set_item_specific_data(poller_item, worker);

    DEBUG_ITEM(poller_get_item_id(poller_item), "Finished init of server item, worker %s", worker_get_path(worker->worker));
    return SUCCEED;
};

static void delete_item(poller_item_t *poller_item)
{
    worker_t *worker = poller_get_item_specific_data(poller_item);
    DEBUG_ITEM(poller_get_item_id(poller_item), "Deleting server worker item ");

    glb_worker_destroy(worker->worker);
    zbx_free(worker);
}

static int json_responce_has_timestamp(char *data) {
    struct zbx_json_parse jp;
    int err_flag;
    long int ts;

    if (SUCCEED != zbx_json_open(data, &jp))
        return FAIL;

    if ((FAIL != (ts = glb_json_get_int_value_by_name(&jp, "timestamp", &err_flag))) ||   
        (FAIL != (ts = glb_json_get_int_value_by_name(&jp, "time", &err_flag))) ) {
        
        if (ts < 1000000000) //year 2001, no data expected that old
            return FAIL;

        if (ts > 16000000000) //year 2477 
            ts = ts / 1000; // maybe its msec time
         
        if (ts > 1000000000  && //~2001
            ts < 10000000000 ) {// ~2286
            return ts;
            }
    }

    return FAIL;
}


#define MAX_ITERATIONS 10000
ITEMS_ITERATOR(check_workers_data_cb)
{
    worker_t *worker = poller_get_item_specific_data(poller_item);
    int iterations = 0;
    int now = time(NULL);
    char *worker_response = NULL;
    u_int64_t timestamp = 0;
    zbx_timespec_t ts = {0};

    /* workers have own alive and restart checks, we don't bother here*/
    while (iterations < MAX_ITERATIONS)
    {
        if (FAIL == glb_worker_get_async_buffered_responce(worker->worker, &worker_response) || NULL == worker_response) 
             break;

        iterations++;

        DEBUG_ITEM(poller_get_item_id(poller_item), "Got from worker: %s", worker_response);
        poller_inc_responses();
        
        if (FAIL != ( timestamp = json_responce_has_timestamp(worker_response))) {
            DEBUG_ITEM(poller_get_item_id(poller_item), "Set timestamp from time field: %ld", timestamp);
            ts.sec = timestamp;
            poller_preprocess_str(poller_item, &ts, worker_response);
        }
        else {
             DEBUG_ITEM(poller_get_item_id(poller_item), "No timestamp in the data, using current time");
            poller_preprocess_str(poller_item, NULL, worker_response);
        }
        
        worker->last_heard = now;
        zbx_free(worker_response);
    }
    
    /*if worker is silent for too long, restarting it*/
    if (worker->last_heard < time(NULL) - worker->delay)
    {
        LOG_INF("Worker has been silent for %d seconds, restarting", worker->delay);
        glb_worker_restart(worker->worker, "Worker has been silent for too long");
        worker->last_heard = now + rand() % 10;
        
        DEBUG_ITEM(poller_get_item_id(poller_item), "Worker has been silent for too long, setting UNSUPPORTED value");
        poller_preprocess_error(poller_item, "Couldn't read from the worker - worker is silent for too long");
    }

    return POLLER_ITERATOR_CONTINUE;
}

static void handle_async_io(void)
{
    poller_items_iterate(check_workers_data_cb, NULL);
}


extern int CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

static int forks_count(void)
{
    return CONFIG_FORKS[GLB_PROCESS_TYPE_SERVER];
}

int glb_worker_server_init(void)
{
    poller_set_poller_callbacks(init_item, delete_item, handle_async_io, NULL, NULL, 
            forks_count, NULL, NULL);
    return SUCCEED;
}
