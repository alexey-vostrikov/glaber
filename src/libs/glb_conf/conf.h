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

#include "zbxjson.h"

int glb_config_init();

int glb_conf_iterate_on_set_data(char *json_buff, char *id_name, 
			elems_hash_t *elems, elems_hash_process_cb_t create_update_func);

int glb_conf_add_json_param_strpool(struct zbx_json_parse *jp,  strpool_t *strpool, char *name, const char **param_addr);
int glb_conf_add_json_param_memf(struct zbx_json_parse *jp, mem_funcs_t *memf, char *name, char **addr);

int glb_conf_host_get_host(u_int64_t hostid, char **host);
int glb_conf_host_get_name(u_int64_t hostid, char **name);

void glb_conf_set_json_data_table(char *buffer, int table);

typedef void (*glb_conf_array_free_func_cb_t)(void *data, mem_funcs_t *memf, strpool_t *strpool);
typedef int (*glb_conf_array_create_func_cb_t)(void *data, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool);
typedef void (*glb_conf_array_iter_func_t)(void *elem_ptr, int i, void *data);

#define CONF_ARRAY_CREATE_FROM_JSON_CB(name) \
        	static int name(void *data, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) 

#define CONF_ARRAY_FREE_CB(name) \
			static void name(void *data, mem_funcs_t *memf, strpool_t *strpool)			

#define CONF_ARRAY_ITERATE_CB(name) \
        static void name(void *elem_ptr, int i, void *data)

void glb_conf_free_json_array(void *data, int count, size_t element_size,  mem_funcs_t *memf, strpool_t *strpool,
		glb_conf_array_free_func_cb_t free_cb);
int glb_conf_create_array_from_json(void **data, char * name, struct zbx_json_parse *jp, size_t element_size, mem_funcs_t *memf, strpool_t *strpool,
		glb_conf_array_create_func_cb_t create_cb);
int glb_conf_array_iterate(void *array_ptr, int count, size_t elem_size, glb_conf_array_iter_func_t cb_func, void *data );

//int glb_conf_get_simple_interval(const char *delay);