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
#include "zbxvariant.h"
#include "log.h"
#include "zbxshmem.h"

#include "conf_items.h"
#include "./valuemaps/conf_valuemaps.h"


#define CONFIG_GLB_CONFIG_SIZE ZBX_GIBIBYTE
static  zbx_shmem_info_t	*glb_config_mem;

ZBX_SHMEM_FUNC_IMPL(__glb_conf, glb_config_mem);

typedef struct {
    mem_funcs_t memf;
} config_t;

static config_t *conf;


int glb_config_init() {
   
    char *error = NULL;
	
	if (SUCCEED != zbx_shmem_create(&glb_config_mem, (u_int64_t)2*CONFIG_GLB_CONFIG_SIZE, "GLB Config size", "GLBConfigSize", 0, &error)) {
        zabbix_log(LOG_LEVEL_CRIT,"Shared memory create failed: %s", error);
    	return FAIL;
	}
 	
	if (NULL == (conf = zbx_shmem_malloc(glb_config_mem, NULL, sizeof(config_t)))) {	
		zabbix_log(LOG_LEVEL_CRIT,"Cannot allocate Cache structures, exiting");
		return FAIL;
	}

	//LOG_INF("Created mem segment for glb config of size %lld", CONFIG_GLB_CONFIG_SIZE);
    memset((void *)conf, 0, sizeof(config_t));

    conf->memf.free_func = __glb_conf_shmem_free_func;
	conf->memf.malloc_func = __glb_conf_shmem_malloc_func;
	conf->memf.realloc_func = __glb_conf_shmem_realloc_func;

	glb_conf_valuemaps_init(&conf->memf);
	return SUCCEED;
}
