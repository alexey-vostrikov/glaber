
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

#include "common.h"
#include "zbxalgo.h"
#include "conf.h"
#include "conf_index.h"
#include "../zbxdbcache/changeset.h"

typedef struct {
	strpool_t strpool;
    mem_funcs_t memf;
} config_t;

config_t  *conf = NULL;

int glb_config_init(mem_funcs_t *memf) {

    if (NULL == (conf = memf->malloc_func(NULL, sizeof(config_t)))) 
        return FAIL;

    conf->memf = *memf;
		
	if (FAIL == strpool_init(&conf->strpool, memf) ||  
        FAIL == config_index_init(memf) 
     //   FAIL == conf_triggers_init(memf) //|| 
		 //FAIL == glb_state_triggers_init(&memf)	
        ) 
        return FAIL;
    
    return SUCCEED;
}

