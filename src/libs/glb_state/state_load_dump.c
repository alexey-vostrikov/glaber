#ifndef GLB_CACHE_READWRITE_C
#define GLB_CACHE_READWRITE_C
#include "state_load_dump.h"
#include "common.h"
#include "log.h"
#include <zlib.h>

extern char *CONFIG_VCDUMP_LOCATION;

static char* dump_filename(char *resource_id) {
    static char filename[MAX_STRING_LEN];
    
    zbx_snprintf(filename, MAX_STRING_LEN, "%s/%s.gz",CONFIG_VCDUMP_LOCATION, resource_id);
    
    return filename;
}

static char* dump_new_filename(char *resource_id) {
    static char new_filename[MAX_STRING_LEN];
    zbx_snprintf(new_filename, MAX_STRING_LEN, "%s.new", dump_filename(resource_id));
    
    return new_filename;
}


int state_loader_create(state_loader_t *ldr, char *resource_id) {

    char *filename = dump_filename(resource_id);
    FILE *fp;
    LOG_INF("Will load state file '%s'", filename);

    if ( NULL == (fp = fopen( filename, "a"))) {
		LOG_WRN("Cannot open file %s for access check, exiting",filename);
		return FAIL;
	}
	fclose(fp);

    //reopening the file in the read mode as gzipped file
	if ( Z_NULL == (ldr->gzfile = gzopen(filename, "r"))) {
		LOG_WRN("Cannot open gzipped file %s for reading",filename);
		//checking if we have the permissions on creating and writing the file
		return FAIL;
	}

    ldr->alloc_len =0;
    ldr->alloc_offset = 0;
    ldr->buf =NULL;
    return SUCCEED;
}

#define REASONABLE_BUFFER_SIZE 10000000

char* state_loader_get_line(state_loader_t *ldr) {

    char buffer[MAX_STRING_LEN];
    int got_new_line = 0;
    char *ret;


    if (ldr->alloc_len > REASONABLE_BUFFER_SIZE) {
        zbx_free(ldr->buf);
        ldr->alloc_len = 0;
    }
    
    ldr->alloc_offset =0;

   	while (0 == got_new_line && Z_NULL != (ret = gzgets(ldr->gzfile, buffer, MAX_STRING_LEN))   ) {
    	zbx_snprintf_alloc(&ldr->buf,&ldr->alloc_len,&ldr->alloc_offset,"%s",buffer);
      
        if (NULL != strchr(ldr->buf + ldr->alloc_offset-1, '\n') ) {
            got_new_line = 1;
        }
    }
	       
    if (Z_NULL == ret) return NULL;

    return ldr->buf;
}

int state_loader_destroy(state_loader_t *ldr){
    gzclose(ldr->gzfile);
    zbx_free(ldr->buf);
}

int state_dumper_create(state_dumper_t *dumper, char *resource_id){
   	
    char *new_filename = dump_new_filename(resource_id);
    dumper->gzfile = gzopen(new_filename,"wb");

	if (Z_NULL == dumper->gzfile) {
		LOG_WRN("Cannot open file %s, value cache will not be dumped",new_filename);
		return FAIL;
	}

    dumper->resource_id = zbx_strdup(NULL, resource_id);


};
int state_dumper_write_line(state_dumper_t *dumper, char *buffer, int len) {

   	if ( 0 >= gzwrite(dumper->gzfile, buffer, len) || 
         0 >= gzwrite(dumper->gzfile,"\n",1) )	{
		
        LOG_WRN("Cannot write to cache '%s', errno is %d", dumper->resource_id, errno);
		return FAIL;
	}	
	
}

int state_dumper_destroy(state_dumper_t *dumper) {
    
    gzclose(dumper->gzfile);
    char *filename = dump_filename(dumper->resource_id);
    char *new_filename = dump_new_filename(dumper->resource_id);
   	
    if (0 != rename(new_filename, filename)) {
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't rename %s -> %s (%s)", new_filename, filename,strerror(errno));
		return FAIL;
	}
    
    zbx_free(dumper->resource_id);
}


#endif