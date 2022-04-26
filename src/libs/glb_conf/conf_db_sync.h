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
#ifndef CONF_DB_SYNC_H
#define CONF_DB_SYNC_H
#include "common.h"
#include "zbxalgo.h"
#include "log.h"
#include "db.h"
#include "base64.h"

#define DBSYNC_INIT		0
#define DBSYNC_UPDATE	1
#define DBSYNC_CHANGESET 63

//from dbsync:
#define DBSYNC_ROW_NONE	0
/*  a new object must be added to configuration cache */
#define DBSYNC_ROW_ADD	1
/* a cached object must be updated in configuration cache */
#define DBSYNC_ROW_UPDATE	2
/* a cached object must be removed from configuration cache */
#define DBSYNC_ROW_REMOVE	3




typedef int (*conf_dbsync_add_func_t)(u_int64_t id, char **row);
typedef int (*conf_dbsync_update_func_t)(u_int64_t id, char **row);
typedef int (*conf_dbsync_delete_func_t)(u_int64_t id);
typedef char **(*dbsync_preproc_row_func_t)(char **row);
typedef int (*cmp_func_t)(void* hash_data, DB_ROW row);

int conf_sync_from_db(unsigned char sync_mode, DB_RESULT result, int columns, 
			cmp_func_t cmp_func, dbsync_preproc_row_func_t preproc_func, 
			conf_dbsync_add_func_t add_func, conf_dbsync_update_func_t upd_func, conf_dbsync_delete_func_t del_func, int obj_type, char *sync_name,  
			zbx_vector_uint64_t *ids);


int		dbsync_compare_str(const char *value_raw, const char *value);
int		dbsync_compare_uchar(const char *value_raw, unsigned char value);
int		dbsync_compare_int(const char *value_raw, int value);
int		dbsync_compare_uint64(const char *value_raw, zbx_uint64_t value);
int		dbsync_compare_serialized_expression(const char *col, const unsigned char *data2);

// unsigned char	*conf_sync_decode_serialized_expression(const char *src, mem_funcs_t* memf)
// {
// 	unsigned char	*dst;
// 	int		data_len, src_len;

// 	if (NULL == src || '\0' == *src)
// 		return NULL;

// 	src_len = strlen(src) * 3 / 4;
// 	dst = memf->malloc_func(NULL, src_len);
// 	str_base64_decode(src, (char *)dst, src_len, &data_len);

// 	return dst;
// }
#endif