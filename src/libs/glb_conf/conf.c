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

int glb_conf_add_json_param_memf(struct zbx_json_parse *jp, mem_funcs_t *memf, char *name, char **addr) {
	char tmp[MAX_STRING_LEN];
    size_t len;
	zbx_json_type_t jtype;

	if (SUCCEED == zbx_json_value_by_name(jp, name, tmp, MAX_STRING_LEN, &jtype ))  {
		len = strlen(tmp); 
        *addr = memf->malloc_func(NULL, len + 1);
        zbx_strlcpy(*addr, tmp, len);
        return SUCCEED;
	}
	
    *addr = NULL;
	return FAIL;
}

void glb_conf_free_json_array(void *data, int count, size_t element_size,  mem_funcs_t *memf, strpool_t *strpool, 
    glb_conf_array_free_func_cb_t free_cb) {
    
    int i;
  //  LOG_INF("Freeng elements");
    for (i = 0; i < count; i++) {
        free_cb(data + i * element_size, memf, strpool);
    }
 //   LOG_INF("Freeng memory %p", data);
    memf->free_func(data);
    
}

int glb_conf_create_array_from_json(void **data, char *name, struct zbx_json_parse *jp, size_t element_size, mem_funcs_t *memf, strpool_t *strpool,
     glb_conf_array_create_func_cb_t create_cb) {
    
    int count = 0;
    struct zbx_json_parse jp_arr, jp_elem;
    const char *p = NULL;
    void *elem_ptr;
    
    if (FAIL == zbx_json_brackets_by_name(jp, name, &jp_arr))
        return 0;
    
    if (0 >= (count = zbx_json_count(&jp_arr)))
        return 0;

   // LOG_INF("Allocating %d bytes, memf is %p", count * element_size, memf->malloc_func);
    *data = memf->malloc_func(NULL, count * element_size);
    elem_ptr = *data;

    LOG_INF("Allocated memory at %p", *data);
   // LOG_INF("Iterating");
     while (NULL != (p = zbx_json_next(&jp_arr, p))) {
        if (SUCCEED != zbx_json_brackets_open(p, &jp_elem)) 
            continue;
  
        create_cb(elem_ptr, &jp_elem, memf, strpool);
        
        elem_ptr += element_size;
    }

    return count;
}
