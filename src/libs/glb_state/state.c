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
#ifndef GLB_STATE_C
#define GLB_STATE_C

#include "zbxvariant.h"
#include "log.h"
#include "memalloc.h"
#include "state.h"
#include "state_discovery.h"
#include "state_items.h"
#include "state_triggers.h"
#include "state_problems.h"
//#include "state_events.h"
#include "state_incidents.h"

#define GLB_VCDUMP_RECORD_TYPE_ITEM 1
#define GLB_VCDUMP_RECORD_TYPE_VALUE 2

#define	REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

extern u_int64_t CONFIG_VALUE_CACHE_SIZE;

static  zbx_mem_info_t	*cache_mem;

typedef struct {
    glb_state_stats_t stats;
	mem_funcs_t memf;
} glb_state_t;

static glb_state_t *glb_cache;

ZBX_MEM_FUNC_IMPL(__cache, cache_mem);

int glb_state_init() {
   
    char *error = NULL;
	
	if (SUCCEED != zbx_mem_create(&cache_mem, CONFIG_VALUE_CACHE_SIZE, "State cache size", "GLBCachesize", 1, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	return FAIL;
    }
 
	if (NULL == (glb_cache = (glb_state_t *)zbx_mem_malloc(cache_mem, NULL, sizeof(glb_state_t)))) {	
		zabbix_log(LOG_LEVEL_CRIT,"Cannot allocate Cache structures, exiting");
		return FAIL;
	}
    
    memset((void *)glb_cache, 0, sizeof(glb_state_t));
	
	glb_cache->memf.free_func = __cache_mem_free_func;
	glb_cache->memf.malloc_func = __cache_mem_malloc_func;
	glb_cache->memf.realloc_func = __cache_mem_realloc_func;

	if (SUCCEED != glb_state_items_init(&glb_cache->memf) )
		return FAIL;
	
	if (SUCCEED != glb_state_triggers_init(&glb_cache->memf) )
		return FAIL;
	
	if (SUCCEED != discovery_init(&glb_cache->memf) )
		return FAIL;

//	if (SUCCEED != state_events_init(&glb_cache->memf) )
//		return FAIL;

	if (SUCCEED != state_incidents_init(&glb_cache->memf) )
		return FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "%s:finished", __func__);
	return SUCCEED;
}



int glb_state_get_mem_stats(zbx_mem_stats_t *mem_stats) {
    
    memset(&mem_stats, 0, sizeof(zbx_mem_stats_t));
	zbx_mem_get_stats(cache_mem, mem_stats);
  
}

int glb_state_get_diag_stats(u_int64_t *items_num, u_int64_t *values_num, int *mode) {
}

int glb_state_get_statistics(glb_state_stats_t *stats) {
    stats->hits = glb_cache->stats.hits;
	stats->misses = glb_cache->stats.misses;
	stats->total_size = cache_mem->total_size;
	stats->free_size = cache_mem->free_size;
}


void	glb_state_destroy(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
   
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

int glb_state_housekeep() {

} 

#endif