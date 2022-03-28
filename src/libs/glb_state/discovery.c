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
#include "glb_state.h"
#include "glb_lock.h"
#include "load_dump.h"

typedef struct {
    const char *row;
    int lifetime;
    int delete_time;
} discovery_row_t;

typedef struct {
    u_int64_t itemid;
    zbx_hashset_t discoveries;
} discovery_item_t;

typedef struct {
    elems_hash_t *items;
    mem_funcs_t memf;
    strpool_t strpool;
} conf_t;

static conf_t *conf = NULL;

static int	discovery_cmp_func(const void *d1, const void *d2)
{
	const discovery_row_t *row1 = (const discovery_row_t *)d1;
	const discovery_row_t *row2 = (const discovery_row_t *)d2;

    //LOG_INF("Comparing item1 id%ld, item2 id%ld", row1->itemid, row2->itemid);

	//ZBX_RETURN_IF_NOT_EQUAL(row1->itemid, row2->itemid);
	//ZBX_RETURN_IF_NOT_EQUAL(row1->row, row2->row);

	return 0;
}

static zbx_hash_t discovery_hash_func(const void *data)
{
	const discovery_row_t *row = ((elems_hash_elem_t *)((const discovery_row_t *)data))->data;

	zbx_hash_t		hash;

//	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&row->itemid);
  //  LOG_INF("Hashing item id %ld ", row->itemid);
  //  LOG_INF("Hashing item str is  %s ", row->row);
//	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&row->row, sizeof(char *), hash);
    
	return hash;
}

ELEMS_CREATE(discovery_create_cb){
    LOG_INF("In the callback");
    discovery_row_t *new_row, *row = (discovery_row_t*)data;

 //   LOG_INF("Discovery elem create id is %ld ", row->itemid);
  //  elem->data = NULL;
    return SUCCEED;
}

ELEMS_FREE(discovery_free_cb) {
    discovery_row_t *row = elem->data;
    
    strpool_free(&conf->strpool, row->row);
    conf->memf.free_func(row);
    
    return SUCCEED;
}

ELEMS_CALLBACK(check_row_needs_processing_cb) {
    discovery_row_t *saved_row = elem->data, *new_row = data;
    int now = time(NULL);
    //if (saved_row == )
    if ( saved_row->delete_time < now + new_row->lifetime / 2 ) {
        //LOG_INF("This is new or outdated row, needs processing");
        
        //row was init before, but updating lifetime/delete
        saved_row->lifetime = new_row->lifetime;
        saved_row->delete_time = now + new_row->lifetime;
        
        LOG_INF("NEW: Row is cached and it's %d seconds till half delete time, not updating LLD", 
                    (saved_row->delete_time - now) - new_row->lifetime/2 );
        return SUCCEED;
    } else
        LOG_INF("NOT NEW: Row is cached and it's still %d seconds till half delete time, not updating LLD", 
                    (saved_row->delete_time - now) - new_row->lifetime/2 );
    
}

int discovery_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(conf_t)))) {
        LOG_WRN("Cannot allocate memory for cache struct");
        exit(-1);
    };
    
  //  conf->discovery = elems_hash_init_ext(memf, discovery_create_cb, 
   //                         discovery_free_cb, discovery_cmp_func, discovery_hash_func);
  //  conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
    
  return SUCCEED;
}


int glb_state_discovery_if_row_needs_processing(u_int64_t itemid, struct zbx_json_parse *jp_row, int lifetime) {
    static char* row=NULL;
    
 //   static size_t alloc = 0;
 //   size_t offset = 0;
//    int ret;
    
//    discovery_row_t new_row = {.itemid = itemid, .lifetime=lifetime, 
  //      .delete_time = 0, .row = strpool_add_n(&conf->strpool, jp_row->start, jp_row->end - jp_row->start + 1) };

 //   elems_hash_elem_t elem = {.id = &new_row, .data= &new_row};

//    LOG_INF("Check discovery: processing item %ld, lifettime %d row '%s'", 
//            itemid, lifetime, new_row.row );

//    LOG_INF("New discovery elem create addr is %p", &new_row);

//    LOG_INF("What is the reason of waisting the time????")
//    ret = elems_hash_process(conf->discovery, &elem, check_row_needs_processing_cb, &new_row, 0);
    
 //   strpool_free(&conf->strpool, new_row.row);
    
    return SUCCEED;
}
