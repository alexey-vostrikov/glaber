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
#include "log.h"
#include "db.h"
#include "conf.h"
#include "conf_db_sync.h"
#include "../zbxdbcache/changeset.h"
//#include "dbconfig.h"

typedef struct {
	obj_index_t host_to_template_idx;
	obj_index_t deptrigger_to_trigger_idx;
	obj_index_t item_to_trigger_idx;
    mem_funcs_t memf;
} idx_config_t;

static idx_config_t *idx_conf = NULL;

int config_index_init(mem_funcs_t *memf) {
    
    if (NULL == (idx_conf = memf->malloc_func(NULL, sizeof(idx_config_t)))) 
            return FAIL;
    
    idx_conf->memf = *memf;

    if ( FAIL == obj_index_init(&idx_conf->host_to_template_idx, memf) ||
	    FAIL == obj_index_init(&idx_conf->deptrigger_to_trigger_idx, memf) ||
		FAIL == obj_index_init(&idx_conf->item_to_trigger_idx, memf)  ) 
        return FAIL;
    
	//LOG_INF("Config index init completed");
    return SUCCEED;
}
/* items->triggers */
int conf_index_items_to_triggers_del_trigger(u_int64_t triggerid) {
    return obj_index_del_reverse_id(&idx_conf->item_to_trigger_idx, triggerid);
};

int conf_index_items_to_triggers_del_item(u_int64_t itemid) {
    return obj_index_del_id(&idx_conf->item_to_trigger_idx, itemid);
}

int conf_index_items_to_triggers_add(u_int64_t itemid, u_int64_t triggerid) {
    return obj_index_add_ref(&idx_conf->item_to_trigger_idx, itemid, triggerid);
}

int conf_index_items_to_triggers_get_triggers(u_int64_t itemid, zbx_vector_uint64_t *triggers) {
    obj_index_get_refs(&idx_conf->item_to_trigger_idx, itemid, triggers);
	return SUCCEED;
}

int conf_index_items_to_triggers_get_items(u_int64_t triggerid, zbx_vector_uint64_t *items) {
    return obj_index_get_reverse_refs(&idx_conf->item_to_trigger_idx, triggerid, items);
}

/*dependand triggers ->triggers */
int conf_index_deptrigger_to_trigger_add(u_int64_t deptriggerid, u_int64_t triggerid) {
    int ret;
	LOG_INF("Called %s", __func__);
	//return obj_index_add_ref(&idx_conf->deptrigger_to_trigger_idx, deptriggerid, triggerid);
	ret = obj_index_add_ref(&idx_conf->deptrigger_to_trigger_idx, deptriggerid, triggerid);
	LOG_INF("Finished %s with result %d", __func__, ret);
	return ret;
}

int conf_index_deptrigger_to_trigger_get_triggers(u_int64_t deptriggerid, zbx_vector_uint64_t *triggers) {
    int ret;
	LOG_INF("Called %s", __func__);
	//return obj_index_get_refs(&idx_conf->deptrigger_to_trigger_idx, deptriggerid, triggers);
	ret = obj_index_get_refs(&idx_conf->deptrigger_to_trigger_idx, deptriggerid, triggers);
	LOG_INF("Finished %s with result %d", __func__, ret);
	return ret;
}

int conf_index_deptrigger_to_trigger_get_deptriggers(u_int64_t triggerid, zbx_vector_uint64_t *deptriggers) {
    int ret;
	LOG_INF("Called %s", __func__);
	//return obj_index_get_reverse_refs(&idx_conf->deptrigger_to_trigger_idx, triggerid, deptriggers);
	ret = obj_index_get_reverse_refs(&idx_conf->deptrigger_to_trigger_idx, triggerid, deptriggers);
	LOG_INF("Finished %s with result %d", __func__, ret);
	return ret;
}

int conf_index_deptrigger_to_trigger_del_trigger(u_int64_t triggerid) {
    int ret;
	LOG_INF("Called %s", __func__);
	obj_index_del_id(&idx_conf->deptrigger_to_trigger_idx, triggerid);
    obj_index_del_reverse_id(&idx_conf->deptrigger_to_trigger_idx, triggerid);
	
	LOG_INF("Finished %s", __func__);
	
    return SUCCEED;
}

//for indexes that might be read as from->to pairs this will do semi-partial reload of data
//on partial syncs. This will also delete objects stated in the changeset for quick sync
static int  index_update_from_db_result(unsigned char sync_mode, DB_RESULT result, obj_index_t *old_idx, int obj_type) {
	obj_index_t *idx;
	int full_sync = 0;
	u_int64_t old_id = 0,id, id_ref;
	DB_ROW			dbrow;
	int i = 0, d = 0;
	
	//dbsync_prepare(sync, 2, NULL);
    
    //this is full sync - building new obj_index
	if ( DBSYNC_INIT == sync_mode || DBSYNC_UPDATE == sync_mode)
	{	
		if ( NULL == (idx = idx_conf->memf.malloc_func(NULL,sizeof(obj_index_t)))) 
			return FAIL;

		obj_index_init(idx, &idx_conf->memf);
		full_sync = 1;
	} else  
		idx = old_idx;

	while (NULL != (dbrow = DBfetch(result))) {
		i++;

		is_uint64(dbrow[0], &id);
		is_uint64(dbrow[1], &id_ref);
			
		//note: to work correctly rows must be id-ordered in SQL
		//TODO: this may delete extra indexes - fugure
		if (old_id != id) {
			obj_index_del_reverse_id(idx,id);
			obj_index_del_id(idx,id);
		}

		DEBUG_TRIGGER(id, "Adding dependency ref %ld -> %ld", id, id_ref);
		DEBUG_TRIGGER(id_ref, "Adding dependency ref %ld -> %ld", id, id_ref);

		obj_index_add_ref(idx, id, id_ref);
	}
	DBfree_result(result);

    //on partial sync there might be deleted itemds
	if (!full_sync) {
		if (NULL == (result = DBselect("select obj_id from changeset_work where obj_type = %d and change_type = %d;", obj_type, DB_DELETE))) 
			return FAIL;

		while (NULL != (dbrow = DBfetch(result))) {
			is_uint64(dbrow[0], &id);
			obj_index_del_reverse_id(idx,id);
			obj_index_del_id(idx,id);
			d++;	
		}
		DBfree_result(result);	
    } else 
		obj_index_replace(old_idx, idx);
	
	LOG_INF("Index sync for sync type completed: updated %d items, deleted %d ", i, d);
	return SUCCEED;
}

typedef struct zbx_db_result
{
#if defined(HAVE_MYSQL)
	MYSQL_RES	*result;
#elif defined(HAVE_ORACLE)
	OCIStmt		*stmthp;	/* the statement handle for select operations */
	int		ncolumn;
	DB_ROW		values;
	ub4		*values_alloc;
	OCILobLocator	**clobs;
#elif defined(HAVE_POSTGRESQL)
	void	*pg_result;
	int		row_num;
	int		fld_num;
	int		cursor;
	DB_ROW		values;
#elif defined(HAVE_SQLITE3)
	int		curow;
	char		**data;
	int		nrow;
	int		ncolumn;
	DB_ROW		values;
#endif
} d_res_t;

int conf_index_deptrigger_sync_from_db(unsigned char sync_mode, DB_RESULT result) {
	int ret;
	LOG_INF("Called %s", __func__);

	LOG_INF("Updating triggers from db sync, mode is %d, sync items: %d", (int) sync_mode, result->row_num);
	index_update_from_db_result(sync_mode, result, &idx_conf->deptrigger_to_trigger_idx, OBJ_TRIGGERS);
	LOG_INF("After sync got %d in the hash, %d in rev hash", idx_conf->deptrigger_to_trigger_idx.from_to->elems.num_data, 
				idx_conf->deptrigger_to_trigger_idx.to_from->elems.num_data);

	LOG_INF("Finished %s with");
	return SUCCEED;
	
}

int  conf_index_trigger_to_deptrigger_dump() {

	LOG_INF("Called %s", __func__);

	obj_index_dump(&idx_conf->deptrigger_to_trigger_idx);

	LOG_INF("Finished %s");

	return SUCCEED;
}

int conf_index_items_to_triggers_dump() {
	obj_index_dump(&idx_conf->item_to_trigger_idx);
	return SUCCEED;
}