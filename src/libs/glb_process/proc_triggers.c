
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

#include "common.h"

#include "zbxalgo.h"
#include "dbcache.h"
#include "log.h"
#include "process.h"
#include "zbxserver.h"
#include "../../zabbix_server/events.h"
#include "../glb_conf/conf_index.h"
#include "../glb_state/state_triggers.h"
#include "../glb_conf/conf_triggers.h"
#include "../zbxipcservice/glb_ipc.h"
#include "../zbxserver/expression.h"






//the question is - how to get state and how to live up with locking?
//solution1: process trigger's data inside elems lock: 
//	- bad thing - it's quite long, might deadlock (does so during history sync)
//solution2: mark trigger as locked, while allow reading it
//	- this allow unlicked operations with the triggers
//	- but potentially will fail on trigger deletion
//solution3: copy - mark locked - work on it - copy back - unlock
//  - requires locking and several copies
//solution4: use callback for processing that needs complex data
//  - like state all together, current tags and so on
//  - looks like this is the most logical and robust variant

//actially in most places state isn't required at all,  just need to keep old and new value
//in most other cases state is used for setting error massage just before exit



int process_metric_triggers(u_int64_t itemid) {
     
	static zbx_vector_uint64_t triggers = {.mem_free_func = ZBX_DEFAULT_MEM_FREE_FUNC, .mem_malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, 
	 	.mem_realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC, .values = NULL , .values_alloc = 0, .values_num = 0 };
	 
	int i;

	DEBUG_ITEM(itemid, "Processing triggers for the item");
	zbx_vector_uint64_clear(&triggers);
	
	if (SUCCEED == conf_index_items_to_triggers_get_triggers(itemid, &triggers)) {
	
	 	DEBUG_ITEM(itemid, "Got %d triggers for item %ld from the configuration", triggers.values_num, itemid);
	
	 	for (i = 0; i < triggers.values_num; i++) {
	 		DEBUG_ITEM(itemid, "Recalculating trigger %ld", triggers.values[i]);
			DEBUG_TRIGGER( triggers.values[i], "Recalculating trigger on new data from item %ld", itemid);
	 		recalculate_trigger(triggers.values[i]);
			DEBUG_ITEM(itemid, "Recalculating trigger %ld completed", triggers.values[i]);
			DEBUG_TRIGGER( triggers.values[i], "Recalculating trigger on new data from item %ld completed", itemid);
	 	}
	} 
	return SUCCEED;
}


