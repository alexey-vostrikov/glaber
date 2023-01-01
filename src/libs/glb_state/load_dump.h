//this is interface to read and write state data to 
//some state-keeping device

//each kind of state should have meaningfull string name (for example, "items" or triggers)
//reading and writting of each kind of state should be independant from any other kind of 
//state

//state reader and writer assumes using a glb_worker paradigm for interaction
//to make server core independent from the actual IO technology
//it might be - a memcache or redis or gzip files - or a database (it's better to avoid SQL)

//state read/write doesn't use shm memory and try keep heap footprint reasonable
#ifndef GLB_CACHE_READWRITE_H
#define GLB_CACHE_READWRITE_H


#include "zbxcommon.h"
#include "zbxalgo.h"
#include "zbxjson.h"
#include <zlib.h>


#define DUMPER_TO_JSON(name) \
        static int name(u_int64_t id, void *data, struct zbx_json *json)


#define DUMPER_FROM_JSON(name) \
        static int name(elems_hash_elem_t *elem, mem_funcs_t *memf, struct zbx_json_parse *jp)
        
typedef int	(*state_dumper_to_json_cb)(u_int64_t id, void *data, struct zbx_json *json);
typedef int	(*state_dumper_from_json_cb)(elems_hash_elem_t *elem, mem_funcs_t *memf, struct zbx_json_parse *jp);

int state_load_objects(elems_hash_t *elems, char *table_name, char *id_name, state_dumper_from_json_cb cb_func);
int state_dump_objects(elems_hash_t *elems, char *table_name, state_dumper_to_json_cb cb_func);

#endif