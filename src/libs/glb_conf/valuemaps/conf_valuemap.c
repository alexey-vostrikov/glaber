


//CREATE TABLE valuemap_mapping (
//	valuemap_mappingid       bigint                                    NOT NULL,
//	valuemapid               bigint                                    NOT NULL,
//	value                    varchar(64)     DEFAULT ''                NOT NULL,
//	newvalue                 varchar(64)     DEFAULT ''                NOT NULL,
//	type                     integer         DEFAULT '0'               NOT NULL,
//	sortorder                integer         DEFAULT '0'               NOT NULL,
//	PRIMARY KEY (valuemap_mappingid)


/*jsonrpc": "2.0",     "result": [  
        {           "valuemapid": "4",
                    "name": "APC Battery Replacement Status",
                    "mappings": [ 
                        {  "type": "0",
                            "value": "1",
                            "newvalue": "unknown"
                        }, 
                        {   "type": "0",
                            "value": "2",                     "newvalue": "notInstalled"                 },                 {                     "type": "0",                     "value": "3",                     "newvalue": "ok"                 },
                        .... */
#include "glb_common.h"
#include "zbxcommon.h"
#include "zbxjson.h"
#include "conf_valuemap.h"

typedef struct {
    unsigned char type;
    const char *value;
    const char *newvalue;
} mapping_t;

struct glb_conf_valuemap_t {
    u_int64_t id;
    zbx_vector_ptr_t mappings;
};

void glb_conf_valuemap_free(glb_conf_valuemap_t *vm, mem_funcs_t *memf, strpool_t *strpool) {
    
    for (int i = 0; i < vm->mappings.values_num; i++) {
        mapping_t *map = vm->mappings.values[i];
        strpool_free(strpool,map->value);
        strpool_free(strpool,map->newvalue);
        memf->free_func(map);
    }
    zbx_vector_ptr_destroy(&vm->mappings);
    memf->free_func(vm);
}

mapping_t* json_to_valuemap(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    char value[64], newvalue[64], type[MAX_ID_LEN];
    zbx_json_type_t jtype;
  //  LOG_INF("In %s", __func__);

    if (SUCCEED == zbx_json_value_by_name(jp, "type", type, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "value", value, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "newvalue", newvalue, MAX_ID_LEN, &jtype )) {
        mapping_t *map = memf->malloc_func(NULL, sizeof(mapping_t));

        if (NULL == map) 
            return NULL;
        
        map->type = strtol(type, NULL, 10);
        map->value = strpool_add(strpool, value);
        map->newvalue = strpool_add(strpool, newvalue);
     //   LOG_INF("Succesifully parsed valuemap %d %s %s", map->type, map->value, map->newvalue);
        
        return map;
    }

    return NULL;
}

int parse_valuemap(glb_conf_valuemap_t *vm, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    const char *mapping_ptr = NULL;
    struct zbx_json_parse jp_mapping;
    mapping_t *mapping;
    int i = 0;
    
    //LOG_INF("In %s", __func__);
    
    while (NULL != (mapping_ptr = zbx_json_next(jp, mapping_ptr)))
    {
        //LOG_INF("In %s 1", __func__);
        if ( SUCCEED == zbx_json_brackets_open(mapping_ptr, &jp_mapping) &&
             NULL != (mapping = json_to_valuemap(&jp_mapping, memf, strpool))) {
                //LOG_INF("In %s 3", __func__);
            zbx_vector_ptr_append(&vm->mappings, mapping);
            i++;
        }
    }
    return i;
}


/*takes the whole mapping object and */
glb_conf_valuemap_t *glb_conf_valuemap_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {

    struct zbx_json_parse jp_mappings;
    glb_conf_valuemap_t *vm;
    int errflag;
//    LOG_INF("In %s", __func__);

    if (NULL ==(vm = memf->malloc_func(NULL, sizeof(glb_conf_valuemap_t))))
        return NULL;

    zbx_vector_ptr_create_ext(&vm->mappings, memf->malloc_func, memf->realloc_func, memf->free_func);

    if (SUCCEED != zbx_json_brackets_by_name(jp, "mappings", &jp_mappings) ||
           0 ==( vm->id = glb_json_get_uint64_value_by_name(jp, "valuemapid", &errflag)) || 
           0 == parse_valuemap(vm, &jp_mappings, memf, strpool) ) {
    
        zbx_vector_ptr_destroy(&vm->mappings);
        memf->free_func(vm);

        return NULL;
    } 

    return vm;
};   

