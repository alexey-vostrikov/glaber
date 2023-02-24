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

#ifndef GLB_STATE_PROBLEM_H
#define GLB_STATE_PROBLEM_H

#include "glb_common.h"
#include "glb_state.h"
#include "zbxjson.h"
#include "zbxcacheconfig.h"

#define GLB_PROBLEM_TTL_PROBLEM_STATE 2 * 30 * 86400 //problematic things alive for two month, then closed automatically
#define GLB_PROBLEM_TTL_OK_STATE 5 * 60 //5 minutes for OK problems to be deleted

typedef enum
{
    GLB_PROBLEM_SOURCE_TRIGGER = 0,
    GLB_PROBLEM_SOURCE_COUNT
} glb_problem_source_t;

typedef enum
{
    GLB_PROBLEM_OPER_STATE_PROBLEM = 0,
    GLB_PROBLEM_OPER_STATE_OK,
    GLB_PROBLEM_OPER_STATE_SUPPRESSED,
    GLB_PROBLEM_OPER_STATE_COUNT
} glb_problem_oper_state_t;

typedef struct
{
    u_int64_t problemid;
    glb_problem_source_t source;
    u_int64_t objectid;
    const char *name;
    unsigned char severity;
    glb_problem_oper_state_t oper_state; // ok or suppressed

} glb_problem_info_t;

typedef struct glb_problem_t glb_problem_t;

//glb_problem_t *glb_problem_create(mem_funcs_t *memf, strpool_t *strpool, glb_problem_info_t *info);
glb_problem_t *glb_problem_create_by_trigger(mem_funcs_t *memf, strpool_t *strpool, u_int64_t problemid, CALC_TRIGGER *trigger);
void glb_problem_destroy(mem_funcs_t *memf, strpool_t *strpool, glb_problem_t *problem);
glb_problem_t *glb_problem_copy(mem_funcs_t *memf, strpool_t *strpool, glb_problem_t *src_problem);

/*data retrieval*/
glb_problem_source_t        glb_problem_get_source(glb_problem_t *problem);
glb_problem_oper_state_t    glb_problem_get_oper_state(glb_problem_t *problem);
u_int64_t                   glb_problem_get_objectid(glb_problem_t *problem);
u_int64_t                   glb_problem_get_id(glb_problem_t *problem);
int                         glb_problem_get_create_time(glb_problem_t *problem);

/*set functions*/
int         glb_problem_set_oper_state(glb_problem_t *problem, glb_problem_oper_state_t oper_state);


/*json in/out for external IO */
int glb_problem_marshall_to_json(glb_problem_t *problem, struct zbx_json *json);
int glb_problem_unmarshall_from_json(mem_funcs_t *memf, strpool_t *strpool, glb_problem_t *problem, struct zbx_json_parse *jp);

#endif
