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
#include "conf_db_sync.h"


static int db_full_sync(DB_RESULT result, int columns, cmp_func_t cmp_func, dbsync_preproc_row_func_t preproc_func, 
			conf_dbsync_add_func_t add_func, conf_dbsync_update_func_t upd_func, conf_dbsync_delete_func_t del_func,  
			zbx_vector_uint64_t *ids) {
// 	DB_ROW			dbrow;
// //	zbx_hashset_t		ids;
// 	zbx_hashset_iter_t	iter;
// 	zbx_uint64_t		rowid;
// 	data_t		*data;
// 	int i = 0;

	
// 	LOG_DBG("Doing full compare sync");
		
// 	while (NULL != (dbrow = DBfetch(result)))
// 	{
// //		dbrow = dbsync_preproc_row(sync, dbrow);

// 		ZBX_STR2UINT64(rowid, dbrow[0]);
// 		zbx_hashset_insert(&ids, &rowid, sizeof(rowid));

// 		if (NULL == (data = (data_t *)zbx_hashset_search(hash, &rowid)))
// 			tag = ZBX_DBSYNC_ROW_ADD;
// 		else if (FAIL == (cmp_func)((void *)data, dbrow))
// 			tag = ZBX_DBSYNC_ROW_UPDATE;

// 		if (ZBX_DBSYNC_ROW_NONE != tag)
// 			dbsync_add_row(sync, rowid, tag, dbrow);
// 	}

// 	zbx_hashset_iter_reset(hash, &iter);
// 	while (NULL != (data = zbx_hashset_iter_next(&iter)))
// 	{
// 		if (NULL == zbx_hashset_search(&ids, &data->id))
// 			dbsync_add_row(sync, data->id, ZBX_DBSYNC_ROW_REMOVE, NULL);
// 	}

// 	zbx_hashset_destroy(&ids);
// 	DBfree_result(result);
// 	LOG_DBG("Finished: %s, %d rows synced", __func__, i);
// 	return SUCCEED;


	
// 	return FAIL;
}

static int db_incremental_sync(unsigned char sync_mode, DB_RESULT result, int columns, 
			cmp_func_t cmp_func, dbsync_preproc_row_func_t preproc_func, 
			conf_dbsync_add_func_t add_func, conf_dbsync_update_func_t upd_func, conf_dbsync_delete_func_t del_func, int obj_type,  
			zbx_hashset_t *ids) {

	
	return FAIL;
}


// /*************************************************************************
//  * incremental (using changeset logic)
//  * ***********************************************************************/
// static int glb_inc_dbsync_compare(zbx_dbsync_t *sync, DB_RESULT result, int columns, zbx_hashset_t *hash, 
// 					 zbx_dbsync_preproc_row_func_t preproc_func, int obj_type, char *sync_name) {

// 	DB_ROW			dbrow;
// 	zbx_uint64_t	rowid;
// 	data_t		*data;
// 	int i = 0, d = 0;
	
// 	LOG_DBG("In: %s", __func__);

// 	dbsync_prepare(sync, columns, preproc_func);
	

// 	while (NULL != (dbrow = DBfetch(result)))
// 	{
// 		i++;

// 		ZBX_STR2UINT64(rowid, dbrow[0]);
		
// 		dbrow = dbsync_preproc_row(sync, dbrow);
				
// 		if (NULL == (data = zbx_hashset_search(hash, &rowid))) {
// 			dbsync_add_row(sync, rowid, ZBX_DBSYNC_ROW_ADD, dbrow);	
// 			continue;
// 		}

// 		dbsync_add_row(sync, rowid, ZBX_DBSYNC_ROW_UPDATE, dbrow);
// 	}
	
// 	DBfree_result(result);
	
// 	if (NULL == (result = DBselect(
// 			"select obj_id from changeset_work where obj_type = %d and change_type = %d;", obj_type, DB_DELETE))) 
// 			return FAIL;

// 	while (NULL != (dbrow = DBfetch(result))) {
// 		ZBX_STR2UINT64(rowid, dbrow[0]);
		
// 		dbsync_add_row(sync, rowid, ZBX_DBSYNC_ROW_REMOVE, NULL);
		
// 		d++;	
// 	}
// 	DBfree_result(result);	
// 	if ( i+d > 0)
// 		LOG_INF("Finished: %s, %d rows add/chandged, %d rows deleted in sync of '%s'", __func__, i, d, sync_name);
// 	return SUCCEED;
// }





/************************************************************************
 * non-locking version with config-based functions
 * **********************************************************************/
int conf_sync_from_db(unsigned char sync_mode, DB_RESULT result, int columns, 
			cmp_func_t cmp_func, dbsync_preproc_row_func_t preproc_func, 
			conf_dbsync_add_func_t add_func, conf_dbsync_update_func_t upd_func, conf_dbsync_delete_func_t del_func, int obj_type, char *sync_name,  
			zbx_vector_uint64_t *ids) {

	// if ( DBSYNC_UPDATE == sync_mode || DBSYNC_INIT == sync_mode) 
	// 	return conf_dbsync_full_sync(sync_mode, result, columns, cmp_func, preproc_func, add_func, upd_func, del_func, ids);
		
	// if (DBSYNC_CHANGESET == sync_mode) 
	// 	return conf_dbsync_incremental_sync(sync_mode, result, columns, preproc_func, add_func, upd_func, del_func, ids, obj_type);

	// LOG_WRN("Unsupported items sync type: %d", sync_mode);
	// THIS_SHOULD_NEVER_HAPPEN;
	// exit(-1);
}

/************************************************************
 * gerates inner join sub-sql to limit data only to sepcific 
 * objects type and type of operations (create, udpate)
 * for not incremetnal mode returns empty string
 * *********************************************************/
char * conf_sync_inner_join_subquery(unsigned char mode, char *field){
	static char sql[1024];
	if (DBSYNC_CHANGESET == mode)
		zbx_snprintf(sql, 1024, " inner join changeset_work cs on %s = cs.obj_id", field);
	else 
		zbx_snprintf(sql,1024," ");
	return sql;
}
/************************************************************
 * gerates where sub-sql to limit data only to sepcific 
 * objects type and type of operations (create, udpate)
 * for not incremetnal mode returns empty string
 * *********************************************************/
char * conf_sync_where_condition(unsigned char mode, int obj_type) {
	// static char sql[1024];
	
	// if (DBSYNC_CHANGESET == mode)
	// 	zbx_snprintf(sql, 1024, " and cs.obj_type = %d and cs.change_type in (%d,%d)", obj_type, DB_CREATE, DB_UPDATE);
	// else 
	// 	zbx_snprintf(sql,1024," ");
	// return sql;
}
/* macro value validators */

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_uint64                                            *
 *                                                                            *
 * Purpose: compares 64 bit unsigned integer with a raw database value        *
 *                                                                            *
 ******************************************************************************/
int	dbsync_compare_uint64(const char *value_raw, zbx_uint64_t value)
{
	zbx_uint64_t	value_ui64;

	ZBX_DBROW2UINT64(value_ui64, value_raw);

	return (value_ui64 == value ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_int                                               *
 *                                                                            *
 * Purpose: compares 32 bit signed integer with a raw database value          *
 *                                                                            *
 ******************************************************************************/
int	dbsync_compare_int(const char *value_raw, int value)
{
	return (atoi(value_raw) == value ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_uchar                                             *
 *                                                                            *
 * Purpose: compares unsigned character with a raw database value             *
 *                                                                            *
 ******************************************************************************/

int	dbsync_compare_uchar(const char *value_raw, unsigned char value)
{
	unsigned char	value_uchar;

	ZBX_STR2UCHAR(value_uchar, value_raw);
	return (value_uchar == value ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_str                                               *
 *                                                                            *
 * Purpose: compares string with a raw database value                         *
 *                                                                            *
 ******************************************************************************/

int	dbsync_compare_str(const char *value_raw, const char *value)
{
	return (0 == strcmp(value_raw, value) ? SUCCEED : FAIL);
}

/******************************************************************************
 *                                                                            *
 * Function: dbsync_compare_serialized_expression                             *
 *                                                                            *
 * Purpose: compare serialized expression                                     *
 *                                                                            *
 * Parameter: col   - [IN] the base64 encoded expression                      *
 *            data2 - [IN] the serialized expression in cache                 *
 *                                                                            *
 * Return value: SUCCEED - the expressions are identical                      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	dbsync_compare_serialized_expression(const char *col, const unsigned char *data2)
{
// 	zbx_uint32_t	offset1, len1, offset2, len2;
// 	unsigned char	*data1;
// 	int		col_len, data1_len, ret = FAIL;

// 	if (NULL == data2)
// 	{
// 		if (NULL == col || '\0' == *col)
// 			return SUCCEED;
// 		return FAIL;
// 	}

// 	if (NULL == col || '\0' == *col)
// 		return FAIL;

// 	col_len = strlen(col);
// 	data1 = zbx_malloc(NULL, col_len);

// 	str_base64_decode(col, (char *)data1, col_len, &data1_len);

// 	offset1 = zbx_deserialize_uint31_compact((const unsigned char *)data1, &len1);
// 	offset2 = zbx_deserialize_uint31_compact((const unsigned char *)data2, &len2);

// 	if (offset1 != offset2 || len1 != len2)
// 		goto out;

// 	if (0 != memcmp(data1 + offset1, data2 + offset2, len1))
// 		goto out;

// 	ret = SUCCEED;
// out:
// 	zbx_free(data1);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: encode_expression                                                *
 *                                                                            *
 * Purpose: encode serialized expression to be returned as db field           *
 *                                                                            *
 * Parameter: sync - [OUT] the changeset                                      *
 *                                                                            *
 * Return value: SUCCEED - the changeset was successfully calculated          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
char	*encode_expression(const zbx_eval_context_t *ctx)
{
	unsigned char	*data;
	size_t		len;
	char		*str = NULL;

	len = zbx_eval_serialize(ctx, NULL, &data);
	str_base64_encode_dyn((const char *)data, &str, len);
	zbx_free(data);

	return str;
}
