/*
** Copyright Glaber or is it Zabbix copyright?
** //TODO: figure out can i change the name in the copyright or not?
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

#include "glb_cache.h"
#include "glb_cache_items.h"


typedef struct {
    int count;
    int period;
    
    int count_change;
    int period_change;

    int new_count;
    int new_period;
} item_demand_t;

typedef struct {

    glb_cache_item_meta_t meta;
    glb_tsbuff_t tsbuff;
    item_demand_t demand;

    unsigned value_type; 
    int lastdata; 
    int last_accessed; //housekeeper will delete items that haven't been accessed for a while
    int nextcheck;   
    
    int db_fetched_time;    
    const char *error; 

}  item_elem_t;


typedef struct  {
    int hits;
    int misses;
    int db_requests;
    int db_fails;
    int fails;
} items_stats_t;

typedef struct {
    items_stats_t stats;
} items_cfg_t;

typedef struct {
    int time_sec;
    zbx_variant_t value;
}  glb_cache_item_value_t;

typedef struct {
    items_cfg_t * i_cfg;
    void *data;
} items_cb_params_t;

typedef struct {
    u_int64_t itemid;
    int count;
    int value_type;
    int ts_head;
    int seconds;
    zbx_vector_history_record_t *values;
    items_cfg_t *i_cfg;
    
} fetch_req_t;



void *glb_cache_items_init() {
    
    items_cfg_t *i_cfg = (items_cfg_t *) glb_cache_malloc(NULL, sizeof(items_cfg_t));
    
    if (NULL == i_cfg) {
        LOG_WRN("Cannot allocate mem for items cache, exiting");
        exit(-1);
    }

    bzero(i_cfg, sizeof(items_cfg_t));
    return (void *)i_cfg;
}

static void  glb_cache_item_variant_clear(zbx_variant_t *value ) {

//    LOG_DBG("In: %s, Freeing item of type %d",__func__, value->type);
	    
    switch (value->type)
	{
		case ZBX_VARIANT_STR:
			glb_cache_free(value->data.str);
			break;
		case ZBX_VARIANT_BIN:
			glb_cache_free(value->data.bin);
			break;
		case ZBX_VARIANT_ERR:
			glb_cache_free(value->data.err);
			break;
		case ZBX_VARIANT_DBL_VECTOR:
			zbx_vector_dbl_destroy(value->data.dbl_vector);
			glb_cache_free(value->data.dbl_vector);
			break;
	}
	value->type = ZBX_VARIANT_NONE;
}


/*******************************************************
 * cleans and marks unused outdated items 
 * that exceeds the demand
 * *****************************************************/
static void housekeep_item_values(item_elem_t *item_elem) {
}/*    
    unsigned int now, lastdate, clean = 0;
    glb_tsbuff_t *buff = &item_elem->tsbuff;

    zabbix_log(LOG_LEVEL_DEBUG,"Start %s:",__func__);

    if (-1 == buff->last_idx && -1 == buff->first_idx) //nodata check
            return; 
    
    now = time(NULL);
    
    //starting from the oldest items
    glb_cache_value_t *val = glb_tsbuff_get_latest_ptr(buff);
    
    while ( elem->req_count >= buff->elem_count && 
            buff->elem_count >= GLB_CACHE_MIN_COUNT && 
            -1 != buff->first_idx  && 
            (val->sec + elem->req_duration) <= now ) {
        
        glb_cache_variant_clear(&val->value);
        val->sec = 0;
        
        if (buff->first_idx == buff->last_idx) {
            buff->last_idx = -1;
            buff->first_idx = -1;
            buff->elem_count = 0;
            clean++;
        } else {
            buff->first_idx = (buff->first_idx + 1) % buff->elem_num;
            buff->elem_count--;
        }
        
        val = glb_tsbuff_get_value_ptr(buff,buff->first_idx);
    }
    
    zabbix_log(LOG_LEVEL_DEBUG,"End of %s: clean %d items",__func__, clean);
}
*/

/******************************************************
 * Cache value (variant + time) to history converter  *
 * ***************************************************/
static int glb_cache_value_to_hist_copy(zbx_history_record_t *record, 
                        glb_cache_item_value_t *c_val, unsigned char value_type) {

    switch (value_type) {
    case ITEM_VALUE_TYPE_FLOAT:
        record->value.dbl = c_val->value.data.dbl;
        break;
    
    case ITEM_VALUE_TYPE_UINT64:
        record->value.ui64 = c_val->value.data.ui64;
        break;
    
    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT: //TODO: get rid of the allocation!!!! we can just use the copy from the cache
        if ( NULL != c_val->value.data.str ) {
            record->value.str = zbx_strdup(NULL,c_val->value.data.str);
        }
        else 
            record->value.str = zbx_strdup(NULL,"");
        break;
    
    case ITEM_VALUE_TYPE_LOG: 
        record->value.log = zbx_malloc(NULL, sizeof (zbx_log_t));
        bzero(record->value.log,sizeof (zbx_log_t));
            
        if ( NULL != c_val->value.data.str ) 
            record->value.log->value = zbx_strdup(NULL,c_val->value.data.str);
        else 
            record->value.log->value = zbx_strdup(NULL,"");
        break;
            
    case ITEM_VALUE_TYPE_NONE: //TODO: get rid of the allocation!!!! we can just use the copy from the cache
        if ( NULL != c_val->value.data.err) 
            record->value.err = zbx_strdup(NULL,c_val->value.data.err);
         else 
            record->value.err = NULL;
        break;
     
    default:
        zabbix_log(LOG_LEVEL_WARNING,"Unknown value type %d", value_type);
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);

    }

    record->timestamp.sec = c_val->time_sec;
} 




int glb_cache_hist_val_to_value(glb_cache_item_value_t *cache_val, history_value_t *hist_val, unsigned char value_type, items_cfg_t *i_cfg) {
    
    glb_cache_item_variant_clear(&cache_val->value);

    switch (value_type) {
        case ITEM_VALUE_TYPE_FLOAT:
            zbx_variant_set_dbl(&cache_val->value, hist_val->dbl);
            break;
    
        case ITEM_VALUE_TYPE_UINT64: 
            zbx_variant_set_ui64(&cache_val->value, hist_val->ui64);
            break;
    
        case ITEM_VALUE_TYPE_STR:
        case ITEM_VALUE_TYPE_TEXT: {
            char *tmp_str = hist_val->str;
            if (NULL == hist_val->str) tmp_str="";
            
            int len = strlen(hist_val->str)+1;
            char *str = glb_cache_malloc(NULL, len);
    
            memcpy(str,tmp_str, len);
            zbx_variant_set_str(&cache_val->value, str);
            break;
        }
        case ITEM_VALUE_TYPE_LOG: {
            if ( NULL != hist_val->log) { 
    
                int len = strlen(hist_val->log->value)+1;
                char *str = glb_cache_malloc(NULL, len);

                memcpy(str, hist_val->log->value, len);
                zbx_variant_set_str(&cache_val->value, str);

            } else {
                zbx_variant_set_str(&cache_val->value, "");
            }
            break;
        }

        case ITEM_VALUE_TYPE_NONE: 
            if ( NULL != hist_val->err) {
                int len = strlen(hist_val->err)+1;
                char *str = glb_cache_malloc(NULL, len);
                memcpy(str,hist_val->err, len);
                zbx_variant_set_error(&cache_val->value, str);
            }
            break;
        
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);       
    }
}



int glb_cache_dc_hist_to_value(glb_cache_item_value_t *cache_val, ZBX_DC_HISTORY *hist_v) {
    
    glb_cache_item_variant_clear(&cache_val->value);
    
    switch (hist_v->value_type) {
        case ITEM_VALUE_TYPE_FLOAT:
            zbx_variant_set_dbl(&cache_val->value, hist_v->value.dbl);
            break;
    
        case ITEM_VALUE_TYPE_UINT64: 
            zbx_variant_set_ui64(&cache_val->value, hist_v->value.ui64);
            break;
    
        case ITEM_VALUE_TYPE_STR:
        case ITEM_VALUE_TYPE_TEXT: {
            char *tmp_str = hist_v->value.str;
            if (NULL == hist_v->value.str) tmp_str="";
            
            int len = strlen(hist_v->value.str)+1;
            char *str = glb_cache_malloc(NULL, len);
    
            memcpy(str,tmp_str, len);
            zbx_variant_set_str(&cache_val->value, str);
            break;
        }
        case ITEM_VALUE_TYPE_LOG: {
            if ( NULL != hist_v->value.log) { 
    
                int len = strlen(hist_v->value.log->value)+1;
                char *str = glb_cache_malloc(NULL, len);

                memcpy(str, hist_v->value.log->value, len);
                zbx_variant_set_str(&cache_val->value, str);

            } else {
                zbx_variant_set_str(&cache_val->value, "");
            }
            break;
        }

        case ITEM_VALUE_TYPE_NONE: 
            if ( NULL != hist_v->value.err) {
                int len = strlen(hist_v->value.err)+1;
                char *str = glb_cache_malloc(NULL, len);
                memcpy(str,hist_v->value.err, len);
                zbx_variant_set_error(&cache_val->value, str);
            }
            break;
        
        default:
            LOG_WRN("Wrong data type %d", hist_v->value_type);
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);       
    }
    cache_val->time_sec = hist_v->ts.sec;
}

//TODO: maximum demand limits
int item_update_demand(item_elem_t *elm, unsigned int new_count, unsigned int new_period, unsigned int now) {
    unsigned char need_hk=0;
    item_demand_t *demand = &elm->demand;
    
    if ( demand->count < new_count ) {
        LOG_DBG("GLB_CACHE: demand by count updated: %d -> %d", 
            demand->count, new_count);
        demand->count = new_count;
        demand->new_count = 0;
        demand->count_change = now;
        need_hk = 1;
    } else if (demand->new_count < new_count) 
        demand->new_count = new_count;
    
    if (now - demand->count_change > GLB_CACHE_ITEM_DEMAND_UPDATE) {
        demand->count_change = now;
        demand->count = demand->new_count;
        demand->new_count = 0;
        need_hk = 1;
    }
    
    //checking and updating the time limits
    if (demand->period < new_period ) {
        demand->period = new_period;
        demand->new_period = 0;
        demand->period_change = now;
        need_hk = 1;
    } else if (demand->new_period < new_period) 
        demand->new_period = new_period;
    
    if (now - demand->period_change > GLB_CACHE_ITEM_DEMAND_UPDATE) {
        demand->period_change = now;
        demand->period = demand->new_period;
        demand->new_period = 0;
        need_hk = 1;
    }

  //  if (need_hk) 
  //      glb_cache_housekeep_values(elm);
  
    return SUCCEED;    
}

static int glb_cache_fetch_from_db_by_count(u_int64_t itemid, item_elem_t *elm, int count, int head_time, int now, items_cfg_t *i_cfg) {
    int ret = FAIL, i;
    glb_cache_item_value_t *c_val;
    zbx_vector_history_record_t	values;


    if (elm->db_fetched_time > now) 
        elm->db_fetched_time = now;

    DEBUG_ITEM(itemid, "GLB_CACHE: will be fetching by count from the DB, elem's db_fetched_time is %d, head time is %d, count is %d", elm->db_fetched_time,head_time, count);

   
    if ( elm->db_fetched_time < head_time-1 ) {
        DEBUG_ITEM(itemid, "fetched time: %d, requested head time: %d",elm->db_fetched_time, head_time);
        head_time = elm->db_fetched_time-1;
        //return SUCCEED;
    }

    zbx_history_record_vector_create(&values);
     
    if (SUCCEED == ( ret = glb_history_get(itemid, elm->value_type, 0, count, head_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values))) {
        DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching by count from DB SUCCEED, %d values",itemid, values.values_num);
  
        if (values.values_num > 0 ) {
            elm->db_fetched_time = MIN(elm->db_fetched_time, values.values[0].timestamp.sec);

            //adding the fetched items to the tail of the cache
            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
            history_value_t hist_v;

            for (i = 0; i < values.values_num; i++) {
                if (NULL != (c_val = glb_tsbuff_add_to_tail(&elm->tsbuff, values.values[i].timestamp.sec)))
                    glb_cache_hist_val_to_value(c_val, &values.values[i].value, elm->value_type, i_cfg);
            }
        } else //this isn't precise, but will prevent consecutive calls
            elm->db_fetched_time = head_time; //we don't want requests with the same to be repeated anymore, so shifting one second to past

        zbx_history_record_vector_destroy(&values, elm->value_type);
        return SUCCEED;

    } 
    
    DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB FAILED",itemid);
    zbx_history_record_vector_destroy(&values, elm->value_type);    
    return FAIL;
}
#define MIN_GROW_COUNT 8
#define GROW_PERCENT 120

static int calc_grow_buffer_size( int old_size) {
    int new_size = old_size * GROW_PERCENT / 100;
    
    if ((new_size - old_size) < MIN_GROW_COUNT) 
        new_size = old_size + MIN_GROW_COUNT;
    
    return new_size;
}


static int ensure_cache_demand_count_met(item_elem_t *elm, int count) {
    if (elm->demand.count <=count ) 
        return SUCCEED;
    return FAIL;
}
static int ensure_cache_demand_time_met(item_elem_t *elm, int c_time) {
    if (elm->demand.period <= ( time(NULL) - c_time ) ) 
        return SUCCEED;
    return FAIL;
}

static int ensure_tsbuff_has_space(item_elem_t *elm) {
    glb_cache_item_value_t *c_val;

    if (SUCCEED == glb_tsbuff_is_full(&elm->tsbuff)) {

        c_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, glb_tsbuff_index(&elm->tsbuff, elm->tsbuff.tail + 1 ));

        if (SUCCEED == ensure_cache_demand_count_met(elm, glb_tsbuff_get_count(&elm->tsbuff)-1 ) &&
            SUCCEED == ensure_cache_demand_time_met(elm, c_val->time_sec) ) {
                        
            c_val = glb_tsbuff_get_value_tail(&elm->tsbuff);
        
            glb_cache_item_variant_clear(&c_val->value);
            glb_tsbuff_free_tail(&elm->tsbuff);
            
        } else {
            int new_size = calc_grow_buffer_size(glb_tsbuff_get_size(&elm->tsbuff));
            return glb_tsbuff_resize(&elm->tsbuff,new_size, glb_cache_malloc, glb_cache_free, NULL);
        }    
    }
    return SUCCEED;
}


static int glb_cache_fetch_from_db_by_time(u_int64_t itemid, item_elem_t *elm, int seconds, int head_time,  int now, items_cfg_t *i_cfg) {
    int ret = FAIL, i;
    glb_cache_item_value_t *c_val;
    zbx_vector_history_record_t	values;

    if ( elm->db_fetched_time < (now - seconds) ) {
        
        DEBUG_ITEM(itemid, "CACHE: no reason to fetch data from the history storage at %d, has already requested from %d",
                 now - seconds, elm->db_fetched_time);
        return SUCCEED;
    }
    
    zbx_history_record_vector_create(&values);
    DEBUG_ITEM(itemid, "GLB_CACHE: updating demand to %d seconds", MAX(0, seconds));
 
     
    if (elm->db_fetched_time > now) 
        elm->db_fetched_time = now;

    if (SUCCEED == ( ret = glb_history_get(itemid, elm->value_type, head_time - seconds , 0, 
               elm->db_fetched_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values))) {
    
        DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB SUCCEED, %d values",itemid, values.values_num);

        if (values.values_num > 0 ) {
            elm->db_fetched_time = MIN(elm->db_fetched_time, values.values[0].timestamp.sec);
            //adding the fetched items to the tail of the cache
            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
            history_value_t hist_v;

            for (i = 0; i < values.values_num; i++) {
                
                ensure_tsbuff_has_space(elm);

                if (NULL != (c_val = glb_tsbuff_add_to_tail(&elm->tsbuff, values.values[i].timestamp.sec))) {
                    DEBUG_ITEM(itemid, "Added value from the history to the cache with timestamp %d",  values.values[i].timestamp.sec);
                    glb_cache_hist_val_to_value(c_val, &values.values[i].value, elm->value_type, i_cfg);
                } else {
                    DEBUG_ITEM(itemid, "WARN: COULDN'T add item with timestamp %d",  values.values[i].timestamp.sec);
                }
            } 

        } else //this isn't precise, but will prevent consecutive calls
            elm->db_fetched_time = head_time - seconds; //we don't want requests with the same to be repeated anymore, so shifting one second to past

        zbx_history_record_vector_destroy(&values, elm->value_type);
        return SUCCEED;
    } 
    
    DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB FAILED",itemid);
    zbx_history_record_vector_destroy(&values, elm->value_type);    
    return FAIL;
} 

static int fill_items_values_by_index(item_elem_t *elm, int tail_idx, int head_idx, zbx_vector_history_record_t *values) {
    int i, iter = 0;
    glb_cache_item_value_t *item_val;
    zbx_history_record_t	record;

    if (0 > head_idx || 0 > tail_idx) {
        LOG_WRN("improper head/tail indexes, FAILing, this is a programming bug ");
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }
    
    zbx_vector_history_record_clear(values);
    
    for (i=tail_idx; iter == 0 || i != glb_tsbuff_index(&elm->tsbuff, head_idx +1 ); i = glb_tsbuff_index(&elm->tsbuff, i + 1 )) {
        iter++;
//        LOG_INF("GLB_CACHE: copy item idx %d", i);          
	    item_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, i); 
        glb_cache_value_to_hist_copy(&record, item_val, elm->value_type);
//        LOG_INF("GLB_CACHE: copy item idx %d completed", i);          
        zbx_vector_history_record_append_ptr(values, &record);
//        LOG_INF("GLB_CACHE: add record ptr completed");   
    }
  
    return i;
}

static int fill_items_values_by_count(item_elem_t *elm, int count, int head_idx, zbx_vector_history_record_t *values) {

    int tail_idx = glb_tsbuff_index(&elm->tsbuff, head_idx - count + 1 );

    return fill_items_values_by_index(elm, tail_idx, head_idx, values);
}


static int count_hit(items_cfg_t *i_cfg) {
    i_cfg->stats.hits++;
}
static int count_miss(items_cfg_t *i_cfg) {
    i_cfg->stats.misses++;
}
static int count_db_requests(items_cfg_t *i_cfg) {
    i_cfg->stats.db_requests++;
}
static int count_db_fails(items_cfg_t *i_cfg) {
    i_cfg->stats.db_fails++;
}
static int count_fails(items_cfg_t *i_cfg) {
    i_cfg->stats.fails++;
}


static int update_db_fetch(item_elem_t *elm, int fetch_time) {
    if (elm->db_fetched_time > fetch_time) 
            elm->db_fetched_time = fetch_time;
}


static int cache_fetch_count_cb(glb_cache_elem_t *elem, void *req_data) {
    LOG_DBG("In %s:starting ", __func__);

    fetch_req_t *req = (fetch_req_t*)req_data;
    item_elem_t *elm = ( item_elem_t *)elem->data;
    int head_idx = -1, need_count = 0;
    
    
  //  LOG_INF("Fetching by count");
   // glb_tsbuff_dump(&elm->tsbuff);

    int cache_start =  glb_tsbuff_get_time_head(&elm->tsbuff);
    int now = time(NULL);

    if (cache_start < req->ts_head ) 
        head_idx = elm->tsbuff.head;
    else
        head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);
      
   // LOG_INF("Head idx is %d, request head is %d, count is %d", head_idx, req->ts_head, req->count);
   // LOG_INF("Tsbuff: tail idx: %d, head idx: %d, size: %d, cache start is %d ",elm->tsbuff.tail, elm->tsbuff.head, glb_tsbuff_get_size(&elm->tsbuff), cache_start);
  
    if ( -1 < head_idx  &&  SUCCEED == glb_tsbuff_check_has_enough_count_data_idx(&elm->tsbuff, req->count, head_idx))  {
     //   LOG_INF("has enough data in the cache");
        count_hit(req->i_cfg);
        fill_items_values_by_count(elm, req->count, head_idx, req->values);
        DEBUG_ITEM(elem->id, "filled %d values from the cache", req->values->values_num);
        return SUCCEED;
    }
    
    //LOG_INF("Cache miss");
    count_miss(req->i_cfg);
    
    if (-1 == head_idx) {
      //  LOG_INF("will do request by time to fullfill now->head_time");
        
        count_db_requests(req->i_cfg);
        
        DEBUG_ITEM(elem->id, "Requesting item from the DB by time %d->%d", req->ts_head, now);
        if (FAIL == glb_cache_fetch_from_db_by_time(req->itemid, elm, req->ts_head, now, now, req->i_cfg)) {
            count_db_fails(req->i_cfg);
            count_fails(req->i_cfg);
            return FAIL;
        }
        
        update_db_fetch(elm, req->ts_head);
        
    }

    head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);
    
    if (-1 == head_idx) {
        //there is still no data in the cache, we need to fetch all the data 
        need_count = req->count;
    } else //excluding data that fits from the count
        need_count = req->count - (head_idx - elm->tsbuff.tail + elm->tsbuff.size) % elm->tsbuff.size;

    //LOG_INF("Need to fetch %d items", need_count);
    
    if (need_count > 0) {
        count_db_requests(req->i_cfg);
        DEBUG_ITEM(elem->id, "Requesting item from the DB by count starting at %d,  %d items", req->ts_head, need_count);
        if ( SUCCEED != glb_cache_fetch_from_db_by_count(req->itemid, elm, need_count, req->ts_head, now, req->i_cfg )) {
            count_db_fails(req->i_cfg);
            count_fails(req->i_cfg);
            return FAIL;
        }
        update_db_fetch( elm, req->ts_head );
    }
    //LOG_INF("Still need to fetch %d items", need_count);
       
    if (-1 == (head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head))) {
        count_fails(req->i_cfg);
        DEBUG_ITEM(elem->id, "Fetch from db hasn't retrun any data, cache+db request will fail");
        
        return FAIL;
    }

    if (FAIL == glb_tsbuff_check_has_enough_count_data_idx(&elm->tsbuff, req->count, head_idx)) {
        //LOG_INF("Still has not enough data after db fetch");
        count_fails(req->i_cfg);
        DEBUG_ITEM(elem->id, "Fetch from db hasn't retrun enough data, cache+db request will fail");
        
        return FAIL;
    }
    fill_items_values_by_count(elm, req->count, head_idx, req->values);
    
    DEBUG_ITEM(elem->id, "Fetch from db successful, filled %d values", req->count);
    
    if (req->values->values_num > 0)
        update_db_fetch(elm, req->values->values[0].timestamp.sec);

    return SUCCEED;
}

static int cache_fetch_time_cb(glb_cache_elem_t *elem, void *req_data) {
    LOG_DBG("%s: started",__func__);
    
    fetch_req_t *req = (fetch_req_t*)req_data;
    item_elem_t *elm = ( item_elem_t *)elem->data;
    
    int head_idx = -1, tail_idx = -1;
    
    int fetch_head_time = -1, now = time(NULL);
       
    int cache_start =  glb_tsbuff_get_time_head(&elm->tsbuff);
    head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff,req->ts_head);
    
    tail_idx = glb_tsbuff_find_time_idx(&elm->tsbuff,req->ts_head - req->seconds);

    item_update_demand(elm, 0, MAX(0, req->seconds), now);
    DEBUG_ITEM(elem->id, "Cache stats oldest time(relative):%d, most recent time(relative):%d, total values:%d, cache size: %d",
        now - glb_tsbuff_get_time_tail(&elm->tsbuff), now - glb_tsbuff_get_time_head(&elm->tsbuff), glb_tsbuff_get_count(&elm->tsbuff), 
        glb_tsbuff_get_size(&elm->tsbuff));
					
    if (-1 !=head_idx && -1 != tail_idx) {
        DEBUG_ITEM(elem->id, "Filling from the cache");
        count_hit(req->i_cfg);
        fill_items_values_by_index(elm, tail_idx, head_idx, req->values);
        DEBUG_ITEM(elem->id, "Filled from the cache %d items", req->values->values_num);
        return SUCCEED;
    }
    
    DEBUG_ITEM(elem->id, "NO/NOT ENOUGH data in the cache, requesting from the db");
   
    count_miss(req->i_cfg);
    
    if (glb_tsbuff_get_count(&elm->tsbuff) > 0) 
        fetch_head_time = glb_tsbuff_get_time_tail(&elm->tsbuff) - 1;
    else   
        fetch_head_time = now;
    
    count_db_requests(req->i_cfg);

    if ( SUCCEED != glb_cache_fetch_from_db_by_time(req->itemid, elm, req->seconds, fetch_head_time, now, req->i_cfg )) {
        DEBUG_ITEM(elem->id, "DB FAILED to return data required, cache request will FAIL");
        count_fails(req->i_cfg);
        count_db_fails(req->i_cfg);    
        return FAIL;
    }
    update_db_fetch( elm, req->ts_head );
    
    //looking for start/end againg
    if (req->ts_head >= glb_tsbuff_get_time_head(&elm->tsbuff)) {
        head_idx = elm->tsbuff.head;
    } else 
        head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff,req->ts_head);
    
    tail_idx = glb_tsbuff_find_time_idx(&elm->tsbuff,now - req->seconds);
    if (-1 == tail_idx)
        tail_idx = elm->tsbuff.tail;
    
    if (-1 != tail_idx && -1 != head_idx) {        
        fill_items_values_by_index(elm, tail_idx, head_idx, req->values);
        DEBUG_ITEM(elem->id, "DB request successful, data cached and filled %d values", glb_tsbuff_get_count(&elm->tsbuff));
    }
    
    return SUCCEED;
}

int glb_cache_get_item_values_by_time(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type, 
    zbx_vector_history_record_t *values, int seconds, int ts_head) {
  
    fetch_req_t req = {.itemid = itemid, .value_type = value_type, .values = values, .seconds = seconds, .ts_head = ts_head, .i_cfg = (items_cfg_t*)cfg };
    
    return glb_cache_process_elem(items, itemid, cache_fetch_time_cb, &req);
}

int glb_cache_get_item_values_by_count(void *cfg, glb_cache_elems_t *items, uint64_t itemid, int value_type, 
    zbx_vector_history_record_t *values, int count, int ts_head) {
    
    fetch_req_t req = { .itemid = itemid, .count = count, .value_type = value_type, .values = values, .ts_head = ts_head, .i_cfg = (items_cfg_t*)cfg };
   
    return glb_cache_process_elem(items, itemid, cache_fetch_count_cb, &req);
}
   
int glb_cache_item_create_cb ( glb_cache_elem_t *elem, void *items_cfg) {
    
   // LOG_INF("In %s",__func__);   
    items_cfg_t *i_cfg = (items_cfg_t*)items_cfg;
   // LOG_INF("In %s 0.5",__func__);
   // zbx_mem_malloc_func_t alloc_func = *i_cfg->alloc_func;
    
   // LOG_INF("In %s 1 func addr:%ld",__func__,alloc_func);
    if (NULL == (elem->data = glb_cache_malloc(NULL, sizeof(item_elem_t)))) {
        
        return FAIL;
    }
   // LOG_INF("In %s1.5",__func__);
    item_elem_t *i_data = (item_elem_t*) elem->data;
    //LOG_INF("In %s2",__func__);
    
    bzero(i_data, sizeof(item_elem_t));
    
    //LOG_INF("In %s3",__func__);
    
    i_data->last_accessed = time(NULL);
    i_data->db_fetched_time = ZBX_JAN_2038;
    
    //LOG_INF("In %s4",__func__);
    //i_data->value_type = glb_dc_get_item_value_type(elem->id);
    i_data->value_type = ITEM_VALUE_TYPE_NONE;
    i_data->meta.state = ITEM_STATE_UNKNOWN;
    //LOG_INF("In %s5",__func__);
    glb_tsbuff_init(&i_data->tsbuff, GLB_CACHE_MIN_ITEM_VALUES, sizeof(glb_cache_item_value_t), glb_cache_malloc);
    
    return SUCCEED;

}

static void free_item(item_elem_t *elm, items_cfg_t* i_cfg) {
    glb_cache_item_value_t *cache_val;
    //zbx_mem_free_func_t free_func = *i_cfg->free_func;

    while (NULL != ( cache_val =(glb_cache_item_value_t*) glb_tsbuff_get_value_tail(&elm->tsbuff))) {
        glb_cache_item_variant_clear(&cache_val->value );
        glb_tsbuff_free_tail(&elm->tsbuff);
    }

    glb_tsbuff_destroy(&elm->tsbuff, glb_cache_free);
    glb_cache_free(elm);

}




int add_value_cb(glb_cache_elem_t *elem, void *cb_data) {
    glb_cache_item_value_t *c_val;
    item_elem_t *elm = (item_elem_t *)elem->data;
    items_cb_params_t *params = (items_cb_params_t*)cb_data;
    items_cfg_t *i_cfg = params->i_cfg;
    ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)params->data;

    
    if (elm->value_type == ITEM_VALUE_TYPE_NONE) 
        elm->value_type = h->value_type;
    
    else if ( elm->value_type != h->value_type) {
        items_cb_params_t cb_data;
        LOG_INF("Resetting value type %d -> %d ", elm->value_type, h->value_type);
        
        free_item(elm,i_cfg);
       
        cb_data.i_cfg = i_cfg;
        cb_data.data = NULL;

        glb_cache_item_create_cb(elem, i_cfg);

        elm = (item_elem_t *)elem->data;
        elm->value_type = h->value_type;   
    } else {
        ensure_tsbuff_has_space(elm);
    }
    
    if ( NULL != (c_val = (glb_cache_item_value_t*)glb_tsbuff_add_to_head(&elm->tsbuff, h->ts.sec))) {
        //LOG_INF("Returned cache val %ld",c_val);
        glb_cache_dc_hist_to_value(c_val, h);
        DEBUG_ITEM(elem->id, "Item added, oldest timestamp is %d", glb_tsbuff_get_time_tail(&elm->tsbuff));

    } else {
        LOG_WRN("Cannot add value for item %ld with timestamp %d to the cache", h->itemid, h->ts.sec);
        return FAIL;
    }
    
    return SUCCEED;
}


int  glb_cache_add_item_values(void *cfg_data, glb_cache_elems_t *elems, ZBX_DC_HISTORY *history, int history_num) {

    items_cfg_t *cfg = (items_cfg_t* )cfg_data;
    items_cb_params_t cb_params = { .i_cfg = cfg};
    int i, ret = SUCCEED;
    
    LOG_DBG("In %s: starting adding new history %d values", __func__, history_num);

    for (i = 0; i < history_num; i++)
	{
		ZBX_DC_HISTORY	*h;
		h = (ZBX_DC_HISTORY *)&history[i];
        
        LOG_DBG("Adding value to cache id %ld, value %d out of %d, timestamp is %d",h->itemid,i,history_num, h->ts.sec);

        if (0 != ((ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF ) & h->flags) || ITEM_STATE_NOTSUPPORTED == h->state ) {
            LOG_DBG("GLB_CACHE: not adding item %ld to cache: no_hist flag is set %d", h->itemid, h->flags);
            continue;
        }

        cb_params.data = h;    

        if ( FAIL == glb_cache_process_elem(elems, h->itemid, add_value_cb, &cb_params) ) {
            ret=FAIL;
        };
        DEBUG_ITEM(h->itemid, "Adding value timestamp %d to the cache", h->ts.sec);
    }

    return ret; 
}   

/************************************************************
 * update item's metadata 
 * **********************************************************/
int glb_cache_item_update_meta_cb(glb_cache_elem_t *elem, void *cb_data) {
    
    item_elem_t *elm = (item_elem_t *)elem->data;
    glb_cache_meta_udpate_req_t *req = (glb_cache_meta_udpate_req_t *)cb_data;
            
    if ((req->flags & GLB_CACHE_ITEM_UPDATE_LASTDATA) && elm->meta.lastdata < req->meta->lastdata) {
        elm->meta.lastdata = req->meta->lastdata;
        DEBUG_ITEM(elem->id, "Updated metadata: lastdata");
    }
            
    if (req->flags & GLB_CACHE_ITEM_UPDATE_NEXTCHECK) {
        elm->meta.nextcheck = req->meta->nextcheck;    
        DEBUG_ITEM(elem->id, "Updated metadata: nextcheck");     
    }
    if (req->flags & GLB_CACHE_ITEM_UPDATE_STATE) {
         elm->meta.state = req->meta->state;
        DEBUG_ITEM(elem->id, "Updated metadata: state");     
    }
    if (req->flags & GLB_CACHE_ITEM_UPDATE_ERRORMSG) {
        elm->meta.error = glb_cache_strpool_set_str(elm->meta.error, req->meta->error);
        DEBUG_ITEM(elem->id, "Updated metadata: errmsg");     
    }
   
    return SUCCEED; 
}

int glb_cache_item_get_meta_cb(glb_cache_elem_t *elem, void *cb_data) {
    
    item_elem_t *elm = (item_elem_t *)elem->data;
    glb_cache_meta_udpate_req_t *req = (glb_cache_meta_udpate_req_t *)cb_data;
    static glb_cache_item_meta_t meta = {0};

    meta.lastdata = elm->meta.lastdata;
    meta.nextcheck = elm->meta.nextcheck;
    meta.state = elm->meta.state;   
    
    glb_cache_strpool_release(meta.error);
    meta.error = glb_cache_strpool_acquire(elm->meta.error);

    req->meta = &meta;
    return SUCCEED;
} 
    
int   glb_cache_item_get_state_cb(glb_cache_elem_t *elem, void *cb_data) {
      item_elem_t *elm = (item_elem_t *)elem->data;
      
      return elm->meta.state;
}

int   glb_cache_item_get_nextcheck_cb(glb_cache_elem_t *elem, void *cb_data) {
      item_elem_t *elm = (item_elem_t *)elem->data;
      
      return elm->meta.nextcheck;
}


#define ZBX_DC_FLAGS_NOT_FOR_HISTORY	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)

int add_last_value_json_cb ( ){

}

int glb_cache_item_values_json_cb(glb_cache_elem_t *elem, void *cb_data)
{
    item_last_values_req_t *req = (item_last_values_req_t *)cb_data;
    item_elem_t *elm = (item_elem_t *)elem->data;
    int rcount;

    zbx_json_addobject(req->json, NULL);
    zbx_json_adduint64(req->json, "itemid", elem->id);
    zbx_json_addarray(req->json, "values");

    if (req->count > glb_tsbuff_get_count(&elm->tsbuff))
        rcount = glb_tsbuff_get_count(&elm->tsbuff);
    else
        rcount = req->count;

    int head_idx = elm->tsbuff.head;
   
    if (-1 < head_idx)
    {
        int tail_idx = (elm->tsbuff.head - rcount + 1 + glb_tsbuff_get_size(&elm->tsbuff)) % glb_tsbuff_get_size(&elm->tsbuff);
        LOG_DBG("Processing item %d: rcount is %d requst count is %d, %d->%d", elem->id, rcount, req->count, tail_idx, head_idx);
        
        do
        {
            glb_cache_item_value_t *c_val;
            c_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, head_idx);

            zbx_json_addobject(req->json, NULL);
            zbx_json_adduint64(req->json, "clock", c_val->time_sec);

            switch (elm->value_type)
            {

            case ITEM_VALUE_TYPE_TEXT:
            case ITEM_VALUE_TYPE_STR:
                zbx_json_addstring(req->json, "value", c_val->value.data.str, ZBX_JSON_TYPE_STRING);
                break;

            case ITEM_VALUE_TYPE_LOG:
                zbx_json_addstring(req->json, "value", c_val->value.data.str, ZBX_JSON_TYPE_STRING);
                break;

            case ITEM_VALUE_TYPE_FLOAT:
                zbx_json_addfloat(req->json, "value", c_val->value.data.dbl);
                break;

            case ITEM_VALUE_TYPE_UINT64:
                zbx_json_adduint64(req->json, "value", c_val->value.data.ui64);
                break;

            default:
                THIS_SHOULD_NEVER_HAPPEN;
                exit(-1);
            }

            head_idx = (head_idx - 1 + glb_tsbuff_get_size(&elm->tsbuff)) % glb_tsbuff_get_size(&elm->tsbuff);
            rcount--;

            zbx_json_close(req->json); // closing one value
        } while (rcount > 0);
    }
    zbx_json_close(req->json); // closing the values array of an item
    zbx_json_close(req->json); // closing the item object
}
