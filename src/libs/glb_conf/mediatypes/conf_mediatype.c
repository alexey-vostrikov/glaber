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


#include "glb_common.h"
#include "zbxcommon.h"
#include "zbxjson.h"
#include "conf_mediatype.h"
#include "zbxstr.h"
#include "zbxserver.h"
#include "zbxparam.h"
#include "../items/conf_items.h"

#define VALUEMAP_TYPE_MATCH			0
#define VALUEMAP_TYPE_GREATER_OR_EQUAL	1
#define VALUEMAP_TYPE_LESS_OR_EQUAL		2
#define VALUEMAP_TYPE_RANGE			3
#define VALUEMAP_TYPE_REGEX			4
#define VALUEMAP_TYPE_DEFAULT		5

typedef struct {
    int type;
    const char *value;
    const char *newvalue;
} mapping_t;

struct glb_conf_mediatype_t {
    u_int64_t id;
    
};

void glb_conf_mediatype_free(glb_conf_mediatype_t *mtype, mem_funcs_t *memf, strpool_t *strpool) {
    memf->free_func(mtype);
}

mapping_t* json_to_mediatype(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    return NULL;
}

glb_conf_mediatype_t *glb_conf_mediatype_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    glb_conf_mediatype_t *vm;
    int errflag;

    if (NULL ==(vm = memf->malloc_func(NULL, sizeof(glb_conf_mediatype_t))))
        return NULL;

    if ( 0 ==( vm->id = glb_json_get_uint64_value_by_name(jp, "mediatypeid", &errflag)) ) {
    
        memf->free_func(vm);
        return NULL;
    } 

    return vm;
};   
