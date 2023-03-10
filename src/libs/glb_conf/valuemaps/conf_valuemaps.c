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

#include "zbxalgo.h"
#include "glb_log.h"
#include "zbxjson.h"
#include "conf_valuemap.h"

typedef struct
{
    elems_hash_t *valuemaps;
    mem_funcs_t memf;
    strpool_t strpool;

} valuemap_conf_t;

static valuemap_conf_t *conf = NULL;

ELEMS_CREATE(valuemap_new_cb)
{
    elem->data = NULL;
}

ELEMS_FREE(valuemap_free_cb)
{
    if (NULL != elem->data) 
        glb_conf_valuemap_free(elem->data, memf, &conf->strpool);
    
    return SUCCEED;
}

int glb_conf_valuemaps_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(valuemap_conf_t))))
    {
        LOG_WRN("Cannot allocate memory for glb cache struct");
        exit(-1);
    };
    
    conf->valuemaps = elems_hash_init(memf, valuemap_new_cb, valuemap_free_cb);
    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
    
    return SUCCEED;
}

int glb_conf_valuemaps_destroy()
{
    elems_hash_destroy(conf->valuemaps);
    strpool_destroy(&conf->strpool);
}

ELEMS_CALLBACK(create_update_valuemap) {
    struct zbx_json_parse *jp = data;

    if (NULL != elem->data) 
        glb_conf_valuemap_free(elem->data, memf, &conf->strpool);
        
    if (NULL != (elem->data = glb_conf_valuemap_create_from_json(jp, memf, &conf->strpool)))
        return SUCCEED;
    
    return FAIL;
} 

int glb_conf_valuemaps_set_data(char *json_buff){
    struct zbx_json_parse	jp, jp_result, jp_mapping;
    const char *mapping = NULL;
    int err;
    zbx_vector_uint64_t ids;
    
    zbx_vector_uint64_create(&ids);
    zbx_vector_uint64_reserve(&ids, 65536);

    if (SUCCEED != zbx_json_open(json_buff, &jp) ||
        SUCCEED != zbx_json_brackets_by_name(&jp, "result", &jp_result)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON data (broken or malformed json) %s", json_buff);
		return FAIL;
	}

    while (NULL != (mapping = zbx_json_next(&jp_result, mapping))) {
        if (SUCCEED == zbx_json_brackets_open(mapping, &jp_mapping)) {
           
            u_int64_t vm_id = glb_json_get_uint64_value_by_name(&jp_mapping, "valuemapid", &err);

            if (err)
                continue;
            
//            LOG_INF("Got a valemapping %lld", vm_id);
            zbx_vector_uint64_append(&ids, vm_id);
            //note: for large tables it might be feasible to separate update and create and add cahange check
            if (SUCCEED == elems_hash_process(conf->valuemaps, vm_id, create_update_valuemap, &jp_mapping, 0)) 
                zbx_vector_uint64_append(&ids, vm_id);
  //          else 
  //              LOG_INF("Couldn't add valuemap %ld", vm_id);
        }
    }

    elems_hash_remove_absent_in_vector(conf->valuemaps, &ids);
    zbx_vector_uint64_destroy(&ids);
    return SUCCEED;
}