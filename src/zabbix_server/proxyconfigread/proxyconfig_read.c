/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#include "proxyconfig_read.h"

#include "zbxdbwrap.h"
#include "zbxdbhigh.h"
#include "libs/zbxkvs/kvs.h"
#include "libs/zbxvault/vault.h"
#include "log.h"
#include "zbxcommshigh.h"
#include "zbxcompress.h"
#include "zbxcrypto.h"

extern char	*CONFIG_VAULTDBPATH;
extern int	CONFIG_TRAPPER_TIMEOUT;

typedef struct
{
	char		*path;
	zbx_hashset_t	keys;
}
zbx_keys_path_t;


static int	keys_path_compare(const void *d1, const void *d2)
{
	const zbx_keys_path_t	*ptr1 = *((const zbx_keys_path_t * const *)d1);
	const zbx_keys_path_t	*ptr2 = *((const zbx_keys_path_t * const *)d2);

	return strcmp(ptr1->path, ptr2->path);
}

static zbx_hash_t	keys_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_ALGO(*(const char * const *)data, strlen(*(const char * const *)data),
			ZBX_DEFAULT_HASH_SEED);
}

static int	keys_compare(const void *d1, const void *d2)
{
	return strcmp(*(const char * const *)d1, *(const char * const *)d2);
}

static void	key_path_free(void *data)
{
	zbx_hashset_iter_t	iter;
	char			**ptr;
	zbx_keys_path_t		*keys_path = (zbx_keys_path_t *)data;

	zbx_hashset_iter_reset(&keys_path->keys, &iter);
	while (NULL != (ptr = (char **)zbx_hashset_iter_next(&iter)))
		zbx_free(*ptr);
	zbx_hashset_destroy(&keys_path->keys);

	zbx_free(keys_path->path);
	zbx_free(keys_path);
}

static void	get_macro_secrets(const zbx_vector_ptr_t *keys_paths, struct zbx_json *j)
{
	int		i;
	zbx_kvs_t	kvs;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_kvs_create(&kvs, 100);

	zbx_json_addobject(j, ZBX_PROTO_TAG_MACRO_SECRETS);

	for (i = 0; i < keys_paths->values_num; i++)
	{
		zbx_keys_path_t		*keys_path;
		char			*error = NULL, **ptr;
		zbx_hashset_iter_t	iter;

		keys_path = (zbx_keys_path_t *)keys_paths->values[i];
		if (FAIL == zbx_vault_kvs_get(keys_path->path, &kvs, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot get secrets for path \"%s\": %s", keys_path->path, error);
			zbx_free(error);
			continue;
		}

		zbx_json_addobject(j, keys_path->path);

		zbx_hashset_iter_reset(&keys_path->keys, &iter);
		while (NULL != (ptr = (char **)zbx_hashset_iter_next(&iter)))
		{
			zbx_kv_t	*kv, kv_local;

			kv_local.key = *ptr;

			if (NULL != (kv = zbx_kvs_search(&kvs, &kv_local)))
				zbx_json_addstring(j, kv->key, kv->value, ZBX_JSON_TYPE_STRING);
		}
		zbx_json_close(j);

		zbx_kvs_clear(&kvs);
	}

	zbx_json_close(j);
	zbx_kvs_destroy(&kvs);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: add database row to the proxy config json data                    *
 *                                                                            *
 * Parameters: j      - [OUT] the output json                                 *
 *             row    - [IN] the database row to add                          *
 *             table  - [IN] the table configuration                          *
 *             recids - [OUT] the record identifiers (optional)               *
 *                                                                            *
 ******************************************************************************/
static void	proxyconfig_add_row(struct zbx_json *j, const DB_ROW row, const ZBX_TABLE *table,
		zbx_vector_uint64_t *recids)
{
	int	fld = 0, i;

	zbx_json_addstring(j, NULL, row[fld++], ZBX_JSON_TYPE_INT);

	if (NULL != recids)
	{
		zbx_uint64_t	recid;

		ZBX_STR2UINT64(recid, row[0]);
		zbx_vector_uint64_append(recids, recid);
	}

	for (i = 0; 0 != table->fields[i].name; i++)
	{
		if (0 == (table->fields[i].flags & ZBX_PROXY))
			continue;

		switch (table->fields[i].type)
		{
			case ZBX_TYPE_INT:
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				if (SUCCEED != DBis_null(row[fld]))
					zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_INT);
				else
					zbx_json_addstring(j, NULL, NULL, ZBX_JSON_TYPE_NULL);
				break;
			default:
				zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_STRING);
				break;
		}
		fld++;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: get table fields, add them to output json and sql select          *
 *                                                                            *
 * Parameters: sql        - [IN/OUT] the sql select string                    *
 *             sql_alloc  - [IN/OUT]                                          *
 *             sql_offset - [IN/OUT]                                          *
 *             table      - [IN] the table                                    *
 *             j          - [OUT] the output json                             *
 *                                                                            *
 ******************************************************************************/
static void	proxyconfig_get_fields(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_TABLE *table,
		struct zbx_json *j)
{
	int	i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "select %s", table->recid);

	zbx_json_addarray(j, "fields");
	zbx_json_addstring(j, NULL, table->recid, ZBX_JSON_TYPE_STRING);

	for (i = 0; 0 != table->fields[i].name; i++)
	{
		if (0 == (table->fields[i].flags & ZBX_PROXY))
			continue;

		zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ',');
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, table->fields[i].name);

		zbx_json_addstring(j, NULL, table->fields[i].name, ZBX_JSON_TYPE_STRING);
	}

	zbx_json_close(j);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get global/host macro data of the specified hosts from database   *
 *                                                                            *
 * Parameters: table_name - [IN] table name - globalmacro/hostmacro           *
 *             hostids    - [IN] the target hostids for hostmacro table and   *
 *                               NULL for globalmacro table                   *
 *             keys_paths - [OUT] the vault macro path/key                    *
 *             j          - [OUT] the output json                             *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_macro_updates(const char *table_name, const zbx_vector_uint64_t *hostids,
		zbx_vector_ptr_t *keys_paths, struct zbx_json *j, char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	const ZBX_TABLE	*table;
	char		*sql;
	size_t		sql_alloc =  4 * ZBX_KIBIBYTE, sql_offset = 0;
	int		i, ret = FAIL, offset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	table = DBget_table(table_name);
	zbx_json_addobject(j, table->table);

	sql = (char *)zbx_malloc(NULL, sql_alloc);

	proxyconfig_get_fields(&sql, &sql_alloc, &sql_offset, table, j);
	zbx_json_addarray(j, "data");

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s", table->table);

	if (NULL != hostids)
	{
		/* no hosts to get macros from, send empty data */
		if (0 == hostids->values_num)
			goto end;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids->values, hostids->values_num);
		offset = 1;
	}
	else
		offset = 0;

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by ");
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->recid);

	if (NULL == (result = DBselect("%s", sql)))
	{
		*error = zbx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);
		goto out;
	}

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s", table->table);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_keys_path_t	*keys_path, keys_path_local;
		unsigned char	type;
		char		*path, *key;

		zbx_json_addarray(j, NULL);
		proxyconfig_add_row(j, row, table, NULL);
		zbx_json_close(j);

		ZBX_STR2UCHAR(type, row[3 + offset]);

		if (ZBX_MACRO_VALUE_VAULT != type)
			continue;

		zbx_strsplit_last(row[2 + offset], ':', &path, &key);

		if (NULL == key)
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse macro \"%s\" value \"%s\"",
					row[1 + offset], row[2 + offset]);
			goto next;
		}

		if (NULL != CONFIG_VAULTDBPATH && 0 == strcasecmp(CONFIG_VAULTDBPATH, path) &&
				(0 == strcasecmp(key, ZBX_PROTO_TAG_PASSWORD)
						|| 0 == strcasecmp(key, ZBX_PROTO_TAG_USERNAME)))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse macro \"%s\" value \"%s\":"
					" database credentials should not be used with Vault macros",
					row[1 + offset], row[2 + offset]);
			goto next;
		}

		keys_path_local.path = path;

		if (FAIL == (i = zbx_vector_ptr_search(keys_paths, &keys_path_local, keys_path_compare)))
		{
			keys_path = zbx_malloc(NULL, sizeof(zbx_keys_path_t));
			keys_path->path = path;

			zbx_hashset_create(&keys_path->keys, 0, keys_hash, keys_compare);
			zbx_hashset_insert(&keys_path->keys, &key, sizeof(char *));

			zbx_vector_ptr_append(keys_paths, keys_path);
			path = key = NULL;
		}
		else
		{
			keys_path = (zbx_keys_path_t *)keys_paths->values[i];
			if (NULL == zbx_hashset_search(&keys_path->keys, &key))
			{
				zbx_hashset_insert(&keys_path->keys, &key, sizeof(char **));
				key = NULL;
			}
		}
next:
		zbx_free(key);
		zbx_free(path);
	}
	DBfree_result(result);
end:
	zbx_json_close(j);
	zbx_json_close(j);

	ret = SUCCEED;
out:
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get table data from database                                      *
 *                                                                            *
 * Parameters: table_name - [IN] table name -                                 *
 *             key_name   - [IN] the key field name used to select rows       *
 *                               (all rows selected when NULL)                *
 *             key_ids    - [IN] the key values used to select rows (optional)*
 *             filter     - [IN] custom filter to apply when selecting rows   *
 *                               (optional)                                   *
 *             recids     - [OUT] the selected record identifiers, sorted     *
 *                                (optional)                                  *
 *             j          - [OUT] the output json                             *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_table_data(const char *table_name, const char *key_name,
		const zbx_vector_uint64_t *key_ids, const char *filter, zbx_vector_uint64_t *recids, struct zbx_json *j,
		char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	const ZBX_TABLE	*table;
	char		*sql = NULL;
	size_t		sql_alloc =  4 * ZBX_KIBIBYTE, sql_offset = 0;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	table = DBget_table(table_name);
	zbx_json_addobject(j, table->table);

	sql = (char *)zbx_malloc(NULL, sql_alloc);
	proxyconfig_get_fields(&sql, &sql_alloc, &sql_offset, table, j);

	zbx_json_addarray(j, ZBX_PROTO_TAG_DATA);

	if (NULL == key_ids || 0 != key_ids->values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s", table->table);

		if (NULL != key_ids || NULL != filter)
		{
			const char	*keyword = " where";

			if (NULL != key_ids)
			{
				zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, keyword);
				DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, key_name, key_ids->values,
						key_ids->values_num);
				keyword = " and";
			}

			if (NULL != filter)
			{
				zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, keyword);
				zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, filter);
			}
		}

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by ");
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->recid);

		if (NULL == (result = DBselect("%s", sql)))
		{
			*error = zbx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);
			goto out;
		}

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s", table->table);

		while (NULL != (row = DBfetch(result)))
		{
			zbx_json_addarray(j, NULL);
			proxyconfig_add_row(j, row, table, recids);
			zbx_json_close(j);
		}
		DBfree_result(result);
	}

	zbx_json_close(j);
	zbx_json_close(j);

	if (NULL != recids)
		zbx_vector_uint64_sort(recids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	ret = SUCCEED;
out:
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

typedef struct
{
	zbx_uint64_t	itemid;
	zbx_uint64_t	master_itemid;
	DB_ROW		row;
	int		cols_num;
}
zbx_proxyconfig_dep_item_t;

ZBX_PTR_VECTOR_DECL(proxyconfig_dep_item_ptr, zbx_proxyconfig_dep_item_t *)
ZBX_PTR_VECTOR_IMPL(proxyconfig_dep_item_ptr, zbx_proxyconfig_dep_item_t *)

static void	proxyconfig_dep_item_free(zbx_proxyconfig_dep_item_t *item)
{
	int	i;

	for (i = 0; i < item->cols_num; i++)
		zbx_free(item->row[i]);

	zbx_free(item->row);
	zbx_free(item);
}

static zbx_proxyconfig_dep_item_t	*proxyconfig_dep_item_create(zbx_uint64_t itemid, zbx_uint64_t master_itemid,
		const DB_ROW row, int cols_num)
{
	zbx_proxyconfig_dep_item_t	*item;
	int				i;

	item = (zbx_proxyconfig_dep_item_t *)zbx_malloc(NULL, sizeof(zbx_proxyconfig_dep_item_t));
	item->itemid = itemid;
	item->master_itemid = master_itemid;
	item->cols_num = cols_num;
	item->row = (DB_ROW)zbx_malloc(NULL, sizeof(char *) * (size_t)cols_num);

	for (i = 0; i < cols_num; i++)
	{
		if (NULL == row[i])
			item->row[i] = NULL;
		else
			item->row[i] = zbx_strdup(NULL, row[i]);
	}

	return item;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get item data from items table                                    *
 *                                                                            *
 * Parameters: hostids    - [IN] the target host identifiers                  *
 *             itemids    - [IN] the selected item identifiers, sorted        *
 *             j          - [OUT] the output json                             *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_item_data(const zbx_vector_uint64_t *hostids, zbx_vector_uint64_t *itemids,
		struct zbx_json *j, char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	const ZBX_TABLE	*table;
	char		*sql;
	size_t		sql_alloc =  4 * ZBX_KIBIBYTE, sql_offset = 0;
	int		ret = FAIL, fld_key = -1, fld_type = -1, fld_master_itemid = -1, i, fld, dep_items_num;
	zbx_uint64_t	itemid, master_itemid;

	zbx_vector_proxyconfig_dep_item_ptr_t	dep_items;
	zbx_hashset_t				items;
	zbx_proxyconfig_dep_item_t		*dep_item;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_hashset_create(&items, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_proxyconfig_dep_item_ptr_create(&dep_items);

	table = DBget_table("items");

	/* get type, key_ field indexes used to check if item is processed by server */
	for (i = 0, fld = 1; 0 != table->fields[i].name; i++)
	{
		if (0 == (table->fields[i].flags & ZBX_PROXY))
			continue;

		if (0 == strcmp(table->fields[i].name, "type"))
			fld_type = fld;
		else if (0 == strcmp(table->fields[i].name, "key_"))
			fld_key = fld;
		else if (0 == strcmp(table->fields[i].name, "master_itemid"))
			fld_master_itemid = fld;
		fld++;
	}

	if (-1 == fld_type || -1 == fld_key || -1 == fld_master_itemid)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	zbx_json_addobject(j, table->table);

	sql = (char *)zbx_malloc(NULL, sql_alloc);
	proxyconfig_get_fields(&sql, &sql_alloc, &sql_offset, table, j);

	zbx_json_addarray(j, ZBX_PROTO_TAG_DATA);

	if (0 != hostids->values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s where", table->table);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids->values, hostids->values_num);
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and flags<>%d and type<>%d",
				ZBX_FLAG_DISCOVERY_PROTOTYPE, ITEM_TYPE_CALCULATED);

		if (NULL == (result = DBselect("%s", sql)))
		{
			*error = zbx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);
			goto out;
		}

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s", table->table);

		while (NULL != (row = DBfetch(result)))
		{
			unsigned char	type;

			ZBX_STR2UCHAR(type, row[fld_type]);
			if (SUCCEED == is_item_processed_by_server(type, row[fld_key]))
					continue;

			ZBX_DBROW2UINT64(itemid, row[0]);

			if (ITEM_TYPE_DEPENDENT != atoi(row[fld_type]))
			{
				zbx_json_addarray(j, NULL);
				proxyconfig_add_row(j, row, table, itemids);
				zbx_json_close(j);

				zbx_hashset_insert(&items, &itemid, sizeof(itemid));
			}
			else
			{
				ZBX_DBROW2UINT64(master_itemid, row[fld_master_itemid]);
				dep_item = proxyconfig_dep_item_create(itemid, master_itemid, row, fld);
				zbx_vector_proxyconfig_dep_item_ptr_append(&dep_items, dep_item);
			}
		}
		DBfree_result(result);

		/* add dependent items processed by proxy */
		if (0 != dep_items.values_num)
		{
			do
			{
				dep_items_num = dep_items.values_num;

				for (i = 0; i < dep_items.values_num; )
				{
					dep_item = dep_items.values[i];

					if (NULL != zbx_hashset_search(&items, &dep_item->master_itemid))
					{
						zbx_json_addarray(j, NULL);
						proxyconfig_add_row(j, dep_item->row, table, itemids);
						zbx_json_close(j);

						zbx_hashset_insert(&items, &dep_item->itemid, sizeof(zbx_uint64_t));
						proxyconfig_dep_item_free(dep_item);
						zbx_vector_proxyconfig_dep_item_ptr_remove_noorder(&dep_items, i);
					}
					else
						i++;
				}
			}
			while (dep_items_num != dep_items.values_num);
		}
	}

	zbx_json_close(j);
	zbx_json_close(j);

	zbx_vector_uint64_sort(itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	ret = SUCCEED;
out:
	zbx_free(sql);

	zbx_vector_proxyconfig_dep_item_ptr_clear_ext(&dep_items, proxyconfig_dep_item_free);
	zbx_vector_proxyconfig_dep_item_ptr_destroy(&dep_items);
	zbx_hashset_destroy(&items);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get host and related table data from database                     *
 *                                                                            *
 * Parameters: hostids    - [IN] the target host identifiers                  *
 *             j          - [OUT] the output json                             *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_host_data(const zbx_vector_uint64_t *hostids, struct zbx_json *j, char **error)
{
	zbx_vector_uint64_t	interfaceids, itemids;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&interfaceids);
	zbx_vector_uint64_create(&itemids);

	if (SUCCEED != proxyconfig_get_table_data("hosts", "hostid", hostids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("interface",  "hostid", hostids, NULL, &interfaceids, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("interface_snmp",  "interfaceid", &interfaceids, NULL, NULL,
			j, error))
	{
		goto out;
	}

	if (SUCCEED != proxyconfig_get_table_data("host_inventory", "hostid", hostids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_item_data(hostids, &itemids, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("item_rtdata", "itemid", &itemids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("item_preproc", "itemid", &itemids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("item_parameter", "itemid", &itemids, NULL, NULL, j, error))
		goto out;

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&itemids);
	zbx_vector_uint64_destroy(&interfaceids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get discovery rule and checks data from database                  *
 *                                                                            *
 * Parameters: proxy - [IN] the target proxy                                  *
 *             j     - [OUT] the output json                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_drules_data(const DC_PROXY *proxy, struct zbx_json *j, char **error)
{
	zbx_vector_uint64_t	druleids;
	zbx_vector_uint64_t	proxy_hostids;
	int			ret = FAIL;
	char			*filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&druleids);
	zbx_vector_uint64_create(&proxy_hostids);

	zbx_vector_uint64_append(&proxy_hostids, proxy->hostid);

	zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset, " status=%d", DRULE_STATUS_MONITORED);

	if (SUCCEED != proxyconfig_get_table_data("drules", "proxy_hostid", &proxy_hostids, filter, &druleids, j,
			error))
	{
		goto out;
	}

	if (SUCCEED != proxyconfig_get_table_data("dchecks", "druleid", &druleids, NULL, NULL, j, error))
		goto out;

	ret = SUCCEED;
out:
	zbx_free(filter);
	zbx_vector_uint64_destroy(&proxy_hostids);
	zbx_vector_uint64_destroy(&druleids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get global regular expression (regexps/expressions) data from     *
 *          database                                                          *
 *                                                                            *
 * Parameters: j     - [OUT] the output json                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_expression_data(struct zbx_json *j, char **error)
{
	zbx_vector_uint64_t	regexpids;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&regexpids);

	if (SUCCEED != proxyconfig_get_table_data("regexps", NULL, NULL, NULL, &regexpids, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("expressions", "regexpid", &regexpids, NULL, NULL, j, error))
		goto out;

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&regexpids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get httptest and related data from database                       *
 *                                                                            *
 * Parameters: httptestids - [IN] the httptest identifiers                    *
 *             j           - [OUT] the output json                            *
 *             error       - [OUT] the error message                          *
 *                                                                            *
 * Return value: SUCCEED - the data was read successfully                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	proxyconfig_get_httptest_data(const zbx_vector_uint64_t *httptestids, struct zbx_json *j, char **error)
{
	zbx_vector_uint64_t	httpstepids;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&httpstepids);

	if (SUCCEED != proxyconfig_get_table_data("httptest", "httptestid", httptestids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("httptestitem", "httptestid", httptestids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("httptest_field", "httptestid", httptestids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("httpstep", "httptestid", httptestids, NULL, &httpstepids, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("httpstepitem", "httpstepid", &httpstepids, NULL, NULL, j, error))
		goto out;

	if (SUCCEED != proxyconfig_get_table_data("httpstep_field", "httpstepid", &httpstepids, NULL, NULL, j, error))
		goto out;

	ret = SUCCEED;
out:
	zbx_vector_uint64_destroy(&httpstepids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

static int	proxyconfig_get_tables(const DC_PROXY *proxy, zbx_uint64_t proxy_config_revision,
		const zbx_dc_revision_t *dc_revision, struct zbx_json *j, zbx_proxyconfig_status_t *status,
		char **error)
{
#define ZBX_PROXYCONFIG_SYNC_HOSTS		0x0001
#define ZBX_PROXYCONFIG_SYNC_GMACROS		0x0002
#define ZBX_PROXYCONFIG_SYNC_HMACROS		0x0004
#define ZBX_PROXYCONFIG_SYNC_DRULES		0x0008
#define ZBX_PROXYCONFIG_SYNC_EXPRESSIONS	0x0010
#define ZBX_PROXYCONFIG_SYNC_CONFIG		0x0020
#define ZBX_PROXYCONFIG_SYNC_HTTPTESTS		0x0040
#define ZBX_PROXYCONFIG_SYNC_AUTOREG		0x0080

#define ZBX_PROXYCONFIG_SYNC_ALL	(ZBX_PROXYCONFIG_SYNC_HOSTS | ZBX_PROXYCONFIG_SYNC_GMACROS | 		\
					ZBX_PROXYCONFIG_SYNC_HMACROS |ZBX_PROXYCONFIG_SYNC_DRULES |		\
					ZBX_PROXYCONFIG_SYNC_EXPRESSIONS | ZBX_PROXYCONFIG_SYNC_CONFIG | 	\
					ZBX_PROXYCONFIG_SYNC_HTTPTESTS | ZBX_PROXYCONFIG_SYNC_AUTOREG)

	zbx_vector_uint64_t	hostids, httptestids, updated_hostids, removed_hostids, del_macro_hostids,
				macro_hostids;
	zbx_vector_ptr_t	keys_paths;
	int			global_macros = FAIL, ret = FAIL, i;
	zbx_uint64_t		flags = 0;

	zbx_vector_uint64_create(&hostids);
	zbx_vector_uint64_create(&updated_hostids);
	zbx_vector_uint64_create(&removed_hostids);
	zbx_vector_uint64_create(&httptestids);
	zbx_vector_uint64_create(&macro_hostids);
	zbx_vector_uint64_create(&del_macro_hostids);
	zbx_vector_ptr_create(&keys_paths);

	if (proxy_config_revision < proxy->revision || proxy_config_revision < proxy->macro_revision)
	{
		zbx_vector_uint64_reserve(&hostids, 1000);
		zbx_vector_uint64_reserve(&updated_hostids, 1000);
		zbx_vector_uint64_reserve(&removed_hostids, 100);
		zbx_vector_uint64_reserve(&httptestids, 100);
		zbx_vector_uint64_reserve(&macro_hostids, 1000);
		zbx_vector_uint64_reserve(&del_macro_hostids, 100);

		zbx_dc_get_proxy_config_updates(proxy->hostid, proxy_config_revision, &hostids, &updated_hostids,
				&removed_hostids, &httptestids);

		zbx_dc_get_macro_updates(&hostids, &updated_hostids, proxy_config_revision, &macro_hostids,
				&global_macros, &del_macro_hostids);
	}

	if (0 != proxy_config_revision)
	{
		if (0 != updated_hostids.values_num)
			flags |= ZBX_PROXYCONFIG_SYNC_HOSTS;

		if (SUCCEED == global_macros)
			flags |= ZBX_PROXYCONFIG_SYNC_GMACROS;

		if(0 != macro_hostids.values_num)
			flags |= ZBX_PROXYCONFIG_SYNC_HMACROS;

		if (proxy_config_revision < proxy->revision)
			flags |= ZBX_PROXYCONFIG_SYNC_DRULES;

		if (proxy_config_revision < dc_revision->expression)
			flags |= ZBX_PROXYCONFIG_SYNC_EXPRESSIONS;

		if (proxy_config_revision < dc_revision->config_table)
			flags |= ZBX_PROXYCONFIG_SYNC_CONFIG;

		if (0 != httptestids.values_num)
			flags |= ZBX_PROXYCONFIG_SYNC_HTTPTESTS;

		if (proxy_config_revision < dc_revision->autoreg_tls)
			flags |= ZBX_PROXYCONFIG_SYNC_AUTOREG;
	}
	else
		flags = ZBX_PROXYCONFIG_SYNC_ALL;

	zbx_json_addobject(j, ZBX_PROTO_TAG_DATA);

	if (0 != flags)
	{
		DBbegin();

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_HOSTS) &&
				SUCCEED != proxyconfig_get_host_data(&updated_hostids, j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_GMACROS) &&
				SUCCEED != proxyconfig_get_macro_updates("globalmacro", NULL, &keys_paths, j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_HMACROS))
		{
			if (SUCCEED != proxyconfig_get_table_data("hosts_templates", "hostid", &macro_hostids, NULL,
					NULL, j, error))
			{
				goto out;
			}

			if (SUCCEED != proxyconfig_get_macro_updates("hostmacro", &macro_hostids, &keys_paths, j, error))
				goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_DRULES) &&
				SUCCEED != proxyconfig_get_drules_data(proxy, j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_EXPRESSIONS) &&
				SUCCEED != proxyconfig_get_expression_data(j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_CONFIG) &&
				SUCCEED != proxyconfig_get_table_data("config", NULL, NULL, NULL, NULL, j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_HTTPTESTS) &&
				SUCCEED != proxyconfig_get_httptest_data(&httptestids, j, error))
		{
			goto out;
		}

		if (0 != (flags & ZBX_PROXYCONFIG_SYNC_AUTOREG) &&
				SUCCEED != proxyconfig_get_table_data("config_autoreg_tls", NULL, NULL, NULL, NULL, j,
						error))
		{
			goto out;
		}
	}

	zbx_json_close(j);

	if (0 != removed_hostids.values_num)
	{
		zbx_json_addarray(j, ZBX_PROTO_TAG_REMOVED_HOSTIDS);

		for (i = 0; i < removed_hostids.values_num; i++)
			zbx_json_adduint64(j, NULL, removed_hostids.values[i]);

		zbx_json_close(j);
	}

	if (0 != del_macro_hostids.values_num)
	{
		zbx_json_addarray(j, ZBX_PROTO_TAG_REMOVED_MACRO_HOSTIDS);

		for (i = 0; i < del_macro_hostids.values_num; i++)
			zbx_json_adduint64(j, NULL, del_macro_hostids.values[i]);

		zbx_json_close(j);
	}

	if (0 != keys_paths.values_num)
		get_macro_secrets(&keys_paths, j);

	if (0 == flags && 0 == removed_hostids.values_num && 0 == del_macro_hostids.values_num)
		*status = ZBX_PROXYCONFIG_STATUS_EMPTY;
	else
		*status = ZBX_PROXYCONFIG_STATUS_DATA;

	ret = SUCCEED;
out:
	if (0 != flags)
		DBcommit();

	zbx_vector_ptr_clear_ext(&keys_paths, key_path_free);
	zbx_vector_ptr_destroy(&keys_paths);
	zbx_vector_uint64_destroy(&httptestids);
	zbx_vector_uint64_destroy(&macro_hostids);
	zbx_vector_uint64_destroy(&del_macro_hostids);
	zbx_vector_uint64_destroy(&removed_hostids);
	zbx_vector_uint64_destroy(&updated_hostids);
	zbx_vector_uint64_destroy(&hostids);

	return ret;

#undef ZBX_PROXYCONFIG_SYNC_HOSTS
#undef ZBX_PROXYCONFIG_SYNC_GMACROS
#undef ZBX_PROXYCONFIG_SYNC_HMACROS
#undef ZBX_PROXYCONFIG_SYNC_DRULES
#undef ZBX_PROXYCONFIG_SYNC_EXPRESSIONS
#undef ZBX_PROXYCONFIG_SYNC_CONFIG
#undef ZBX_PROXYCONFIG_SYNC_HTTPTESTS
#undef ZBX_PROXYCONFIG_SYNC_AUTOREG
}

/******************************************************************************
 *                                                                            *
 * Purpose: prepare proxy configuration data                                  *
 *                                                                            *
 ******************************************************************************/
int	zbx_proxyconfig_get_data(DC_PROXY *proxy, const struct zbx_json_parse *jp_request, struct zbx_json *j,
		zbx_proxyconfig_status_t *status, char **error)
{
	int			ret = FAIL;
	char			token[ZBX_SESSION_TOKEN_SIZE + 1], tmp[ZBX_MAX_UINT64_LEN + 1];
	zbx_uint64_t		proxy_config_revision;
	zbx_dc_revision_t	dc_revision;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:" ZBX_FS_UI64, __func__, proxy->hostid);

	if (SUCCEED != zbx_json_value_by_name(jp_request, ZBX_PROTO_TAG_SESSION, token, sizeof(token), NULL))
	{
		*error = zbx_strdup(NULL, "cannot get session from proxy configuration request");
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name(jp_request, ZBX_PROTO_TAG_CONFIG_REVISION, tmp, sizeof(tmp), NULL))
	{
		*error = zbx_strdup(NULL, "cannot get revision from proxy configuration request");
		goto out;
	}

	if (SUCCEED != zbx_is_uint64(tmp, &proxy_config_revision))
	{
		*error = zbx_dsprintf(NULL, "invalid proxy configuration revision: %s", tmp);
		goto out;
	}

	if (0 != zbx_dc_register_config_session(proxy->hostid, token, proxy_config_revision, &dc_revision) ||
			0 == proxy_config_revision)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() forcing full proxy configuration sync", __func__);
		proxy_config_revision = 0;
		zbx_json_addint64(j, ZBX_PROTO_TAG_FULL_SYNC, 1);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() updating proxy configuration " ZBX_FS_UI64 "->" ZBX_FS_UI64,
				__func__, proxy_config_revision, dc_revision.config);
	}

	if (proxy_config_revision != dc_revision.config)
	{
		if (SUCCEED != (ret = proxyconfig_get_tables(proxy, proxy_config_revision, &dc_revision, j, status,
				error)))
		{
			goto out;
		}

		zbx_json_adduint64(j, ZBX_PROTO_TAG_CONFIG_REVISION, dc_revision.config);

		zabbix_log(LOG_LEVEL_TRACE, "%s() configuration: %s", __func__, j->buffer);
	}
	else
	{
		*status = ZBX_PROXYCONFIG_STATUS_EMPTY;
		ret = SUCCEED;
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: send configuration tables to the proxy from server                *
 *          (for active proxies)                                              *
 *                                                                            *
 ******************************************************************************/
void	zbx_send_proxyconfig(zbx_socket_t *sock, const struct zbx_json_parse *jp)
{
	char				*error = NULL, *buffer = NULL, *version_str = NULL;
	struct zbx_json			j;
	DC_PROXY			proxy;
	int				ret, flags = ZBX_TCP_PROTOCOL, loglevel, version_int;
	size_t				buffer_size, reserved = 0;
	zbx_proxyconfig_status_t	status;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != get_active_proxy_from_request(jp, &proxy, &error))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse proxy configuration data request from active proxy at"
				" \"%s\": %s", sock->peer, error);
		goto out;
	}

	if (SUCCEED != zbx_proxy_check_permissions(&proxy, sock, &error))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	version_str = zbx_get_proxy_protocol_version_str(jp);
	version_int = zbx_get_proxy_protocol_version_int(version_str);

	zbx_update_proxy_data(&proxy, version_str, version_int, (int)time(NULL),
				(0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0), ZBX_FLAGS_PROXY_DIFF_UPDATE_CONFIG);

	if (0 != proxy.auto_compress)
		flags |= ZBX_TCP_COMPRESS;

	if (ZBX_PROXY_VERSION_CURRENT != proxy.compatibility)
	{
		error = zbx_strdup(error, "proxy and server major versions do not match");
		(void)zbx_send_response_ext(sock, NOTSUPPORTED, error, ZABBIX_VERSION, flags, CONFIG_TIMEOUT);
		zabbix_log(LOG_LEVEL_WARNING, "configuration update is disabled for this version of proxy \"%s\" at"
				" \"%s\": %s", proxy.host, sock->peer, error);
		goto out;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	if (SUCCEED != zbx_proxyconfig_get_data(&proxy, jp, &j, &status, &error))
	{
		(void)zbx_send_response_ext(sock, FAIL, error, NULL, flags, CONFIG_TIMEOUT);
		zabbix_log(LOG_LEVEL_WARNING, "cannot collect configuration data for proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
		goto clean;
	}

	loglevel = (ZBX_PROXYCONFIG_STATUS_DATA == status ? LOG_LEVEL_WARNING : LOG_LEVEL_DEBUG);

	if (0 != proxy.auto_compress)
	{
		if (SUCCEED != zbx_compress(j.buffer, j.buffer_size, &buffer, &buffer_size))
		{
			zabbix_log(LOG_LEVEL_ERR,"cannot compress data: %s", zbx_compress_strerror());
			goto clean;
		}

		reserved = j.buffer_size;

		zbx_json_free(&j);	/* json buffer can be large, free as fast as possible */

		zabbix_log(loglevel, "sending configuration data to proxy \"%s\" at \"%s\", datalen "
				ZBX_FS_SIZE_T ", bytes " ZBX_FS_SIZE_T " with compression ratio %.1f", proxy.host,
				sock->peer, (zbx_fs_size_t)reserved, (zbx_fs_size_t)buffer_size,
				(double)reserved / (double)buffer_size);

		ret = zbx_tcp_send_ext(sock, buffer, buffer_size, reserved, (unsigned char)flags,
				CONFIG_TRAPPER_TIMEOUT);
	}
	else
	{
		zabbix_log(loglevel, "sending configuration data to proxy \"%s\" at \"%s\", datalen "
				ZBX_FS_SIZE_T, proxy.host, sock->peer, (zbx_fs_size_t)j.buffer_size);

		ret = zbx_tcp_send_ext(sock, j.buffer, strlen(j.buffer), 0, (unsigned char)flags,
				CONFIG_TRAPPER_TIMEOUT);
	}

	if (SUCCEED != ret)
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot send configuration data to proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, zbx_socket_strerror());
	}
clean:
	zbx_json_free(&j);
out:
	zbx_free(error);
	zbx_free(buffer);
	zbx_free(version_str);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
