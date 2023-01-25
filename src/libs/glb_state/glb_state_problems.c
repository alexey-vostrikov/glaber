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

#include "glb_state.h"
#include "glb_state_problems.h"

#define MAX_PROBLEMS 1000000
#define MAX_PROBLEMS_PER_TRIGGER 1024

typedef struct {
    elems_hash_t *problems; //problems refernced by their ids
    
    mem_funcs_t memf;
    strpool_t strpool;

} problems_conf_t;

static problems_conf_t *conf = NULL; 

typedef struct {
    int lastchange;
    const char *error;
    unsigned char oper_state;
    unsigned char type;
    unsigned char new_state_count;
} problem_t;
    

ELEMS_CREATE(problem_create_cb) {
    elem->data = memf->malloc_func(NULL, sizeof(problem_t));
    problem_t* problem = elem->data;
    
    bzero(problem, sizeof(problem_t));
}

ELEMS_FREE(problem_free_cb) {
    problem_t *problem = elem->data;
        
    memf->free_func(problem);
    elem->data = NULL;
}


int glb_state_problems_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(problems_conf_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
    conf->problems = elems_hash_init(memf, problem_create_cb, problem_free_cb );
    conf->memf = *memf;
    strpool_init(&conf->strpool,memf);
    
    return SUCCEED;
}

int glb_state_problems_destroy() {
    elems_hash_destroy(conf->problems);
    strpool_destroy(&conf->strpool);
}

//fields the problem keeps
//"eventid", "source", "object", "objectid", "clock", "ns", "name", "severity", NULL);

//the real question is ID generation - we need an ID to 

ELEMS_CALLBACK(get_problem_info) {
    problem_t *problem = elem->data;
    glb_state_problem_info_t *info = data;
}

glb_state_interface_info_t *glb_state_interfaces_get_avail(u_int64_t id) {
    static glb_state_interface_info_t info = {0};
    if (0 == id)
        return NULL;
    
    if (FAIL == elems_hash_process(conf->interfaces, id, get_iface_info, &info, ELEM_FLAG_DO_NOT_CREATE))
        return NULL;
    
    return &info;
}

ELEMS_CALLBACK(interface_get_json) {
    struct zbx_json *json = data;
    interface_state_t *iface = elem->data;

    zbx_json_addobject(json, NULL); 
    zbx_json_addint64(json, "id", elem->id);
    zbx_json_addint64(json, "avail", iface->oper_state);
    zbx_json_addint64(json, "lastchange", iface->lastchange);
    zbx_json_addstring(json, "error", iface->error, ZBX_JSON_TYPE_STRING);
    zbx_json_close((struct zbx_json *)data);
}

int glb_state_interfaces_get_state_json(zbx_vector_uint64_t *ids, struct zbx_json *json) 
{
    int i;
   
    zbx_json_addarray(json, ZBX_PROTO_TAG_DATA);
    
    for (i=0; i < ids->values_num; i++) {
        elems_hash_process(conf->interfaces, ids->values[i], interface_get_json, json, ELEM_FLAG_DO_NOT_CREATE);
    }

    zbx_json_close(json); 
}