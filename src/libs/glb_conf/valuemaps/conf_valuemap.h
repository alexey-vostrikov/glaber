

#include "zbxcommon.h"

typedef struct glb_conf_valuemap_t glb_conf_valuemap_t;

//glb_conf_valuemap_t *create_valuemap_from_json(struct zbx_json_t json);

void glb_conf_valuemap_free(glb_conf_valuemap_t *vm, mem_funcs_t *memf, strpool_t *strpool);
glb_conf_valuemap_t *glb_conf_valuemap_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool);