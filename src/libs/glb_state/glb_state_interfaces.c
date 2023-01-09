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
#include "glb_state_interfaces.h"

typedef struct {
    elems_hash_t *interfaces;
    elems_hash_t *ip_to_host_index;

    mem_funcs_t memf;
    strpool_t strpool;

} interfaces_state_t;

static interfaces_state_t *state = NULL; 

typedef struct {
    int lastchange;
    char *error;
    //interface_state_t state;
} interface_info_t;


ELEMS_CREATE(interface_create_cb) {
    elem->data = memf->malloc_func(NULL, sizeof(interface_info_t));
    interface_info_t* interface = elem->data;
    
    bzero(interface, sizeof(interface_info_t));
    interface->lastchange = 0;
}

ELEMS_FREE(interface_free_cb) {
    interface_info_t *interface = elem->data;
    
    memf->free_func(interface->error);
    memf->free_func(interface);
    elem->data = NULL;
}


ELEMS_CREATE(ip_to_host_create_cb) {
    elem->data = 0;
}

ELEMS_FREE(ip_to_host_free_cb) {
    strpool_free(&state->strpool, (char *)elem->id);
}

int glb_state_interfaces_init(mem_funcs_t *memf)
{
    if (NULL == (state = memf->malloc_func(NULL, sizeof(interfaces_state_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
    state->interfaces = elems_hash_init(memf, interface_create_cb, interface_free_cb );
    state->ip_to_host_index = elems_hash_init(memf, ip_to_host_create_cb, ip_to_host_free_cb );
    state->memf = *memf;
    strpool_init(&state->strpool,memf);
    
    return SUCCEED;
}

int glb_state_interfaces_destroy() {
    elems_hash_destroy(state->interfaces);
    strpool_destroy(&state->strpool);
}

typedef struct {
    const char *ip;
    u_int64_t hostid;
} ip_to_host_info_t;


ELEMS_CALLBACK(register_ip_cb) {
    ip_to_host_info_t* ip_to_host = data;
    elem->data = (void *)ip_to_host->hostid;
    strpool_copy((char * )elem->id);
}

ELEMS_CALLBACK(find_ip_cb) {
  
    ip_to_host_info_t* ip_to_host = data;
    ip_to_host->hostid = (u_int64_t)elem->data;
    ip_to_host->ip = (char *)elem->id;
  
    return SUCCEED;
}

int glb_state_interfaces_register_ip(const char *addr, u_int64_t hostid) {
    if (NULL == addr || 0 == hostid)
        return FAIL;

    ip_to_host_info_t ip_to_host = {.ip = strpool_add(&state->strpool, addr), .hostid = hostid};
  
    elems_hash_process(state->ip_to_host_index, (u_int64_t)ip_to_host.ip, register_ip_cb, &ip_to_host, 0);
    strpool_free(&state->strpool, ip_to_host.ip);
  
    return SUCCEED;
}

u_int64_t   glb_state_interfaces_find_host_by_ip(const char *addr) {
    if (NULL == addr)
        return 0;

    ip_to_host_info_t ip_to_host = {.ip = strpool_add(&state->strpool, addr), .hostid = 0};
  
    if (SUCCEED == elems_hash_process(state->ip_to_host_index, (u_int64_t)ip_to_host.ip, find_ip_cb, &ip_to_host, ELEM_FLAG_DO_NOT_CREATE )) {
        strpool_free(&state->strpool, ip_to_host.ip);
        return ip_to_host.hostid;
    }
    
    strpool_free(&state->strpool, ip_to_host.ip);
    return 0;
}

void glb_state_interfaces_release_ip(const char *addr) {
    const char *id = strpool_add(&state->strpool, addr);
    elems_hash_delete(state->ip_to_host_index, (u_int64_t)id );
    strpool_free(&state->strpool, id);
}