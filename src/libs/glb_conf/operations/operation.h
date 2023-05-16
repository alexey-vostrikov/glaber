
#include "zbxalgo.h"
#include "zbxjson.h"

typedef struct glb_operation_t glb_operation_t;

void glb_operation_free(mem_funcs_t *memf, glb_operation_t *operation);
glb_operation_t *glb_operation_create_from_json(mem_funcs_t *memf, struct zbx_json_parse *jp);