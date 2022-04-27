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

typedef struct 
{
    zbx_binary_heap_t queue;
    unsigned char locks;
    mem_funcs_t memf;
    pthread_mutex_t lock;
    event_queue_cb_conf_t *callbacks;
} event_queue_conf_t;

int event_queue_compare_func(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(e1->key, e2->key);
	return 0;
}


void *event_queue_init(mem_funcs_t *s_memf, unsigned char enable_locks, event_queue_cb_conf_t *cbs) {
    mem_funcs_t memf;
    event_queue_conf_t *conf;
    int i;
    unsigned char max_event_id = 0; 
    //LOG_INF("Doing event qurue init");

    if (NULL == s_memf) 
    {   /* using heap memory func by default */
        memf.free_func = ZBX_DEFAULT_MEM_FREE_FUNC;
        memf.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC;
        memf.realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC;
    } else 
        memf = *s_memf;
    
    if (NULL == (conf = memf.malloc_func(NULL, sizeof(event_queue_conf_t))))
        return NULL;    
    conf->memf = memf;
    zbx_binary_heap_create_ext(&conf->queue, event_queue_compare_func, 0, memf.malloc_func, memf.realloc_func, memf.free_func);

    conf->locks = enable_locks;
    
    if (conf->locks) 
        glb_lock_init(&conf->lock);
    
    i=0;
    while (0 != cbs[i].event_id ) {
      //  LOG_INF("Parsing cb, event id is %d", cbs[i].event_id);
        max_event_id = MAX( max_event_id, cbs[i].event_id);
     
        if (i> 20) {
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);
        }
           i++;
    }

    //LOG_INF("Max event id is %d",(int)max_event_id);
    if ( NULL == (conf->callbacks = conf->memf.malloc_func(NULL, sizeof(event_queue_cb_conf_t) * (max_event_id+1))))
        return NULL;
    //LOG_INF("doing bzero ");
    bzero(conf->callbacks, sizeof(event_queue_cb_conf_t) * max_event_id);
    i=0;
    //LOG_INF("doing cbs init");
    while (0 != cbs[i].event_id ) {
      //  LOG_INF("Creating callback %d", cbs[i].event_id);
        unsigned char event_id = cbs[i].event_id;
        conf->callbacks[event_id].event_id = event_id;
        conf->callbacks[event_id].func = cbs[i].func;
        
        i++;
    }
    
    //LOG_INF("init: Heap queue addr is %p",&conf->queue);
    //LOG_INF("init: Conf is %p", conf);
    return conf;
}

void event_queue_destroy(void *eq_conf) {
    event_queue_conf_t *conf = eq_conf;
    
    zbx_binary_heap_destroy(&conf->queue);
    conf->memf.free_func(conf->callbacks);
    conf->memf.free_func(conf);
}


int event_queue_process_events(void *eq_conf, int max_events) {
    event_queue_conf_t *conf = eq_conf;
    int now = time(NULL);
    int i = 0;

    //LOG_INF("It's %d events in the queue",conf->queue.elems_num);

    while (FAIL == zbx_binary_heap_empty(&conf->queue) && 
            (max_events == 0 || i < max_events) )
	{
		zbx_binary_heap_elem_t *min;
        unsigned char event_id;
        u_int64_t msec_time;
        void *data;

        if (conf->locks) 
             glb_lock_block(&conf->lock);

		min = zbx_binary_heap_find_min(&conf->queue);
		 
		if ( min->key > now ) {
        //    LOG_INF("Closest event is in %d seconds", min->key-now);
            if (conf->locks) 
                glb_lock_unlock(&conf->lock);   
            break;
        }

      //  LOG_INF("Queue is %d seconds late", now - min->key);
        event_id = (unsigned char )min->local_data;
        data = (void *) min->data;
        msec_time = min->key;

		zbx_binary_heap_remove_min(&conf->queue);

        if (conf->locks) 
            glb_lock_unlock(&conf->lock);   

      //  LOG_INF("Calling callback for event %d", event_id);
        conf->callbacks[event_id].func(conf, msec_time, event_id, data, &conf->memf);
        //LOG_INF("Finished callback for event %d", event_id);
        i++;
    }
    return i;
}

int event_queue_add_event(void *eq_conf, int time, unsigned char event_id, void *data) {
    event_queue_conf_t *conf = eq_conf;
 
    //LOG_INF("In the %s, conf is %p", __func__, eq_conf);
   //LOG_INF("Adding new queue elemnt");
    		
	zbx_binary_heap_elem_t elem = { .key = time, .local_data = (u_int64_t) event_id, .data = (const void *)data};
	//LOG_INF("Heap queue addr is %p",&conf->queue);
    //LOG_INF("Add elem: Conf is %p", conf);
    //LOG_INF("Element is : key: %ld, local_data: %ld, data: %ld", elem.key, elem.local_data, elem.data);
    zbx_binary_heap_insert(&conf->queue, &elem);
	//zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished", __func__);
}

