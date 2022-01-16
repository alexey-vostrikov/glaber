/*
** Glaber
** Copyright (C) 2001-2389 Glaber JSC
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
#include "dbschema.h"
#include "../zbxdbupgrade/dbupgrade.h"
#include "db.h"
#include "log.h"
#include "changeset.h"

#define CHANGESET_TABLE "changeset"

void changeset_delete_work_table() {
	DBdrop_table(CHANGESET_WORK_TABLE);	
}

void changeset_create_table() {
	const ZBX_TABLE	table =
			{CHANGESET_TABLE, "", 0,
				{
					{"clock", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"obj_type", 0, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"obj_id", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"change_type", 0, NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{NULL}
				},
				NULL
			};
	DBcreate_table(&table);
	DBcreate_index(CHANGESET_TABLE, CHANGESET_TABLE "_idx_clock", "clock",0);
	DBcreate_index(CHANGESET_TABLE, CHANGESET_TABLE "_idx_obj_type", "obj_type",0);
	DBcreate_index(CHANGESET_TABLE, CHANGESET_TABLE "_idx_obj_id", "obj_id",0);
}

void changeset_prepare_work_table() {

	if (SUCCEED ==  DBtable_exists(CHANGESET_WORK_TABLE)) {
		DBdrop_table(CHANGESET_WORK_TABLE);
	}
	
	DBbegin();
	DBrename_table(CHANGESET_TABLE, CHANGESET_WORK_TABLE);
	DBrename_index(CHANGESET_WORK_TABLE,CHANGESET_TABLE "_idx_clock", CHANGESET_WORK_TABLE "_idx_clock", "clock", 0);
	DBrename_index(CHANGESET_WORK_TABLE,CHANGESET_TABLE "_idx_obj_type", CHANGESET_WORK_TABLE "_idx_obj_type", "obj_type", 0);
	DBrename_index(CHANGESET_WORK_TABLE,CHANGESET_TABLE "_idx_obj_id", CHANGESET_WORK_TABLE "_idx_obj_id", "obj_id", 0);
	changeset_create_table();
	DBcommit();
}

/************************************************************
 * deletes and creates the changeset table					*
 * it's ok if the changeset table doesn't exists yet
 * **********************************************************/
void  changeset_flush_tables() 
{
	//https://stackoverflow.com/questions/8433011/what-happens-to-index-once-the-table-got-dropped
	//there is no need to drop index when dropping the table for all the supported db's
		
	if (SUCCEED == DBtable_exists(CHANGESET_TABLE) ) {
		DBdrop_table(CHANGESET_TABLE);	
	}
	
	if (SUCCEED == DBtable_exists(CHANGESET_WORK_TABLE) ) {
		DBdrop_table(CHANGESET_WORK_TABLE);	
	}

	changeset_create_table();
}

/**********************************************************************
 * adds a record to the changeset									  *
 * function should be used when db connection is already established
 * ********************************************************************/
void add_to_changeset(int obj_type, u_int64_t obj_id, int change_type) {
    DBexecute("INSERT INTO changeset (clock, obj_type, obj_id, change_type) VALUES (%ld,%d,%ld,%d);", 
            time(NULL), obj_type, obj_id, change_type);
	
}
/*********************************************************************
 * mass add funrctions set - when adding lots of records it's 
 * much fatser to use this way
 * ******************************************************************/
void changeset_prepare(glb_changeset_t* cset) {
	bzero(cset,sizeof(glb_changeset_t));
}

void changeset_add_to_cache(glb_changeset_t *cset, int obj_type, u_int64_t *obj_id, int change_type, int num) {
	int i;
	for (i = 0; i < num ; i ++) {
		zbx_snprintf_alloc(&cset->sql, &cset->alloc, &cset->offset, "INSERT INTO changeset (clock, obj_type, obj_id, change_type) VALUES (%ld,%d,%ld,%d);\n", 
            time(NULL), obj_type, obj_id[i], change_type );
		LOG_DBG("In %s: adding object %ld to cset", __func__, obj_id[i]);
	}
}

void changeset_flush(glb_changeset_t *cset) {
	if (cset->alloc == 0 ) 
		return;
	DBexecute(cset->sql);
	zbx_free(cset->sql);
	cset->alloc = 0;
	cset->offset = 0;
}

/***********************************************
 * for autosync of LLD changes - checks what 
 * most recent data changeset table is old enough
 * ************************************************/
int changeset_get_recent_time() {
	DB_RESULT result;
	DB_ROW dbrow;
	
	u_int64_t clock = 0;

	if (NULL == (result = DBselect("select max(clock) from %s;", CHANGESET_TABLE)))  
		return FAIL;
	
	if (NULL != (dbrow = DBfetch(result))) {
		if (NULL != dbrow[0])
			ZBX_STR2UINT64(clock, dbrow[0]);		
	}

	DBfree_result(result);	
	return clock;
}


#undef CHANGESET_TABLE_INDEX
#undef CHANGESET_TABLE
