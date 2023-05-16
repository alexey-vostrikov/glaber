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

#ifndef GLB_STATE_PROBLEMS_H
#define GLB_STATE_PROBLEMS_H
#include "glb_state.h"
#include "glb_state_problem.h"
#include "zbxcacheconfig.h"
#include "../glb_conf/tags/tags.h"

int glb_state_problems_init(mem_funcs_t *memf);
int glb_state_problems_destroy();



/* note: problem is created by the trigger id (it will be source id) 
    for most of ui and api calls we'll need host->problems index. Since a problem might be 
    calculated on several items/hosts, there might be several hosts pointing to the same problem

    it is possible and likely, that other types of objects might create problems in the future, since it's a 
    good idea to have a standard notification on other monitoring events like trigger calc fail or lld rule fail 
    or whatever. 

    all indexes are id-based meaning there is no strict reason to do linked cleaning, however it's possible
    that for an existing index a problem might not exist anymore
*/

int     glb_state_problem_recover(u_int64_t problemid, u_int64_t userid);
void    glb_state_problems_clean(zbx_vector_ptr_t *problems);
void    glb_state_problems_housekeep(); //UNFINISHED YET - does no cleanup
int     glb_state_problems_dump();


int         glb_state_problems_get_count();
u_int64_t   glb_state_problems_create_by_trigger(calc_trigger_t *trigger);

void    glb_state_problems_process_trigger_value(calc_trigger_t *trigger, int *maxseverity);
int     glb_state_problems_recover_by_trigger(u_int64_t triggerid, int leave_unrecovered);

int     glb_state_problems_get_count_by_trigger(u_int64_t triggerid);
int     glb_state_problems_get_count_by_trigger_unresolved(u_int64_t triggerid);
int     glb_state_problems_get_count_by_trigger_acknowledged(u_int64_t triggerid);
int     glb_state_problems_get_count_by_trigger_unacknowledged(u_int64_t triggerid);
int     glb_state_problems_get_severity(u_int64_t problemid, int *severity);
int     glb_state_problems_get_triggerid(u_int64_t problemid, u_int64_t *triggerid);
int     glb_state_problems_get_by_triggerid(u_int64_t triggerid, zbx_vector_ptr_t *problems);
int     glb_state_problems_get_by_hostid(u_int64_t triggerid, zbx_vector_ptr_t *problems);
int     glb_state_problems_get_ids_by_hostid(u_int64_t hostid, zbx_vector_uint64_t *ids);
int     glb_state_problems_get_by_hostids_json(zbx_vector_uint64_t *ids, struct zbx_json *response_json);
int     glb_state_problems_get_by_triggerids_json(zbx_vector_uint64_t *ids, struct zbx_json *response_json);
int     glb_state_problems_get_all_json(struct zbx_json *response_json);
int     glb_state_problems_get_hostids(u_int64_t problemid, zbx_vector_uint64_t *ids);
int     glb_state_problems_get_suppressed(u_int64_t problemid, glb_problem_suppress_state_t *is_suppressed);
int     glb_state_problems_get_clock(u_int64_t problemid, int *clock);
int     glb_state_problems_get_name(u_int64_t problemid, char *name, size_t len);

int     glb_state_problems_check_tag_name(u_int64_t problemid, tag_t *tag, unsigned char operation, int *result);
int     glb_state_problems_check_tag_value(u_int64_t problemid, tag_t *tag, unsigned char operation, int *result);

int     glb_state_problems_is_acknowledged(u_int64_t problemid, int *is_ack);
int     glb_state_problems_if_exists(u_int64_t problemid);

//todo: methods for load


#endif