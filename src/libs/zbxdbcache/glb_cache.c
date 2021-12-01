/*
** Glaber
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

//#define GLB_CACHE_TESTS
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
#include "glb_lock.h"
#include "glb_cache_items.h"
#include <zlib.h>

extern char	*CONFIG_VCDUMP_LOCATION;

#define GLB_VCDUMP_RECORD_TYPE_ITEM 1
#define GLB_VCDUMP_RECORD_TYPE_VALUE 2

#define GLB_CACHE_MAX_DURATION  86400 

#define GLB_CACHE_ITEMS_INIT_SIZE   8192
#define GLB_CACHE_MIN_ITEM_VALUES   10 //reduce to 2 if too much mem will be used

#define GLB_CACHE_DEMAND_UPDATE 86400 //daily demand update is a good thing
#define GLB_CACHE_MIN_DURATION 2 //duration up to this is ignored and considered set to 0
#define GLB_CACHE_DEFAULT_DURATION 2 * 86400 //by default we downloading two days worth of data
                                            //however if duration is set, this might be bigger

#define	REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

extern u_int64_t CONFIG_VALUE_CACHE_SIZE;

static  zbx_mem_info_t	*cache_mem;

typedef struct {
    glb_cache_elems_t items;
    glb_cache_stats_t stats;
    zbx_hashset_t  strpool;
    pthread_rwlock_t strpool_lock;

} glb_cache_t;

static glb_cache_t *glb_cache;

#ifdef GLB_CACHE_TESTS
#include "glb_cache_tests.h"
#endif


/******************************************************
 * memory alloc handlers, //TODO: implement buffer queues
 * as a general mechanic, not only IPC or CACHE- specific
 * and use them here
 * ***************************************************/
void *glb_cache_malloc(void *old, size_t size) {
    void *buff = NULL;
	buff = zbx_mem_malloc(cache_mem,old,size);
	return buff;
}

void *glb_cache_realloc(void *old, size_t size) {
    void *buff = NULL;
    buff = zbx_mem_realloc(cache_mem,old,size);
    return buff;

}

void glb_cache_free(void *ptr) {
    if (NULL == ptr) 
        return;
	zbx_mem_free(cache_mem, ptr);  
}


static zbx_hash_t	__strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	__strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}

const char	*glb_cache_strpool_intern(const char *str)
{
	void		*record;
	zbx_uint32_t	*refcount;

	if (NULL == str) 
		return NULL;

    glb_rwlock_rdlock(&glb_cache->strpool_lock);
	record = zbx_hashset_search(&glb_cache->strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		glb_rwlock_unlock(&glb_cache->strpool_lock);
        glb_rwlock_wrlock(&glb_cache->strpool_lock);

        record = zbx_hashset_insert_ext(&glb_cache->strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);
		*(zbx_uint32_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	(*refcount)++;
    glb_rwlock_unlock(&glb_cache->strpool_lock);
	return (char *)record + REFCOUNT_FIELD_SIZE;
}


void	glb_cache_strpool_release(const char *str)
{
	zbx_uint32_t	*refcount;

	if ( NULL == str ) return;
	
	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
    glb_rwlock_wrlock(&glb_cache->strpool_lock);
	
    if (0 == --(*refcount))
		zbx_hashset_remove(&glb_cache->strpool, str - REFCOUNT_FIELD_SIZE);
    
    glb_rwlock_unlock(&glb_cache->strpool_lock);

}

const char *glb_cache_strpool_set_str(const char *old, const char *new) {
    
    glb_cache_strpool_release(old);
    
    return glb_cache_strpool_intern(new);
}


const char	*glb_cache_strpool_acquire(const char *str)
{
	zbx_uint32_t	*refcount;

	if (NULL == str) 
		return NULL;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

int glb_cache_init() {
   
    char *error = NULL;
	//zabbix_log(LOG_LEVEL_INFORMATION,"Allocating shared memory for glb cache size %ld",CONFIG_VALUE_CACHE_SIZE);
	//SHM
	if (SUCCEED != zbx_mem_create(&cache_mem, CONFIG_VALUE_CACHE_SIZE, "Cache cache size", "GLBCachesize", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Zbx mem create failed");
    	return FAIL;
    }
	//cache struct 
	if (NULL == (glb_cache = (glb_cache_t *)zbx_mem_malloc(cache_mem, NULL, sizeof(glb_cache_t)))) {	
		zabbix_log(LOG_LEVEL_CRIT,"Cannot allocate Cache structures, exiting");
		return FAIL;
	}
    
    memset((void *)glb_cache, 0, sizeof(glb_cache_t));
    
    zbx_hashset_create_ext(&glb_cache->items.hset, GLB_CACHE_ITEMS_INIT_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			glb_cache_malloc, glb_cache_realloc, glb_cache_free);

    zbx_hashset_create_ext(&glb_cache->strpool, 128,
			__strpool_hash, __strpool_compare, NULL,
			glb_cache_malloc, glb_cache_realloc, glb_cache_free);
	
	glb_cache->items.config = glb_cache_items_init(glb_cache_malloc, glb_cache_free);
	glb_cache->items.elem_create_func = glb_cache_item_create_cb;


#ifdef GLB_CACHE_TESTS
	LOG_INF("WILL RUN GLB CACHE TESTS");
	glb_run_cache_tests(glb_cache);
	//TEST_FAIL("Intentional stop");
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "%s:finished", __func__);
	return SUCCEED;
}


int  glb_ic_add_values( ZBX_DC_HISTORY *history, int history_num) {
	 return glb_cache_add_item_values(glb_cache->items.config, &glb_cache->items, history, history_num);
};


int	zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, const zbx_timespec_t *ts) {
	int now = time(NULL);
	
	return glb_ic_get_values(itemid, value_type,values, seconds, count, ts->sec);
}

int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value) {
	zbx_vector_history_record_t	values;
	int				ret = FAIL;

	zbx_history_record_vector_create(&values);
	DEBUG_ITEM(itemid, "Cache request: single value at %d",ts->sec);

	if (SUCCEED != glb_ic_get_values(itemid, value_type, &values, 0, 1, ts->sec)) {
		zbx_history_record_vector_destroy(&values, value_type);
		return FAIL;
	}
		
	*value = values.values[0];
//	reset values vector size so the returned value is not cleared when destroying the vector 
	values.values_num = 0;

	zbx_history_record_vector_destroy(&values, value_type);
	return SUCCEED;
}


int  glb_cache_item_update_meta(u_int64_t itemid, glb_cache_item_meta_t *meta, unsigned int flags, int value_type) {
	glb_cache_meta_udpate_req_t req = { .meta = meta, .flags = flags, .value_type = value_type};

	return glb_cache_process_elem(&glb_cache->items, itemid, glb_cache_item_update_meta_cb, &req);
}

int glb_cache_item_get_state(u_int64_t itemid) {
	return glb_cache_process_elem(&glb_cache->items, itemid, glb_cache_item_get_state_cb, NULL);
}
int glb_cache_item_get_nextcheck(u_int64_t itemid) {
	return glb_cache_process_elem(&glb_cache->items, itemid, glb_cache_item_get_nextcheck_cb, NULL);
}


int  glb_ic_get_values( u_int64_t itemid, int value_type, zbx_vector_history_record_t *values, int ts_start, int count, int ts_end) {
	LOG_DBG("In %s:starting itemid: %ld, count: %d", __func__,itemid, count );

	if (count > 0)  {
		return glb_cache_get_item_values_by_count(glb_cache->items.config, &glb_cache->items, itemid, value_type, values, count, ts_end);
	}

	return glb_cache_get_item_values_by_time(glb_cache->items.config, &glb_cache->items, itemid, value_type, values, ts_start, ts_end);
};

static int glb_cache_add_elem(glb_cache_elems_t *elems, uint64_t id ) {
	int ret;
	glb_cache_elem_t *elem, new_elem ={0};
	elem_update_func_t elem_create_func = elems->elem_create_func;
	
	if (NULL == elem_create_func)
		return FAIL;
    LOG_DBG("Doing wrlock for id %ld", id);
	glb_rwlock_wrlock(&elems->meta_lock);
	LOG_DBG("Wr locked for id %ld", id);
	
	if (NULL != zbx_hashset_search(&elems->hset, &id)) {
		LOG_DBG("Element id %ld exists, unlocking, exiting", id);
		glb_rwlock_unlock(&glb_cache->items.meta_lock);
		return FAIL;	
	}

	new_elem.id = id;
	
	if (NULL != (elem = zbx_hashset_insert(&elems->hset, &new_elem, sizeof(new_elem)))) {
		
		LOG_DBG("Calling for create function for id %ld",id);
		ret = elem_create_func(elem,elems->config);
		LOG_DBG("Create function for id %ld finished",id);	

		if (ret == FAIL) 
			zbx_hashset_remove(&elems->hset,&id);

	} else 
		ret = FAIL;
	LOG_DBG("Unlocking id %ld", id);
	glb_rwlock_unlock(&elems->meta_lock);
	
	return ret;
}

int glb_cache_process_elem(glb_cache_elems_t *elems, uint64_t id, elem_update_func_t process_func, void *data) {
	int ret = FAIL;
    
	LOG_DBG("In %s:starting", __func__);
    glb_cache_elem_t *elem;
    
	if (NULL == process_func)
		return FAIL;

	glb_rwlock_rdlock(&elems->meta_lock);
    LOG_DBG("In %s:locked, id is %ld", __func__,id);

	if (NULL == (elem = zbx_hashset_search(&elems->hset,&id))) {
		glb_rwlock_unlock(&elems->meta_lock);

		LOG_DBG("In %s:rd Unlocked, elem not found doing add elem %ld", __func__,id);

		if (FAIL == glb_cache_add_elem(elems,id)) {
			return FAIL;
		}

		LOG_DBG("In %s:Elem added elem, rdlocking %ld", __func__,id);
		glb_rwlock_rdlock(&elems->meta_lock);
		
		if (NULL == (elem = zbx_hashset_search(&elems->hset,&id))) {
			THIS_SHOULD_NEVER_HAPPEN;
			LOG_DBG("In %s:Elem %ld not found, exiting", __func__,id);
			glb_rwlock_unlock(&elems->meta_lock);
			return FAIL;
		}
	} 
    
	LOG_DBG("In %s:Elem blocking element %ld", __func__,id);
    glb_lock_block(&elem->lock);
	LOG_DBG("In %s:Elem blocked, calling callback", __func__);
	ret = process_func(elem,data);
	LOG_DBG("In %s:Elem callback finished, unblocking elment %ld" , __func__, id);
    glb_lock_unlock(&elem->lock);
	LOG_DBG("In %s:Elem callback finished, unblocking elmen %ld" , __func__, id);	
	glb_rwlock_unlock(&elems->meta_lock);
	return ret;
} 
	


/**********************************************************
 * fetches element in the locked state                    *
 * ********************************************************/
glb_cache_elem_t *glb_cache_get_elem(glb_cache_elems_t* elems,  u_int64_t id) {
    
    void *ret = NULL;
    
    glb_cache_elem_t *elem;
    glb_rwlock_rdlock(&elems->meta_lock);
    
    if (NULL != (elem=zbx_hashset_search(&elems->hset,&id))) {
        glb_lock_block(&elem->lock);
        ret = elem;
    } 
    glb_rwlock_unlock(&glb_cache->items.meta_lock);
    return ret;
}


int glb_cache_get_mem_stats(zbx_mem_stats_t *mem_stats) {
    
    memset(&mem_stats, 0, sizeof(zbx_mem_stats_t));
	zbx_mem_get_stats(cache_mem, mem_stats);
  
}

int glb_cache_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode) {
}
/*
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
*/

int glb_cache_get_statistics(glb_cache_stats_t *stats) {
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
		item_stats->itemid = elem->id;
		//TODO: change this to overall cache statistics or move to the items
        //item_stats->values_num = elem->values->elem_num;
		zbx_vector_ptr_append(stats, item_stats);
	}

    glb_rwlock_unlock(&glb_cache->items.meta_lock);
}


int glb_cache_items_get_state_json(zbx_vector_uint64_t *itemids, struct zbx_json *json) {
    
    int i;
    zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    glb_cache_meta_udpate_req_t req;

    for (i=0; i < itemids->values_num; i++) {

      //  zabbix_log(LOG_LEVEL_INFORMATION, "%s: processing item %ld", __func__,itemids->values[i]);
        if ( FAIL != glb_cache_process_elem(&glb_cache->items, itemids->values[i], glb_cache_item_get_meta_cb, &req) ) {

            zbx_json_addobject(json,NULL);
			zbx_json_adduint64(json,"itemid",itemids->values[i]);
            zbx_json_adduint64(json,"lastdata", req.meta->lastdata);
            zbx_json_adduint64(json,"nextcheck", req.meta->nextcheck);
            zbx_json_adduint64(json,"state", req.meta->state);
            
            if (NULL != req.meta->error)
                zbx_json_addstring(json,"error",req.meta->error,ZBX_JSON_TYPE_STRING );

            zbx_json_close(json);
            
            DEBUG_ITEM(itemids->values[i], "Added info to the trapper status request");
        }
    }
    zbx_json_close(json); 
}


int glb_cache_get_items_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count) {
	glb_cache_elem_t *elem;
	int i, j, rcount;

	if (count < 1 ) 
        count = GLB_CACHE_MIN_COUNT;

	item_last_values_req_t req = {.count = count, .json = json};
    
	
    LOG_DBG("%s: starting, requested %d items count %d", __func__,itemids->values_num, count);
	zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);
    
    for (i=0; i<itemids->values_num; i++) {
		DEBUG_ITEM(itemids->values[i], "Item is requested from the value cache, count %d", count);
		glb_cache_process_elem(&glb_cache->items,itemids->values[i],glb_cache_item_values_json_cb, &req);
	}	
 
  	zbx_json_close(json); //closing the items array
	LOG_DBG("Result is %s: ",json->buffer);
  	zabbix_log(LOG_LEVEL_DEBUG, "%s: finished: response is: %s", __func__,json->buffer);
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
	
	zabbix_log(LOG_LEVEL_DEBUG, "Reading valuecache from %s",CONFIG_VCDUMP_LOCATION);
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
	zabbix_log(LOG_LEVEL_DEBUG,"Finished loading valuecache data, loaded %d items; %d values",items,vals);
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

	zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
	
    if (NULL == CONFIG_VCDUMP_LOCATION )
	 		return FAIL;
	
	//zabbix_log(LOG_LEVEL_INFORMATION, "Will dump value cache to %s",CONFIG_VCDUMP_LOCATION);
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
	zabbix_log(LOG_LEVEL_DEBUG,"In %s: finished, total %d items, %d values dumped", __func__,items,vals);
	return SUCCEED;
}

int glb_cache_housekeep() {
} /*
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
*/


#endif