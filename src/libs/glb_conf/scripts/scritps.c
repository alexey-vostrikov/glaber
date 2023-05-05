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

#include "zbxalgo.h"
#include "glbstr.h"
#include "glb_log.h"
#include "zbxjson.h"
#include "scripts.h"
#include "zbxstr.h"

typedef struct
{
    elems_hash_t *scripts;
    mem_funcs_t memf;
    strpool_t strpool;

} scripts_conf_t;

static scripts_conf_t *conf = NULL;

ELEMS_CREATE(script_new_cb)
{
    elem->data = NULL;
}

ELEMS_FREE(script_free_cb)
{
    if (NULL != elem->data) 
        glb_conf_script_free(elem->data, memf, &conf->strpool);
    
    return SUCCEED;
}

int glb_conf_scripts_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(scripts_conf_t))))
    {
        LOG_WRN("Cannot allocate memory for glb cache struct");
        exit(-1);
    };
    
    conf->scripts = elems_hash_init(memf, script_new_cb, script_free_cb);
    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
    
    return SUCCEED;
}

int glb_conf_scripts_destroy() {
    elems_hash_destroy(conf->scripts);
    strpool_destroy(&conf->strpool);
    conf->memf.free_func(conf);
}

ELEMS_CALLBACK(create_update_script) {
    struct zbx_json_parse *jp = data;

    if (NULL != elem->data) 
        glb_conf_script_free(elem->data, memf, &conf->strpool);
        
    if (NULL != (elem->data = glb_conf_script_create_from_json(jp, memf, &conf->strpool)))
        return SUCCEED;
    
    return FAIL;
} 

int glb_conf_script_set_data(char *json_buff){
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
           
            u_int64_t vm_id = glb_json_get_uint64_value_by_name(&jp_mapping, "scriptid", &err);

            if (err)
                continue;
            

            zbx_vector_uint64_append(&ids, vm_id);

            if (SUCCEED == elems_hash_process(conf->scripts, vm_id, create_update_script, &jp_mapping, 0)) 
                zbx_vector_uint64_append(&ids, vm_id);
        }
    }

    elems_hash_remove_absent_in_vector(conf->scripts, &ids);
    zbx_vector_uint64_destroy(&ids);
    return SUCCEED;
}

//TODO: implement interface to fetch and work with the scripts 
int glb_conf_scripts_set_data(char *json_buffer) {
    LOG_INF("Setting data based on json: %s:", json_buffer);
    HALT_HERE();
}