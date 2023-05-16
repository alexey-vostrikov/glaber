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
#include "conf_mediatype.h"
#include "../items/conf_items.h"
#include "zbxstr.h"

typedef struct
{
    elems_hash_t *mtypes;
    mem_funcs_t memf;
    strpool_t strpool;

} mtypes_conf_t;

static mtypes_conf_t *conf = NULL;

ELEMS_CREATE(mtype_new_cb)
{
    elem->data = NULL;
}

ELEMS_FREE(mtype_free_cb)
{
    if (NULL != elem->data) 
        glb_conf_mtype_free(elem->data, memf, &conf->strpool);
    
    return SUCCEED;
}

int glb_conf_mediatypes_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(mtypes_conf_t))))
    {
        LOG_WRN("Cannot allocate memory for glb mediatypes struct");
        exit(-1);
    };
    
    conf->mtypes = elems_hash_init(memf, mtype_new_cb, mtype_free_cb);
    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
    
    return SUCCEED;
}

int glb_conf_mediatypes_destroy() {
    elems_hash_destroy(conf->mtypes);
    strpool_destroy(&conf->strpool);
    conf->memf.free_func(conf);
}

ELEMS_CALLBACK(create_update_mediatype) {
    struct zbx_json_parse *jp = data;

    if (NULL != elem->data) 
        glb_conf_mediatype_free(elem->data, memf, &conf->strpool);
        
    if (NULL != (elem->data = glb_conf_mediatype_create_from_json(jp, memf, &conf->strpool)))
        return SUCCEED;
    
    return FAIL;
} 

int glb_conf_mediatypes_set_data(char *json_buff){
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
           
            u_int64_t vm_id = glb_json_get_uint64_value_by_name(&jp_mapping, "mediatypeid", &err);

            if (err)
                continue;
          //  zbx_vector_uint64_append(&ids, vm_id);

            //note: for large tables it might be feasible to separate update and create and add change check
            if (SUCCEED == elems_hash_process(conf->mtypes, vm_id, create_update_mediatype, &jp_mapping, 0)) 
                zbx_vector_uint64_append(&ids, vm_id);
        }
    }

    elems_hash_remove_absent_in_vector(conf->mtypes, &ids);
    zbx_vector_uint64_destroy(&ids);
    return SUCCEED;
}
