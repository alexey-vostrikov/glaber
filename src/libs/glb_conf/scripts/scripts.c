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
#include "../conf.h"
#include "script.h"

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

//this proc is probably should go to a library -
//it will be common for most sets
int glb_conf_scripts_set_data(char *json_buff){
    return glb_conf_iterate_on_set_data(json_buff, "scriptid", conf->scripts, create_update_script);
}
