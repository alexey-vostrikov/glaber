/*
** Glaber
** Copyright (C) 2001-2100
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
#include "glb_lock.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxtime.h"

#define MAX_EVENT_CALLBACK_ID 255
/*event queue is done to process async logic
 - has millisecond precision
 - a typical implementation is:
 //init
 event_queue_init();

 event_queue_add_callback(EVENT_TYPE1, callback1);
 event_queue_add_callback(EVENT_TYPE2, callback2);
 ...
 event_queue_add_callback(EVENT_TYPE22, callback22);
//end of init

 event_queue_add_event(TYPE, time_to_call_callback);

 while (some condition)
    event_queue_process_events(MAX_EVENT_PER_RUN); //will process MAX_EVENTS_PER_RUN for which time is arrived
                                                   //the event handlers might place new events

 //note: there is no way to delete an event from the queue
 // so to do this you save time for which the right event is planned
 // and while processing the event make sure that it has matching time
 // this logic doesn't covered in the event handling

 //event data holds:
    zbx_uint64_t	key;
    const void		*data;

    which allows to save object ids in the local_data without extra allocations
*/

struct event_queue_t
{
    zbx_binary_heap_t queue;
    unsigned char locks;
    mem_funcs_t memf;
    pthread_mutex_t lock;
    event_queue_cb_func_t callbacks[MAX_EVENT_CALLBACK_ID + 1];
};

int event_queue_compare_func(const void *d1, const void *d2)
{
    const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
    const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

    ZBX_RETURN_IF_NOT_EQUAL(e1->key, e2->key);
    return 0;
}

event_queue_t *event_queue_init(mem_funcs_t *s_memf)
{
    mem_funcs_t memf;
    event_queue_t *conf;
    int i, enable_locks = 1;
    unsigned char max_event_id = 0;

    //  LOG_INF("qfqewrfwerfwr");
    if (NULL == s_memf)
    { /* using heap memory func by default */
        memf.free_func = ZBX_DEFAULT_MEM_FREE_FUNC;
        memf.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC;
        memf.realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC;
    }
    else
    {
        memf = *s_memf;
    }

    if (NULL == (conf = memf.malloc_func(NULL, sizeof(event_queue_t))))
        return NULL;

    if (NULL == s_memf)
        conf->locks = 0;
    else
    {
        conf->locks = 1;
        glb_lock_init(&conf->lock);
    }

    conf->memf = memf;
    zbx_binary_heap_create_ext(&conf->queue, event_queue_compare_func, 0, memf.malloc_func, memf.realloc_func, memf.free_func);

    return conf;
}

int event_queue_add_callback(event_queue_t *conf, unsigned char callback_id, event_queue_cb_func_t cb_func)
{

    if (callback_id > MAX_EVENT_CALLBACK_ID)
        return FAIL;

    conf->callbacks[callback_id] = cb_func;
    return SUCCEED;
}

void event_queue_destroy(event_queue_t *conf)
{

    zbx_binary_heap_destroy(&conf->queue);
    conf->memf.free_func(conf);
}

int event_queue_process_events(event_queue_t *conf, int max_events)
{

    u_int64_t now = glb_ms_time();

    int i = 0;

    while (FAIL == zbx_binary_heap_empty(&conf->queue) &&
           (max_events == 0 || i < max_events))
    {
        zbx_binary_heap_elem_t *min;
        unsigned char event_id;

        u_int64_t msec_time;
        void *data;

        if (conf->locks)
            glb_lock_block(&conf->lock);

        min = zbx_binary_heap_find_min(&conf->queue);

        if (min->key > now)
        {
            LOG_DBG("Queue is %ld mseconds ahead, q size is %d", min->key - now, conf->queue.elems_num);
            if (conf->locks)
                glb_lock_unlock(&conf->lock);
            break;
        }

        LOG_DBG("Queue is %ld mseconds late, q size is %d", now - min->key, conf->queue.elems_num);
        event_id = (unsigned char)min->local_data;
        data = (void *)min->data;
        msec_time = min->key;

        zbx_binary_heap_remove_min(&conf->queue);

        if (conf->locks)
            glb_lock_unlock(&conf->lock);
        // LOG_INF("processing events3, event id is %ld, addr is %p, data is %ld, time is %ld, total %d",
        //         min->local_data, conf->callbacks[event_id], data, msec_time, conf->queue.elems_num);
        conf->callbacks[event_id](conf, msec_time, event_id, data, &conf->memf);
        i++;
    }
    return i;
}

int event_queue_add_event(event_queue_t *conf, u_int64_t msec_time, unsigned char event_id, void *data)
{

    zbx_binary_heap_elem_t elem = {.key = msec_time, .local_data = (u_int64_t)event_id, .data = (const void *)data};
    // LOG_INF("Heap queue addr is %p",&conf->queue);
    // LOG_INF("Add elem: Conf is %p", conf);
    //   LOG_INF("Adding Element: key: %ld, local_data: %ld, data: %ld", elem.key, elem.local_data, elem.data);
    if (NULL == conf->callbacks[event_id])
    {
        LOG_INF("Added event for not-existing callback");
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }

    zbx_binary_heap_insert(&conf->queue, &elem);
    // zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}

int event_queue_get_events_count(event_queue_t *conf)
{
    return conf->queue.elems_num;
}

u_int64_t event_queue_get_delay(event_queue_t *conf, u_int64_t now)
{
    u_int64_t ret;
    zbx_binary_heap_elem_t *min;

    if (conf->locks)
        glb_lock_block(&conf->lock);

    if (FAIL == zbx_binary_heap_empty(&conf->queue))
    {
        min = zbx_binary_heap_find_min(&conf->queue);

        ret = now - min->key;
    }
    else
        ret = 0;

    if (conf->locks)
        glb_lock_unlock(&conf->lock);

    return ret;
}