#ifndef DISCOVERY_H
#define DISCOVERY_H

#include "common.h"

//chekcs if row has to be passwed to the processing of LLD
int discovery_init(mem_funcs_t *memf);
int glb_state_discovery_if_row_needs_processing(u_int64_t itemid, struct zbx_json_parse *jp_row, int lifetime);

#endif