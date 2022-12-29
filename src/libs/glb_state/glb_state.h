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
//#include "glb_state_items.h"
//#include "glb_state_items.h"


//there are two type of data - cache (several records related to an object)
#define STATE_ITEMS_INIT_SIZE (1000)
#define STATE_STRPOOL_INIT_SIZE	(1000)

//how long we learn new ranges per item
#define GLB_CACHE_LEARN_RANGE   24*8600
extern size_t CONFIG_VALUE_CACHE_SIZE;

#define GLB_CACHE_MIN_COUNT     10
#define GLB_CACHE_MAX_COUNT     1024*1024 

#define GLB_CACHE_ELEM_NOT_FOUND		-2
#define GLB_CACHE_CANNOT_CREATE			-3

typedef struct
{
	/* in glaber cache hits/misses are measured by requests (in Zabbix it's by metrics)
    but it's more honest and important to judge if request was full filled or not */

	zbx_uint64_t	hits;
	zbx_uint64_t	misses;

	zbx_uint64_t	total_size;
	zbx_uint64_t	free_size;
}
glb_state_stats_t;

/* item diagnostic statistics (either from valucache.c) */
typedef struct
{
	zbx_uint64_t	itemid;
	int		values_num;
}
glb_state_item_stats_t;



int glb_state_init();

int glb_state_get_statistics(glb_state_stats_t *stats);

int glb_state_get_mem_stats(zbx_shmem_stats_t *mem_stats);
int glb_state_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode);

int glb_state_housekeep();
void glb_state_destroy(void);



#endif