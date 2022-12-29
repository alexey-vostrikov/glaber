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
#include <zlib.h>


typedef struct {
    //char *id;
    
    char *buf;
	size_t alloc_len;
    size_t alloc_offset;
   	
    gzFile gzfile;
} state_loader_t;

typedef struct {
    char *resource_id;
    gzFile gzfile;
    
} state_dumper_t;

int state_loader_create(state_loader_t *loader, char *resource_id);
char *state_loader_get_line(state_loader_t *loader);
int state_loader_destroy(state_loader_t *loader);

int state_dumper_create(state_dumper_t *dumper, char *resource_id);
int state_dumper_write_line(state_dumper_t *dumper, char *buffer, int len);
int state_dumper_destroy(state_dumper_t *dumper);

#endif