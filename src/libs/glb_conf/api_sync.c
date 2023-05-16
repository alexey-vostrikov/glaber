/*
** Glaber
** Copyright (C) 2001-2023 Glaber
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
#include "zbxcommon.h"
#include "api_io/glb_api_conf.h"
#include "api_sync.h"
#include "zbxalgo.h"
#include "glb_lock.h"

typedef struct {
    mem_funcs_t memf;
    char *json_tables[GLB_CONF_API_OBJECTS_MAX];
    pthread_rwlock_t table_lock;
} api_sync_conf_t; 

static api_sync_conf_t *conf = NULL;

int glb_conf_api_sync_init(mem_funcs_t *memf) {
    conf = memf->malloc_func(NULL, sizeof(api_sync_conf_t));
    if (NULL == conf ) 
        return FAIL;
    bzero(conf, sizeof(api_sync_conf_t));
    conf->memf = *memf;
    
    glb_rwlock_init(&conf->table_lock);
}

void glb_conf_api_sync_destroy( ) {
    int i; 
    
    glb_rwlock_wrlock(&conf->table_lock);

    for (i =0 ; i < GLB_CONF_API_OBJECTS_MAX; i++)
        if ( NULL != conf->json_tables[i])
            conf->memf.free_func(conf->json_tables[i]);

    conf->memf.free_func(conf);
}


void glb_conf_set_json_data_table(char *buffer, int table) {
    char *buff;
    size_t len = strlen(buffer);
    buff = conf->memf.malloc_func(NULL, len + 1);
    zbx_strlcpy(buff, buffer, len + 1);

    glb_rwlock_wrlock(&conf->table_lock);
    
    if (NULL != conf->json_tables[table])
        conf->memf.free_func(conf->json_tables[table]);
    
    conf->json_tables[table] = buff;
    
    glb_rwlock_unlock(&conf->table_lock);
}

char *glb_conf_get_json_data_table(int table) {
    char *buff = NULL;
  //  LOG_INF("Locking");
    glb_rwlock_rdlock(&conf->table_lock);
//    LOG_INF("Copy");
    if (NULL != conf->json_tables[table] ) {
  //      LOG_INF("Not null");
        buff = strdup(conf->json_tables[table]);
    }
//    LOG_INF("Unlocking");
    glb_rwlock_unlock(&conf->table_lock);
  //  LOG_INF("Finished, returning %p", buff);
    return buff;
}


/*syncs operations table (indexed by action id ) that holds
what operations are to be done for what actions*/
int  glb_api_sync_operations() {
    char *buffer = NULL;

    char *query = 
    "{  \"jsonrpc\": \"2.0\", \
        \"method\": \"action.get\",\
        \"params\": \
            { \"output\": \"extend\", \
              \"selectOperations\": \"extend\",\
              \"selectRecoveryOperations\": \"extend\",\
              \"selectUpdateOperations\": \"extend\",\
              \"selectFilter\": \"extend\"\
            }, \
            \"id\": 28\
     }";

    if (FAIL == glb_api_conf_sync_request(query, &buffer, "actions_operations", API_NO_CACHE))
        return FAIL;

 
    glb_conf_set_json_data_table(buffer, GLB_CONF_API_ACTIONS_OPERATIONS);
    zbx_free(buffer);
 
    return SUCCEED;
}