#ifndef GLB_CACHE_H
#define GLB_CACHE_H

#include "zbxalgo.h"
#include "zbxhistory.h"
#include "log.h"
#include "zbxshmem.h"


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
void glb_state_destroy(void);

int glb_state_load();

int glb_state_get_statistics(glb_state_stats_t *stats);
int glb_state_get_mem_stats(zbx_shmem_stats_t *mem_stats);
int glb_state_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode);

int glb_state_housekeep();
int glb_state_dump(); 


#endif