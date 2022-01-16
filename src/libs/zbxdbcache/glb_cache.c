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

	if (SUCCEED != zbx_mem_create(&cache_mem, CONFIG_VALUE_CACHE_SIZE, "Cache cache size", "GLBCachesize", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Zbx mem create failed");
    	return FAIL;
    }
 
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
	
	glb_cache->items.config = glb_cache_items_init();
	glb_cache->items.elem_create_func = glb_cache_item_create_cb;
	glb_rwlock_init(&glb_cache->items.meta_lock);
	
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
//	int now = time(NULL);
	
	return glb_ic_get_values(itemid, value_type,values, seconds, count, ts->sec);
}

int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value) {
	zbx_vector_history_record_t	values;
	//int				ret = FAIL;

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

int glb_cache_add_elem(glb_cache_elems_t *elems, uint64_t id ) {
	int ret;
	int lock_time;

	glb_cache_elem_t *elem, new_elem ={0};
	elem_update_func_t elem_create_func = elems->elem_create_func;
	zbx_timespec_t ts_start;

	if (NULL == elem_create_func)
		return FAIL;
    LOG_DBG("Doing wrlock for id %ld", id);
	glb_rwlock_wrlock(&elems->meta_lock);
	LOG_DBG("Wr locked for id %ld", id);
	zbx_timespec(&ts_start);
	
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

	if (1000 < (lock_time = zbx_get_duration_ms(&ts_start))) {
		LOG_INF("LOCK: A wr lock has took too much time: %d msec", lock_time);
		THIS_SHOULD_NEVER_HAPPEN;
	}
	
	return ret;
}

int glb_cache_process_elem(glb_cache_elems_t *elems, uint64_t id, elem_update_func_t process_func, void *data) {
	int ret;
    
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



//TODO: this must be a universal function
//which accepts cache table, filename, parsing and cration callbacks
int glb_vc_load_items_cache() {

	FILE *fp;
	gzFile gzfile;

	//size_t read, len =0;
	char buffer[MAX_STRING_LEN];
	size_t alloc_len =0, alloc_offset = 0;

	char *json_buffer = NULL;
	int items = 0, vals = 0;
	int lcnt = 0;
	
	struct zbx_json_parse jp;

	zbx_json_type_t j_type;
	char id_str[MAX_ID_LEN];
	
	zabbix_log(LOG_LEVEL_DEBUG, "Reading valuecache from %s",CONFIG_VCDUMP_LOCATION);
	
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

	while (Z_NULL != gzgets(gzfile, buffer, MAX_STRING_LEN) ) {

		zbx_snprintf_alloc(&json_buffer,&alloc_len,&alloc_offset,"%s",buffer);
		
		if (SUCCEED != zbx_json_open(json_buffer, &jp)) {
			if ( (lcnt > 0 &&  0 == strncmp(buffer,"{\"itemid\":",10)) || 
				  (lcnt == 0 && json_buffer[0] != '{') )
			 {
				LOG_WRN("Garbage in the dump detected, line count %d dropping data, starting from new line: %s", lcnt, json_buffer);
				lcnt = 0;
				alloc_offset =0;
				continue;
			}
			lcnt++;
			continue;
		}

		
		if (SUCCEED != zbx_json_value_by_name(&jp, "itemid", id_str, MAX_ID_LEN, &j_type)) {
        	LOG_INF("Couldn't find id in the JSON '%s':",json_buffer);
        	continue;
    	}
		u_int64_t id = strtol(id_str,NULL,10);

		if (id == 0)
			LOG_INF("Couldn't find id in the JSON");

		alloc_offset = 0;
		lcnt = 0;
		items++;
		//creating the new element

		//and calling unmarshall callback for it
		glb_cache_process_elem(&glb_cache->items, id, glb_cache_items_umarshall_item_cb, &jp);

    }
	gzclose(gzfile);
	zbx_free(json_buffer);

	LOG_INF("Finished loading valuecache data, loaded %d items; %d values",items,vals);
//	sleep(2);

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
	struct zbx_json	json;
	int items=0, vals=0;
	char new_file[MAX_STRING_LEN], tmp[MAX_STRING_LEN];

	zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);
	
    if (NULL == CONFIG_VCDUMP_LOCATION )
	 		return FAIL;
	
	zbx_snprintf(new_file,MAX_STRING_LEN,"%s%s",CONFIG_VCDUMP_LOCATION,".new");
	
	gzFile gzfile = gzopen(new_file,"wb");

	if (Z_NULL == gzfile) {
		zabbix_log(LOG_LEVEL_WARNING, "Cannot open file %s, value cache will not be dumped",new_file);
		return FAIL;
	}
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

    glb_rwlock_rdlock(&glb_cache->items.meta_lock);
    zbx_hashset_iter_reset(&glb_cache->items.hset,&iter);
		 
	while (NULL != (elem=(glb_cache_elem_t*)zbx_hashset_iter_next(&iter))) {
		zbx_json_clean(&json);
		
		glb_lock_block(&elem->lock);
		int ret = glb_cache_items_marshall_item_cb(elem, &json);
		glb_lock_unlock(&elem->lock);
	   
		if (0 < ret) {
        	if ( 0 >= gzwrite(gzfile, json.buffer, json.buffer_offset + 1))	{
				zabbix_log(LOG_LEVEL_WARNING,"Cannot write to cache %s, errno is %d",new_file,errno);
				break;
			}	
			gzwrite(gzfile,"\n",1);
		}
		
		items++;
		vals+=ret;

	}
	glb_rwlock_unlock(&glb_cache->items.meta_lock);
	
	zbx_json_free(&json);
	gzclose(gzfile);

	if (0 != rename(new_file, CONFIG_VCDUMP_LOCATION)) {
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't rename %s -> %s (%s)", new_file, CONFIG_VCDUMP_LOCATION,strerror(errno));
		return FAIL;
	}
	
	LOG_INF("In %s: finished, total %d items, %d values dumped", __func__,items,vals);
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