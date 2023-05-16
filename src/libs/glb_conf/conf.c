/*
** Copyright Glaber 2018-2023
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
#include "zbxjson.h"

#include "./items/conf_items.h"
#include "./valuemaps/conf_valuemaps.h"
#include "./scripts/scripts.h"
#include "conf.h"
#include "api_sync.h"


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
	glb_conf_scripts_init(&conf->memf);
    glb_conf_api_sync_init(&conf->memf); 
    
    return SUCCEED;
}

/*for table-based data to process the complete set at once*/
int glb_conf_iterate_on_set_data(char *json_buff, char *id_name, 
			elems_hash_t *elems, elems_hash_process_cb_t create_update_func){
    struct zbx_json_parse	jp, jp_result, jp_element;
    const char *json_element = NULL;
    int err;
    zbx_vector_uint64_t ids;
    zbx_vector_uint64_create(&ids);
    zbx_vector_uint64_reserve(&ids, 65536);

    if (SUCCEED != zbx_json_open(json_buff, &jp) ||
        SUCCEED != zbx_json_brackets_by_name(&jp, "result", &jp_result)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON data (broken or malformed json) %s", json_buff);
		return FAIL;
	}
    
    while (NULL != (json_element = zbx_json_next(&jp_result, json_element))) {
        if (SUCCEED == zbx_json_brackets_open(json_element, &jp_element)) {
           
            u_int64_t id = glb_json_get_uint64_value_by_name(&jp_element, id_name, &err);

            if (err)
                continue;

            if (SUCCEED == elems_hash_process(elems, id, create_update_func, &jp_element, 0)) 
                zbx_vector_uint64_append(&ids, id);
        }
    }

    elems_hash_remove_absent_in_vector(elems, &ids);
    zbx_vector_uint64_destroy(&ids);
    
    return SUCCEED;
}
/*for parsing arrays of objects that belongs to the same object - parses the array, 
  allocates the necessary mem and calls callback to fill the data per-subobject
  unlike elems hash - it's just an array without indexing, thow good for the compact storage
  for rather small objects number
  */
// int glb_conf_json_array_parse(struct zbx_json_parse *jp_array, mem_funcs_t *memf, strpool_t *strpool, size_t element_size, element_parse_callback cb_func) {
    
//     void *arr = NULL;
//     int n = glb_json_get_array_elem_count(jp_array);

//     if ( 0 == n)
//         return SUCCEED;

    


// }


int glb_conf_add_json_param_strpool(struct zbx_json_parse *jp, strpool_t *strpool, char *name, const char **addr) {
	char tmp[MAX_STRING_LEN];
	zbx_json_type_t jtype;

	if (SUCCEED == zbx_json_value_by_name(jp, name, tmp, MAX_STRING_LEN, &jtype ))  {
		*addr =(char *) strpool_add(strpool, tmp);
		return SUCCEED;
	}
	*addr = NULL;
	return FAIL;
}

