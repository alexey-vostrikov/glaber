/*one cache is expected to store an information of of *
* some kind, like items or values or states            *
*/

//to make the access less concurent CAHCE must be split to N
//N should be the number of the processed
#include "zbxalgo.h"
#include "zbxhistory.h"
#include "log.h"


//there are two type of data - cache (several records related to an object)
#define STATE_ITEMS_INIT_SIZE (1000)
#define STATE_STRPOOL_INIT_SIZE	(1000)

extern int CONFIG_PREPROCMAN_FORKS;
extern int CONFIG_STATE_MEM_SIZE;
extern int CONFIG_VALUE_CACHE_SIZE;

typedef struct {  
    u_int64_t id;
    
    
    //TODO: the following 5 elements have to go to meta 
    //instead of just being placed here, since they are pretty item-specific
    //and events will have different metadata

    /* The range of the largest request in seconds/num             */
	/* Used to determine if data can be removed from cache.       */
	int		active_range;
    int     active_num;
    /* The range for last 24 hours since active_range update.     */
	/* Once per day the active_range is synchronized (updated)    */
	/* with daily_range and the daily range is reset.             */
	int		daily_range;
    int     daily_num;
    
    /* the hour when the current/global range sync was done       */
    unsigned char	range_sync_hour;

    //maybe a union should be used here, however this might cause 
    //an extra memory usage in case if some specific type has too large metadata
    //so far will use double allocation

    void *meta; //item-specific settings (item state,mtime,lastcheck,nextcheck for items, trigger state for triggers)
  //  void *element_state;
  
    int data_size;
    int data_num;

    void *data;//array of data allocated, data is cache-type specific
                               //but first four bytes expected to hold time
    int start; //data is circular buffer, so start and end point at the places where data 
    int end;   //is in the buffer
    
 } GLB_CACHE_TS_ELEMENT; //this is one element holding cashed timeseries in circular buffer for 
                   //one item
typedef struct {
    unsigned int mtime;
    unsigned int nextcheck;
    //unsigned int lastcheck;// maybe theese two isn;t needed
    //unsigned char lastcode;
} GLB_ITEM_STATE;

typedef struct {
    unsigned char state;

} GLB_TRIGGER_STATE;


typedef struct {
    //GLB_CACHE_TABLE *values;
    zbx_hashset_t  items; //hashmap holds GLB_CACHE_TS_ELEMENT of zbx_history_values
   // zbx_hashset_t  events; //hashmap holds GLB_CACHE_TS_ELEMENT of events (struct to be defined yet)

    zbx_hashset_t  strpool; //strings, deduplicated
    zbx_rwlock_t   lock; //lock for table access
    
    //memory handling functions
    zbx_mem_malloc_func_t	mem_malloc_func;
	zbx_mem_realloc_func_t	mem_realloc_func;
	zbx_mem_free_func_t	mem_free_func;

} GLB_STATE;

static GLB_STATE *state = NULL;
static zbx_rwlock_t	*lock = NULL;

static zbx_mem_info_t	*vc_mem = NULL;
ZBX_MEM_FUNC_IMPL(__vc, vc_mem)

/******************************************************************************
 *                                                                            *
 * String pool definitions & functions, sourced from valuecache.c             *
 *                                                                            *
 ******************************************************************************/

#define REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)
static zbx_hash_t	vc_strpool_hash_func(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	vc_strpool_compare_func(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}


/****************************************************************
 * inits structures and allocates shared memory for the 
 * state cache.
 * *************************************************************/
int glb_init_state(char **error) {

    int i, ret = FAIL;

    //creating locks
    //maybe they should either go to VC, not stay in heap
    lock = (zbx_rwlock_t *) zbx_malloc(NULL, sizeof(zbx_rwlock_t) * CONFIG_PREPROCMAN_FORKS);
    
    //TODO: name the memory and functions differently
    if (SUCCEED != zbx_mem_create(&vc_mem, CONFIG_VALUE_CACHE_SIZE, "value cache size", "ValueCacheSize", 1, error))
		goto out;
    
    state = (GLB_STATE *)__vc_mem_malloc_func(NULL, sizeof(GLB_STATE) * CONFIG_PREPROCMAN_FORKS);
	
    if (NULL == state) {
		*error = zbx_strdup(*error, "cannot allocate state structure data");
		goto out;
	}

	memset(state, 0, sizeof(GLB_STATE) * CONFIG_PREPROCMAN_FORKS);

    //now creating as many caches as there are preprocessors 
    for (i = 0; i < CONFIG_PREPROCMAN_FORKS; i++) {
        zabbix_log(LOG_LEVEL_INFORMATION, "Init state cache %d out of %d", i+1, CONFIG_PREPROCMAN_FORKS);
        
        if (SUCCEED != (ret = zbx_rwlock_create(&lock[i], ZBX_RWLOCK_VALUECACHE, error)))
		    return FAIL;
        
        zbx_hashset_create_ext(&state[i].items, STATE_ITEMS_INIT_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);
        if (NULL == state[i].items.slots) {
		    *error = zbx_strdup(*error, "cannot allocate items cache table storage");
		    goto out;
        }

        zbx_hashset_create_ext(&state[i].strpool, STATE_STRPOOL_INIT_SIZE,
			vc_strpool_hash_func, vc_strpool_compare_func, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);

	    if (NULL == state[i].strpool.slots) {
		    *error = zbx_strdup(*error, "cannot allocate string pool for state data storage");
		    goto out;
	    }
	}

    ret = SUCCEED;
    
 out:
  
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
	return ret;
}
/******************************************************************************
* Destroy state cache and all the structures                                 *
******************************************************************************/
void	glb_state_destroy(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL != state)
	{
		int i;
        
        for (i = 0; i < CONFIG_PREPROCMAN_FORKS; i++ ) {
		    zbx_rwlock_destroy(&lock[i]);
    		zbx_hashset_destroy(&state[i].items);
		    zbx_hashset_destroy(&state[i].strpool);
        }

		__vc_mem_free_func(state);
		state = NULL;
        zbx_free(lock);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/***********************************************************
 * lock/unlock operations
 * *********************************************************/
//to calc whch cache we are working with, using host id to find its cache piece
#define	RDLOCK_CACHE(hostid)	zbx_rwlock_rdlock(lock[hostid % CONFIG_PREPROCMAN_FORKS]);
#define	WRLOCK_CACHE(hostid)	zbx_rwlock_wrlock(lock[hostid % CONFIG_PREPROCMAN_FORKS]);
#define	UNLOCK_CACHE(hostid)	zbx_rwlock_unlock(lock[hostid % CONFIG_PREPROCMAN_FORKS]);

/************************************************
 * Cache and state manupulating functions       *
 * **********************************************/
static glb_state_item_add_value(ts_item,const zbx_item_diff_t * diff) {

}


/********************************************************************
 * there could be two sources of values:                            *
 * one comes from DC_update_state in item_diff                      *
 *   in this case we need to add value type to the item diff        *                        
 * another is when a trigger requests data missing in               *
 * the cache and then history backend returns vector of metrics     *
 * ******************************************************************/
int glb_state_add_values(const zbx_vector_ptr_t *item_diff) {
    
    int i, state_idx=0;
    const zbx_item_diff_t	*diff;
    GLB_CACHE_TS_ELEMENT *ts_item;

    if (0 == item_diff->values_num)
		return;
    
    for (i = 0; i < item_diff->values_num; i++) {
        diff = (const zbx_item_diff_t *)item_diff->values[i];

        //only relocking cache if host relates to another cache piece or hasn't been locked yet
        if (i = 0) {
            state_idx = diff->hostid % CONFIG_PREPROCMAN_FORKS;
            WRLOCK_CACHE(state_idx);
        } else if ( state_idx != diff->hostid % CONFIG_PREPROCMAN_FORKS ) {
            UNLOCK_CACHE(state_idx);
            state_idx = diff->hostid % CONFIG_PREPROCMAN_FORKS;
            WRLOCK_CACHE(state_idx);
        }

        //adding the new data: first fetching the item 
        if (NULL == (ts_item = (GLB_CACHE_TS_ELEMENT*)zbx_hashset_search(&state[state_idx].items,&diff[i].itemid ))) {
            //oops, there is no item (yet), adding a new one 
            GLB_CACHE_TS_ELEMENT ts_item_loc;
            bzero(&ts_item_loc,sizeof(GLB_CACHE_TS_ELEMENT));
            //initial data size is 2 elements, not less
            ts_item_loc.data = __vc_mem_malloc_func(NULL, 2 * sizeof(zbx_history_record_t));
            zbx_history_record_t *dst = (zbx_history_record_t *)ts_item_loc.data;
            dst->value = diff->value;
            dst->timestamp = diff->mtime;

            ts_item=(GLB_CACHE_TS_ELEMENT*)zbx_hashset_insert(&state[state_idx].items,&ts_item_loc,sizeof(GLB_CACHE_TS_ELEMENT));
        }
        //adding the value to the item
        glb_state_item_add_value(ts_item,diff);
    }
    UNLOCK_CACHE(last_lock_idx);

}
//int glb_state_add_values



void glb_cache_housekeep() {

}

int glb_cache_add_data(int table, int lockid, u_int64_t dataid, void *data) {

    return SUCCEED;    
}

int glb_cache_get_data(int table, int lockid, u_int64_t dataid, u_int64_t start, 
            u_int64_t end, int count, void *callback() ) {

    return SUCCEED;    
}

void *glb_cache_get_meta(int table, int lockid, u_int64_t dataid, void *meta) {

    return SUCCEED;    
}

int glb_cache_set_meta(int table, int lockid, u_int64_t dataid, void *meta) {

    return SUCCEED;    
}
