#ifndef GLB_CACHE_H
#define GLB_CACHE_H
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

//how long we learn new ranges per item
#define GLB_CACHE_LEARN_RANGE   24*8600
//extern int CONFIG_STATE_MEM_SIZE;
extern size_t CONFIG_VALUE_CACHE_SIZE;

#define  GLB_CACHE_ITEM_UPDATE_LASTDATA     0x01
#define  GLB_CACHE_ITEM_UPDATE_NEXTCHECK    0x02
#define  GLB_CACHE_ITEM_UPDATE_STATE        0x04

typedef struct {
    int state;
    int lastdata;
    int nextcheck;
} glb_cache_item_meta_t;

typedef struct {
    int data_size; //data element size
    int data_num; //currently allocated 
    void *data; //actual link to data buffer
    int start; //data is circular buffer, so start and end point  (indexes) at the places where data 
    int end;   //is in the buffer 
} glb_cache_cbuf_t;

typedef struct {
    unsigned int ts_sec;
    zbx_variant_t value;

} glb_cache_item_value_t;

//for types of data requiring 
//typedef struct {  
//    u_int64_t id;
 
//    void *metadata; //item-specific settings (item state,mtime,lastcheck,nextcheck for items, trigger state for triggers)
//    glb_cache_cbuf_t* cache; //hold time-based cache of object's states    
    
// } glb_cache_element_t; //this is one element holding cashed timeseries in circular buffer for 
                   //one item

/* the cache statistics  - taken from valucache.h for compatibility*/
typedef struct
{
	/* in glaber cache hits/misses are measured by requests (in Zabbix it's by metrics)
    but it's more honest and important to judge if request was full filled or not */

	zbx_uint64_t	hits;
	zbx_uint64_t	misses;

	zbx_uint64_t	total_size;
	zbx_uint64_t	free_size;
}
glb_cache_stats_t;

/* item diagnostic statistics (either from valucache.c) */
typedef struct
{
	zbx_uint64_t	itemid;
	int		values_num;
}
glb_cache_item_stats_t;

int	zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, const zbx_timespec_t *ts);
int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value);
        
int  glb_cache_add_values(zbx_vector_ptr_t *hist_values);
int glb_cache_get_item_values(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, int ts_end);
int glb_cache_get_statistics(glb_cache_stats_t *stats);
int glb_vc_load_cache();
int glb_cache_get_mem_stats(zbx_mem_stats_t *mem_stats);
int glb_cache_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode);
void glb_cache_get_item_stats(zbx_vector_ptr_t *stats);

int glb_cache_update_item_meta(u_int64_t itemid, glb_cache_item_meta_t *meta, unsigned int flags);
int glb_cache_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count);

int glb_cache_housekeep();
int glb_cache_init();
void glb_cache_destroy(void);
#endif