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
#include "zbxvariant.h"
#include "zbx_item_constants.h"
#include "glb_history.h"
#include "zbxcacheconfig.h"

#include "glb_state.h"
#include "glb_state_items.h"
#include <zlib.h>
#include "glb_lock.h"
#include "load_dump.h"

extern char	*CONFIG_VCDUMP_LOCATION;

typedef struct
{
    int count;
    int period;

    int count_change;
    int period_change;

    int new_count;
    int new_period;
} item_demand_t;

typedef struct
{
    glb_state_item_meta_t meta;
    glb_tsbuff_t tsbuff;
    item_demand_t demand;

    unsigned char value_type;
    int last_accessed;
    int db_fetched_time;
    int db_fetched_count;

} item_elem_t;

typedef struct
{
    int hits;
    int misses;
    int db_requests;
    int db_fails;
    int fails;
} stats_t;

typedef struct
{
    int time_sec;
    zbx_variant_t value;
} glb_state_item_value_t;

typedef struct
{
    u_int64_t itemid;
    int count;
    int value_type;
    int ts_head;
    int seconds;
    zbx_vector_history_record_t *values;
} fetch_req_t;

typedef struct {
    elems_hash_t *items;
    strpool_t strpool;
    mem_funcs_t memf;
    stats_t stats;
} items_state_t;

static items_state_t *state = NULL; 

typedef struct {
    glb_state_item_meta_t *meta;
    u_int64_t flags;
    unsigned char value_type;
} glb_state_meta_udpate_req_t; 

typedef struct {
    int count;
    struct zbx_json *json;
} item_last_values_req_t;

#define GLB_CACHE_ITEMS_MAX_DURATION  2*86400 
#define GLB_CACHE_ITEMS_INIT_SIZE   8192
#define GLB_CACHE_ITEM_MIN_VALUES   10 //reduce to 2 if too much mem will be used

#define GLB_CACHE_DEMAND_UPDATE 86400 //daily demand update is a good thing
#define GLB_CACHE_MIN_DURATION 2 //duration up to this is ignored and considered set to 0
#define GLB_CACHE_DEFAULT_DURATION 2 * 86400 //by default we downloading two days worth of data
                                            //however if duration is set, this might be bigger
#define GLB_CACHE_ITEM_DEMAND_UPDATE 86400

ELEMS_CREATE(item_create_cb)
{
    if (NULL == (elem->data = memf->malloc_func(NULL, sizeof(item_elem_t))))
        return FAIL;

    item_elem_t *i_data = (item_elem_t *)elem->data;

    bzero(i_data, sizeof(item_elem_t));

    i_data->last_accessed = time(NULL);
    i_data->db_fetched_time = ZBX_JAN_2038;
    i_data->value_type = ITEM_VALUE_TYPE_NONE;
    i_data->meta.state = ITEM_STATE_UNKNOWN;
    glb_tsbuff_init(&i_data->tsbuff, GLB_CACHE_ITEM_MIN_VALUES, sizeof(glb_state_item_value_t), memf->malloc_func);
    
    return SUCCEED;
}



static void item_variant_clear(zbx_variant_t *value)
{
    switch (value->type)
    {
    case ZBX_VARIANT_STR:
        state->memf.free_func(value->data.str);
        break;
    case ZBX_VARIANT_BIN:
        state->memf.free_func(value->data.bin);
        break;
    case ZBX_VARIANT_ERR:
        state->memf.free_func(value->data.err);
        break;
    case ZBX_VARIANT_DBL_VECTOR:
        zbx_vector_dbl_destroy(value->data.dbl_vector);
        state->memf.free_func(value->data.dbl_vector);
        break;
    }
    value->type = ZBX_VARIANT_NONE;
}


ELEMS_FREE(item_free_cb) {
    item_elem_t *elm = elem->data;
    strpool_free(&state->strpool, elm->meta.error);
    
    while(glb_tsbuff_get_count(&elm->tsbuff) > 0 ){
        glb_state_item_value_t *c_val = glb_tsbuff_get_value_tail(&elm->tsbuff);
        item_variant_clear(&c_val->value);
        glb_tsbuff_free_tail(&elm->tsbuff);
    }

    glb_tsbuff_destroy(&elm->tsbuff, memf->free_func);
    return SUCCEED;
}


int glb_state_items_init(mem_funcs_t *memf)
{
    if (NULL == (state = memf->malloc_func(NULL, sizeof(items_state_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
    state->items = elems_hash_init(memf,item_create_cb, item_free_cb );
    state->memf = *memf;
    strpool_init(&state->strpool, memf);
    
  return SUCCEED;
}
/*******************************************************
 * cleans and marks unused outdated items
 * that exceeds the demand
 * *****************************************************/
static void housekeep_item_values(item_elem_t *item_elem)
{
}
/******************************************************
 * Cache value (variant + time) to history converter  *
 * ***************************************************/
static int glb_state_value_to_hist_copy(zbx_history_record_t *record,
                                        glb_state_item_value_t *c_val, unsigned char value_type)
{

    switch (value_type)
    {
    case ITEM_VALUE_TYPE_FLOAT:
        record->value.dbl = c_val->value.data.dbl;
        break;

    case ITEM_VALUE_TYPE_UINT64:
        record->value.ui64 = c_val->value.data.ui64;
        break;

    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT: // TODO: get rid of the allocation!!!! we can just use the copy from the cache
        if (NULL != c_val->value.data.str)
        {
            record->value.str = zbx_strdup(NULL, c_val->value.data.str);
        }
        else
            record->value.str = zbx_strdup(NULL, "");
        break;

    case ITEM_VALUE_TYPE_LOG:
        record->value.log = zbx_malloc(NULL, sizeof(zbx_log_t));
        bzero(record->value.log, sizeof(zbx_log_t));

        if (NULL != c_val->value.data.str)
            record->value.log->value = zbx_strdup(NULL, c_val->value.data.str);
        else
            record->value.log->value = zbx_strdup(NULL, "");
        break;

    case ITEM_VALUE_TYPE_NONE: // TODO: get rid of the allocation!!!! we can just use the copy from the cache
        if (NULL != c_val->value.data.err)
            record->value.err = zbx_strdup(NULL, c_val->value.data.err);
        else
            record->value.err = NULL;
        break;

    default:
        zabbix_log(LOG_LEVEL_WARNING, "Unknown value type %d", value_type);
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }

    record->timestamp.sec = c_val->time_sec;
}

int hist_val_to_value(glb_state_item_value_t *cache_val, zbx_history_value_t *hist_val, int time_sec, unsigned char value_type)
{

    item_variant_clear(&cache_val->value);

    switch (value_type)
    {
    case ITEM_VALUE_TYPE_FLOAT:
        zbx_variant_set_dbl(&cache_val->value, hist_val->dbl);
        break;

    case ITEM_VALUE_TYPE_UINT64:
        zbx_variant_set_ui64(&cache_val->value, hist_val->ui64);
        break;

    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT:
    {
        char *tmp_str = hist_val->str;
        if (NULL == hist_val->str)
            tmp_str = "";

        int len = strlen(hist_val->str) + 1;
        char *str = state->memf.malloc_func(NULL, len);

        memcpy(str, tmp_str, len);
        zbx_variant_set_str(&cache_val->value, str);
        break;
    }
    case ITEM_VALUE_TYPE_LOG:
    {
        if (NULL != hist_val->log)
        {

            int len = strlen(hist_val->log->value) + 1;
            char *str = state->memf.malloc_func(NULL, len);

            memcpy(str, hist_val->log->value, len);
            zbx_variant_set_str(&cache_val->value, str);
        }
        else
        {
            zbx_variant_set_str(&cache_val->value, "");
        }
        break;
    }

    case ITEM_VALUE_TYPE_NONE:
        if (NULL != hist_val->err)
        {
            int len = strlen(hist_val->err) + 1;
            char *str = state->memf.malloc_func(NULL, len);
            memcpy(str, hist_val->err, len);
            zbx_variant_set_error(&cache_val->value, str);
        }
        break;

    default:
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }
    cache_val->time_sec = time_sec;
}

static int dc_hist_to_value(glb_state_item_value_t *cache_val, ZBX_DC_HISTORY *hist_v)
{

    item_variant_clear(&cache_val->value);

    switch (hist_v->value_type)
    {
    case ITEM_VALUE_TYPE_FLOAT:
        zbx_variant_set_dbl(&cache_val->value, hist_v->value.dbl);
        break;

    case ITEM_VALUE_TYPE_UINT64:
        zbx_variant_set_ui64(&cache_val->value, hist_v->value.ui64);
        break;

    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT:
    {
        char *tmp_str = hist_v->value.str;
        if (NULL == hist_v->value.str)
            tmp_str = "";

        int len = strlen(hist_v->value.str) + 1;
        char *str = state->memf.malloc_func(NULL, len);

        memcpy(str, tmp_str, len);
        zbx_variant_set_str(&cache_val->value, str);
        break;
    }
    case ITEM_VALUE_TYPE_LOG:
    {
        if (NULL != hist_v->value.log)
        {

            int len = strlen(hist_v->value.log->value) + 1;
            char *str = state->memf.malloc_func(NULL, len);

            memcpy(str, hist_v->value.log->value, len);
            zbx_variant_set_str(&cache_val->value, str);
        }
        else
        {
            zbx_variant_set_str(&cache_val->value, "");
        }
        break;
    }

    case ITEM_VALUE_TYPE_NONE:
        if (NULL != hist_v->value.err)
        {
            int len = strlen(hist_v->value.err) + 1;
            char *str = state->memf.malloc_func(NULL, len);
            memcpy(str, hist_v->value.err, len);
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

int item_update_demand(item_elem_t *elm, unsigned int new_count, unsigned int new_period, unsigned int now)
{
    unsigned char need_hk = 0;
    item_demand_t *demand = &elm->demand;

    if (demand->count < new_count)
    {
        LOG_DBG("GLB_CACHE: demand by count updated: %d -> %d",
                demand->count, new_count);
        demand->count = new_count;
        demand->new_count = 0;
        demand->count_change = now;
        need_hk = 1;
    }
    else if (demand->new_count < new_count)
        demand->new_count = new_count;

    if (now - demand->count_change > GLB_CACHE_ITEM_DEMAND_UPDATE)
    {
        demand->count_change = now;
        demand->count = demand->new_count;
        demand->new_count = 0;
        need_hk = 1;
    }

    if (demand->period < new_period)
    {
        demand->period = new_period;
        demand->new_period = 0;
        demand->period_change = now;
        need_hk = 1;
    }
    else if (demand->new_period < new_period)
        demand->new_period = new_period;

    if (now - demand->period_change > GLB_CACHE_ITEM_DEMAND_UPDATE)
    {
        demand->period_change = now;
        demand->period = demand->new_period;
        demand->new_period = 0;
        need_hk = 1;
    }

    //  if (need_hk)
    //      glb_state_housekeep_values(elm);

    return SUCCEED;
}

static int fetch_from_db_by_count(u_int64_t itemid, item_elem_t *elm, int count, int head_time, int now )
{
    int ret = FAIL, i;
    glb_state_item_value_t *c_val;
    zbx_vector_history_record_t values;

    if (elm->db_fetched_time > now)
        elm->db_fetched_time = now;

    // LOG_INF("GLB_CACHE: will be fetching itme %ld by count from the DB, elem's db_fetched_time is %d, db_fetched_count is %d, head time is %d, count is %d",
    //             itemid, elm->db_fetched_time, elm->db_fetched_count, head_time, count);

    // glb_tsbuff_dump(&elm->tsbuff);

    DEBUG_ITEM(itemid, "GLB_CACHE: will be fetching by count from the DB, elem's db_fetched_time is %d, db_fetched_count is %d, head time is %d, count is %d",
               elm->db_fetched_time, elm->db_fetched_count, head_time, count);

    if (elm->db_fetched_time < head_time && elm->db_fetched_count >= count)
    {
        LOG_DBG("No reason to fetch %d items, already fetched %d items", count, elm->db_fetched_count);
        DEBUG_ITEM(itemid, "No reason to fetch %d items, already fetched %d items", count, elm->db_fetched_count);
        return SUCCEED;
    }
    head_time = elm->db_fetched_time - 1;

    zbx_history_record_vector_create(&values);

    // LOG_INF("CACHE DB COUNT GET: itemid:%ld, head_time: %d, duration: %d", itemid, head_time, count);
    if (SUCCEED == (ret = glb_history_get_history(itemid, elm->value_type, 0, count, head_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values)))
    {
        // LOG_INF("GLB_CACHE: DB item %ld, fetching by count from DB SUCCEED, %d values", itemid, values.values_num);
        DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching by count from DB SUCCEED, %d values", itemid, values.values_num);
        elm->db_fetched_count = MAX(elm->db_fetched_count, count);

        if (values.values_num > 0)
        {
            elm->db_fetched_time = MIN(elm->db_fetched_time, values.values[0].timestamp.sec);

            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
            zbx_history_value_t hist_v;

            for (i = 0; i < values.values_num; i++)
            {
                if (NULL != (c_val = glb_tsbuff_add_to_tail(&elm->tsbuff, values.values[i].timestamp.sec)))
                    hist_val_to_value(c_val, &values.values[i].value, values.values[i].timestamp.sec, elm->value_type);
            }
        }
        else
            elm->db_fetched_time = head_time;

        zbx_history_record_vector_destroy(&values, elm->value_type);
        return SUCCEED;
    }

    DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB FAILED", itemid);
    zbx_history_record_vector_destroy(&values, elm->value_type);
    return FAIL;
}
#define MIN_GROW_COUNT 8
#define GROW_PERCENT 120

static int calc_grow_buffer_size(int old_size)
{
    int new_size = old_size * GROW_PERCENT / 100;

    if ((new_size - old_size) < MIN_GROW_COUNT)
        new_size = old_size + MIN_GROW_COUNT;

    return new_size;
}

static int ensure_cache_demand_count_met(item_elem_t *elm, int count)
{
    if (elm->demand.count <= count)
        return SUCCEED;
    return FAIL;
}

static int ensure_cache_demand_time_met(item_elem_t *elm, int c_time)
{
    if (elm->demand.period <= (time(NULL) - c_time))
        return SUCCEED;
    return FAIL;
}

static int ensure_tsbuff_has_space(item_elem_t *elm)
{
    glb_state_item_value_t *c_val;

    if (SUCCEED == glb_tsbuff_is_full(&elm->tsbuff))
    {
    
        c_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, glb_tsbuff_index(&elm->tsbuff, elm->tsbuff.tail + 1));

        if (SUCCEED == ensure_cache_demand_count_met(elm, glb_tsbuff_get_count(&elm->tsbuff) - 1) &&
            SUCCEED == ensure_cache_demand_time_met(elm, c_val->time_sec))
        {

            c_val = glb_tsbuff_get_value_tail(&elm->tsbuff);

            item_variant_clear(&c_val->value);
            glb_tsbuff_free_tail(&elm->tsbuff);
        }
        else
        {
            int new_size = calc_grow_buffer_size(glb_tsbuff_get_size(&elm->tsbuff));
            return glb_tsbuff_resize(&elm->tsbuff, new_size, state->memf.malloc_func, state->memf.free_func, NULL);
        }
    }
    return SUCCEED;
}


static int glb_state_fetch_from_db_by_time(u_int64_t itemid, item_elem_t *elm, int seconds, int head_time, int now)
{
    int ret = FAIL, i;
    glb_state_item_value_t *c_val;
    zbx_vector_history_record_t values;

    if (elm->db_fetched_time <= (head_time - seconds))
    {

        DEBUG_ITEM(itemid, "CACHE: no reason to fetch data from the history storage at %d, has already requested from %d",
                   head_time - seconds, elm->db_fetched_time);
        return SUCCEED;
    }
    else
    {
        DEBUG_ITEM(itemid, "Item has been fetched since %d, need %d", elm->db_fetched_time, head_time - seconds)
    }

    zbx_history_record_vector_create(&values);
    DEBUG_ITEM(itemid, "GLB_CACHE: updating db fetch demand to %d seconds", MAX(0, seconds));

    if (elm->db_fetched_time > now)
        elm->db_fetched_time = now;
    // LOG_INF("CACHE DB TIME GET: itemid:%ld, head_time: %d, duration: %d", itemid, head_time, seconds );
    if (SUCCEED == (ret = glb_history_get_history(itemid, elm->value_type, head_time - seconds, 0,
                                          elm->db_fetched_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values)))
    {

        DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB SUCCEED, %d values", itemid, values.values_num);

        if (values.values_num > 0)
        {
            // adding the fetched items to the tail of the cache
            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
            zbx_history_value_t hist_v;

            for (i = 0; i < values.values_num; i++)
            {

                ensure_tsbuff_has_space(elm);

                if (NULL != (c_val = glb_tsbuff_add_to_tail(&elm->tsbuff, values.values[i].timestamp.sec)))
                {
                    DEBUG_ITEM(itemid, "Added value from the history to the cache with timestamp %d", values.values[i].timestamp.sec);
                    hist_val_to_value(c_val, &values.values[i].value, values.values[i].timestamp.sec, elm->value_type);
                }
                else
                {
                    DEBUG_ITEM(itemid, "WARN: COULDN'T add item with timestamp %d, lowest time in the cache is %d",
                               values.values[i].timestamp.sec, glb_tsbuff_get_time_tail(&elm->tsbuff));
                }
            }
            elm->db_fetched_time = MIN(values.values[values.values_num - 1].timestamp.sec, head_time - seconds);
        }
        // this isn't precise, but will prevent consecutive calls
        elm->db_fetched_time = head_time - seconds; // we don't want requests with the same to be repeated anymore, so shifting one second to past
        DEBUG_ITEM(itemid, "Set fetched time to %d", elm->db_fetched_time);
        zbx_history_record_vector_destroy(&values, elm->value_type);
        return SUCCEED;
    }

    DEBUG_ITEM(itemid, "GLB_CACHE: DB item %ld, fetching from DB FAILED", itemid);
    zbx_history_record_vector_destroy(&values, elm->value_type);
    return FAIL;
}

static int fill_items_values_by_index(u_int64_t itemid, item_elem_t *elm, int tail_idx, int head_idx, zbx_vector_history_record_t *values)
{
    int i, iter = 0;
    glb_state_item_value_t *item_val;
    zbx_history_record_t record;

    if (0 > head_idx || 0 > tail_idx)
    {
        LOG_WRN("improper head/tail indexes, FAILing, this is a programming bug ");
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }

    zbx_vector_history_record_clear(values);

    for (i = tail_idx; iter == 0 || i != glb_tsbuff_index(&elm->tsbuff, head_idx + 1); i = glb_tsbuff_index(&elm->tsbuff, i + 1))
    {
        iter++;

        item_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, i);
        
        DEBUG_ITEM(itemid, "Filled value from cache: ts: %ld, type: '%s', value: '%s'", item_val->time_sec, 
            zbx_variant_type_desc(&item_val->value), zbx_variant_value_desc(&item_val->value));

        glb_state_value_to_hist_copy(&record, item_val, elm->value_type);

        zbx_vector_history_record_append_ptr(values, &record);
    }

    return i;
}

static int fill_items_values_by_count(u_int64_t itemid, item_elem_t *elm, int count, int head_idx, zbx_vector_history_record_t *values)
{
    int tail_idx = glb_tsbuff_index(&elm->tsbuff, head_idx - count + 1);

    return fill_items_values_by_index(itemid, elm, tail_idx, head_idx, values);
}

static int count_hit()
{
    state->stats.hits++;
}
static int count_miss()
{
    state->stats.misses++;
}
static int count_db_requests()
{
    state->stats.db_requests++;
}
static int count_db_fails()
{
    state->stats.db_fails++;
}
static int count_fails()
{
    state->stats.fails++;
}

static int update_db_fetch(item_elem_t *elm, int fetch_time)
{
    if (elm->db_fetched_time > fetch_time)
        elm->db_fetched_time = fetch_time;
}

static void free_item(item_elem_t *elm)
{
    glb_state_item_value_t *cache_val;

    while (NULL != (cache_val = (glb_state_item_value_t *)glb_tsbuff_get_value_tail(&elm->tsbuff)))
    {
        item_variant_clear(&cache_val->value);
        glb_tsbuff_free_tail(&elm->tsbuff);
    }

    glb_tsbuff_destroy(&elm->tsbuff, state->memf.free_func);
    state->memf.free_func(elm);
}

int cache_fetch_count_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *req_data)
{
    LOG_DBG("In %s:starting ", __func__);
       
    fetch_req_t *req = (fetch_req_t *)req_data;
    item_elem_t *elm = (item_elem_t *)elem->data;
      
    int count;
    int value_type;
    int ts_head;
    int seconds;

    DEBUG_ITEM(elem->id, "Fetching from the cache, item %ld has %d values, oldest(tail) value ts: %d, newest(head) value ts: %d, Request: head ts: %d, count %d, second: %d", 
            elem->id,  glb_tsbuff_get_count(&elm->tsbuff), glb_tsbuff_get_time_tail(&elm->tsbuff), glb_tsbuff_get_size(&elm->tsbuff),
            req->ts_head, req->count, req->seconds  );
    
    int head_idx = -1, need_count = 0;
    
    if (req->value_type != elm->value_type && glb_tsbuff_get_count(&elm->tsbuff) > 0 )
    {
        DEBUG_ITEM(elem->id,"Item type mismactch: current: %d, new %d, recreating the item", elm->value_type, req->value_type);
        free_item(elm);
        item_create_cb(elem, memf, NULL);
      
        elm = (item_elem_t *)elem->data;
        elm->value_type = req->value_type;
    }

    elm->value_type = req->value_type;
    int cache_start = glb_tsbuff_get_time_head(&elm->tsbuff);
    int now = time(NULL);

    if (cache_start < req->ts_head)
        head_idx = elm->tsbuff.head;
    else
        head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);

    if (-1 < head_idx && SUCCEED == glb_tsbuff_check_has_enough_count_data_idx(&elm->tsbuff, req->count, head_idx))
    {
        count_hit();
        fill_items_values_by_count(elem->id, elm, req->count, head_idx, req->values);
        DEBUG_ITEM(elem->id, "filled %d values from the cache", req->values->values_num);
        
        return SUCCEED;
    }
    
    count_miss();
 
    if (-1 == head_idx)
    {
        count_db_requests();
        DEBUG_ITEM(elem->id, "Requesting item from the DB by time %d->%d", req->ts_head, now);
                
        if (FAIL == glb_state_fetch_from_db_by_time(req->itemid, elm, now - req->ts_head, now, now))
        {
            count_db_fails();
            count_fails();
            return FAIL;
        }

        update_db_fetch(elm, req->ts_head);
    }
   
    head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);

    if (-1 == head_idx)
    {
        need_count = req->count;
    }
    else // excluding data that fits from the count
        need_count = req->count - (head_idx - elm->tsbuff.tail + elm->tsbuff.size) % elm->tsbuff.size;

    if (need_count > 0)
    {
        count_db_requests();
        DEBUG_ITEM(elem->id, "Requesting item from the DB by count starting at %d,  %d items", req->ts_head, need_count);
        if (SUCCEED != fetch_from_db_by_count(req->itemid, elm, need_count, req->ts_head, now))
        {
            count_db_fails();
            count_fails();
            return FAIL;
        }
        update_db_fetch(elm, req->ts_head);
    }

    if (-1 == (head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head)))
    {
        count_fails();
        DEBUG_ITEM(elem->id, "Fetch from db hasn't retrun the data we need, cache + db request FAILed");

        return FAIL;
    }

    if (FAIL == glb_tsbuff_check_has_enough_count_data_idx(&elm->tsbuff, req->count, head_idx))
    {
        count_fails();
        DEBUG_ITEM(elem->id, "Fetch from db hasn't retrun enough data, cache+db request will fail");

        return FAIL;
    }
    
    fill_items_values_by_count(elem->id, elm, req->count, head_idx, req->values);

    DEBUG_ITEM(elem->id, "Fetch from db successful, filled %d values", req->count);

    if (req->values->values_num > 0)
        update_db_fetch(elm, req->values->values[0].timestamp.sec);

    return SUCCEED;
}

static int calc_time_data_overhead(int fetch_seconds)
{
    if (fetch_seconds < 300)
        return fetch_seconds + 300;
    return (fetch_seconds * 1.2);
}
static int cache_fetch_time_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *req_data)
{
    LOG_DBG("%s: started", __func__);
   
    fetch_req_t *req = (fetch_req_t *)req_data;
    item_elem_t *elm = (item_elem_t *)elem->data;

    int head_idx = -1, tail_idx = -1;

    if (req->value_type != elm->value_type)
    {
        DEBUG_ITEM(elem->id, "Resetting value type %d -> %d ", elm->value_type, req->value_type);

        free_item(elm);
        item_create_cb(elem, memf, NULL);

        elm = (item_elem_t *)elem->data;
        elm->value_type = req->value_type;
    }

    int fetch_head_time = -1,
        now = time(NULL),
        fetch_seconds = req->seconds;

    int cache_start = glb_tsbuff_get_time_head(&elm->tsbuff);

    head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);
    tail_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head - req->seconds);

    item_update_demand(elm, 0, MAX(0, req->seconds), now);
    if (glb_tsbuff_get_count(&elm->tsbuff) > 0)
    {
        DEBUG_ITEM(elem->id, "Cache stats oldest time(relative):%d, most recent time(relative):%d, total values:%d, cache size: %d",
                   now - glb_tsbuff_get_time_tail(&elm->tsbuff), now - glb_tsbuff_get_time_head(&elm->tsbuff), glb_tsbuff_get_count(&elm->tsbuff),
                   glb_tsbuff_get_size(&elm->tsbuff));
    }
    else
    {
        DEBUG_ITEM(elem->id, "Cache is empty, cache size: %d", glb_tsbuff_get_size(&elm->tsbuff));
    }

    if (-1 != head_idx && -1 != tail_idx)
    {
        DEBUG_ITEM(elem->id, "Filling from the cache");
        count_hit();
        fill_items_values_by_index(elem->id, elm, tail_idx, head_idx, req->values);
        DEBUG_ITEM(elem->id, "Filled from the cache %d items", req->values->values_num);
        return SUCCEED;
    }

    DEBUG_ITEM(elem->id, "NO/NOT ENOUGH data in the cache, requesting from the db");

    count_miss();

    if (glb_tsbuff_get_count(&elm->tsbuff) > 0 &&
        req->ts_head >= glb_tsbuff_get_time_tail(&elm->tsbuff))
    {

        fetch_head_time = glb_tsbuff_get_time_tail(&elm->tsbuff) + 1;
        fetch_seconds = req->seconds - (req->ts_head - fetch_head_time);
        // to make sure we have the data needed, we need to request a bit more data
        fetch_seconds = calc_time_data_overhead(fetch_seconds);
        DEBUG_ITEM(elem->id, "new calculated demand is %d seconds", fetch_seconds);
    }
    else
        fetch_head_time = now;

    count_db_requests();

    if (SUCCEED != glb_state_fetch_from_db_by_time(req->itemid, elm, fetch_seconds, fetch_head_time, now))
    {
        DEBUG_ITEM(elem->id, "DB FAILED to return data required, cache request will FAIL");
        count_fails();
        count_db_fails();
        return FAIL;
    }
    update_db_fetch(elm, req->ts_head);

    // looking for start/end againg
    if (req->ts_head >= glb_tsbuff_get_time_head(&elm->tsbuff))
    {
        head_idx = elm->tsbuff.head;
    }
    else
        head_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, req->ts_head);

    tail_idx = glb_tsbuff_find_time_idx(&elm->tsbuff, now - req->seconds);

    if (-1 == tail_idx)
    {
        if (0 == glb_tsbuff_get_count(&elm->tsbuff))
        {
            DEBUG_ITEM(elem->id, "No data in the cache after DB fetch, FAILing");
            return FAIL;
        }

        DEBUG_ITEM(elem->id, "DB request successful, but still not enough data to fullfill the demand (has %d seconds, need %d seconds), FAILing",
                   now - glb_tsbuff_get_time_tail(&elm->tsbuff), req->seconds + (now - req->ts_head));

        return FAIL;
    }

    if (-1 != tail_idx && -1 != head_idx)
    {
        fill_items_values_by_index(elem->id, elm, tail_idx, head_idx, req->values);
        DEBUG_ITEM(elem->id, "DB request successful, data cached and filled %d values", glb_tsbuff_get_count(&elm->tsbuff));
    }

    return SUCCEED;
}

int add_value_lld_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *data) {
    glb_state_item_value_t *c_val;
    item_elem_t *elm = elem->data;
    ZBX_DC_HISTORY *h = data;

    int now = time(NULL);
    
    //for lld-based items need to store only one element
    if (glb_tsbuff_get_size(&elm->tsbuff) != 1) {
        glb_tsbuff_resize(&elm->tsbuff, 1 , memf->malloc_func, memf->free_func, NULL);
    }

    while (1 <= glb_tsbuff_get_count(&elm->tsbuff) ) {
        c_val = glb_tsbuff_get_value_tail(&elm->tsbuff);
        item_variant_clear(&c_val->value);
        glb_tsbuff_free_tail(&elm->tsbuff);
  
    }


    if (NULL != (c_val = (glb_state_item_value_t *)glb_tsbuff_add_to_head(&elm->tsbuff, h->ts.sec)))
    {
        dc_hist_to_value(c_val, h);
        elm->value_type = h->value_type;
        DEBUG_ITEM(elem->id, "Item added to LLD, oldest timestamp is %ld", glb_tsbuff_get_time_tail(&elm->tsbuff));
    }
    else
    {
        LOG_WRN("Cannot add LLD value for item %ld with timestamp %d to the cache", h->itemid, h->ts.sec);
        return FAIL;
    }
   
}

int add_value_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *data)
{
    glb_state_item_value_t *c_val;
    item_elem_t *elm;

    elm = (item_elem_t *)elem->data;
    ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)data;
    int now = time(NULL);

    DEBUG_ITEM(elem->id, "Adding item to the items value cache");

    if (elm->value_type == ITEM_VALUE_TYPE_NONE) {
        elm->value_type = h->value_type;
    }
    else if (elm->value_type != h->value_type)
    {
        DEBUG_ITEM(elem->id, "Resetting value type %d -> %d ", elm->value_type, h->value_type);
        free_item(elm);
        item_create_cb(elem, memf, NULL);
        elm = (item_elem_t *)elem->data;
        elm->value_type = h->value_type;
    }
    else
    {
        DEBUG_ITEM(elem->id, "Ensuring there is enough space, before there there %d items, tsbuff size is %d, type is %d", elm->tsbuff.count, elm->tsbuff.size, elm->value_type );
        ensure_tsbuff_has_space(elm);
        DEBUG_ITEM(elem->id, "After ensuring there %d items, tsbuff size is %d", elm->tsbuff.count, elm->tsbuff.size );
    }

    if (h->ts.sec > now + 300)
    {
        DEBUG_ITEM(elem->id, "Warn: item's imestamp %ld is too much ahead of the real time %ld, not adding to the cache",  h->ts.sec, now);
        return FAIL;
    }

    if (NULL != (c_val = (glb_state_item_value_t *)glb_tsbuff_add_to_head(&elm->tsbuff, h->ts.sec)))
    {
        dc_hist_to_value(c_val, h);
        DEBUG_ITEM(elem->id, "Item added, oldest timestamp is %ld", glb_tsbuff_get_time_tail(&elm->tsbuff));
    }
    else
    {
        DEBUG_ITEM(h->itemid, "Cannot add value for item with timestamp %d to the cache",  h->ts.sec);
        return FAIL;
    }

    return SUCCEED;
}
static int item_update_nextcheck_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *cb_data)
{
    item_elem_t *elm = (item_elem_t *)elem->data;

    elm->meta.nextcheck = *(int *)cb_data;
    DEBUG_ITEM(elem->id, "Updated metadata: nextcheck");
}

static int item_update_meta_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *cb_data)
{

    item_elem_t *elm = (item_elem_t *)elem->data;
    glb_state_meta_udpate_req_t *req = (glb_state_meta_udpate_req_t *)cb_data;

    if ((req->flags & GLB_CACHE_ITEM_UPDATE_LASTDATA) && elm->meta.lastdata < req->meta->lastdata)
    {
        elm->meta.lastdata = req->meta->lastdata;
        DEBUG_ITEM(elem->id, "Updated metadata: lastdata");
    }

    if (req->flags & GLB_CACHE_ITEM_UPDATE_NEXTCHECK)
    {
        elm->meta.nextcheck = req->meta->nextcheck;
        DEBUG_ITEM(elem->id, "Updated metadata: nextcheck");
    }
    if (req->flags & GLB_CACHE_ITEM_UPDATE_STATE)
    {
        elm->meta.state = req->meta->state;
        DEBUG_ITEM(elem->id, "Updated metadata: state");
    }
    if (req->flags & GLB_CACHE_ITEM_UPDATE_ERRORMSG)
    {
        DEBUG_ITEM(elem->id, "Updating metadata: errmsg");
        DEBUG_ITEM(elem->id, "Updating metadata: errmsg old %s", elm->meta.error);
        //DEBUG_ITEM(elem->id, "Updating metadata: errmsg new %s", req->meta->error);
       
        elm->meta.error = strpool_replace(&state->strpool, elm->meta.error, req->meta->error);
        DEBUG_ITEM(elem->id, "Updating metadata: errmsg new %s", elm->meta.error);
    }

    return SUCCEED;
}

static int item_get_meta_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *cb_data)
{

    item_elem_t *elm = (item_elem_t *)elem->data;
    glb_state_meta_udpate_req_t *req = (glb_state_meta_udpate_req_t *)cb_data;
    static glb_state_item_meta_t meta = {0};

    meta.lastdata = elm->meta.lastdata;
    meta.nextcheck = elm->meta.nextcheck;
    meta.state = elm->meta.state;
    meta.error = strpool_replace(&state->strpool, meta.error, elm->meta.error);

    req->meta = &meta;
    return SUCCEED;
}

static int item_get_state_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *cb_data)
{
    item_elem_t *elm = (item_elem_t *)elem->data;

    return elm->meta.state;
}

int item_get_nextcheck_cb(elems_hash_elem_t *elem, mem_funcs_t *memf, void *cb_data)
{
    item_elem_t *elm = (item_elem_t *)elem->data;
    return elm->meta.nextcheck;
}

#define ZBX_DC_FLAGS_NOT_FOR_HISTORY (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)
int add_json_item_value(int value_type, struct zbx_json *json, glb_state_item_value_t *c_val)
{
    zbx_json_addobject(json, NULL);
    zbx_json_addint64(json, "clock", c_val->time_sec);

    switch (value_type)
    {

    case ITEM_VALUE_TYPE_TEXT:
    case ITEM_VALUE_TYPE_STR:
        zbx_json_addstring(json, "value", c_val->value.data.str, ZBX_JSON_TYPE_STRING);
        break;

    case ITEM_VALUE_TYPE_LOG:
        zbx_json_addstring(json, "value", c_val->value.data.str, ZBX_JSON_TYPE_STRING);
        break;

    case ITEM_VALUE_TYPE_FLOAT:
        zbx_json_addfloat(json, "value", c_val->value.data.dbl);
        break;

    case ITEM_VALUE_TYPE_UINT64:
        zbx_json_adduint64(json, "value", c_val->value.data.ui64);
        break;

    default:
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
    }

    zbx_json_close(json); // closing one value
}

int item_get_values_json_cb(elems_hash_elem_t *elem, mem_funcs_t *memf,  void *cb_data)
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
        do
        {
            glb_state_item_value_t *c_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, head_idx);

            add_json_item_value(elm->value_type, req->json, c_val);

            head_idx = (head_idx - 1 + glb_tsbuff_get_size(&elm->tsbuff)) % glb_tsbuff_get_size(&elm->tsbuff);
            rcount--;

        } while (rcount > 0);
    }
    zbx_json_close(req->json); // closing the values array of an item
    zbx_json_close(req->json); // closing the item object
}


static int parse_json_item_fields(struct zbx_json_parse *jp, item_elem_t *elm)
{
    int errflag = 0;
    zbx_json_type_t type;

    if ( FAIL ==( elm->value_type = glb_json_get_int_value_by_name(jp, "value_type", &errflag) ))
        return FAIL;

    if ((elm->value_type >= ITEM_VALUE_TYPE_MAX || elm->value_type < 0) && elm->value_type != ITEM_VALUE_TYPE_NONE) {
        LOG_WRN("Imporeper value type is set in the file: %d: '%s'", elm->value_type, jp->start);
        return FAIL;
    }
    elm->last_accessed = time(NULL);

    return SUCCEED;
}

static int parse_json_item_state(struct zbx_json_parse *jp, glb_state_item_meta_t *meta)
{
    int errflag = 0;

    meta->state = glb_json_get_int_value_by_name(jp, "state", &errflag);
    meta->lastdata = glb_json_get_int_value_by_name(jp, "lastdata", &errflag);
    meta->nextcheck = glb_json_get_int_value_by_name(jp, "nextcheck", &errflag);

    if (0 == errflag)
        return SUCCEED;

    return FAIL;
}

static int parse_json_item_demand(struct zbx_json_parse *jp, item_demand_t *demand)
{
    int errflag = 0;

    demand->count = glb_json_get_int_value_by_name(jp, "count", &errflag);
    demand->period = glb_json_get_int_value_by_name(jp, "period", &errflag);
    demand->count_change = glb_json_get_int_value_by_name(jp, "count_change", &errflag);
    demand->period_change = glb_json_get_int_value_by_name(jp, "period_change", &errflag);

    if (0 == errflag)
        return SUCCEED;

    return FAIL;
}

int json_to_hist_record(struct zbx_json_parse *jp, unsigned char value_type, ZBX_DC_HISTORY *hist)
{
    int errflag = 0;
    size_t alloc = 0;

    char *str_value = NULL;
    zbx_json_type_t type;
    bzero(hist, sizeof(ZBX_DC_HISTORY));

    if (FAIL == (hist->ts.sec = glb_json_get_int_value_by_name(jp, "clock", &errflag)))
        return FAIL;

    hist->ts.ns = 0;
    hist->value_type = value_type;

    switch (value_type)
    {
    case ITEM_VALUE_TYPE_UINT64:
        hist->value.ui64 = glb_json_get_int_value_by_name(jp, "value", &errflag);
        break;
    case ITEM_VALUE_TYPE_FLOAT:
        hist->value.dbl = glb_json_get_dbl_value_by_name(jp, "value", &errflag);
        break;
    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT:
    case ITEM_VALUE_TYPE_LOG:
        if (SUCCEED == zbx_json_value_by_name_dyn(jp, "value", &str_value, &alloc, &type))
        {
            hist->value.str = str_value;
        }
        else
        {
            hist->value.str = NULL;
        }
    case ITEM_VALUE_TYPE_NONE:
        return FAIL;
    default:
        LOG_WRN("Unknow value type %d: %s", value_type, jp->start);
        THIS_SHOULD_NEVER_HAPPEN;
        return FAIL;
    }


    return SUCCEED;
}

static int parse_json_item_values(struct zbx_json_parse *jp, elems_hash_elem_t *elem, mem_funcs_t *memf)
{
    const char *value_ptr = NULL;
    struct zbx_json_parse jp_value;
    int errflag = 0;
    int i = 0;
    item_elem_t *elm = (item_elem_t *)elem->data;
    ZBX_DC_HISTORY h = {0};

    while (NULL != (value_ptr = zbx_json_next(jp, value_ptr)))
    {

        if (SUCCEED == zbx_json_brackets_open(value_ptr, &jp_value) &&
            SUCCEED == json_to_hist_record(&jp_value, elm->value_type, &h))
        {
            add_value_cb(elem, memf,  &h);
            i++;
        }
    }
    return i;
}

DUMPER_FROM_JSON(unmarshall_item_cb)
{
    struct zbx_json_parse jp_state, jp_demand, jp_values;
    item_elem_t *elm = (item_elem_t *)elem->data;
    
    if (SUCCEED != parse_json_item_fields(jp, elm))
    {
        LOG_INF("Couldn't parse item fields %s", jp->start);
        return FAIL;
    }

    if (SUCCEED != zbx_json_brackets_by_name(jp, "item_metadata", &jp_state) ||
        SUCCEED != zbx_json_brackets_by_name(jp, "demand", &jp_demand) ||
        SUCCEED != zbx_json_brackets_by_name(jp, "values", &jp_values))
    {
        LOG_INF("Couldn't find either state or demand or values objects in the buffer: %s", jp->start);
        return FAIL;
    }

    if (SUCCEED != parse_json_item_state(&jp_state, &elm->meta))
    {
        LOG_INF("Couldn't parse item state (meta) %s", jp->start);
        return FAIL;
    }

    if (SUCCEED != parse_json_item_demand(&jp_demand, &elm->demand))
    {
        LOG_INF("Couldn't parse item demand %s", jp->start);
        return FAIL;
    }

    DEBUG_ITEM(elem->id, "Loaded item value type %d", elm->value_type);

    return parse_json_item_values(&jp_values, elem, memf);
}

int  glb_state_item_add_lld_value(ZBX_DC_HISTORY *h) {
    DEBUG_ITEM(h->itemid,"Adding LLD value to the history");
    return elems_hash_process(state->items, h->itemid, add_value_lld_cb, h, 0);
}

int  glb_state_item_add_values( ZBX_DC_HISTORY *history, int history_num) {
    int i, ret = SUCCEED;

    LOG_DBG("In %s: starting adding new history %d values", __func__, history_num);

    for (i = 0; i < history_num; i++)
    {
        ZBX_DC_HISTORY *h;
        h = (ZBX_DC_HISTORY *)&history[i];

        DEBUG_ITEM(h->itemid, "Adding to value cache, flags is %d state is %d", h->flags, h->state);
        if (0 != ((ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF) & h->flags) || ITEM_STATE_NOTSUPPORTED == h->state)
        {
            DEBUG_ITEM(h->itemid, "Not adding to value cache, no_hist flag is set");
            continue;
        }

        if (FAIL == elems_hash_process(state->items, h->itemid, add_value_cb, h, 0))
        {
            ret = FAIL;
        };
        DEBUG_ITEM(h->itemid, "Adding value with timestamp %d to the cache", h->ts.sec);
    }

    return ret;
};


int	zbx_vc_get_values(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, const zbx_timespec_t *ts) {
     
    DEBUG_ITEM(itemid, "Cache request: multiple items reqeuest: secodns:%d, count:%d, start:%d ", seconds, count, ts->sec);
	return glb_state_item_get_values(itemid, value_type, values, seconds, count, ts->sec);
}

int	zbx_vc_get_value(zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value) {
	zbx_vector_history_record_t	values;

	zbx_history_record_vector_create(&values);
	DEBUG_ITEM(itemid, "Cache request: single value at %d",ts->sec);


	if (SUCCEED != glb_state_item_get_values(itemid, value_type, &values, 0, 1, ts->sec)) {
		zbx_history_record_vector_destroy(&values, value_type);
		return FAIL;
	}
		
	*value = values.values[0];
//	reset values vector size so the returned value is not cleared when destroying the vector 
	values.values_num = 0;

	zbx_history_record_vector_destroy(&values, value_type);
	return SUCCEED;
}

int glb_state_item_update_nextcheck(u_int64_t itemid, int nextcheck) {
		return elems_hash_process(state->items, itemid, item_update_nextcheck_cb, &nextcheck, 0);	
}

int  glb_state_item_update_meta(u_int64_t itemid, glb_state_item_meta_t *meta, unsigned int flags, int value_type) {
	glb_state_meta_udpate_req_t req = { .meta = meta, .flags = flags, .value_type = value_type};

	return elems_hash_process(state->items, itemid, item_update_meta_cb, &req, 0);
}

ELEMS_CALLBACK(item_set_error_cb) {
    item_elem_t *elm = (item_elem_t *)elem->data;
    const char *error = (const char *)data;

    if (NULL == error)
        return FAIL;
    
    elm->meta.error = strpool_replace(&state->strpool, elm->meta.error, error);
    elm->meta.state = ITEM_STATE_NOTSUPPORTED;
   
    return SUCCEED;
}

int  glb_state_item_set_error(u_int64_t itemid, const char *error) {
	return elems_hash_process(state->items, itemid, item_set_error_cb, (void *)error, 0);
}


int glb_state_item_get_state(u_int64_t itemid) {
	int st;
    if (FAIL == (st = elems_hash_process(state->items, itemid, item_get_state_cb, NULL, ELEM_FLAG_DO_NOT_CREATE))) {
        return ITEM_STATE_UNKNOWN;
    }
    return st;
}
int glb_state_item_get_nextcheck(u_int64_t itemid) {
	return elems_hash_process(state->items, itemid, item_get_nextcheck_cb, NULL, ELEM_FLAG_DO_NOT_CREATE);
}


int  glb_state_item_get_values( u_int64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds, int count, int ts_head) {
	
    DEBUG_ITEM(itemid,"In %s:starting itemid: %ld, count: %d", __func__,itemid, count );

	if (count > 0)  {
        fetch_req_t req = {.itemid = itemid, .count = count, .value_type = value_type, .values = values, .ts_head = ts_head};
    	
        return elems_hash_process(state->items, itemid, cache_fetch_count_cb, &req, ELEM_FLAG_DO_NOT_CREATE);
	}

    fetch_req_t req = {.itemid = itemid, .value_type = value_type, .values = values, .seconds = seconds, .ts_head = ts_head };
    
    return elems_hash_process(state->items, itemid, cache_fetch_time_cb, &req, ELEM_FLAG_DO_NOT_CREATE);
};

/******************************************************************************
 *                                                                            *
 * Function: glb_state_get_item_stats                                         *
 *                                                                            *
 * Purpose: get statistics of cached items                                    *
 *                                                                            *
 ******************************************************************************/
void	glb_state_get_item_stats(zbx_vector_ptr_t *stats)
{
	zbx_hashset_iter_t	iter;
	elems_hash_elem_t		*elem;
	glb_state_item_stats_t	*item_stats;
	int i;

}

int glb_state_items_get_state_json(zbx_vector_uint64_t *itemids, struct zbx_json *json) {
    
    int i;
    zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    
    glb_state_meta_udpate_req_t req;

    for (i=0; i < itemids->values_num; i++) {

        if ( FAIL != elems_hash_process(state->items, itemids->values[i], item_get_meta_cb, &req, ELEM_FLAG_DO_NOT_CREATE) ) {

            zbx_json_addobject(json,NULL);
			zbx_json_adduint64string(json,"itemid",itemids->values[i]);
            zbx_json_adduint64string(json,"lastdata", req.meta->lastdata);
            zbx_json_adduint64string(json,"nextcheck", req.meta->nextcheck);
            zbx_json_adduint64string(json,"state", req.meta->state);
        
            if (NULL != req.meta->error && strlen(req.meta->error) > 0)
                zbx_json_addstring(json,"error",req.meta->error,ZBX_JSON_TYPE_STRING );

            zbx_json_close(json);
            
            DEBUG_ITEM(itemids->values[i], "Added info to the trapper status request: lastdata: %d, state %d, error %d",req.meta->lastdata,
                     req.meta->state, req.meta->error);
        }
    }
    zbx_json_close(json); 
}

int glb_state_get_items_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count) {
	elems_hash_elem_t *elem;
	int i, j, rcount;

	if (count < 1 ) 
        count = GLB_CACHE_MIN_COUNT;

	item_last_values_req_t req = {.count = count, .json = json};
    
	
    LOG_DBG("%s: starting, requested %d items count %d", __func__,itemids->values_num, count);
	zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    
    for (i=0; i<itemids->values_num; i++) {
		DEBUG_ITEM(itemids->values[i], "Item is requested from the value cache, count %d", count);
		elems_hash_process(state->items, itemids->values[i], item_get_values_json_cb, &req, ELEM_FLAG_DO_NOT_CREATE);
	}	
 
  	zbx_json_close(json); //closing the items array
	LOG_DBG("Result is %s: ",json->buffer);
  	zabbix_log(LOG_LEVEL_DEBUG, "%s: finished: response is: %s", __func__,json->buffer);
}


int glb_state_items_load() {
    state_load_objects(state->items, "items", "itemid", unmarshall_item_cb );
    return SUCCEED;
}

/*****************************************************************
    element to json for dumping and api
*****************************************************************/
DUMPER_TO_JSON(item_to_json_cb)
{   int head_idx;
    item_elem_t *elm = data;

    zbx_json_addint64(json, "itemid", id);
    zbx_json_addint64(json, "value_type", elm->value_type);
    zbx_json_addint64(json, "db_fetched_time", elm->db_fetched_time);
    zbx_json_addint64(json, "last_accessed", elm->last_accessed);

    // meta
    zbx_json_addobject(json, "item_metadata");
    zbx_json_addint64(json, "state", elm->meta.state);
    zbx_json_addint64(json, "lastdata", elm->meta.lastdata);
    zbx_json_addint64(json, "nextcheck", elm->meta.nextcheck);

    if (NULL != elm->meta.error)
        zbx_json_addstring(json, "error", elm->meta.error, ZBX_JSON_TYPE_STRING);

    zbx_json_close(json);

    // demand
    zbx_json_addobject(json, "demand");
    zbx_json_addint64(json, "count", elm->demand.count);
    zbx_json_addint64(json, "period", elm->demand.period);
    zbx_json_addint64(json, "count_change", elm->demand.count_change);
    zbx_json_addint64(json, "period_change", elm->demand.period_change);
    zbx_json_close(json);

    // data
    zbx_json_addarray(json, "values");
    int tail_idx = elm->tsbuff.tail;
    if (-1 < tail_idx)
    {
        int head_idx = elm->tsbuff.head;
        int rcount = glb_tsbuff_get_count(&elm->tsbuff);
        do
        {
            glb_state_item_value_t *c_val = glb_tsbuff_get_value_ptr(&elm->tsbuff, tail_idx);

            add_json_item_value(elm->value_type, json, c_val);
            tail_idx = (tail_idx + 1 + glb_tsbuff_get_size(&elm->tsbuff)) % glb_tsbuff_get_size(&elm->tsbuff);
            rcount--;

        } while (rcount > 0);
    }
    zbx_json_close(json);

    return (glb_tsbuff_get_count(&elm->tsbuff));
}


int glb_state_items_dump() {
    state_dump_objects(state->items, "items", item_to_json_cb );
	return SUCCEED;
}

ELEMS_CALLBACK(get_valuetype_cb) {
     item_elem_t* item  = elem->data;    
     return item->value_type;
}

int  glb_state_get_item_valuetype(u_int64_t itemid) {
    return elems_hash_process(state->items, itemid, get_valuetype_cb, 0, ELEM_FLAG_DO_NOT_CREATE);
}



int glb_state_items_remove(zbx_vector_uint64_t *deleted_itemids) {
	int i;
	
    for (i = 0; i< deleted_itemids->values_num; i++) {
        LOG_INF("Deleting item %ld from the cache", deleted_itemids->values[i]);
        DEBUG_ITEM(deleted_itemids->values[i],"Deleting item from the value cache");
    	elems_hash_delete(state->items, deleted_itemids->values[i]);
	}
}

void glb_state_items_housekeep() {

}