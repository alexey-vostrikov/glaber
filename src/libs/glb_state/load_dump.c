#ifndef GLB_CACHE_READWRITE_C
#define GLB_CACHE_READWRITE_C
#include "load_dump.h"
#include "zbxcommon.h"
#include "zbxalgo.h"
#include "zbxjson.h"
#include "log.h"
#include <zlib.h>


typedef struct {
    char *resource_id;
    gzFile gzfile;
} state_dumper_t;

typedef struct {
    struct zbx_json *json;
    int objects;
    state_dumper_t *dumper;
    state_dumper_to_json_cb cb_func;
} marshall_data_t;

typedef struct {
    char *buf;
	size_t alloc_len;
    size_t alloc_offset;
   	gzFile gzfile;
} state_loader_t;

typedef struct {
    struct zbx_json_parse jp;
    state_loader_t loader;
    state_dumper_from_json_cb cb_func;
} unmarshall_data_t;



extern char *CONFIG_VCDUMP_LOCATION;
#define REASONABLE_BUFFER_SIZE 10000000

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


static int state_loader_create(state_loader_t *ldr, char *resource_id) {

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

static char* state_loader_get_line(state_loader_t *ldr) {

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

static int state_loader_destroy(state_loader_t *ldr){
    gzclose(ldr->gzfile);
    zbx_free(ldr->buf);
}

static int state_dumper_create(state_dumper_t *dumper, char *resource_id){
   	
    char *new_filename = dump_new_filename(resource_id);
    dumper->gzfile = gzopen(new_filename,"wb");

	if (Z_NULL == dumper->gzfile) {
		LOG_WRN("Cannot open file %s, value cache will not be dumped",new_filename);
		return FAIL;
	}

    dumper->resource_id = zbx_strdup(NULL, resource_id);
    return SUCCEED;
};

static int state_dumper_write_line(state_dumper_t *dumper, char *buffer, int len) {

   	if ( 0 >= gzwrite(dumper->gzfile, buffer, len) || 
         0 >= gzwrite(dumper->gzfile,"\n",1) )	{
		
        LOG_WRN("Cannot write to cache '%s', errno is %d", dumper->resource_id, errno);
		return FAIL;
	}	
	
}

static int state_dumper_destroy(state_dumper_t *dumper) {
    
    gzclose(dumper->gzfile);
    char *filename = dump_filename(dumper->resource_id);
    char *new_filename = dump_new_filename(dumper->resource_id);
   	
    if (0 != rename(new_filename, filename)) {
		zabbix_log(LOG_LEVEL_WARNING,"Couldn't rename %s -> %s (%s)", new_filename, filename,strerror(errno));
		return FAIL;
	}
    
    zbx_free(dumper->resource_id);
}

ELEMS_CALLBACK(load_parse_cb){
    unmarshall_data_t *unmdata = data;

    return unmdata->cb_func(elem, memf, &unmdata->jp);
}

int state_load_objects(elems_hash_t *elems, char *table_name, char *id_name, state_dumper_from_json_cb cb_func)
{
	int lines = 0, parsed = 0;
	
	zbx_json_type_t j_type;
	char id_str[MAX_ID_LEN];
    unmarshall_data_t unmdata = {.cb_func = cb_func};
        
    char *buffer;
	
    if (FAIL == state_loader_create(&unmdata.loader,table_name) ) 
        return FAIL;
    
    while (NULL != (buffer = state_loader_get_line(&unmdata.loader))) {
        
        lines++;
        if (SUCCEED != zbx_json_open(buffer, &unmdata.jp)) {
            LOG_INF("Warning: reading items cache: broken line on line %d", lines);
            continue;
        }

		if (SUCCEED != zbx_json_value_by_name(&unmdata.jp, id_name, id_str, MAX_ID_LEN, &j_type)) {
        	LOG_INF("Couldn't parse line %d: cannot find id %s in the JSON '%s':", lines, id_name, buffer);
        	continue;
    	}
	
		u_int64_t id = strtol(id_str,NULL,10);

		if (id == 0) {
			LOG_INF("Couldn't convert itemid or 0 in line %d find id in the JSON", lines);
            continue;
        }

		parsed += elems_hash_process(elems, id, load_parse_cb, &unmdata, 0);
    }

	state_loader_destroy(&unmdata.loader);

	LOG_INF("STATE: finished loading trigger data, loaded %d lines; parsed %d lines",lines, parsed);
    return parsed;
 }

ELEMS_CALLBACK(dump_cb) {
    marshall_data_t *mdata = data;
    
    zbx_json_clean(mdata->json);
    mdata->objects += mdata->cb_func(elem->id, elem->data, mdata->json);
    state_dumper_write_line(mdata->dumper,mdata->json->buffer, mdata->json->buffer_offset + 1);
}

int state_dump_objects(elems_hash_t *elems, char *table_name, state_dumper_to_json_cb cb_func) {
	struct zbx_json	json;
    state_dumper_t dumper;

	zabbix_log(LOG_LEVEL_DEBUG,"In %s: starting", __func__);

    marshall_data_t mdata = {.dumper = &dumper, .json = &json, .objects = 0, .cb_func = cb_func};
    
    if (SUCCEED == state_dumper_create(&dumper, table_name)) {
        zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
        elems_hash_iterate(elems, dump_cb, &mdata, ELEMS_HASH_READ_ONLY);
        state_dumper_destroy(&dumper);
	    zbx_json_free(&json);
    }

	return SUCCEED;
}

#endif