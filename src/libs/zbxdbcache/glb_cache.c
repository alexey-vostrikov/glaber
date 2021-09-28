/******************the copyright ***************/

//glb cache doesn't make assumptions 
#ifndef GLB_CACHE_C
#define GLB_CACHE_C

#include "zbxvariant.h"
#include "log.h"
#include "memalloc.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "glb_lock.h"
#include "glb_cache.h"
#include "zbxhistory.h"
#include <zlib.h>

extern char	*CONFIG_VCDUMP_LOCATION;

//limits - for sanity and safety
#define GLB_VCDUMP_RECORD_TYPE_ITEM 1
#define GLB_VCDUMP_RECORD_TYPE_VALUE 2

#define GLB_CACHE_MIN_COUNT     4
#define GLB_CACHE_MAX_COUNT     1024*1024 
#define GLB_CACHE_MAX_DURATION  86400 

#define GLB_CACHE_ITEMS_INIT_SIZE   8192
#define GLB_CACHE_MIN_ITEM_VALUES   4 //reduce to 2 if too much mem will be used

#define GLB_CACHE_DEMAND_UPDATE 86400 //daily demand update is a good thing
#define GLB_CACHE_MIN_DURATION 2 //duration up to this is ignored and considered set to 0
#define GLB_CACHE_DEFAULT_DURATION 2 * 86400 //by default we downloading two days worth of data
                                            //however if duration is set, this might be bigger

extern u_int64_t CONFIG_VALUE_CACHE_SIZE;

typedef struct {
    int elem_num;
    size_t elem_size;
    void *data;
    int first_idx;
    int last_idx;
    int elem_count;

} glb_cache_ring_buffer_t;

typedef struct {
    unsigned int sec;
    zbx_variant_t value;
} glb_cache_value_t;

typedef struct {
    u_int64_t itemid;
    unsigned char value_type; //using ITEM_VALUE_TYPE*
    
    int lastdata; //last time a data for the element appeared (it's not required that that data would be cached)
                   //this is needed to fix data appearance on submit stage
                   //the actual data might be filtered and not appear in the cache
    int state;  //element's type specific values here 
    int last_accessed; //housekeeper will delete items that haven't been accessed for a while
    int nextcheck; //next expected check of the item (perhaps, useless for triggers, maybe it's better to create an element-spceific struct)
    
    int req_count;   
    int req_duration;
    
    int duration_change;
    int count_change;

    int new_req_count;
    int new_req_duration;

    int db_fetched_time;

    glb_cache_ring_buffer_t *values;//this will be reallocated with the desired size
    pthread_mutex_t lock;

} glb_cache_elem_t;

typedef struct 
{
    zbx_hashset_t hset;
    unsigned int min_values; //how many values keep in the cache even if demand is lower that this
    unsigned int max_values; //same, but upper limit;
    pthread_rwlock_t meta_lock; //all operations with the meta only by rr or rw locking the meta. Searches should use rr


} glb_cache_elems_t; 

typedef struct {
    glb_cache_elems_t items;
    pthread_mutex_t mem_lock;
    glb_cache_stats_t stats;

} glb_cache_t;

static  zbx_mem_info_t	*cache_mem;

static glb_cache_t *glb_cache;



int glb_cache_get_mem_stats(zbx_mem_stats_t *mem_stats) {
    memset(&mem_stats, 0, sizeof(zbx_mem_stats_t));
	glb_lock_block(&glb_cache->mem_lock);
    zbx_mem_get_stats(cache_mem, mem_stats);
    glb_lock_unlock(&glb_cache->mem_lock);		
}

int glb_cache_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode) {

	zbx_hashset_iter_t	iter;
	glb_cache_elem_t	*elem;
	
	*values_num = 0;

    glb_rwlock_rdlock(&glb_cache->items.meta_lock);

	*items_num = glb_cache->items.hset.num_data;
	
	zbx_hashset_iter_reset(&glb_cache->items.hset, &iter);
	while (NULL != (elem = (glb_cache_elem_t *)zbx_hashset_iter_next(&iter)))
			*values_num += elem->values->elem_num;

    glb_rwlock_unlock(&glb_cache->items.meta_lock);
	
}
/**********************************************************
 * for cache monitoring                                   *
 * ********************************************************/
int glb_cache_get_statistics(glb_cache_stats_t *stats) {
//TODO: fill the statistics    
    stats->hits = glb_cache->stats.hits;
	stats->misses = glb_cache->stats.misses;
	stats->total_size = cache_mem->total_size;
	stats->free_size = cache_mem->free_size;
}

/******************************************************************************
 *                                                                            *
 * Function: glb_cache_get_item_stats                                         *
 *                                                                            *
 * Purpose: get statistics of cached items                                    *
 *                                                                            *
 ******************************************************************************/
void	glb_cache_get_item_stats(zbx_vector_ptr_t *stats)
{
	zbx_hashset_iter_t	iter;
	glb_cache_elem_t		*elem;
	glb_cache_item_stats_t	*item_stats;
	int i;

	glb_rwlock_rdlock(&glb_cache->items.meta_lock);
    	
    zbx_vector_ptr_reserve(stats, glb_cache->items.hset.num_data);
	zbx_hashset_iter_reset(&glb_cache->items.hset, &iter);
	
    while (NULL != (elem = (glb_cache_elem_t *)zbx_hashset_iter_next(&iter))) {
		
        item_stats = (glb_cache_item_stats_t *)zbx_malloc(NULL, sizeof(glb_cache_item_stats_t));
		item_stats->itemid = elem->itemid;
		item_stats->values_num = elem->values->elem_num;
		zbx_vector_ptr_append(stats, item_stats);
	}

    glb_rwlock_unlock(&glb_cache->items.meta_lock);
}

/************************************************************
 * updated item's metadata 
 * **********************************************************/
int  glb_cache_update_item_meta(u_int64_t itemid, glb_cache_item_meta_t *meta, unsigned int flags) {
    glb_cache_elem_t *elem;
    glb_rwlock_rdlock(&glb_cache->items.meta_lock);
    if (NULL != (elem = (glb_cache_elem_t *)zbx_hashset_search(&glb_cache->items.hset,&itemid))) {
        glb_lock_block(&elem->lock);
        
            if (flags & GLB_CACHE_ITEM_UPDATE_LASTDATA) {
                if (elem->lastdata < meta->lastdata) {
                    elem->lastdata = meta->lastdata;
                }
            }
            if (flags & GLB_CACHE_ITEM_UPDATE_NEXTCHECK) 
                elem->nextcheck = meta->nextcheck;
            
            if (flags & GLB_CACHE_ITEM_UPDATE_STATE) 
                elem->state = meta->state;

            
        glb_lock_unlock(&elem->lock);
    }
    glb_rwlock_unlock(&glb_cache->items.meta_lock);
    if (NULL != elem) 
        return SUCCEED;
    return FAIL;
}

/*************************************************************
* retruns ptr to the cache element's adressed by the index
* plus does some sanity checks 
*************************************************************/
static  glb_cache_value_t *glb_cache_get_value_ptr(glb_cache_ring_buffer_t *rbuf, int idx) {
    if (0 > idx ) {
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);
        return NULL;
    }

    if (rbuf->first_idx >= rbuf->last_idx ) {
        //start is higher then end - buffer is "circuled", actually any idx is valid
        //withing the buffer size (buffer as full)
        if (idx >= rbuf->elem_num ) {
            zabbix_log(LOG_LEVEL_INFORMATION,"Wrong requested idx:%d, first is %d last id %d", idx, rbuf->first_idx, rbuf->last_idx);
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);
        }
    } else {
        //the idx should be lower or equal to last_idx, in this case first idx is always 0
        if ( idx > rbuf->last_idx  ) {
            zabbix_log(LOG_LEVEL_INFORMATION,"Wrong requested idx:%d, first is %d last id %d", idx, rbuf->first_idx, rbuf->last_idx);
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);
        }
    }
    
    //ok, idx seems to be fine, doing the ptr calc
    void *ptr =  (void *) rbuf->data + idx* rbuf->elem_size; //to avoid compiler data size indexing using void during calc
    //zabbix_log(LOG_LEVEL_INFORMATION,"Rbuf data is %ld, idx is %d, ptr is %ld",rbuf->data, idx,ptr);
    return (glb_cache_value_t *)ptr;
}
/*********************************************************
 * retruns cache's values items minimum time or future date
 * *******************************************************/
static int glb_cache_min_time(glb_cache_elem_t * elem, int now) {
    glb_cache_value_t *c_val;
    
    if (-1 == elem->values->first_idx) 
        return now;
    
    c_val = glb_cache_get_value_ptr(elem->values, elem->values->first_idx);
    
    return c_val->sec;
}

/******************************************************
 * memory alloc handlers, //TODO: implement buffer queues
 * as a general mechanic, not only IPC or CACHE- specific
 * and use them here
 * ***************************************************/
void *glb_cache_malloc(void *old, size_t size) {
    void *buff = NULL;
    static int last_dump = 0;

    glb_lock_block(&glb_cache->mem_lock);
    buff = zbx_mem_malloc(cache_mem,old,size);
    glb_lock_unlock(&glb_cache->mem_lock);
    return buff;
}

void *glb_cache_realloc(void *old, size_t size) {
    void *buff = NULL;
    
    glb_lock_block(&glb_cache->mem_lock);
    buff = zbx_mem_realloc(cache_mem,old,size);
    glb_lock_unlock(&glb_cache->mem_lock);
  
    return buff;

}


void glb_cache_free(void *ptr) {
    if (NULL == ptr) 
        return;

    glb_lock_block(&glb_cache->mem_lock);
    zbx_mem_free(cache_mem, ptr);
    glb_lock_unlock(&glb_cache->mem_lock);
  
}

/******************************************************
 * cache specific variant functions, mostly copied 
 * from the zbxvariant, but redone to use 
 * custom memory alloc/dealloc
 * ****************************************************/
void    glb_cache_variant_clear(zbx_variant_t *value)
{
    zabbix_log(LOG_LEVEL_DEBUG,"In: %s, Freeing item of type %d",__func__, value->type);
	
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
void glb_cache_housekeep_values(glb_cache_elem_t *elem) {
    
    //we can remove items if there are more then max_count in the cache and item's exceeds maximum requested 
    //timeframe
    unsigned int now, lastdate, clean = 0;
    glb_cache_ring_buffer_t *buff = elem->values;

    zabbix_log(LOG_LEVEL_DEBUG,"Start %s:",__func__);

    if (-1 == buff->last_idx && -1 == buff->first_idx) //nodata check
            return; 
    
    now = time(NULL);
    
    //starting from the oldest items
    glb_cache_value_t *val = glb_cache_get_value_ptr(buff,buff->first_idx);
    
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
        
        val = glb_cache_get_value_ptr(buff,buff->first_idx);
    }
    
    zabbix_log(LOG_LEVEL_DEBUG,"End of %s: clean %d items",__func__, clean);
}

/********************************************
 * inits the ring bufer                     *
 * ******************************************/
static void glb_cache_init_ring_buffer(glb_cache_ring_buffer_t *rbuff,unsigned int elem_num, size_t elem_size) {
    rbuff->data = glb_cache_malloc(NULL, elem_num * elem_size);
    bzero(rbuff->data,elem_num * elem_size);
    rbuff->first_idx = -1;
    rbuff->last_idx = -1;
    rbuff->elem_num = elem_num;
    rbuff->elem_size = elem_size;
    rbuff->elem_count = 0;
  }


/***********************************************
 * safely stores new elemnt 
 * ********************************************/
 static glb_cache_elem_t *glb_cache_add_elem(glb_cache_elems_t *elems, glb_cache_elem_t* elem) {
    glb_cache_elem_t *ret = NULL;

    glb_rwlock_wrlock(&elems->meta_lock);
    ret = zbx_hashset_insert(&elems->hset,elem,sizeof(glb_cache_elem_t));
    glb_rwlock_unlock(&elems->meta_lock);

    return ret;
}


/*******************************************
 * returns pointer to the item from 
 * a hash, apllicable to all types
 * items, hosts, triggers
 * *****************************************/
glb_cache_elem_t * glb_cache_get_elem(glb_cache_elems_t *elems, u_int64_t elemid) {
    glb_cache_elem_t *elem;
   
    glb_rwlock_rdlock(&elems->meta_lock);
    elem = zbx_hashset_search(&elems->hset, &elemid);
    glb_rwlock_unlock(&elems->meta_lock);

    return elem;
}

/************************************************************
 * finds time in the buffer by dividing by halves
 * assumes array is ascending time-sorted
 * **********************************************************/
static int glb_cache_find_time_idx(glb_cache_elem_t *elem, unsigned int tm_sec) {
    glb_cache_value_t *first, *last;
    //uses division by half to speed up the process
    //assumes linear item distribution
    zabbix_log(LOG_LEVEL_INFORMATION, "In %s: starting", __func__);

    if (elem->values->elem_count == 0 )  //nodata - no result
        return -1;
    
    //fisrt, checks the requested time is within first and last data
    first = glb_cache_get_value_ptr(elem->values,elem->values->first_idx);
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld, First time is %d", elem->itemid, first->sec);
    
    if (first->sec > tm_sec) //reqested time is out of the cached data boundaries
        return -1;
        
    last = glb_cache_get_value_ptr(elem->values,elem->values->last_idx);
    
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld, Last time is %d", elem->itemid, last->sec);
    if (last->sec < tm_sec )  //if all the data is older than requested, returning the last as the closest value
        return elem->values->last_idx;
     
    //value lies withing the first and last, finding the closest element
    int first_idx = elem->values->first_idx;
    int last_idx = elem->values->last_idx;
    int guess_idx; 

    if (last_idx < first_idx) 
        last_idx+= elem->values->elem_num;
    
    while (1) {
        glb_cache_value_t *c_val1, *c_val2;
        guess_idx = (first_idx + last_idx) / 2; 
        
        if ( guess_idx == first_idx) //if only two numbers left, point guess to the last one
            guess_idx = last_idx;
              
        //c_val2  = glb_cache_get_value_ptr(elem->values, guess_idx % elem->values->elem_num);
        c_val2  = glb_cache_get_value_ptr(elem->values, guess_idx % elem->values->elem_num);

        //zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: c_val2 is %ld, buffer addr is %ld",elem->itemid, c_val2, elem->values->data);

        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: selected quess idx is %d, first is %d, last is %d",
                elem->itemid, guess_idx,first_idx, last_idx);
        
       

        if (tm_sec == c_val2->sec) {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: requested time mathces items at guess idx is %d",elem->itemid,guess_idx );
              return guess_idx % elem->values->elem_num ;
        }
        c_val1 = glb_cache_get_value_ptr(elem->values, first_idx %  elem->values->elem_num);
       
        //zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: asdfvaefe",elem->itemid);

        if (tm_sec == c_val1->sec) {
            return first_idx % elem->values->elem_num ;
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: requested time mathces items at first_idx is %d",elem->itemid,first_idx );
        }

        //zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld: selected quess idx2",elem->itemid);
        if (tm_sec >c_val1->sec && tm_sec < c_val2->sec) {
            //the value is withing first_idx ... guess_idx 
            if (first_idx +1 == guess_idx) {
                return first_idx  % elem->values->elem_num;
            }
            
            //new search will be on the `left side`
            last_idx = guess_idx;
        } else {
            //the value is withing guess_idx ... last_idx
            if (guess_idx + 1 == last_idx) 
                return guess_idx  % elem->values->elem_num;
            //new search will be on the `right side`
            first_idx = guess_idx;
        }
    }
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_lastvalues_json                                       *
 *                                                                            *
 * Purpose: generates json object holding two last values from the requested  *
 * 			itemids															  *
 * 		data[{"itemid":1234,"clock":2342343,"value":234, "nextcheck":2342342},*
 * 			 {"itemid":1234,"clock":2342300,"value":200, "nextcheck":2342342},*
 * 			{....}, ]														  *
 *  //TODO: this should go from config cache to the state cache               *
 ******************************************************************************/
int glb_cache_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count) {
	
	glb_cache_elem_t *elem;
	int i,j;
	history_value_t *val;
	u_int64_t clock;
	ZBX_DC_HISTORY	hr;

	zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    
    glb_rwlock_rdlock(&glb_cache->items.meta_lock);
	
    if (count <1 ) 
        count = GLB_CACHE_MIN_COUNT;

    for (i=0; i<itemids->values_num; i++) {
		if ( NULL != (elem= (glb_cache_elem_t*)zbx_hashset_search(&glb_cache->items.hset,&itemids->values[i])) ) {
			glb_lock_block(&elem->lock);
			        
            zbx_json_addobject(json,NULL);

			zbx_json_adduint64(json,"itemid",elem->itemid);

            //iterating over the data found, will return the data in the same history
            //format, the difference is only the data is fetched from the cache
			zbx_json_adduint64(json,"lastdata", elem->lastdata);
            zbx_json_adduint64(json,"nextcheck", elem->nextcheck);
            zbx_json_adduint64(json,"state", elem->state);
			
            //now adding the data 
            zbx_json_addarray(json,"data");

            //iterating over existing values (if there are less in the cache, returning 
            // any that present)
            int last_idx = elem->values->last_idx;
            
            if (count > elem->values->elem_count) 
                count = elem->values->elem_count;

            int i = ( elem->values->last_idx - count + elem->values->elem_num) % elem->values->elem_num;
            while ( i != last_idx +1 ) {
                glb_cache_value_t *c_val;
                c_val = glb_cache_get_value_ptr(elem->values,i);
                
                zbx_json_addobject(json,NULL);
                zbx_json_adduint64(json,"time_sec", c_val->sec);

			    switch (elem->value_type) {
			
				    case ITEM_VALUE_TYPE_TEXT:
				    case ITEM_VALUE_TYPE_STR:
					    zbx_json_addstring(json,"value",c_val->value.data.str, ZBX_JSON_TYPE_STRING);
					    break;

				    case ITEM_VALUE_TYPE_LOG:
					    zbx_json_addstring(json,"value",c_val->value.data.str, ZBX_JSON_TYPE_STRING);
					    break;

				    case ITEM_VALUE_TYPE_FLOAT: 
					    zbx_json_addfloat(json,"value",c_val->value.data.dbl);
					    break;

				    case ITEM_VALUE_TYPE_UINT64:
					    zbx_json_adduint64(json,"value",c_val->value.data.ui64);
					    break;

				    default:
					    THIS_SHOULD_NEVER_HAPPEN;
					    exit(-1);
                    }
                    i = (i+i ) % elem->values->elem_num;
                }
                zbx_json_close(json); //closing one value    
			}
			zbx_json_close(json); //closing the item
            glb_lock_unlock(&elem->lock);
	   
	}	
	zbx_json_close(json); //closing the response
    
    glb_rwlock_unlock(&glb_cache->items.meta_lock);
	

	return SUCCEED;
}

/******************************************************************************
 * gets data from the value cache
 * id  - object id to get the data
 * type - type of object GLB_CACHE_TYPE_*
 * count - number of values to fetch
 * offset_time - fetch values starting from offset_time 
 * 
 * even if cache is written tщ high efficiency file, sometimes cache db reads might be required
 * this might happen on introducing a new trigger with with long period check
 * so need to implement it, but in a way to reduce history backend usage ASAP
 * 
 * however: at least should do proper notification and debugging that a trigger
 * couldn't be _temporarily_ calculated due to failure of getting a data
 * and this should be distingushed from data absence 
 * 
 * ****************************************************************************/

//fetches data from the history backend to the elem, reallocates the elem data
//sets the metadata: db upload 
//this also quite an expensive call, so history backend protection might cause fails of it
//so need to deal with the fail 


/********************************************************************
 * combines existing cache data with the database data to fill 
 * cache and be able to calc triggers and calc items,
 * updates the demand: if request is done by by count with delayed
 * start then demand is set by time either:
 * the oldest value's time plus some safety margin is used to set 
 * the new items demand
 * count demands are still used, but only for items requested 
 * as of current time (end time should be set to 0)
 * ******************************************************************/


//so the rules of getting the data from the cache:
//if count is set to 0 - fetching all items by time range
//otherwize - count items
//ts sets the end of the timeperiod for fetching the data


static int glb_cache_value_to_hist_copy(zbx_history_record_t *record, glb_cache_value_t *c_val, unsigned char value_type) {
    zabbix_log(LOG_LEVEL_INFORMATION,"copy to hist val");

    switch (value_type) {
    case ITEM_VALUE_TYPE_FLOAT:
        zabbix_log(LOG_LEVEL_INFORMATION,"copy to hist val dbl");
        record->value.dbl = c_val->value.data.dbl;
        break;
    case ITEM_VALUE_TYPE_UINT64:
        zabbix_log(LOG_LEVEL_INFORMATION,"copy to hist val uint");
        record->value.ui64 = c_val->value.data.ui64;
        break;
    case ITEM_VALUE_TYPE_STR:
    case ITEM_VALUE_TYPE_TEXT: //TODO: get rid of the allocation!!!! we can just use the copy from the cache
                      
        if ( NULL != c_val->value.data.str ) 
            record->value.str = zbx_strdup(NULL,c_val->value.data.str);
        else 
            record->value.str = zbx_strdup(NULL,"");
        break;
    case ITEM_VALUE_TYPE_NONE: //TODO: get rid of the allocation!!!! we can just use the copy from the cache
        if ( NULL != c_val->value.data.err) {
            zabbix_log(LOG_LEVEL_INFORMATION,"copy to hist val err");
            record->value.err = zbx_strdup(NULL,c_val->value.data.err);
        } else {
            record->value.err = NULL;
        }
        break;
     
    default:
        zabbix_log(LOG_LEVEL_INFORMATION,"Unknown value type %d",c_val->value.type);
        THIS_SHOULD_NEVER_HAPPEN;
        exit(-1);

    }
    record->timestamp.sec = c_val->sec;
    zabbix_log(LOG_LEVEL_INFORMATION,"Finished");
}
//if demand exceeds the current one, demand is updated and remembered demand is reset
//if demand exceeds the remembered demand, it's updated
//in case is rememebered demand last apply time was too long ago, the current demand updated and remembered 
//demand is reset
int glb_cache_update_demand(glb_cache_elem_t *elem, unsigned int new_count, unsigned int new_duration, unsigned int now) {
    //unsigned int now=time(NULL);
    unsigned char need_hk=0;
    
    if (elem->req_count < new_count ) {
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld demand by count updated: %d -> %d", 
            elem->itemid, elem->req_count, new_count);
        elem->req_count = new_count;
        elem->new_req_count = 0;
        elem->count_change = now;
        need_hk = 1;
    } else if (elem->new_req_count < new_count) {
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld new demand by count updated: %d -> %d", 
            elem->itemid, elem->new_req_count, new_count);
        elem->new_req_count = new_count;
    }

    if (now - elem->count_change > GLB_CACHE_DEMAND_UPDATE) {
        elem->count_change = now;
        elem->req_count = elem->new_req_count;
        elem->new_req_count = 0;
        need_hk = 1;
    }
    
    //checking and updating the time limits
    if (elem->req_duration < new_duration ) {
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld demand by time updated: %d -> %d", 
            elem->itemid, elem->req_duration, new_duration);
        elem->req_duration = new_duration;
        elem->new_req_duration = 0;
        elem->duration_change = now;
        need_hk = 1;
    } else if (elem->new_req_duration < new_duration) {
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld new demand by time updated: %d -> %d", 
            elem->itemid, elem->new_req_duration, new_duration);
        elem->new_req_duration = new_duration;
    }
    
    if (now - elem->duration_change > GLB_CACHE_DEMAND_UPDATE) {
        elem->duration_change = now;
        elem->req_duration = elem->new_req_duration;
        elem->new_req_duration = 0;
        need_hk = 1;
    }

    if (need_hk) 
        glb_cache_housekeep_values(elem);
    
    return SUCCEED;    
}


/************************************************************************
 * dumps the buffer, mostly usefull for debugging
 * **********************************************************************/
static void glb_cache_rbuf_dump(glb_cache_elem_t  *elem) {
    glb_cache_ring_buffer_t *rbuf = elem->values;
    int i;

    zabbix_log(LOG_LEVEL_INFORMATION,"Dump rbuff for item %ld: fisrt %d last %d, size:%d, count:%d", 
                elem->itemid, rbuf->first_idx, rbuf->last_idx, rbuf->elem_num, rbuf->elem_count);
    i = rbuf->first_idx;
    if ( -1 == i ) 
        return;

    do {
        glb_cache_value_t *c_val = glb_cache_get_value_ptr(elem->values,i);
        zabbix_log(LOG_LEVEL_INFORMATION,"  %d,  sec:%d, type:%d, val:%s",i, c_val->sec, c_val->value.type," ");
        i = (i + 1 ) % rbuf->elem_num;
    } while(i !=  ((rbuf->last_idx +1 ) % rbuf->elem_num)) ;
}


/******************************************************
 * grows ring buffer for grow_pcnt percents but not less then grown cnt values
 * *************************************************************/
static void glb_cache_rbuf_grow(glb_cache_elem_t *elem, int grow_pcnt, int grow_cnt) {
    
    zabbix_log(LOG_LEVEL_DEBUG,"%s: starting", __func__);

    void *new_data = NULL;
    int new_size = elem->values->elem_num + MAX( (elem->values->elem_num * grow_pcnt) /100 , grow_cnt );
    //over - grow prevention
    if (new_size > GLB_CACHE_MAX_COUNT ) 
            new_size = GLB_CACHE_MAX_COUNT;

    zabbix_log(LOG_LEVEL_INFORMATION,"Will grow buffer %d->%d elements",elem->values->elem_num, new_size);

    new_data = glb_cache_malloc(NULL, elem->values->elem_size * new_size);
    bzero(new_data,elem->values->elem_size * new_size);
    
    if (elem->values->first_idx > -1 ) {
        if (elem->values->first_idx <= elem->values->last_idx) {
            zabbix_log(LOG_LEVEL_INFORMATION,"Doing simple linear copy");
            //the simplest case, we can just copy the existing buffer, nothing will be changed
            memcpy(new_data, elem->values->data, elem->values->elem_num * elem->values->elem_size);
            zabbix_log(LOG_LEVEL_INFORMATION,"finished linear copy");
        } else {
            zabbix_log(LOG_LEVEL_INFORMATION,"Doing double - segment copy");
            void *first_segment_ptr=(void *) glb_cache_get_value_ptr(elem->values, elem->values->first_idx);
            size_t first_size = (elem->values->elem_num - elem->values->first_idx) * elem->values->elem_size;
            size_t last_size =  elem->values->last_idx * elem->values->elem_size;
            zabbix_log(LOG_LEVEL_INFORMATION, "Old buffer ptr: %ld, new buffer: %ld, elements %d, element size %d",elem->values->data,
                    elem->values->elem_count, elem->values->elem_size);

            //first coping the oldest data (from start till the buffer end)
            memcpy(new_data, first_segment_ptr, first_size );
            //now copying the newest part
            zabbix_log(LOG_LEVEL_INFORMATION,"Doing double - segment copy2");
            memcpy(new_data + first_size, elem->values->data, last_size);
            //fixing indexes
            elem->values->first_idx = 0;
            elem->values->last_idx = elem->values->elem_count - 1;
        }
    } 

    glb_cache_free(elem->values->data);

    elem->values->data = new_data;
    elem->values->elem_num = new_size;
    zabbix_log(LOG_LEVEL_DEBUG,"%s: finished", __func__);
}

/*********************************************************
 * retruns ptr to a new value, if there is free space in 
 * the buffer, uses it, if there is not - overwrites the 
 * oldest value, it's the caller's business to fill the value
 * ******************************************************/
static glb_cache_value_t *glb_cache_rbuff_add_value(glb_cache_elem_t *elem)  {
    //checking if there is free space
    void *ptr;
    int new_idx;

    glb_cache_value_t *c_val;

    if (-1 == elem->values->first_idx ) { //no data case
        elem->values->first_idx = 0;
        elem->values->last_idx = 0;
        elem->values->elem_count = 1;
        return elem->values->data;
    }       

    new_idx = ( elem->values->last_idx + 1 ) % elem->values->elem_num;
    
    if (new_idx == elem->values->first_idx) { //there was no new space, overwritting the val
        
        glb_cache_value_t *c_val = glb_cache_get_value_ptr(elem->values, elem->values->first_idx);
        glb_cache_variant_clear(&c_val->value);
        
        elem->values->first_idx = (elem->values->first_idx + 1 ) % elem->values->elem_count;
    } else {
        elem->values->elem_count++;
    }
    
    elem->values->last_idx = new_idx;
       
    return glb_cache_get_value_ptr(elem->values, elem->values->last_idx);;
}

int glb_cache_hist_to_value(glb_cache_value_t *cache_val, history_value_t *hist_v, int time_sec, unsigned char value_type) {
   // zabbix_log(LOG_LEVEL_INFORMATION, "Cleaning hist hist addr is %ld", hist_v);   
    glb_cache_variant_clear(&cache_val->value);
    
   // zabbix_log(LOG_LEVEL_INFORMATION, "Setting the val, val type is %d",value_type);
    
    switch (value_type) {
        case ITEM_VALUE_TYPE_FLOAT:
            zbx_variant_set_dbl(&cache_val->value, hist_v->dbl);
            break;
        case ITEM_VALUE_TYPE_UINT64: 
            zbx_variant_set_ui64(&cache_val->value, hist_v->ui64);
            break;
        case ITEM_VALUE_TYPE_STR:
        case ITEM_VALUE_TYPE_TEXT: {
            char *tmp_str = hist_v->str;
            if (NULL == hist_v->str) tmp_str="";
            
            int len = strlen(hist_v->str)+1;
            
           // zabbix_log(LOG_LEVEL_INFORMATION,"Setting str text %s len is %d", tmp_str, len);

            char *str = glb_cache_malloc(NULL, len);
          //  zabbix_log(LOG_LEVEL_INFORMATION,"memcpy");
            memcpy(str,tmp_str, len);
          //  zabbix_log(LOG_LEVEL_INFORMATION,"set_str");
            zbx_variant_set_str(&cache_val->value, str);
            
            break;
        }
        case ITEM_VALUE_TYPE_LOG: {
            if ( NULL != hist_v->log) {
                int len = strlen(hist_v->log->value)+1;
                char *str = glb_cache_malloc(NULL, len);
                memcpy(str,hist_v->log, len);
                //there is no log type in variant ... at least yet
                zbx_variant_set_str(&cache_val->value, str);
            } else {
                zbx_variant_set_str(&cache_val->value, "");
            }
            break;
        }

        case ITEM_VALUE_TYPE_NONE: 
            if ( NULL != hist_v->err) {
                int len = strlen(hist_v->err)+1;
                char *str = glb_cache_malloc(NULL, len);
                memcpy(str,hist_v->err, len);
                zbx_variant_set_error(&cache_val->value, str);
            }
            break;
        
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            exit(-1);       
    }
    cache_val->sec = time_sec;
  //  zabbix_log(LOG_LEVEL_INFORMATION,"%s: finished",__func__);
}

/****************************************************************
 * adds a single value considering all complications and
 * restrictions
 * **************************************************************/
void glb_cache_add_value(glb_cache_elem_t *elem, history_value_t *hist_v, int time_sec) {
    int now = time(NULL);
    glb_cache_value_t *cache_val;

    glb_lock_block(&elem->lock);
    glb_cache_housekeep_values(elem);

    int oldest_time = now;

    if (elem->values->first_idx > -1 ) { 
        glb_cache_value_t *val = glb_cache_get_value_ptr(elem->values,elem->values->first_idx);
        oldest_time = val->sec;
    }

    if (elem->values->elem_count == elem->values->elem_num) 
        if ( elem->values->elem_count < MAX (GLB_CACHE_MIN_ITEM_VALUES,elem->req_count) ||
                elem->req_duration > now - oldest_time) {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld Growing ring buffer to add new values", elem->itemid);
            glb_cache_rbuf_grow(elem, 10, 5);
        } 
 
    cache_val = glb_cache_rbuff_add_value(elem);
    glb_cache_hist_to_value(cache_val, hist_v, time_sec, elem->value_type);
 
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: Added item %ld to the cache with time %d, total items %d", elem->itemid, cache_val->sec,  elem->values->elem_count);
    elem->last_accessed = time(NULL);
  
    glb_lock_unlock(&elem->lock);
}

/***********************************************************************
 * fetches data that to meet the demand set
 * treats the elem as items->values, shouldn't be called
 * on other types
 * 
 * It maybe a bit tricky, but quite simple: 
 *      if in time mode the fills the cache with all data from range till 
 *          the earliest data requested time
 * 
 *      if in count mode, then fills count values older than the earliest 
 *          requested time
 *      
 * *********************************************************************/

int glb_cache_fetch_from_db(glb_cache_elem_t *elem, int count, int req_time, unsigned int now) {
    
    int ret = FAIL;
    zbx_vector_history_record_t	values;
    
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, will be fetching item the DB, elem's db_fetched_time is %d", elem->itemid,elem->db_fetched_time);

    if ( elem->db_fetched_time < req_time ) {
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CAHCE: DB item %ld, no reason to fetch data from the history storage at %d, has already requested from %d",
                elem->itemid, req_time, elem->db_fetched_time);
            return FAIL;
    }
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CAHCE: DB item %ld, updating demand to %d seconds", MAX(0, now - req_time));

    zbx_history_record_vector_create(&values);
    
    if (count > 0) {
        //count mode
        int fetched_time = elem->db_fetched_time;
        if (fetched_time > now) 
                fetched_time = now;
        
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CAHCE: DB item %ld, fetching in count mode %d items in range : %d -> %d",
                    elem->itemid, count, fetched_time - GLB_CACHE_DEFAULT_DURATION, fetched_time);
        
      //  glb_cache->stats.hits++;

        if (SUCCEED == ( ret = glb_history_get(elem->itemid, elem->value_type, 
                        fetched_time - GLB_CACHE_DEFAULT_DURATION, count, 
                        fetched_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values)) ) {    
            //succesifully fetched, updating the fetched time
           
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, fetching from DB SUCCEED, %d values",elem->itemid,values.values_num);
            if (values.values_num > 0 ) {
                elem->db_fetched_time = MIN(elem->db_fetched_time, values.values[0].timestamp.sec);
            } else //this isn't precise, but will prevent consequitive calls
                elem->db_fetched_time = fetched_time - 1; //we don't want requests with the same to be repeated anymore, so shifting one second to past
        } else {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, fetching from DB FAILED",elem->itemid);
        }
    } else {
        //timerange mode
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CAHCE: DB item %ld, fetching in timerange mode: %d -> %d",
                    elem->itemid, req_time, elem->db_fetched_time);
         
       // glb_cache->items.stats.hits++;
        if (SUCCEED == ( ret = glb_history_get(elem->itemid, elem->value_type, req_time, 0, 
                elem->db_fetched_time, GLB_HISTORY_GET_NON_INTERACTIVE, &values))) {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, fetching from DB SUCCEED, %d values",elem->itemid,values.values_num);
            elem->db_fetched_time = req_time;
        } else {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, fetching from DB FAILED",elem->itemid);
        }

    }

    if (SUCCEED == ret && 0 < values.values_num) {
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: DB item %ld, db query succeed, %d values, new db_fetched_time is %d", 
                    elem->itemid, values.values_num, elem->db_fetched_time); 
        
        //putting fetched items to the cache
        zbx_vector_history_record_sort(&values, (zbx_compare_func_t)zbx_history_record_compare_asc_func);
        
        if (values.values_num > elem->values->elem_num - elem->values->elem_count ) {
            zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: DB: item %ld: need to enlarge the cache to add items from the DB %d->%d",
                elem->itemid, elem->values->elem_num, elem->values->elem_count + values.values_num - elem->values->elem_count + GLB_CACHE_MIN_COUNT);
            glb_cache_rbuf_grow(elem,10,elem->values->elem_count + values.values_num - elem->values->elem_count + GLB_CACHE_MIN_COUNT);
        }
        
        int i, start_index;
        
        if (elem->values->first_idx == -1) 
            start_index = 0;
        else 
            start_index = (elem->values->first_idx - values.values_num + elem->values->elem_num) % elem->values->elem_num;
            
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: DB: item %ld start index is %d", elem->itemid, start_index);
            
        elem->values->first_idx = start_index;
        
        for (i = start_index; i < start_index + values.values_num; i++) {
            glb_cache_value_t *cache_val;
        
            zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: DB: item %ld adding new item at index %d", elem->itemid, i);
            cache_val = glb_cache_get_value_ptr(elem->values, i % elem->values->elem_num);
            glb_cache_hist_to_value(cache_val, &values.values[i-start_index].value, values.values[i-start_index].timestamp.sec, elem->value_type);
            
        }
            
        if (elem->values->last_idx == -1) 
            elem->values->last_idx = start_index + values.values_num - 1;
        
        elem->values->elem_count += values.values_num;
    }

    zbx_history_record_vector_destroy(&values, elem->value_type);
    zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: DB: item %ld: after adding new item at indexes are %d -> %d", elem->itemid, elem->values->first_idx, elem->values->last_idx);
    return ret;
}

int glb_cache_fill_values(glb_cache_elem_t *elem, int start_idx, int end_idx, zbx_vector_history_record_t *values) {
    int i;
    glb_cache_value_t *c_val;
    zbx_history_record_t	record;

    zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld: filling responce idx %d->%d", elem->itemid, start_idx, end_idx);
    zbx_vector_history_record_clear(values);

    for (i=start_idx; i<=end_idx; i++) {
        c_val = glb_cache_get_value_ptr(elem->values, i); 
                    
	    glb_cache_value_to_hist_copy(&record, c_val, elem->value_type);
	    zbx_vector_history_record_append_ptr(values, &record);
    }

    zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: item %ld: added %d items to the cache response",values->values_num);
    return i;
}
static glb_cache_elem_t *glb_cache_init_item_elem(u_int64_t itemid, unsigned char value_type) {

    glb_cache_elem_t new_item, *elem;
    bzero(&new_item, sizeof(glb_cache_elem_t));

    new_item.itemid = itemid;
    new_item.last_accessed = time(NULL);
    new_item.db_fetched_time = ZBX_JAN_2038;
    new_item.value_type = value_type;

    glb_lock_init(&new_item.lock);

        //doing element array of values init 
    new_item.values = (glb_cache_ring_buffer_t *)glb_cache_malloc(NULL, sizeof(glb_cache_ring_buffer_t));
    glb_cache_init_ring_buffer(new_item.values, GLB_CACHE_MIN_ITEM_VALUES, sizeof(glb_cache_value_t));

    elem = glb_cache_add_elem(&glb_cache->items,&new_item);
    return elem;
}

/******************************************************************************
 * fetches items from the cache                                               *
 * logic (from valuecache.c):                                                 *
 * If <count> is set then value range is defined as <count> values            *
 *           before <timestamp>. Otherwise the range is defined as <seconds>  *
 *           seconds before <timestamp>.                                      *
 * ***************************************************************************/
int	glb_cache_get_item_values(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int ts_start,
	    int count,  int ts_end) {
    int now = time(NULL);
    int time_demand;

    //we need to fetch either count items ending at ts or if count is set to 0, the 
    //items starting at ts-seconds to ts, so checking if the data in the cache for the requested time period
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: In %s: started: itemid: %ld, value_type: %d, ts_start: %d, count: %d, ts_end: %d",
                    __func__, itemid, value_type, ts_start, count, ts_end);
  
    //first of all, lets check if the element exists at all  
    glb_cache_elem_t *elem = glb_cache_get_elem(&glb_cache->items, itemid);
    if (NULL == elem) {
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: no element exist for item %ld, creating a new one", itemid);
        elem = glb_cache_init_item_elem(itemid,value_type);
        zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: element item %ld created: ptr %ld", itemid, elem);
    }

    glb_lock_block(&elem->lock);
    elem->last_accessed = now;
    
    int cache_start_time = glb_cache_min_time(elem, now);
    int start_fit_idx, last_fit_idx = glb_cache_find_time_idx(elem,ts_end);
    int ret = FAIL;
    //there are 2 general modes of getting the data - time range and count mode
    //but the common thing is to download data 
    
    if (count > 0 ) { 
        
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: itemid %ld Fetching in COUNT mode, last_fit_idx is %d, have items %d, first_idx is %d, last_idx is %d",
            elem->itemid, last_fit_idx, 
            last_fit_idx ==-1 ? 0 : (last_fit_idx - elem->values->first_idx + elem->values->elem_num) % elem->values->elem_num +1 ,
            elem->values->first_idx,
            elem->values->last_idx);
        int need_count;
        //checking if there are enough items to respond from the cache

        if ( last_fit_idx > -1 && 
            (last_fit_idx - elem->values->first_idx + elem->values->elem_num) % elem->values->elem_num +1  >= count ) {
            
            //this is HIT situation, the data in the cache, filling and exiting
            zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: HIT!!!: itemid %ld requested %d elems with offset %d, elems in the cache %d, cache size is %d", 
                    elem->itemid, count, time(NULL) - ts_end, elem->values->elem_count, elem->values->elem_num );
            glb_cache->stats.hits++;

            glb_cache_fill_values(elem, (last_fit_idx - count + 1 + elem->values->elem_num) % elem->values->elem_num,
                                                 last_fit_idx, values);
            glb_lock_unlock(&elem->lock);
            
            return SUCCEED;
    
        }

        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld MISS!!! Cache start time is %d", elem->itemid, cache_start_time);
        glb_cache->stats.misses++;
        //only get here if threre wasn't enough data in the cache
        
        //count mode, a bit complicated, first do time based fetch
        if (cache_start_time > ts_end && ts_end < (now - 1) ) { //only fetch from db if ts_end is not now
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld fetching by timerange till %d ", elem->itemid,ts_end);
            //cache has no data for the period, first we try to fetch the data by time
            ret = glb_cache_fetch_from_db(elem, 0, ts_end, now);
        } else {
            ret = SUCCEED;
        }

        //calculating, how many values we still need after the fetch
        if (-1 == last_fit_idx) //there is still no data in the cache, we need to fetch all the data 
            need_count = count;
        else //excluding data that fits from the count
            need_count = count - (last_fit_idx - elem->values->first_idx + elem->values->elem_num) % elem->values->elem_num;

        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld need to fetch %d more values starting at %d", elem->itemid, need_count, ts_end);

        if (SUCCEED == ret && need_count > 0) {
            ret = glb_cache_fetch_from_db(elem, need_count, ts_end, now);
        }
        if (-1 == last_fit_idx) 
            last_fit_idx = elem->values->last_idx;

        start_fit_idx =(last_fit_idx - count + 1 + elem->values->elem_num) % elem->values->elem_num;
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld finished count fetch, ret is %d, start_idx is %d last_fit_idx is %d",elem->itemid, ret, start_fit_idx, last_fit_idx);

    } else { //time range mode
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld Fetching in TIMERANGE mode cache start is %d",
                elem->itemid, cache_start_time);
        //note: in timerange mode ts_start is relative to ts_end
        if (cache_start_time > ts_end - ts_start ) { //cache either empty or has not enough data 
            //fetching from the db 
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld MISS!!! ", elem->itemid);
            ret = glb_cache_fetch_from_db(elem, 0, ts_end - ts_start, now);
            glb_cache->stats.misses++;
        } else {
            zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld HIT!!! ", elem->itemid);
            glb_cache->stats.hits++;
            ret = SUCCEED;
        }

        start_fit_idx = glb_cache_find_time_idx(elem,ts_end - ts_start);
        zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: item %ld finished timerange fetch, ret is %d, start_idx is %d",elem->itemid, ret, start_fit_idx);
    }
    
    if (SUCCEED == ret && start_fit_idx > -1 ) {
        glb_cache_fill_values(elem,start_fit_idx,last_fit_idx, values);
      
    }
    
    glb_lock_unlock(&elem->lock);
    zabbix_log(LOG_LEVEL_INFORMATION, "GLB_CACHE: %s finished, item %ld, returning %d values ", __func__, elem->itemid, values->values_num);
    return ret;
}


/******************************************************************************
 * to replace calls to the original cache func                                *
 * Function: zbx_vc_get_values                                                *
 *                                                                            *
 * Purpose: get item history data for the specified time period               *
 *                                                                            *
 * Parameters: itemid     - [IN] the item id                                  *
 *             value_type - [IN] the item value type                          *
 *             values     - [OUT] the item history data stored time/value     *
 *                          pairs in descending order                         *
 *             seconds    - [IN] the time period to retrieve data for         *
 *             count      - [IN] the number of history values to retrieve     *
 *             ts         - [IN] the period end timestamp                     *
 *                                                                            *
 * Return value:  SUCCEED - the item history data was retrieved successfully  *
 *                FAIL    - the item history data was not retrieved           *
 *                                                                            *
 * Comments: If the data is not in cache, it's read from DB, so this function *
 *           will always return the requested data, unless some error occurs. *
 *                                                                            *
 *           If <count> is set then value range is defined as <count> values  *
 *           before <timestamp>. Otherwise the range is defined as <seconds>  *
 *           seconds before <timestamp>.                                      *
 *                                                                            *
 ******************************************************************************/
int	zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, const zbx_timespec_t *ts)
{
    return glb_cache_get_item_values(itemid, value_type, values, seconds, count, ts->sec);
}


int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value)
{
	zbx_vector_history_record_t	values;
	int				ret = FAIL;

	zbx_history_record_vector_create(&values);


	if (SUCCEED != glb_cache_get_item_values(itemid, value_type, &values, ts->sec, 1, ts->sec) || 0 == values.values_num)
		goto out;

	*value = values.values[0];

	/* reset values vector size so the returned value is not cleared when destroying the vector */
	values.values_num = 0;

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, value_type);

	return ret;
}


/*******************************************
 * Add a new item values to the cache       *
 * *****************************************/
//TODO: redo to work on GLB_METRIC and variant
#define ZBX_DC_FLAGS_NOT_FOR_HISTORY	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)
int  glb_cache_add_values(zbx_vector_ptr_t *history) {
    int i;

    for (i = 0; i < history->values_num; i++)
	{
		ZBX_DC_HISTORY	*h;
		h = (ZBX_DC_HISTORY *)history->values[i];
        glb_cache_elem_t *elem;

        if (0 != (ZBX_DC_FLAGS_NOT_FOR_HISTORY & h->flags))
			continue;

       // zabbix_log(LOG_LEVEL_INFORMATION, "Finding cache itemid %ld",h->itemid);
        if (NULL ==( elem = glb_cache_get_elem(&glb_cache->items,h->itemid))) {
           //zabbix_log(LOG_LEVEL_INFORMATION, "Item %ld doesn't exists in the Cache, adding a new one", h->itemid);
           elem = glb_cache_init_item_elem(h->itemid,h->value_type);
        } else 
      //      zabbix_log(LOG_LEVEL_INFORMATION, "Items index for item %ld has been found", h->itemid);
       // zabbix_log(LOG_LEVEL_INFORMATION, "Adding a value to the item's %ld cache, value type is %d",h->itemid, h->value_type);
      
        if (elem->value_type != h->value_type) {
            glb_lock_block(&elem->lock);
            zabbix_log(LOG_LEVEL_INFORMATION,"elem %ld: value type has changed, cleaning the existing cache and demand");
            elem->new_req_count =0;
            elem->new_req_duration =0;
            elem->req_count = 0;
            elem->req_duration = 0;
            
            //this will cleanup all the values in the cache
            glb_cache_housekeep_values(elem);
            elem->value_type = h->value_type;

            glb_lock_unlock(&elem->lock);
            zabbix_log(LOG_LEVEL_INFORMATION,"GLB_CACHE: elem %ld: value type has changed, dropped the existing cache",elem->itemid);
        }
        glb_cache_add_value(elem,&h->value,h->ts.sec);
    }
   return SUCCEED; 
}   


int glb_cache_init() {
   
    char *error = NULL;

	zabbix_log(LOG_LEVEL_INFORMATION,"Allocating shared memory for glb cache size %ld",CONFIG_VALUE_CACHE_SIZE);

	//SHM
	if (SUCCEED != zbx_mem_create(&cache_mem, CONFIG_VALUE_CACHE_SIZE, "Cache cache size", "GLBCachesize", 1, &error)) {
        zabbix_log(LOG_LEVEL_INFORMATION,"Zbx mem create failed");
    	return FAIL;
    
    }
	//cache struct 
	if (NULL == (glb_cache = (glb_cache_t *)zbx_mem_malloc(cache_mem, NULL, sizeof(glb_cache_t)))) {	
		zabbix_log(LOG_LEVEL_CRIT,"Cannot allocate Cache structures, exiting");
		return FAIL;
	}
    
    glb_lock_init(&glb_cache->mem_lock);

	memset((void *)glb_cache, 0, sizeof(glb_cache_t));
    
    zbx_hashset_create_ext(&glb_cache->items.hset, GLB_CACHE_ITEMS_INIT_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			glb_cache_malloc, glb_cache_realloc, glb_cache_free);
	
	zabbix_log(LOG_LEVEL_INFORMATION, "%s:finished", __func__);
	return SUCCEED;
}

//that's pretty strange destroy proc yet written, the cache is in the SHM which will be destroyed
//just after the process dies
void	glb_cache_destroy(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
   
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


/*****************************************************************
 * parses json to item metadata
   ****************************************************************/
  /*
static int  glb_parse_item_metadata(struct zbx_json_parse  *jp, glb_cache_elem_t *item)  {
	//jp pints to the json containing the metadata
	char  itemid_str[MAX_ID_LEN], hostid_str[MAX_ID_LEN], value_type_str[MAX_ID_LEN], state_str[MAX_ID_LEN],
		status_str[MAX_ID_LEN],range_sync_hour_str[MAX_ID_LEN], values_total_str[MAX_ID_LEN],
		last_accessed_str[MAX_ID_LEN], active_range_str[MAX_ID_LEN], db_cached_from_str[MAX_ID_LEN];
	zbx_json_type_t type;	

	if (SUCCEED != zbx_json_value_by_name(jp,"itemid",itemid_str,MAX_ID_LEN, &type) ||
		SUCCEED != zbx_json_value_by_name(jp,"hostid",hostid_str,MAX_ID_LEN, &type) || 
	    SUCCEED != zbx_json_value_by_name(jp,"value_type",value_type_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"status",status_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"range_sync_hour",range_sync_hour_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"values_total",values_total_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"last_accessed",last_accessed_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"active_range",active_range_str,MAX_ID_LEN, &type) || 
		SUCCEED != zbx_json_value_by_name(jp,"db_cached_from",db_cached_from_str,MAX_ID_LEN, &type) 
	) return FAIL;

	item->itemid = strtol(itemid_str,NULL,10);
	item->value_type = strtol(value_type_str,NULL,10);
	item->status = strtol(status_str,NULL,10);
	item->range_sync_hour = strtol(range_sync_hour_str,NULL,10);
	item->active_range = strtol(active_range_str,NULL,10);
	item->last_accessed = strtol(last_accessed_str,NULL,10);
	
	//theese might not need to be in the dump as they will be recalced on adding a new data
	item->values_total = 0;//strtol(values_total_str,NULL,10);
	item->db_cached_from = strtol(db_cached_from_str,NULL,10);
	
	zabbix_log(LOG_LEVEL_DEBUG,"Parsed item metadata: hostid: %d, itemid: %ld, value_type:%d, status:%d, range_sync_hour: %d, values_total:%d, last_accessed: %d, active_range: %d, db_cached_from:%d", 
					item->hostid, item->itemid, item->value_type, item->status, item->range_sync_hour,
					 item->values_total, item->last_accessed, item->active_range, item->db_cached_from);

	return SUCCEED;

}

*/
/*****************************************************************
 * loads valuecache from the  file stated in the configuration 
 * file is read line by line and either items are parsed and
 * created or data is loaded
 ****************************************************************/

int glb_vc_load_cache() {
	FILE *fp;
	gzFile gzfile;

	size_t read, len =0;
	char line[MAX_STRING_LEN];
	int req_type=0, items = 0, vals = 0;
	struct zbx_json_parse jp;
	zbx_json_type_t j_type;
	char type_str[MAX_ID_LEN];
	
	zabbix_log(LOG_LEVEL_INFORMATION, "Reading valuecache from %s",CONFIG_VCDUMP_LOCATION);
	/*
    //sleep(1);
	//return SUCCEED;

	if ( NULL == (fp = fopen(CONFIG_VCDUMP_LOCATION, "a"))) {
		zabbix_log(LOG_LEVEL_WARNING, "Cannot open file %s for access check, exiting",CONFIG_VCDUMP_LOCATION);
		//checking if we have the permissions on creating and writing the file
		return FAIL;
	}
	fclose(fp);

	//reopening the file in the read mode as gzipped file
	if ( Z_NULL == (gzfile = gzopen(CONFIG_VCDUMP_LOCATION, "r"))) {
		zabbix_log(LOG_LEVEL_WARNING, "Cannot open gzipped file %s for reading",CONFIG_VCDUMP_LOCATION);
		//checking if we have the permissions on creating and writing the file
		return FAIL;
	}

	while (Z_NULL != gzgets(gzfile, line, MAX_STRING_LEN) ) {
		int vc_idx;
		
		//ok, detecting the type of record
		//zabbix_log(LOG_LEVEL_INFORMATION,"Retrieved line of length %zu:", read);
        //zabbix_log(LOG_LEVEL_INFORMATION,"%s", line);
		
		if (SUCCEED != zbx_json_open(line, &jp)) {
			zabbix_log(LOG_LEVEL_INFORMATION,"Cannot parse line '%s', incorrect JSON", line);
			continue;
		}

		//reading type of the record
		if (SUCCEED != zbx_json_value_by_name(&jp, "type", type_str, MAX_ID_LEN, &j_type)) {
        	zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse line '%s': no 'type' parameter",line);
        	continue;
    	}
		req_type = strtol(type_str,NULL,10);

		switch (req_type) {
			case GLB_VCDUMP_RECORD_TYPE_ITEM: {
				glb_cache_elem_t   new_elem, *elem;
				
				bzero(&new_elem, sizeof(glb_cache_elem_t));
				
				if (SUCCEED == glb_parse_item_metadata(&jp,&new_elem) ) {
					//let's see if the item is there already
					if (NULL == (elem = (glb_cache_elem_t *)zbx_hashset_insert(&glb_cache->items.hset, &new_elem, sizeof(glb_cache_elem_t)))) {
						zabbix_log(LOG_LEVEL_INFORMATION, "Couldnt add item %ld to Glaber Cache, it's already there, skipping", new_elem.itemid);
					};

					items++;
				} else {
					zabbix_log(LOG_LEVEL_INFORMATION, "Failed to parse an item metadata: %s", jp.start);
				}
				break;
			}
			case GLB_VCDUMP_RECORD_TYPE_VALUE: {
					glb_cache_value_t c_value;
                    glb_cache_elem_t *elem;
					u_int64_t itemid, hostid;
					time_t expire_timestamp;
					char tmp_str[MAX_ID_LEN];
					zbx_json_type_t type;

					expire_timestamp = time(NULL) - GLB_CACHE_DEFAULT_DURATION;

					bzero(&c_value, sizeof(c_value));

					if (FAIL == zbx_json_value_by_name(&jp,"itemid",tmp_str,MAX_ID_LEN, &type) ) {
						zabbix_log(LOG_LEVEL_INFORMATION,"Couldn't find itemid in the value record: %s",jp.start);
						continue;
					}
					
					itemid = strtol(tmp_str,NULL,10);
	
					if (NULL == (elem = (glb_cache_elem_t *)zbx_hashset_search(&glb_cache->items,&itemid ))) {
						zabbix_log(LOG_LEVEL_WARNING,"Couldn't find itemid in the items cache %ld",itemid);
						continue;
					}
					
					if (SUCCEED == glb_history_json2val(&jp, elem->value_type, &c_value) ) {
						if ( c_value.sec < expire_timestamp ) {
							zabbix_log(LOG_LEVEL_DEBUG,"Item %ld value timestamp %d is too old (%d second in the past) not adding to VC",
									elem->itemid, c_value.sec, time(NULL) - c_value.sec );
							zbx_history_record_clear(&c_value,elem->value_type);
							continue;
						}
						 
                        
						glb_cache_add_value(elem, &c_value.value, c_value.sec );
						
                        vals++;
						zbx_history_record_clear(&c_value,item->value_type);
						
					} else {
						zabbix_log(LOG_LEVEL_INFORMATION,"Couldn't parse value json: %s",jp.start);
					}
				
				break;
			}
			default: 
				zabbix_log(LOG_LEVEL_INFORMATION,"Unknown type of record '%s', ignoring line",type_str);
				break;
			
		}
    }
	gzclose(gzfile);
*/	
	zabbix_log(LOG_LEVEL_INFORMATION,"Finished loading valuecache data, loaded %d items; %d values",items,vals);
	return SUCCEED;
}


/*****************************************************************
 * dumps valuecache to a file stated in the configuration 
 * 
 * the dump will hold read lock on the hash during the dump
 * in most situations this should be harmless, unless
 * dumps takes too long time AND there are lots of 
 * changes (adding new or removing old elems) are 
 * happening at the same time, in this case some operations
 * will wait
*****************************************************************/
#define BUFFER_ITEMS 1024*1024
int glb_vc_dump_cache() {

	zbx_hashset_iter_t iter;
	glb_cache_elem_t *elem;
	int items = 0, vals = 0, buff_items = 0, i;
	
	size_t buff_alloc, buff_offset;
	char *buffer = NULL;

	char new_file[MAX_STRING_LEN], tmp[MAX_STRING_LEN], tmp_val[MAX_STRING_LEN], tmp_val2[MAX_STRING_LEN*2];
	size_t len;

	zabbix_log(LOG_LEVEL_INFORMATION,"In %s: starting", __func__);
	
    if (NULL == CONFIG_VCDUMP_LOCATION )
	 		return FAIL;
	
	zabbix_log(LOG_LEVEL_INFORMATION, "Will dump value cache to %s",CONFIG_VCDUMP_LOCATION);
	//old cache file is renamed to *.old postfix due to possibility of corrupting or not completing the dump
	//of something is wrong, this way will have a bit outdated but functional copy of the cache
	zbx_snprintf(new_file,MAX_STRING_LEN,"%s%s",CONFIG_VCDUMP_LOCATION,".new");
	//zbx_snprintf_alloc(buffer,alloc_len,offset,)
/*	
	gzFile gzfile = gzopen(new_file,"wb");

	zabbix_log(LOG_LEVEL_INFORMATION, "Cache file has been opened");

	if (Z_NULL == gzfile) {
		zabbix_log(LOG_LEVEL_WARNING, "Cannot open file %s, value cache will not be dumped",new_file);
		return FAIL;
	}

    glb_rwlock_rdlock(&glb_cache->items.meta_lock);
    zbx_hashset_iter_reset(&glb_cache->items.hset,&iter);
	
	while (NULL != (elem=(glb_cache_elem_t*)zbx_hashset_iter_next(&iter))) {
		
        zbx_snprintf_alloc(&buffer, &buff_alloc, &buff_offset, 
				"{\"type\":%d,\"itemid\":%ld,\"value_type\":%d,\"state\":%d,\"r\":%d,\"req_count\":%d,\"req_duration\":%d,\"last_accessed\":%d}\n", 
		    			GLB_VCDUMP_RECORD_TYPE_ITEM, elem->itemid, elem->value_type, elem->state, elem->req_count, elem->req_duration, 
                        elem->last_accessed, elem->db_fetched_time);
		
        buff_items++;
		
        i = elem->values->first_idx;
        if ( -1 == i ) 
            continue;

        do {
            char buff[MAX_STRING_LEN * 4];

            glb_cache_value_t *c_val = glb_cache_get_value_ptr(elem->values,i);
            zbx_snprintf_alloc(&buffer, &buff_alloc, &buff_offset,
                "{\"type\":%d,\"itemid\":%ld,\"ts\":%d,\"value\":",
                elem->value_type, elem->itemid, c_val->sec);
            
            char *val = zbx_variant_value_desc(*c_val->value);
            glb_escape_worker_string(val,buff);
               

            zabbix_log(LOG_LEVEL_INFORMATION,"  %d,  sec:%d, type:%d, val:%s",i, c_val->sec, c_val->value.type," ");
                i = (i + 1 ) % elem->values->elem_num;
        
        } while(i !=  ((elem->values->last_idx +1 ) % elem->values->elem_num)) ;


		zabbix_log(LOG_LEVEL_DEBUG, "In %s: dumping data value %d ts is %d",__func__, i , curr_chunk->slots[i].timestamp.sec);
			
		zbx_history_value2str(tmp_val,MAX_STRING_LEN,&curr_chunk->slots[i].value,item->value_type);
		
        glb_escape_worker_string(tmp_val,tmp_val2);
		
        
        
        zbx_snprintf_alloc(&buffer, &buff_alloc, &buff_offset,"{\"type\":%d,\"itemid\":%ld,\"hostid\":%ld,\"ts\":%d,\"value\":\"%s\"}\n",
			GLB_VCDUMP_RECORD_TYPE_VALUE,item->itemid, item->hostid, curr_chunk->slots[i].timestamp.sec,tmp_val2);
		
        	vals++;	
        }  
		
		if (buff_items > BUFFER_ITEMS) {
		   	if ( 0 >= gzwrite(gzfile,buffer,buff_offset))	{
				zabbix_log(LOG_LEVEL_WARNING,"Cannot write to cache %s, errno is %d",new_file,errno);
				break;
			}
			buff_offset=0;
			
		}
	}
	glb_rwlock_unlock(&glb_cache->items.meta_lock);
	
    //dumping remainings in the buffer
	if ( 0 >= gzwrite(gzfile,buffer,buff_offset)) {
		zabbix_log(LOG_LEVEL_WARNING,"Cannot write to %s, errno is %d",new_file, errno);
	}

	zbx_free(buffer);
	gzclose(gzfile);

	if (0 != rename(new_file, CONFIG_VCDUMP_LOCATION)) {
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't rename %s -> %s (%s)", new_file, CONFIG_VCDUMP_LOCATION,strerror(errno));
		return FAIL;
	}
*/
	zabbix_log(LOG_LEVEL_INFORMATION,"In %s: finished, total %d items, %d values dumped", __func__,items,vals);
	return SUCCEED;
}

int glb_cache_housekeep() {

    zbx_hashset_iter_t iter;
    glb_cache_elem_t *elem;
    int now = time(NULL);

    glb_rwlock_rdlock(&glb_cache->items.meta_lock); 
    
    //iterate on the items hash and remove outdated ones 
    zbx_hashset_iter_reset(&glb_cache->items.hset,&iter);
    
    while (NULL !=(elem=(glb_cache_elem_t *) zbx_hashset_iter_next(&iter))) {
        //don't care that much about locking or CPU caching
        //we are ok to deal with the outdated data
        if (elem->last_accessed < now - GLB_CACHE_MAX_DURATION) {
            if (0 == pthread_mutex_trylock(&elem->lock)) {
                //the element is quite outdated, removing it
                //first iterating on existing values to free
                //strings 

                //then 

                glb_lock_unlock(&elem->lock);
            }
            
        }
    }

    glb_rwlock_unlock(&glb_cache->items.meta_lock); 

}
#endif