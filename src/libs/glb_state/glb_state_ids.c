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
#include "zbxcommon.h"
#include "glb_state_ids.h"


// | epoch /reserved 4 bits | timestamp 42 bit | sequence 10bit  | 8 bits (proc/server/datacenter id) |

//epoch/reserved: expected to be 0 in next 60 years
//timestamp: _unsigned_ (no y2038 problem!) unix timestamp with milliseconds precision
//sequence: increases for each id in the same timestamp range, if needed, might be used to microseconds precision
//reserved 8 bits - for clustering/datacenter ids split

//id is generated using current timestamp. Sequence is incremented upon requests in the same millisecond and zeroed, when millisecond changes
//upon exceeding sequence counter, generator will lock for 10microseconds. 

//beware that current logic is limited to 1million of ids per second. The intention of such an ID generation is to be used auxiliary data, not
//the metrics flow, for events/problems/escalations. 

//if there is a need to identify or assign id for the metrics flow, it's expected to use more bits from last reserved 8 bits for making it possible 
//to generate uo to 256M ids per second

//to avoid using atomics and make id generation fork and thread - safe, using process id 
//in the lower 8 bits. This limits number of id-generating processes to 256 which is more then enough
//in most cases 1-2 is sufficient

#include "zbxtime.h"

u_int32_t proc_id = 0;
u_int32_t sequence = 0;
u_int64_t last_time = 0;

int glb_id_init(u_int32_t id) {
    if (id > 255) 
        return FAIL;
    proc_id = id;
}

u_int64_t glb_id_gen_new(glb_id_type_t type) {

    u_int64_t new_time;
    zbx_timespec_t ts;
    zbx_timespec(&ts);

    while (sequence > 999 ) {

        new_time = ((u_int64_t)ts.sec * 1000 + (u_int64_t)ts.ns/1000000);
    
        if (new_time != last_time) {
            last_time = new_time;
            sequence = 0;
            break;
        }

        usleep(10);
    }
       
    sequence++;
    return (new_time << 18) | proc_id;
}
