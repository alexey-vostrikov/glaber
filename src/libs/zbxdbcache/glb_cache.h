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
//#include "glb_cache_items.h"
//#include "glb_cache_items.h"


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
#define  GLB_CACHE_ITEM_UPDATE_ERRORMSG     0x08
#define  GLB_CACHE_ITEM_UPDATE_ERRORCODE    0x10

#define GLB_CACHE_MIN_COUNT     10
#define GLB_CACHE_MAX_COUNT     1024*1024 

#define GLB_CACHE_ELEM_NOT_FOUND		-2
#define GLB_CACHE_CANNOT_CREATE			-3

typedef struct {
    u_int64_t id;
    pthread_mutex_t lock;
    void *data;
} glb_cache_elem_t; 

typedef int	(*elem_update_func_t)(glb_cache_elem_t *elem, void *params);

typedef struct 
{
    zbx_hashset_t hset;
    pthread_rwlock_t meta_lock; 
	
	elem_update_func_t elem_create_func;
	void *config;
} glb_cache_elems_t; 


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

typedef struct {
    int state;
    int lastdata;
    int nextcheck;
    const char *error;
    int errcode;
} glb_cache_item_meta_t;

typedef struct {
    glb_cache_item_meta_t *meta;
    u_int64_t flags;
    unsigned char value_type;
} glb_cache_meta_udpate_req_t; 


typedef struct {
    int count;
    struct zbx_json *json;
} item_last_values_req_t;


int glb_cache_init();

int glb_ic_add_values( ZBX_DC_HISTORY *history, int history_num);
int	glb_ic_get_values( u_int64_t itemid, int value_type, zbx_vector_history_record_t *values, int ts_start, int count, int ts_end);


int glb_cache_process_elem(glb_cache_elems_t *elems, uint64_t id, elem_update_func_t process_func, void *data);
int	glb_cache_add_elem(glb_cache_elems_t *elems, uint64_t id);
int	glb_cache_item_update_meta(u_int64_t itemid, glb_cache_item_meta_t *meta, unsigned int flags, int value_type);
int glb_cache_item_get_nextcheck(u_int64_t itemid);
int glb_cache_item_get_state(u_int64_t itemid);

int glb_cache_items_get_state_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);

const char	*glb_cache_strpool_intern(const char *str);
const char	*glb_cache_strpool_set_str(const char *old, const char *new);
const char	*glb_cache_strpool_acquire(const char *str);
void		glb_cache_strpool_release(const char *str);

/************************************
************************************
Untested below
*/






int	zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
		int count, const zbx_timespec_t *ts);
int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value);
        
//int glb_cache_add_values(ZBX_DC_HISTORY *history, int history_num);
int glb_cache_update_elem(glb_cache_elems_t elems, u_int64_t id, elem_update_func_t func, void *request);

int glb_cache_get_statistics(glb_cache_stats_t *stats);
//int glb_vc_load_cache();
int glb_cache_get_mem_stats(zbx_mem_stats_t *mem_stats);
int glb_cache_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode);

void *glb_cache_malloc(void *old, size_t size);
void glb_cache_free(void *ptr);


int glb_cache_housekeep();

void glb_cache_destroy(void);

int     glb_cache_get_items_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json, int count) ;
int     glb_cache_get_items_status_json(zbx_vector_uint64_t *itemids, struct zbx_json *json);
void    glb_cache_get_item_stats(zbx_vector_ptr_t *stats);
int     glb_cache_get_item_state(u_int64_t itemid);


int     glb_cache_get_values_by_count(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int count, int ts_end);

int     glb_cache_get_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds, int ts_end);

//int	zbx_vc_get_value(u_int64_t hostid, zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value);
//int	zbx_vc_get_values(zbx_uint64_t hostid, zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
//		int count, const zbx_timespec_t *ts);
#endif