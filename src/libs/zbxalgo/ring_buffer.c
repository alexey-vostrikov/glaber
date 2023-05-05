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

#include "zbxcommon.h"
#include "zbxalgo.h"
#include "log.h"

#define GLB_TSBUFF_MAX_SZIE 1000000

#define GLB_TSBUFF_ASCENDING 1
#define GLB_TSBUFF_DESCENDING -1

typedef struct {
    int time; //time should go first
    int data;
} tsbuff_value_t;


#define BUFF_IDX(tsbuff, new_idx) ((new_idx + tsbuff->size) % tsbuff->size)



int glb_tsbuff_index(glb_tsbuff_t *tsbuff, int index) {
    
    return BUFF_IDX(tsbuff, index);
}

void  glb_tsbuff_destroy(glb_tsbuff_t *tsbuff, zbx_mem_free_func_t free_func) {
    
    if (tsbuff->count > 0 ) {
        LOG_WRN("Shoudln't destroy buffer with elements in it, this is a programming BUG");
        assert(0);
    }

    free_func(tsbuff->data);
}


int glb_tsbuff_free_tail(glb_tsbuff_t *tsbuff) {
    
    if (tsbuff->count ==0)
        return FAIL;
    
    tsbuff->count --;
    
    if (0 == tsbuff->count)
        tsbuff->tail = -1;
    else 
        tsbuff->tail = BUFF_IDX(tsbuff, tsbuff->tail+1);
    
    return SUCCEED;
}

int glb_tsbuf_get_oldest_time(glb_tsbuff_t* tsbuff) {
    if (tsbuff->tail > -1) {
        tsbuff_value_t *b_val = glb_tsbuff_get_value_ptr(tsbuff, tsbuff->tail);
        return b_val->time;
    }
    
    return 0;
} 

void* glb_tsbuff_get_value_ptr(glb_tsbuff_t* tsbuff, int idx) {
    if ( 0 > idx ||  idx >= glb_tsbuff_get_size(tsbuff) ) {
        LOG_WRN("Index %d out of range 0..%d", idx,  glb_tsbuff_get_size(tsbuff) -1 );
        THIS_SHOULD_NEVER_HAPPEN;
        assert(0);
    };

    return  (void *) tsbuff->data + idx* tsbuff->item_size; //to avoid compiler data size indexing using void during calc
}
 
void *glb_tsbuff_get_value_head(glb_tsbuff_t* tsbuff) {
    
    return glb_tsbuff_get_value_ptr(tsbuff, tsbuff->head);
}

void *glb_tsbuff_get_value_tail(glb_tsbuff_t* tsbuff) {
    
    if (glb_tsbuff_get_count(tsbuff) == 0) 
        return NULL;
    
    return glb_tsbuff_get_value_ptr(tsbuff, tsbuff->tail);
}

void*  glb_tsbuff_add_to_head(glb_tsbuff_t *tsbuff, int time) {
    
    tsbuff_value_t *val;
    int old_time, new_head;
    
    if (glb_tsbuff_get_count(tsbuff) > 0) {
        old_time = glb_tsbuff_get_time_head(tsbuff);

        if (old_time != ZBX_JAN_2038 && old_time > time)  {
            LOG_DBG("Old time is %d, not adding item to the head due to lower time: %d", old_time, time);
            return NULL;     
        }

       new_head = BUFF_IDX( tsbuff, tsbuff->head + 1);
    } else {
        new_head = 0;
    }
     
    if (new_head == tsbuff->tail && glb_tsbuff_get_size(tsbuff) > 1) {
        LOG_WRN("New head reached the tail, not enough buffer space");
        return NULL;
    }

    tsbuff->head=new_head;

    val = glb_tsbuff_get_value_ptr(tsbuff, tsbuff->head);
    val->time = time;
    tsbuff->count++;
    
    if (1 == tsbuff->count) 
        tsbuff->tail = tsbuff->head;
    
    //LOG_WRN("Total count2 is %d", glb_tsbuff_get_count(tsbuff));
    return val;
}

void*   glb_tsbuff_add_to_tail(glb_tsbuff_t *tsbuff, int time) {
    tsbuff_value_t *val;
    int old_time, new_tail;

    if (glb_tsbuff_get_count(tsbuff) > 0) {
        old_time = glb_tsbuff_get_time_tail(tsbuff);
       // LOG_INF("Got old time %d new time is %d", old_time, time);
        
        if (old_time > 0 && old_time < time) {
           // LOG_INF("Cannot add new item with time %d to the tail: the item time is higher the the oldest value time %d",time, old_time);
            return NULL; 
        }
    }

    if (0 < tsbuff->count ) {
        new_tail = BUFF_IDX( tsbuff, tsbuff->tail - 1);
        
        if (new_tail == tsbuff->head) 
            return NULL;
    } else {
        new_tail = 0;
        tsbuff->head = 0;
    }
    
    tsbuff->tail=new_tail;

    val = glb_tsbuff_get_value_ptr(tsbuff, tsbuff->tail);
    
    val->time = time;
    tsbuff->count++;

    return val;

}

int   glb_tsbuff_get_size(glb_tsbuff_t *tsbuff) {
    return tsbuff->size;
}

int   glb_tsbuff_get_count(glb_tsbuff_t *tsbuff) {
    return tsbuff->count;
}

int glb_tsbuff_is_full(glb_tsbuff_t *tsbuff) {
    if (tsbuff->count == tsbuff->size) 
        return SUCCEED;
    return FAIL;
}

int  glb_tsbuff_init(glb_tsbuff_t *tsbuff, unsigned int size, size_t item_size, zbx_mem_malloc_func_t malloc_func) {
    if (size == 0 || item_size == 0 || malloc_func == NULL)
        return FAIL;
    
    tsbuff->size = size;
    tsbuff->item_size = item_size;
    tsbuff->count = 0;

    if (NULL == (tsbuff->data = malloc_func(NULL, size * item_size ))) {
        LOG_INF("There is not enough memory increase the size in the config");
        exit(-1);
    };

    bzero(tsbuff->data,  size * item_size);
    tsbuff->head = -1;
    tsbuff->tail = -1;
       
    return SUCCEED;
}; 

int glb_tsbuff_get_time_tail(glb_tsbuff_t *tsbuff) {

    tsbuff_value_t *val;
    
    if ( 0 == glb_tsbuff_get_count(tsbuff)) {
//        THIS_SHOULD_NEVER_HAPPEN;
  //      exit(-1);
        return -1;
    }
    if (NULL != (val = glb_tsbuff_get_value_ptr(tsbuff,tsbuff->tail))) {
        return val->time;
    }
    
    return -1;
}

int glb_tsbuff_get_time_head(glb_tsbuff_t *tsbuff) {
    tsbuff_value_t *val;

    if ( 0 == glb_tsbuff_get_count(tsbuff)) 
        return -1;
    
    if (NULL != (val = glb_tsbuff_get_value_ptr(tsbuff,tsbuff->head))) {
        return val->time;
    }

    return -1;
}


int glb_tsbuff_find_time_idx(glb_tsbuff_t *tsbuff, int tm_sec) {
    
    int head_time, tail_time, tail_idx, head_idx, guess_idx;
   
    if (glb_tsbuff_get_count(tsbuff) == 0) 
        return FAIL;
        
    head_time = glb_tsbuff_get_time_head(tsbuff);
    tail_time = glb_tsbuff_get_time_tail(tsbuff);
    

    if (tail_time > tm_sec || head_time < tm_sec) 
        return FAIL; 
    
    tail_idx = tsbuff->tail;
    head_idx = tsbuff->head;
    
    while (1) {
        tsbuff_value_t *guess_val, *head_val, *tail_val;
        
        if (head_idx < tail_idx) 
            guess_idx = (tail_idx + head_idx + tsbuff->size ) / 2 ; 
        else 
            guess_idx = BUFF_IDX(tsbuff, (tail_idx + head_idx) / 2 + tsbuff->size); 
        //LOG_INF("Tail idx %d, head idx %d, guess idx %d", tail_idx, head_idx, guess_idx);
        
        guess_val = glb_tsbuff_get_value_ptr(tsbuff, BUFF_IDX(tsbuff,guess_idx));
        head_val = glb_tsbuff_get_value_ptr(tsbuff, head_idx);
        
        if (tm_sec == guess_val->time) 
            return BUFF_IDX(tsbuff,guess_idx);
        
        if (tm_sec == head_val->time) 
            return head_idx;
 
        if ( BUFF_IDX(tsbuff,guess_idx) == tail_idx) { //two numbers left, use older time one
          //  LOG_INF("Only two numbers left, selecting one from the tail");
            return tail_idx;
        }
        
        if (tm_sec > guess_val->time) { //the value is withing guess_idx...head_idx 
            if (BUFF_IDX(tsbuff,guess_idx + 1) == head_idx) { //two values left, choosing the older one
                return  BUFF_IDX(tsbuff,guess_idx);
            }
                      
            tail_idx = BUFF_IDX(tsbuff,guess_idx);
        } else {
            //the value is withing tail_idx .. guess_idx
            if (BUFF_IDX(tsbuff, guess_idx -1 ) == tail_idx) 
                return tail_idx;

            head_idx = BUFF_IDX(tsbuff,guess_idx);
        }
    }
}

static int tsbuff_upsize(glb_tsbuff_t *tsbuff, int new_size, zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func) {
    void *new_data = NULL;

    if (NULL == (new_data = alloc_func(NULL, tsbuff->item_size * new_size))) 
        return FAIL;

    bzero(new_data, tsbuff->item_size * new_size);
    
    if (tsbuff->head > -1) {

        if (tsbuff->head >= tsbuff->tail) { //no reordering needed
            memcpy(new_data, tsbuff->data, tsbuff->count * tsbuff->item_size);
       
        } else {

            void   *first_segment_ptr = (void *)glb_tsbuff_get_value_tail(tsbuff);
            size_t first_size = (tsbuff->size - tsbuff->tail) * tsbuff->item_size;
            size_t last_size = (tsbuff->head + 1) * tsbuff->item_size;
    
            memcpy(new_data, first_segment_ptr, first_size );
            memcpy(new_data + first_size, tsbuff->data, last_size);
    
            tsbuff->tail = 0;
            tsbuff->head = tsbuff->count - 1;
        }
    }
    
    free_func(tsbuff->data);
    tsbuff->data = new_data;
    tsbuff->size = new_size;

    return new_size;

}

static int tsbuff_downsize(glb_tsbuff_t *tsbuff, int new_size, zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func, glb_tsbuff_val_free_func_t val_free_func) {
    int i,j=0;
    void *new_data;

    if (NULL == (new_data = alloc_func(NULL, tsbuff->item_size * new_size))) 
        return FAIL;

    if ( NULL!= val_free_func )
        for (i = tsbuff->tail; BUFF_IDX(tsbuff, i) != BUFF_IDX(tsbuff, tsbuff->head - new_size + 1); i++ ) {
            val_free_func(alloc_func, free_func, glb_tsbuff_get_value_ptr(tsbuff,i));
        }

    for (i = BUFF_IDX(tsbuff, tsbuff->head - new_size + 1); i != BUFF_IDX(tsbuff, tsbuff->head + 1); i=BUFF_IDX(tsbuff,i+1)) {
        memcpy(new_data + j * tsbuff->item_size, glb_tsbuff_get_value_ptr(tsbuff,i), tsbuff->item_size);

        j++;
    }

    tsbuff->count = j;
    tsbuff->size = new_size;
    tsbuff->tail = 0;
    tsbuff->head = j-1;
    
    free_func(tsbuff->data);
    tsbuff->data = new_data;
    
  
    return j;
}

void glb_tsbuff_dump(glb_tsbuff_t *tsbuff) {
    int i = tsbuff->tail, c;
    LOG_INF("DUMP START, tail: %d, head: %d, count: %d, size: %d, ", tsbuff->tail, tsbuff->head, tsbuff->count, tsbuff->size);
    
    for (c = 0 ; c < tsbuff->count; c++ ) {
        glb_tsbuff_value_t *val=glb_tsbuff_get_value_ptr(tsbuff,i);
        LOG_INF("DUMP: %i -> %d", i, val->sec);
        i = BUFF_IDX(tsbuff, i+1);
    }
    LOG_INF("DUMP END");
}

int glb_tsbuff_resize(glb_tsbuff_t *tsbuff, int new_size, zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func, glb_tsbuff_val_free_func_t val_free_func) {

    if (new_size > GLB_TSBUFF_MAX_SZIE || new_size < 1|| new_size == tsbuff->size) 
        return FAIL;
    
    if (new_size < tsbuff->size) 
        return tsbuff_downsize(tsbuff, new_size, alloc_func, free_func, val_free_func);
    else 
        return tsbuff_upsize(tsbuff,new_size, alloc_func, free_func);

}

int glb_tsbuff_check_has_enough_count_data_idx(glb_tsbuff_t *tsbuff, int need_count, int head_idx) {
    
    //LOG_INF("Checking if there are enough data: %d items starting at idx %d", need_count, head_idx);
    if (need_count == 0)
        return FAIL;

    if (tsbuff->tail > head_idx ) {
        if (tsbuff->tail <= head_idx + tsbuff->size - need_count + 1 ) 
            return SUCCEED;
        else 
            return FAIL;
    }
    
   // LOG_INF("idx is %d; need count is %d, tail is %d", head_idx, need_count,tsbuff->tail );
    if (head_idx - need_count + 1 >=tsbuff->tail) 
            return SUCCEED;
        else 
            return FAIL;
};

int glb_tsbuff_check_has_enough_count_data_time(glb_tsbuff_t *tsbuff, int need_count, int time) {
    int idx;
    
    if (time < 0 || time  > glb_tsbuff_get_time_head(tsbuff) || time < glb_tsbuff_get_time_tail (tsbuff) || need_count == 0)
        return FAIL;
    
    idx = glb_tsbuff_find_time_idx(tsbuff, time);
    
    return glb_tsbuff_check_has_enough_count_data_idx(tsbuff,need_count,idx);
};
