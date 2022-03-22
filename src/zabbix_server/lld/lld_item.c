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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "lld.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxserver.h"
#include "zbxregexp.h"
#include "zbxprometheus.h"
#include "zbxvariant.h"
#include "../../libs/zbxdbcache/changeset.h"

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		valuemapid;
	zbx_uint64_t		interfaceid;
	zbx_uint64_t		master_itemid;
	char			*name;
	char			*key;
	char			*delay;
	char			*history;
	char			*trends;
	char			*trapper_hosts;
	char			*units;
	char			*formula;
	char			*logtimefmt;
	char			*params;
	char			*ipmi_sensor;
	char			*snmp_oid;
	char			*username;
	char			*password;
	char			*publickey;
	char			*privatekey;
	char			*description;
	char			*jmx_endpoint;
	char			*timeout;
	char			*url;
	char			*query_fields;
	char			*posts;
	char			*status_codes;
	char			*http_proxy;
	char			*headers;
	char			*ssl_cert_file;
	char			*ssl_key_file;
	char			*ssl_key_password;
	unsigned char		verify_peer;
	unsigned char		verify_host;
	unsigned char		follow_redirects;
	unsigned char		post_type;
	unsigned char		retrieve_mode;
	unsigned char		request_method;
	unsigned char		output_format;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		status;
	unsigned char		authtype;
	unsigned char		allow_traps;
	unsigned char		discover;
	zbx_vector_ptr_t	lld_rows;
	zbx_vector_ptr_t	preproc_ops;
	zbx_vector_ptr_t	item_params;
	zbx_vector_ptr_t	item_tags;
}
zbx_lld_item_prototype_t;

#define	ZBX_DEPENDENT_ITEM_MAX_COUNT	29999
#define	ZBX_DEPENDENT_ITEM_MAX_LEVELS	3

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		master_itemid;
	unsigned char		item_flags;
}
zbx_item_dependence_t;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		parent_itemid;
	zbx_uint64_t		master_itemid;
#define ZBX_FLAG_LLD_ITEM_UNSET				__UINT64_C(0x0000000000000000)
#define ZBX_FLAG_LLD_ITEM_DISCOVERED			__UINT64_C(0x0000000000000001)
#define ZBX_FLAG_LLD_ITEM_UPDATE_NAME			__UINT64_C(0x0000000000000002)
#define ZBX_FLAG_LLD_ITEM_UPDATE_KEY			__UINT64_C(0x0000000000000004)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TYPE			__UINT64_C(0x0000000000000008)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE		__UINT64_C(0x0000000000000010)
#define ZBX_FLAG_LLD_ITEM_UPDATE_DELAY			__UINT64_C(0x0000000000000040)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY		__UINT64_C(0x0000000000000100)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS			__UINT64_C(0x0000000000000200)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS		__UINT64_C(0x0000000000000400)
#define ZBX_FLAG_LLD_ITEM_UPDATE_UNITS			__UINT64_C(0x0000000000000800)
#define ZBX_FLAG_LLD_ITEM_UPDATE_FORMULA		__UINT64_C(0x0000000000004000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT		__UINT64_C(0x0000000000008000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID		__UINT64_C(0x0000000000010000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS			__UINT64_C(0x0000000000020000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR		__UINT64_C(0x0000000000040000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_OID		__UINT64_C(0x0000000000100000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE		__UINT64_C(0x0000000010000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_USERNAME		__UINT64_C(0x0000000020000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PASSWORD		__UINT64_C(0x0000000040000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY		__UINT64_C(0x0000000080000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY		__UINT64_C(0x0000000100000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION		__UINT64_C(0x0000000200000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_INTERFACEID		__UINT64_C(0x0000000400000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT		__UINT64_C(0x0000001000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM		__UINT64_C(0x0000002000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TIMEOUT		__UINT64_C(0x0000004000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_URL			__UINT64_C(0x0000008000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS		__UINT64_C(0x0000010000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_POSTS			__UINT64_C(0x0000020000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES		__UINT64_C(0x0000040000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS	__UINT64_C(0x0000080000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_POST_TYPE		__UINT64_C(0x0000100000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY		__UINT64_C(0x0000200000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HEADERS		__UINT64_C(0x0000400000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE		__UINT64_C(0x0000800000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD		__UINT64_C(0x0001000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT		__UINT64_C(0x0002000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE		__UINT64_C(0x0004000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE		__UINT64_C(0x0008000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD	__UINT64_C(0x0010000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER		__UINT64_C(0x0020000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST		__UINT64_C(0x0040000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS		__UINT64_C(0x0080000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE			(~ZBX_FLAG_LLD_ITEM_DISCOVERED)
	zbx_uint64_t		flags;
	char			*key_proto;
	char			*name;
	char			*name_proto;
	char			*key;
	char			*key_orig;
	char			*delay;
	char			*delay_orig;
	char			*history;
	char			*history_orig;
	char			*trends;
	char			*trends_orig;
	char			*units;
	char			*units_orig;
	char			*params;
	char			*params_orig;
	char			*username;
	char			*username_orig;
	char			*password;
	char			*password_orig;
	char			*ipmi_sensor;
	char			*ipmi_sensor_orig;
	char			*snmp_oid;
	char			*snmp_oid_orig;
	char			*description;
	char			*description_orig;
	char			*jmx_endpoint;
	char			*jmx_endpoint_orig;
	char			*timeout;
	char			*timeout_orig;
	char			*url;
	char			*url_orig;
	char			*query_fields;
	char			*query_fields_orig;
	char			*posts;
	char			*posts_orig;
	char			*status_codes;
	char			*status_codes_orig;
	char			*http_proxy;
	char			*http_proxy_orig;
	char			*headers;
	char			*headers_orig;
	char			*ssl_cert_file;
	char			*ssl_cert_file_orig;
	char			*ssl_key_file;
	char			*ssl_key_file_orig;
	char			*ssl_key_password;
	char			*ssl_key_password_orig;
	int			lastcheck;
	int			ts_delete;
	const zbx_lld_row_t	*lld_row;
	zbx_vector_ptr_t	preproc_ops;
	zbx_vector_ptr_t	dependent_items;
	zbx_vector_ptr_t	item_params;
	zbx_vector_ptr_t	item_tags;
	zbx_vector_db_tag_ptr_t	override_tags;
	unsigned char		status;
	unsigned char		type;
}
zbx_lld_item_t;

typedef struct
{
	zbx_uint64_t	item_preprocid;
	int		step;
	int		type;
	int		error_handler;
	char		*params;
	char		*error_handler_params;

#define ZBX_FLAG_LLD_ITEM_PREPROC_UNSET				__UINT64_C(0x00)
#define ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED			__UINT64_C(0x01)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE			__UINT64_C(0x02)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS			__UINT64_C(0x04)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER		__UINT64_C(0x08)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS	__UINT64_C(0x10)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP			__UINT64_C(0x20)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE				\
		(ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE |		\
		ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS |		\
		ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER |	\
		ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS |	\
		ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP			\
		)
	zbx_uint64_t	flags;
}
zbx_lld_item_preproc_t;

#define ZBX_ITEM_PARAMETER_FIELD_NAME	1
#define ZBX_ITEM_PARAMETER_FIELD_VALUE	2

typedef struct
{
	zbx_uint64_t	item_parameterid;
	char		*name;
	char		*value;

#define ZBX_FLAG_LLD_ITEM_PARAM_UNSET				__UINT64_C(0x00)
#define ZBX_FLAG_LLD_ITEM_PARAM_DISCOVERED			__UINT64_C(0x01)
#define ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_NAME			__UINT64_C(0x02)
#define ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_VALUE			__UINT64_C(0x04)
#define ZBX_FLAG_LLD_ITEM_PARAM_UPDATE				\
		(ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_NAME | ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_VALUE)
	zbx_uint64_t	flags;
}
zbx_lld_item_param_t;

#define ZBX_ITEM_TAG_FIELD_TAG		1
#define ZBX_ITEM_TAG_FIELD_VALUE	2

typedef struct
{
	zbx_uint64_t	item_tagid;
	char		*tag;
	char		*value;

#define ZBX_FLAG_LLD_ITEM_TAG_UNSET				__UINT64_C(0x00)
#define ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED			__UINT64_C(0x01)
#define ZBX_FLAG_LLD_ITEM_TAG_UPDATE_TAG			__UINT64_C(0x02)
#define ZBX_FLAG_LLD_ITEM_TAG_UPDATE_VALUE			__UINT64_C(0x04)
#define ZBX_FLAG_LLD_ITEM_TAG_UPDATE				\
		(ZBX_FLAG_LLD_ITEM_TAG_UPDATE_TAG | ZBX_FLAG_LLD_ITEM_TAG_UPDATE_VALUE)
	zbx_uint64_t	flags;
}
zbx_lld_item_tag_t;

/* item index by prototype (parent) id and lld row */
typedef struct
{
	zbx_uint64_t	parent_itemid;
	zbx_lld_row_t	*lld_row;
	zbx_lld_item_t	*item;
}
zbx_lld_item_index_t;

/* reference to an item either by its id (existing items) or structure (new items) */
typedef struct
{
	zbx_uint64_t	itemid;
	zbx_lld_item_t	*item;
}
zbx_lld_item_ref_t;

/* items index hashset support functions */
static zbx_hash_t	lld_item_index_hash_func(const void *data)
{
	zbx_lld_item_index_t	*item_index = (zbx_lld_item_index_t *)data;
	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&item_index->parent_itemid,
			sizeof(item_index->parent_itemid), ZBX_DEFAULT_HASH_SEED);
	return ZBX_DEFAULT_PTR_HASH_ALGO(&item_index->lld_row, sizeof(item_index->lld_row), hash);
}

static int	lld_item_index_compare_func(const void *d1, const void *d2)
{
	zbx_lld_item_index_t	*i1 = (zbx_lld_item_index_t *)d1;
	zbx_lld_item_index_t	*i2 = (zbx_lld_item_index_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(i1->parent_itemid, i2->parent_itemid);
	ZBX_RETURN_IF_NOT_EQUAL(i1->lld_row, i2->lld_row);

	return 0;
}

/* string pointer hashset (used to check for duplicate item keys) support functions */
static zbx_hash_t	lld_items_keys_hash_func(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC(*(char **)data);
}

static int	lld_items_keys_compare_func(const void *d1, const void *d2)
{
	return ZBX_DEFAULT_STR_COMPARE_FUNC(d1, d2);
}

static int	lld_item_preproc_sort_by_step(const void *d1, const void *d2)
{
	zbx_lld_item_preproc_t	*op1 = *(zbx_lld_item_preproc_t **)d1;
	zbx_lld_item_preproc_t	*op2 = *(zbx_lld_item_preproc_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(op1->step, op2->step);
	return 0;
}

static int	lld_item_param_sort_by_name(const void *d1, const void *d2)
{
	zbx_lld_item_param_t	*ip1 = *(zbx_lld_item_param_t **)d1;
	zbx_lld_item_param_t	*ip2 = *(zbx_lld_item_param_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(ip1->name, ip2->name);
	return 0;
}

static int	lld_item_tag_sort_by_tag(const void *d1, const void *d2)
{
	zbx_lld_item_tag_t	*it1 = *(zbx_lld_item_tag_t **)d1;
	zbx_lld_item_tag_t	*it2 = *(zbx_lld_item_tag_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(it1->tag, it2->tag);
	return 0;
}

static void	lld_item_preproc_free(zbx_lld_item_preproc_t *op)
{
	zbx_free(op->params);
	zbx_free(op->error_handler_params);
	zbx_free(op);
}

static void	lld_item_param_free(zbx_lld_item_param_t *param)
{
	zbx_free(param->name);
	zbx_free(param->value);
	zbx_free(param);
}

static void	lld_item_tag_free(zbx_lld_item_tag_t *tag)
{
	zbx_free(tag->tag);
	zbx_free(tag->value);
	zbx_free(tag);
}

static void	lld_item_prototype_free(zbx_lld_item_prototype_t *item_prototype)
{
	zbx_free(item_prototype->name);
	zbx_free(item_prototype->key);
	zbx_free(item_prototype->delay);
	zbx_free(item_prototype->history);
	zbx_free(item_prototype->trends);
	zbx_free(item_prototype->trapper_hosts);
	zbx_free(item_prototype->units);
	zbx_free(item_prototype->formula);
	zbx_free(item_prototype->logtimefmt);
	zbx_free(item_prototype->params);
	zbx_free(item_prototype->ipmi_sensor);
	zbx_free(item_prototype->snmp_oid);
	zbx_free(item_prototype->username);
	zbx_free(item_prototype->password);
	zbx_free(item_prototype->publickey);
	zbx_free(item_prototype->privatekey);
	zbx_free(item_prototype->description);
	zbx_free(item_prototype->jmx_endpoint);
	zbx_free(item_prototype->timeout);
	zbx_free(item_prototype->url);
	zbx_free(item_prototype->query_fields);
	zbx_free(item_prototype->posts);
	zbx_free(item_prototype->status_codes);
	zbx_free(item_prototype->http_proxy);
	zbx_free(item_prototype->headers);
	zbx_free(item_prototype->ssl_cert_file);
	zbx_free(item_prototype->ssl_key_file);
	zbx_free(item_prototype->ssl_key_password);

	zbx_vector_ptr_destroy(&item_prototype->lld_rows);

	zbx_vector_ptr_clear_ext(&item_prototype->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);
	zbx_vector_ptr_destroy(&item_prototype->preproc_ops);

	zbx_vector_ptr_clear_ext(&item_prototype->item_params, (zbx_clean_func_t)lld_item_param_free);
	zbx_vector_ptr_destroy(&item_prototype->item_params);

	zbx_vector_ptr_clear_ext(&item_prototype->item_tags, (zbx_clean_func_t)lld_item_tag_free);
	zbx_vector_ptr_destroy(&item_prototype->item_tags);

	zbx_free(item_prototype);
}

static void	lld_item_free(zbx_lld_item_t *item)
{
	zbx_free(item->key_proto);
	zbx_free(item->name);
	zbx_free(item->name_proto);
	zbx_free(item->key);
	zbx_free(item->key_orig);
	zbx_free(item->delay);
	zbx_free(item->delay_orig);
	zbx_free(item->history);
	zbx_free(item->history_orig);
	zbx_free(item->trends);
	zbx_free(item->trends_orig);
	zbx_free(item->units);
	zbx_free(item->units_orig);
	zbx_free(item->params);
	zbx_free(item->params_orig);
	zbx_free(item->ipmi_sensor);
	zbx_free(item->ipmi_sensor_orig);
	zbx_free(item->snmp_oid);
	zbx_free(item->snmp_oid_orig);
	zbx_free(item->username);
	zbx_free(item->username_orig);
	zbx_free(item->password);
	zbx_free(item->password_orig);
	zbx_free(item->description);
	zbx_free(item->description_orig);
	zbx_free(item->jmx_endpoint);
	zbx_free(item->jmx_endpoint_orig);
	zbx_free(item->timeout);
	zbx_free(item->timeout_orig);
	zbx_free(item->url);
	zbx_free(item->url_orig);
	zbx_free(item->query_fields);
	zbx_free(item->query_fields_orig);
	zbx_free(item->posts);
	zbx_free(item->posts_orig);
	zbx_free(item->status_codes);
	zbx_free(item->status_codes_orig);
	zbx_free(item->http_proxy);
	zbx_free(item->http_proxy_orig);
	zbx_free(item->headers);
	zbx_free(item->headers_orig);
	zbx_free(item->ssl_cert_file);
	zbx_free(item->ssl_cert_file_orig);
	zbx_free(item->ssl_key_file);
	zbx_free(item->ssl_key_file_orig);
	zbx_free(item->ssl_key_password);
	zbx_free(item->ssl_key_password_orig);

	zbx_vector_ptr_clear_ext(&item->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);
	zbx_vector_ptr_destroy(&item->preproc_ops);
	zbx_vector_ptr_clear_ext(&item->item_params, (zbx_clean_func_t)lld_item_param_free);
	zbx_vector_ptr_destroy(&item->item_params);
	zbx_vector_ptr_clear_ext(&item->item_tags, (zbx_clean_func_t)lld_item_tag_free);
	zbx_vector_ptr_destroy(&item->item_tags);
	zbx_vector_ptr_destroy(&item->dependent_items);

	zbx_vector_db_tag_ptr_destroy(&item->override_tags);

	zbx_free(item);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: retrieves existing items for the specified item prototypes        *
 *                                                                            *
 * Parameters: item_prototypes - [IN] item prototypes                         *
 *             items           - [OUT] list of items                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items)
{
	DB_RESULT			result;
	DB_ROW				row;
	zbx_lld_item_t			*item, *master;
	zbx_lld_item_preproc_t		*preproc_op;
	zbx_lld_item_param_t		*item_param;
	zbx_lld_item_tag_t		*item_tag;
	const zbx_lld_item_prototype_t	*item_prototype;
	zbx_uint64_t			db_valuemapid, db_interfaceid, itemid, master_itemid;
	zbx_vector_uint64_t		parent_itemids;
	int				i, index;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&parent_itemids);
	zbx_vector_uint64_reserve(&parent_itemids, item_prototypes->values_num);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[i];

		zbx_vector_uint64_append(&parent_itemids, item_prototype->itemid);
	}

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select id.itemid,id.key_,id.lastcheck,id.ts_delete,i.name,i.key_,i.type,i.value_type,"
				"i.delay,i.history,i.trends,i.trapper_hosts,i.units,"
				"i.formula,i.logtimefmt,i.valuemapid,i.params,i.ipmi_sensor,i.snmp_oid,"
				"i.authtype,i.username,i.password,i.publickey,i.privatekey,"
				"i.description,i.interfaceid,i.jmx_endpoint,i.master_itemid,"
				"i.timeout,i.url,i.query_fields,i.posts,i.status_codes,i.follow_redirects,i.post_type,"
				"i.http_proxy,i.headers,i.retrieve_mode,i.request_method,i.output_format,"
				"i.ssl_cert_file,i.ssl_key_file,i.ssl_key_password,i.verify_peer,i.verify_host,"
				"id.parent_itemid,i.allow_traps"
			" from item_discovery id"
				" join items i"
					" on id.itemid=i.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[45]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[index];

		item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));

		ZBX_STR2UINT64(item->itemid, row[0]);
		item->parent_itemid = itemid;
		item->key_proto = zbx_strdup(NULL, row[1]);
		item->lastcheck = atoi(row[2]);
		item->ts_delete = atoi(row[3]);
		item->name = zbx_strdup(NULL, row[4]);
		item->name_proto = NULL;
		item->key = zbx_strdup(NULL, row[5]);
		item->key_orig = NULL;
		item->flags = ZBX_FLAG_LLD_ITEM_UNSET;

		item->type = item_prototype->type;
		if ((unsigned char)atoi(row[6]) != item_prototype->type)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_TYPE;

		if ((unsigned char)atoi(row[7]) != item_prototype->value_type)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE;

		item->delay = zbx_strdup(NULL, row[8]);
		item->delay_orig = NULL;

		item->history = zbx_strdup(NULL, row[9]);
		item->history_orig = NULL;

		item->trends = zbx_strdup(NULL, row[10]);
		item->trends_orig = NULL;

		if (0 != strcmp(row[11], item_prototype->trapper_hosts))
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS;

		item->units = zbx_strdup(NULL, row[12]);
		item->units_orig = NULL;

		if (0 != strcmp(row[13], item_prototype->formula))
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_FORMULA;

		if (0 != strcmp(row[14], item_prototype->logtimefmt))
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT;

		ZBX_DBROW2UINT64(db_valuemapid, row[15]);
		if (db_valuemapid != item_prototype->valuemapid)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID;

		item->params = zbx_strdup(NULL, row[16]);
		item->params_orig = NULL;

		item->ipmi_sensor = zbx_strdup(NULL, row[17]);
		item->ipmi_sensor_orig = NULL;

		item->snmp_oid = zbx_strdup(NULL, row[18]);
		item->snmp_oid_orig = NULL;

		if ((unsigned char)atoi(row[19]) != item_prototype->authtype)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE;

		item->username = zbx_strdup(NULL, row[20]);
		item->username_orig = NULL;

		item->password = zbx_strdup(NULL, row[21]);
		item->password_orig = NULL;

		if (0 != strcmp(row[20], item_prototype->publickey))
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY;

		if (0 != strcmp(row[23], item_prototype->privatekey))
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY;

		item->description = zbx_strdup(NULL, row[24]);
		item->description_orig = NULL;

		ZBX_DBROW2UINT64(db_interfaceid, row[25]);
		if (db_interfaceid != item_prototype->interfaceid)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_INTERFACEID;

		item->jmx_endpoint = zbx_strdup(NULL, row[26]);
		item->jmx_endpoint_orig = NULL;

		ZBX_DBROW2UINT64(item->master_itemid, row[27]);

		item->timeout = zbx_strdup(NULL, row[28]);
		item->timeout_orig = NULL;

		item->url = zbx_strdup(NULL, row[29]);
		item->url_orig = NULL;

		item->query_fields = zbx_strdup(NULL, row[30]);
		item->query_fields_orig = NULL;

		item->posts = zbx_strdup(NULL, row[31]);
		item->posts_orig = NULL;

		item->status_codes = zbx_strdup(NULL, row[32]);
		item->status_codes_orig = NULL;

		if ((unsigned char)atoi(row[33]) != item_prototype->follow_redirects)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS;

		if ((unsigned char)atoi(row[34]) != item_prototype->post_type)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_POST_TYPE;

		item->http_proxy = zbx_strdup(NULL, row[35]);
		item->http_proxy_orig = NULL;

		item->headers = zbx_strdup(NULL, row[36]);
		item->headers_orig = NULL;

		if ((unsigned char)atoi(row[37]) != item_prototype->retrieve_mode)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE;

		if ((unsigned char)atoi(row[38]) != item_prototype->request_method)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD;

		if ((unsigned char)atoi(row[39]) != item_prototype->output_format)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT;

		item->ssl_cert_file = zbx_strdup(NULL, row[40]);
		item->ssl_cert_file_orig = NULL;

		item->ssl_key_file = zbx_strdup(NULL, row[41]);
		item->ssl_key_file_orig = NULL;

		item->ssl_key_password = zbx_strdup(NULL, row[42]);
		item->ssl_key_password_orig = NULL;

		if ((unsigned char)atoi(row[43]) != item_prototype->verify_peer)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER;

		if ((unsigned char)atoi(row[44]) != item_prototype->verify_host)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST;

		if ((unsigned char)atoi(row[46]) != item_prototype->allow_traps)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS;

		item->lld_row = NULL;

		zbx_vector_ptr_create(&item->preproc_ops);
		zbx_vector_ptr_create(&item->dependent_items);
		zbx_vector_ptr_create(&item->item_params);
		zbx_vector_ptr_create(&item->item_tags);
		zbx_vector_db_tag_ptr_create(&item->override_tags);

		zbx_vector_ptr_append(items, item);
	}
	DBfree_result(result);

	zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	if (0 == items->values_num)
		goto out;

	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (zbx_lld_item_t *)items->values[i];
		master_itemid = item->master_itemid;

		if (0 != master_itemid && FAIL != (index = zbx_vector_ptr_bsearch(items, &master_itemid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			/* dependent items based on prototypes should contain prototype itemid */
			master = (zbx_lld_item_t *)items->values[index];
			master_itemid = master->parent_itemid;
		}

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[index];

		if (master_itemid != item_prototype->master_itemid)
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM;

		item->master_itemid = item_prototype->master_itemid;
	}

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select ip.item_preprocid,ip.itemid,ip.step,ip.type,ip.params,ip.error_handler,"
				"ip.error_handler_params"
			" from item_discovery id"
				" join item_preproc ip"
					" on id.itemid=ip.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[1]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(items, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = (zbx_lld_item_t *)items->values[index];

		preproc_op = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
		preproc_op->flags = ZBX_FLAG_LLD_ITEM_PREPROC_UNSET;
		ZBX_STR2UINT64(preproc_op->item_preprocid, row[0]);
		preproc_op->step = atoi(row[2]);
		preproc_op->type = atoi(row[3]);
		preproc_op->params = zbx_strdup(NULL, row[4]);
		preproc_op->error_handler = atoi(row[5]);
		preproc_op->error_handler_params = zbx_strdup(NULL, row[6]);
		zbx_vector_ptr_append(&item->preproc_ops, preproc_op);
	}
	DBfree_result(result);

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select ip.item_parameterid,ip.itemid,ip.name,ip.value"
			" from item_discovery id"
				" join item_parameter ip"
					" on id.itemid=ip.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[1]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(items, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = (zbx_lld_item_t *)items->values[index];

		item_param = (zbx_lld_item_param_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_param_t));
		item_param->flags = ZBX_FLAG_LLD_ITEM_PARAM_UNSET;
		ZBX_STR2UINT64(item_param->item_parameterid, row[0]);
		item_param->name = zbx_strdup(NULL, row[2]);
		item_param->value = zbx_strdup(NULL, row[3]);
		zbx_vector_ptr_append(&item->item_params, item_param);
	}
	DBfree_result(result);

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select it.itemtagid,it.itemid,it.tag,it.value"
			" from item_discovery id"
				" join item_tag it"
					" on id.itemid=it.itemid"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "id.parent_itemid", parent_itemids.values,
			parent_itemids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[1]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(items, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = (zbx_lld_item_t *)items->values[index];

		item_tag = (zbx_lld_item_tag_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_tag_t));
		item_tag->flags = ZBX_FLAG_LLD_ITEM_TAG_UNSET;
		ZBX_STR2UINT64(item_tag->item_tagid, row[0]);
		item_tag->tag = zbx_strdup(NULL, row[2]);
		item_tag->value = zbx_strdup(NULL, row[3]);
		zbx_vector_ptr_append(&item->item_tags, item_tag);
	}
	DBfree_result(result);
out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&parent_itemids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: is_user_macro                                                    *
 *                                                                            *
 * Purpose: checks if string is user macro                                    *
 *                                                                            *
 * Parameters: str - [IN] string to validate                                  *
 *                                                                            *
 * Returns: SUCCEED - either "{$MACRO}" or "{$MACRO:"{#MACRO}"}"              *
 *          FAIL    - not user macro or contains other characters for example:*
 *                    "dummy{$MACRO}", "{$MACRO}dummy" or "{$MACRO}{$MACRO}"  *
 *                                                                            *
 ******************************************************************************/
static int	is_user_macro(const char *str)
{
	zbx_token_t	token;

	if (FAIL == zbx_token_find(str, 0, &token, ZBX_TOKEN_SEARCH_BASIC) ||
			0 == (token.type & ZBX_TOKEN_USER_MACRO) ||
			0 != token.loc.l || '\0' != str[token.loc.r + 1])
	{
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_item_param                                          *
 *                                                                            *
 ******************************************************************************/
static int	lld_validate_item_param(zbx_uint64_t itemid, int type, size_t len, char *param, char **error)
{
	if (SUCCEED != zbx_is_utf8(param))
	{
		char	*param_utf8;

		param_utf8 = zbx_strdup(NULL, param);
		zbx_replace_invalid_utf8(param_utf8);
		*error = zbx_strdcatf(*error, "Cannot %s item: parameter's %s \"%s\" has invalid UTF-8 sequence.\n",
				(0 != itemid ? "update" : "create"),
				(ZBX_ITEM_PARAMETER_FIELD_NAME != type ? "name" : "value"), param_utf8);
		zbx_free(param_utf8);
		return FAIL;
	}

	if (zbx_strlen_utf8(param) > len)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: parameter's %s \"%s\" is too long.\n",
				(0 != itemid ? "update" : "create"),
				(ZBX_ITEM_PARAMETER_FIELD_NAME != type ? "name" : "value"), param);
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_item_tag                                            *
 *                                                                            *
 ******************************************************************************/
static int	lld_validate_item_tag(zbx_uint64_t itemid, int type, char *tag, char **error)
{
	size_t	len;
	if (SUCCEED != zbx_is_utf8(tag))
	{
		char	*tag_utf8;

		tag_utf8 = zbx_strdup(NULL, tag);
		zbx_replace_invalid_utf8(tag_utf8);
		*error = zbx_strdcatf(*error, "Cannot %s item: tag's %s \"%s\" has invalid UTF-8 sequence.\n",
				(0 != itemid ? "update" : "create"),
				(ZBX_ITEM_TAG_FIELD_TAG != type ? "tag" : "value"), tag_utf8);
		zbx_free(tag_utf8);
		return FAIL;
	}

	len = zbx_strlen_utf8(tag);

	if (ITEM_TAG_FIELD_LEN < len)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: tag's %s \"%s\" is too long.\n",
				(0 != itemid ? "update" : "create"),
				(ZBX_ITEM_TAG_FIELD_TAG != type ? "tag" : "value"), tag);
		return FAIL;
	}
	else if (0 == len && ZBX_ITEM_TAG_FIELD_TAG == type)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: empty tag name.\n", (0 != itemid ? "update" : "create"));
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_item_field                                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_item_field(zbx_lld_item_t *item, char **field, char **field_orig, zbx_uint64_t flag,
		size_t field_len, char **error)
{
	if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
		return;

	/* only new items or items with changed data or item type will be validated */
	if (0 != item->itemid && 0 == (item->flags & flag) && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TYPE))
		return;

	if (SUCCEED != zbx_is_utf8(*field))
	{
		zbx_replace_invalid_utf8(*field);
		*error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" has invalid UTF-8 sequence.\n",
				(0 != item->itemid ? "update" : "create"), *field);
	}
	else if (zbx_strlen_utf8(*field) > field_len)
	{
		const char	*err_val;
		char		key_short[VALUE_ERRMSG_MAX * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];

		if (0 != (flag & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
			err_val = zbx_truncate_itemkey(*field, VALUE_ERRMSG_MAX, key_short, sizeof(key_short));
		else
			err_val = zbx_truncate_value(*field, VALUE_ERRMSG_MAX, key_short, sizeof(key_short));

		*error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" is too long.\n",
				(0 != item->itemid ? "update" : "create"), err_val);
	}
	else
	{
		int	value;
		char	*errmsg = NULL;

		switch (flag)
		{
			case ZBX_FLAG_LLD_ITEM_UPDATE_NAME:
				if ('\0' != **field)
					return;

				*error = zbx_strdcatf(*error, "Cannot %s item: name is empty.\n",
						(0 != item->itemid ? "update" : "create"));
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_DELAY:
				switch (item->type)
				{
					case ITEM_TYPE_TRAPPER:
					case ITEM_TYPE_SNMPTRAP:
					case ITEM_TYPE_DEPENDENT:
						return;
					case ITEM_TYPE_ZABBIX_ACTIVE:
						if (0 == strncmp(item->key, "mqtt.get[", ZBX_CONST_STRLEN("mqtt.get[")))
							return;
				}

				if (SUCCEED == zbx_validate_interval(*field, &errmsg))
					return;

				*error = zbx_strdcatf(*error, "Cannot %s item: %s\n",
						(0 != item->itemid ? "update" : "create"), errmsg);
				zbx_free(errmsg);

				/* delay alone cannot be rolled back as it depends on item type, revert all updates */
				if (0 != item->itemid)
				{
					item->flags &= ZBX_FLAG_LLD_ITEM_DISCOVERED;
					return;
				}
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
						(ZBX_HK_HISTORY_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = zbx_strdcatf(*error, "Cannot %s item: invalid history storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
						(ZBX_HK_TRENDS_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = zbx_strdcatf(*error, "Cannot %s item: invalid trends storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			default:
				return;
		}
	}

	if (0 != item->itemid)
		lld_field_str_rollback(field, field_orig, &item->flags, flag);
	else
		item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependence_add                                          *
 *                                                                            *
 * Purpose: add a new dependency                                              *
 *                                                                            *
 * Parameters: item_dependencies - [IN\OUT] list of dependencies              *
 *             itemid            - [IN] item id                               *
 *             master_itemid     - [IN] master item id                        *
 *             item_flags        - [IN] item flags (ZBX_FLAG_DISCOVERY_*)     *
 *                                                                            *
 * Returns: item dependence                                                   *
 *                                                                            *
 * Comments: Memory is allocated to store item dependence. This memory must   *
 *           be freed by the caller.                                          *
 *                                                                            *
 ******************************************************************************/
static zbx_item_dependence_t	*lld_item_dependence_add(zbx_vector_ptr_t *item_dependencies, zbx_uint64_t itemid,
		zbx_uint64_t master_itemid, unsigned int item_flags)
{
	zbx_item_dependence_t	*dependence = (zbx_item_dependence_t *)zbx_malloc(NULL, sizeof(zbx_item_dependence_t));

	dependence->itemid = itemid;
	dependence->master_itemid = master_itemid;
	dependence->item_flags = item_flags;

	zbx_vector_ptr_append(item_dependencies, dependence);

	return dependence;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_get                                        *
 *                                                                            *
 * Purpose: recursively get dependencies with dependent items taking into     *
 *          account item prototypes                                           *
 *                                                                            *
 * Parameters: item_prototypes   - [IN] item prototypes                       *
 *             item_dependencies - [OUT] list of dependencies                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_dependencies_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *item_dependencies)
{
#define NEXT_CHECK_BY_ITEM_IDS		0
#define NEXT_CHECK_BY_MASTERITEM_IDS	1

	int			i, check_type;
	zbx_vector_uint64_t	processed_masterid, processed_itemid, next_check_itemids, next_check_masterids,
				*check_ids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&processed_masterid);
	zbx_vector_uint64_create(&processed_itemid);
	zbx_vector_uint64_create(&next_check_itemids);
	zbx_vector_uint64_create(&next_check_masterids);

	/* collect the item id of prototypes for searching dependencies into database */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		const zbx_lld_item_prototype_t	*item_prototype;

		item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[i];

		if (0 != item_prototype->master_itemid)
		{
			lld_item_dependence_add(item_dependencies, item_prototype->itemid,
					item_prototype->master_itemid, ZBX_FLAG_DISCOVERY_PROTOTYPE);
			zbx_vector_uint64_append(&next_check_itemids, item_prototype->master_itemid);
			zbx_vector_uint64_append(&next_check_masterids, item_prototype->master_itemid);
		}
	}

	/* search dependency in two directions (masteritem_id->itemid and itemid->masteritem_id) */
	while (0 < next_check_itemids.values_num || 0 < next_check_masterids.values_num)
	{
		if (0 < next_check_itemids.values_num)
		{
			check_type = NEXT_CHECK_BY_ITEM_IDS;
			check_ids = &next_check_itemids;
		}
		else
		{
			check_type = NEXT_CHECK_BY_MASTERITEM_IDS;
			check_ids = &next_check_masterids;
		}

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,master_itemid,flags from items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset,
				NEXT_CHECK_BY_ITEM_IDS == check_type ? "itemid" : "master_itemid",
				check_ids->values, check_ids->values_num);

		if (NEXT_CHECK_BY_ITEM_IDS == check_type)
			zbx_vector_uint64_append_array(&processed_itemid, check_ids->values, check_ids->values_num);
		else
			zbx_vector_uint64_append_array(&processed_masterid, check_ids->values, check_ids->values_num);

		zbx_vector_uint64_clear(check_ids);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			int			dependence_found = 0;
			zbx_item_dependence_t	*dependence = NULL;
			zbx_uint64_t		itemid, master_itemid;
			unsigned int		item_flags;

			ZBX_STR2UINT64(itemid, row[0]);
			ZBX_DBROW2UINT64(master_itemid, row[1]);
			ZBX_STR2UCHAR(item_flags, row[2]);

			for (i = 0; i < item_dependencies->values_num; i++)
			{
				dependence = (zbx_item_dependence_t *)item_dependencies->values[i];

				if (dependence->itemid == itemid && dependence->master_itemid == master_itemid)
				{
					dependence_found = 1;
					break;
				}
			}

			if (0 == dependence_found)
			{
				dependence = lld_item_dependence_add(item_dependencies, itemid, master_itemid,
						item_flags);
			}

			if (FAIL == zbx_vector_uint64_search(&processed_masterid, dependence->itemid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				zbx_vector_uint64_append(&next_check_masterids, dependence->itemid);
			}

			if (NEXT_CHECK_BY_ITEM_IDS != check_type || 0 == dependence->master_itemid)
				continue;

			if (FAIL == zbx_vector_uint64_search(&processed_itemid, dependence->master_itemid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				zbx_vector_uint64_append(&next_check_itemids, dependence->master_itemid);
			}
		}
		DBfree_result(result);
	}
	zbx_free(sql);

	zbx_vector_uint64_destroy(&processed_masterid);
	zbx_vector_uint64_destroy(&processed_itemid);
	zbx_vector_uint64_destroy(&next_check_itemids);
	zbx_vector_uint64_destroy(&next_check_masterids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

#undef NEXT_CHECK_BY_ITEM_IDS
#undef NEXT_CHECK_BY_MASTERITEM_IDS
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_count                                      *
 *                                                                            *
 * Purpose: recursively count the number of dependencies                      *
 *                                                                            *
 * Parameters: itemid            - [IN] item ID to be checked                 *
 *             dependencies      - [IN] item dependencies                     *
 *             processed_itemids - [IN\OUT] list of checked item ids          *
 *             dependencies_num  - [IN\OUT] number of dependencies            *
 *             depth_level       - [IN\OUT] depth level                       *
 *                                                                            *
 * Returns: SUCCEED - the number of dependencies was successfully counted     *
 *          FAIL    - the limit of dependencies is reached                    *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_dependencies_count(const zbx_uint64_t itemid, const zbx_vector_ptr_t *dependencies,
		zbx_vector_uint64_t *processed_itemids, int *dependencies_num, unsigned char *depth_level)
{
	int	ret = FAIL, i, curr_depth_calculated = 0;

	for (i = 0; i < dependencies->values_num; i++)
	{
		zbx_item_dependence_t	*dep = (zbx_item_dependence_t *)dependencies->values[i];

		/* check if item is a master for someone else */
		if (dep->master_itemid != itemid)
			continue;

		/* check the limit of dependent items */
		if (0 == (dep->item_flags & ZBX_FLAG_DISCOVERY_PROTOTYPE) &&
				ZBX_DEPENDENT_ITEM_MAX_COUNT <= ++(*dependencies_num))
		{
			goto out;
		}

		/* check the depth level */
		if (0 == curr_depth_calculated)
		{
			curr_depth_calculated = 1;

			if (ZBX_DEPENDENT_ITEM_MAX_LEVELS < ++(*depth_level))
			{
				/* API shouldn't allow to create dependencies deeper */
				THIS_SHOULD_NEVER_HAPPEN;
				goto out;
			}
		}

		/* check if item was calculated in previous iterations */
		if (FAIL != zbx_vector_uint64_search(processed_itemids, dep->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;

		if (SUCCEED != lld_item_dependencies_count(dep->itemid, dependencies, processed_itemids,
				dependencies_num, depth_level))
		{
			goto out;
		}

		/* add counted item id */
		zbx_vector_uint64_append(processed_itemids, dep->itemid);
	}

	ret = SUCCEED;
out:
	if (1 == curr_depth_calculated)
		(*depth_level)--;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_check                                      *
 *                                                                            *
 * Purpose: check the limits of dependent items                               *
 *                                                                            *
 * Parameters: item             - [IN] discovered item                        *
 *             item_prototype   - [IN] item prototype to be checked for limit *
 *             dependencies     - [IN] item dependencies                      *
 *                                                                            *
 * Returns: SUCCEED - the check was successful                                *
 *          FAIL    - the limit of dependencies is exceeded                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_item_dependencies_check(const zbx_lld_item_t *item, const zbx_lld_item_prototype_t *item_prototype,
		zbx_vector_ptr_t *dependencies)
{
	zbx_item_dependence_t	*dependence = NULL, *top_dependence = NULL, *tmp_dep;
	int 			ret = FAIL, i, dependence_num = 0, item_in_deps = FAIL;
	unsigned char		depth_level = 0;
	zbx_vector_uint64_t	processed_itemids;

	/* find the dependency of the item by item id */
	for (i = 0; i < dependencies->values_num; i++)
	{
		dependence = (zbx_item_dependence_t *)dependencies->values[i];
		if (item_prototype->itemid == dependence->itemid)
			break;
	}

	if (NULL == dependence || i == dependencies->values_num)
		return SUCCEED;

	/* find the top dependency that doesn't have a master item id */
	while (NULL == top_dependence)
	{
		for (i = 0; i < dependencies->values_num; i++)
		{
			tmp_dep = (zbx_item_dependence_t *)dependencies->values[i];

			if (item->itemid == tmp_dep->itemid)
				item_in_deps = SUCCEED;

			if (dependence->master_itemid == tmp_dep->itemid)
			{
				dependence = tmp_dep;
				break;
			}
		}

		if (0 == dependence->master_itemid)
		{
			top_dependence = dependence;
		}
		else if (ZBX_DEPENDENT_ITEM_MAX_LEVELS < ++depth_level)
		{
			/* API shouldn't allow to create dependencies deeper than ZBX_DEPENDENT_ITEM_MAX_LEVELS */
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
		}
	}

	depth_level = 0;
	zbx_vector_uint64_create(&processed_itemids);

	ret = lld_item_dependencies_count(top_dependence->itemid, dependencies, &processed_itemids, &dependence_num,
			&depth_level);

	zbx_vector_uint64_destroy(&processed_itemids);

	if (SUCCEED == ret && SUCCEED != item_in_deps
			&& 0 == (top_dependence->item_flags & ZBX_FLAG_DISCOVERY_PROTOTYPE))
	{
		lld_item_dependence_add(dependencies, item_prototype->itemid, item->master_itemid,
				ZBX_FLAG_DISCOVERY_CREATED);
	}

out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_step_validate                                  *
 *                                                                            *
 * Purpose: validation of a item preprocessing step expressions for discovery *
 *          process                                                           *
 *                                                                            *
 * Parameters: pp       - [IN] the item preprocessing step                    *
 *             itemid   - [IN] item ID for logging                            *
 *             error    - [IN/OUT] the lld error message                      *
 *                                                                            *
 * Return value: SUCCEED - if preprocessing step is valid                     *
 *               FAIL    - if preprocessing step is not valid                 *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_preproc_step_validate(const zbx_lld_item_preproc_t * pp, zbx_uint64_t itemid, char ** error)
{
	int		ret = SUCCEED;
	zbx_token_t	token;
	char		err[MAX_STRING_LEN], *errmsg = NULL;
	char		param1[ITEM_PREPROC_PARAMS_LEN * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1], *param2;
	const char	*regexp_err = NULL;
	zbx_uint64_t	value_ui64;
	zbx_jsonpath_t	jsonpath;

	*err = '\0';

	if (0 == (pp->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE)
			|| (SUCCEED == zbx_token_find(pp->params, 0, &token, ZBX_TOKEN_SEARCH_BASIC)
			&& 0 != (token.type & ZBX_TOKEN_USER_MACRO)))
	{
		return SUCCEED;
	}

	switch (pp->type)
	{
		case ZBX_PREPROC_REGSUB:
			/* break; is not missing here */
		case ZBX_PREPROC_ERROR_FIELD_REGEX:
			zbx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				zbx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}

			*param2 = '\0';

			if (FAIL == (ret = zbx_regexp_compile(param1, NULL, &regexp_err)))
			{
				zbx_strlcpy(err, regexp_err, sizeof(err));
			}
			break;
		case ZBX_PREPROC_JSONPATH:
			/* break; is not missing here */
		case ZBX_PREPROC_ERROR_FIELD_JSON:
			if (FAIL == (ret = zbx_jsonpath_compile(pp->params, &jsonpath)))
				zbx_strlcpy(err, zbx_json_strerror(), sizeof(err));
			else
				zbx_jsonpath_clear(&jsonpath);
			break;
		case ZBX_PREPROC_XPATH:
			/* break; is not missing here */
		case ZBX_PREPROC_ERROR_FIELD_XML:
			ret = xml_xpath_check(pp->params, err, sizeof(err));
			break;
		case ZBX_PREPROC_MULTIPLIER:
			if (FAIL == (ret = is_double(pp->params, NULL)))
				zbx_snprintf(err, sizeof(err), "value is not numeric or out of range: %s", pp->params);
			break;
		case ZBX_PREPROC_VALIDATE_RANGE:
			zbx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				zbx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}
			*param2++ = '\0';
			zbx_lrtrim(param1, " ");
			zbx_lrtrim(param2, " ");

			if ('\0' != *param1 && FAIL == (ret = is_double(param1, NULL)))
			{
				zbx_snprintf(err, sizeof(err), "first parameter is not numeric or out of range: %s",
						param1);
			}
			else if ('\0' != *param2 && FAIL == (ret = is_double(param2, NULL)))
			{
				zbx_snprintf(err, sizeof(err), "second parameter is not numeric or out of range: %s",
						param2);
			}
			else if ('\0' == *param1 && '\0' == *param2)
			{
				zbx_snprintf(err, sizeof(err), "at least one parameter must be defined: %s", pp->params);
				ret = FAIL;
			}
			else if ('\0' != *param1 && '\0' != *param2)
			{
				/* use variants to handle uint64 and double values */
				zbx_variant_t	min, max;

				zbx_variant_set_numeric(&min, param1);
				zbx_variant_set_numeric(&max, param2);

				if (0 < zbx_variant_compare(&min, &max))
				{
					zbx_snprintf(err, sizeof(err), "first parameter '%s' must be less than second "
							"'%s'", param1, param2);
					ret = FAIL;
				}

				zbx_variant_clear(&min);
				zbx_variant_clear(&max);
			}

			break;
		case ZBX_PREPROC_VALIDATE_REGEX:
			/* break; is not missing here */
		case ZBX_PREPROC_VALIDATE_NOT_REGEX:
			if (FAIL == (ret = zbx_regexp_compile(pp->params, NULL, &regexp_err)))
				zbx_strlcpy(err, regexp_err, sizeof(err));
			break;
		case ZBX_PREPROC_THROTTLE_TIMED_VALUE:
			if (SUCCEED != str2uint64(pp->params, "smhdw", &value_ui64) || 0 == value_ui64)
			{
				zbx_snprintf(err, sizeof(err), "invalid time interval: %s", pp->params);
				ret = FAIL;
			}
			break;
		case ZBX_PREPROC_PROMETHEUS_PATTERN:
			zbx_strlcpy(param1, pp->params, sizeof(param1));
			if (NULL == (param2 = strchr(param1, '\n')))
			{
				zbx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}
			*param2++ = '\0';

			if (FAIL == zbx_prometheus_validate_filter(param1, &errmsg))
			{
				zbx_snprintf(err, sizeof(err), "invalid pattern: %s", param1);
				zbx_free(errmsg);
				ret = FAIL;
				break;
			}

			if (FAIL == zbx_prometheus_validate_label(param2))
			{
				zbx_snprintf(err, sizeof(err), "invalid label name: %s", param2);
				ret = FAIL;
				break;
			}

			break;
		case ZBX_PREPROC_PROMETHEUS_TO_JSON:
			if (FAIL == zbx_prometheus_validate_filter(pp->params, &errmsg))
			{
				zbx_snprintf(err, sizeof(err), "invalid pattern: %s", pp->params);
				zbx_free(errmsg);
				ret = FAIL;
				break;
			}
			break;
		case ZBX_PREPROC_STR_REPLACE:
			if ('\n' == *pp->params)
			{
				zbx_snprintf(err, sizeof(err), "first parameter is expected");
				ret = FAIL;
			}
			break;
	}

	if (SUCCEED != ret)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: invalid value for preprocessing step #%d: %s.\n",
				(0 != itemid ? "update" : "create"), pp->step, err);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_validate                                               *
 *                                                                            *
 * Parameters: hostid            - [IN] host id                               *
 *             items             - [IN] list of items                         *
 *             item_prototypes   - [IN] the item prototypes                   *
 *             item_dependencies - [IN] list of dependencies                  *
 *             error             - [IN/OUT] the lld error message             *
 *                                                                            *
 *****************************************************************************/
static void	lld_items_validate(zbx_uint64_t hostid, zbx_vector_ptr_t *items, zbx_vector_ptr_t *item_prototypes,
		zbx_vector_ptr_t *item_dependencies, char **error)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			i, j;
	zbx_lld_item_t		*item;
	zbx_vector_uint64_t	itemids;
	zbx_vector_str_t	keys;
	zbx_hashset_t		items_keys;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&itemids);
	zbx_vector_str_create(&keys);		/* list of item keys */

	/* check an item name validity */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		lld_validate_item_field(item, &item->name, &item->name_proto,
				ZBX_FLAG_LLD_ITEM_UPDATE_NAME, ITEM_NAME_LEN, error);
		lld_validate_item_field(item, &item->key, &item->key_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_KEY, ITEM_KEY_LEN, error);
		lld_validate_item_field(item, &item->delay, &item->delay_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_DELAY, ITEM_DELAY_LEN, error);
		lld_validate_item_field(item, &item->history, &item->history_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY, ITEM_HISTORY_LEN, error);
		lld_validate_item_field(item, &item->trends, &item->trends_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS, ITEM_TRENDS_LEN, error);
		lld_validate_item_field(item, &item->units, &item->units_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_UNITS, ITEM_UNITS_LEN, error);
		lld_validate_item_field(item, &item->params, &item->params_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS, ITEM_PARAM_LEN, error);
		lld_validate_item_field(item, &item->ipmi_sensor, &item->ipmi_sensor_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR, ITEM_IPMI_SENSOR_LEN, error);
		lld_validate_item_field(item, &item->snmp_oid, &item->snmp_oid_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_OID, ITEM_SNMP_OID_LEN, error);
		lld_validate_item_field(item, &item->username, &item->username_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_USERNAME, ITEM_USERNAME_LEN, error);
		lld_validate_item_field(item, &item->password, &item->password_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_PASSWORD, ITEM_PASSWORD_LEN, error);
		lld_validate_item_field(item, &item->description, &item->description_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION, ITEM_DESCRIPTION_LEN, error);
		lld_validate_item_field(item, &item->jmx_endpoint, &item->jmx_endpoint_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT, ITEM_JMX_ENDPOINT_LEN, error);
		lld_validate_item_field(item, &item->timeout, &item->timeout_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_TIMEOUT, ITEM_TIMEOUT_LEN, error);
		lld_validate_item_field(item, &item->url, &item->url_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_URL, ITEM_URL_LEN, error);
		lld_validate_item_field(item, &item->query_fields, &item->query_fields_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS, ITEM_QUERY_FIELDS_LEN, error);
		lld_validate_item_field(item, &item->posts, &item->posts_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_POSTS, ITEM_POSTS_LEN, error);
		lld_validate_item_field(item, &item->status_codes, &item->status_codes_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES, ITEM_STATUS_CODES_LEN, error);
		lld_validate_item_field(item, &item->http_proxy, &item->http_proxy_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY, ITEM_HTTP_PROXY_LEN, error);
		lld_validate_item_field(item, &item->headers, &item->headers_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_HEADERS, ITEM_HEADERS_LEN, error);
		lld_validate_item_field(item, &item->ssl_cert_file, &item->ssl_cert_file_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE, ITEM_SSL_CERT_FILE_LEN, error);
		lld_validate_item_field(item, &item->ssl_key_file, &item->ssl_key_file_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE, ITEM_SSL_KEY_FILE_LEN, error);
		lld_validate_item_field(item, &item->ssl_key_password, &item->ssl_key_password_orig,
				ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD, ITEM_SSL_KEY_PASSWORD_LEN, error);
	}

	/* check duplicated item keys */

	zbx_hashset_create(&items_keys, items->values_num, lld_items_keys_hash_func, lld_items_keys_compare_func);

	/* add 'good' (existing, discovered and not updated) keys to the hashset */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* skip new or updated item keys */
		if (0 == item->itemid || 0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		zbx_hashset_insert(&items_keys, &item->key, sizeof(char *));
	}

	/* check new and updated keys for duplicated keys in discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* only new items or items with changed key will be validated */
		if (0 != item->itemid && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		if (NULL != zbx_hashset_search(&items_keys, &item->key))
		{
			char key_short[VALUE_ERRMSG_MAX * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];

			*error = zbx_strdcatf(*error, "Cannot %s item: item with the same key \"%s\" already exists.\n",
						(0 != item->itemid ? "update" : "create"),
						zbx_truncate_itemkey(item->key, VALUE_ERRMSG_MAX,
						key_short, sizeof(key_short)));

			if (0 != item->itemid)
			{
				lld_field_str_rollback(&item->key, &item->key_orig, &item->flags,
						ZBX_FLAG_LLD_ITEM_UPDATE_KEY);
			}
			else
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
		}
		else
			zbx_hashset_insert(&items_keys, &item->key, sizeof(char *));
	}

	zbx_hashset_destroy(&items_keys);

	/* check item parameters for new and updated discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_params.values_num; j++)
		{
			zbx_lld_item_param_t	*item_param = (zbx_lld_item_param_t *)item->item_params.values[j];

			if (SUCCEED != lld_validate_item_param(item->itemid, ZBX_ITEM_PARAMETER_FIELD_NAME,
					ITEM_PARAMETER_NAME_LEN, item_param->name, error) ||
					SUCCEED != lld_validate_item_param(item->itemid, ZBX_ITEM_PARAMETER_FIELD_VALUE,
					ITEM_PARAMETER_VALUE_LEN, item_param->value, error))
			{
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}
		}
	}

	/* check item tags for new and updated discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_tags.values_num; j++)
		{
			zbx_lld_item_tag_t	*item_tag = (zbx_lld_item_tag_t *)item->item_tags.values[j], *tag_dup;
			int			k;

			if (SUCCEED != lld_validate_item_tag(item->itemid, ZBX_ITEM_TAG_FIELD_TAG, item_tag->tag,
					error) || SUCCEED != lld_validate_item_tag(item->itemid,
					ZBX_ITEM_TAG_FIELD_VALUE, item_tag->value, error))
			{
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}

			if (0 == (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED))
				continue;

			/* check for duplicated tag */
			for (k = 0; k < j; k++)
			{
				tag_dup = (zbx_lld_item_tag_t *)item->item_tags.values[k];

				if (0 == strcmp(item_tag->tag, tag_dup->tag) &&
						0 == strcmp(item_tag->value, tag_dup->value))
				{
					item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
					*error = zbx_strdcatf(*error, "Cannot create item tag: tag \"%s\","
						"\"%s\" already exists.\n", item_tag->tag, item_tag->value);
					break;
				}
			}

			if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
				break;
		}
	}

	/* check preprocessing steps for new and updated discovered items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			if (SUCCEED != lld_items_preproc_step_validate(item->preproc_ops.values[j], item->itemid,
					error))
			{
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}
		}
	}

	/* check duplicated keys in DB */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (0 != item->itemid)
			zbx_vector_uint64_append(&itemids, item->itemid);

		if (0 != item->itemid && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
			continue;

		zbx_vector_str_append(&keys, item->key);
	}

	if (0 != keys.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 256, sql_offset = 0;

		sql = (char *)zbx_malloc(sql, sql_alloc);

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select key_"
				" from items"
				" where hostid=" ZBX_FS_UI64
					" and",
				hostid);
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "key_",
				(const char **)keys.values, keys.values_num);

		if (0 != itemids.values_num)
		{
			zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
					itemids.values, itemids.values_num);
		}

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			for (i = 0; i < items->values_num; i++)
			{
				item = (zbx_lld_item_t *)items->values[i];

				if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
					continue;

				if (0 == strcmp(item->key, row[0]))
				{
					char key_short[VALUE_ERRMSG_MAX * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];

					*error = zbx_strdcatf(*error, "Cannot %s item:"
							" item with the same key \"%s\" already exists.\n",
							(0 != item->itemid ? "update" : "create"),
							zbx_truncate_itemkey(item->key, VALUE_ERRMSG_MAX,
							key_short, sizeof(key_short)));

					if (0 != item->itemid)
					{
						lld_field_str_rollback(&item->key, &item->key_orig, &item->flags,
								ZBX_FLAG_LLD_ITEM_UPDATE_KEY);
					}
					else
						item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;

					continue;
				}
			}
		}
		DBfree_result(result);

		zbx_free(sql);
	}

	zbx_vector_str_destroy(&keys);
	zbx_vector_uint64_destroy(&itemids);

	/* check limit of dependent items in the dependency tree */
	if (0 != item_dependencies->values_num)
	{
		for (i = 0; i < items->values_num; i++)
		{
			int				index;
			const zbx_lld_item_prototype_t	*item_prototype;

			item = (zbx_lld_item_t *)items->values[i];

			if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED) || 0 == item->master_itemid
					|| (0 != item->itemid && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TYPE)))
			{
				continue;
			}

			if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

			if (SUCCEED != lld_item_dependencies_check(item, item_prototype, item_dependencies))
			{
				*error = zbx_strdcatf(*error,
						"Cannot create item \"%s\": maximum dependent item count reached.\n",
						item->key);

				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
			}
		}
	}

	/* check for broken dependent items */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
		{
			for (j = 0; j < item->dependent_items.values_num; j++)
			{
				zbx_lld_item_t	*dependent;

				dependent = (zbx_lld_item_t *)item->dependent_items.values[j];
				dependent->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
			}
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_formula_macros                                        *
 *                                                                            *
 * Purpose: substitutes lld macros in calculated item formula expression      *
 *                                                                            *
 * Parameters: data            - [IN/OUT] the expression                      *
 *             jp_row          - [IN] the lld data row                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             error           - [IN] pointer to string for reporting errors  *
 *             max_error_len   - [IN] size of 'error' string                  *
 *                                                                            *
 ******************************************************************************/
static int	substitute_formula_macros(char **data, const struct zbx_json_parse *jp_row,
		const zbx_vector_ptr_t *lld_macro_paths, char **error)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() formula:%s", __func__, *data);

	ret = zbx_substitute_expression_lld_macros(data, ZBX_EVAL_CALC_EXPRESSION_LLD, jp_row, lld_macro_paths, error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() formula:%s", __func__, *data);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_make                                                    *
 *                                                                            *
 * Purpose: creates a new item based on item prototype and lld data row       *
 *                                                                            *
 * Parameters: item_prototype  - [IN] the item prototype                      *
 *             lld_row         - [IN] the lld row                             *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *                                                                            *
 * Returns: The created item or NULL if cannot create new item from prototype *
 *                                                                            *
 ******************************************************************************/
static zbx_lld_item_t	*lld_item_make(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row,
		const zbx_vector_ptr_t *lld_macro_paths, char **error)
{
	zbx_lld_item_t			*item;
	const struct zbx_json_parse	*jp_row = (struct zbx_json_parse *)&lld_row->jp_row;
	char				err[MAX_STRING_LEN];
	int				ret;
	const char			*delay, *history, *trends;
	unsigned char			discover;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));

	item->itemid = 0;
	item->parent_itemid = item_prototype->itemid;
	item->lastcheck = 0;
	item->ts_delete = 0;
	item->type = item_prototype->type;
	item->key_proto = NULL;
	item->master_itemid = item_prototype->master_itemid;

	item->name = zbx_strdup(NULL, item_prototype->name);
	item->name_proto = NULL;
	substitute_lld_macros(&item->name, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->name, ZBX_WHITESPACE);

	delay = item_prototype->delay;
	history = item_prototype->history;
	trends = item_prototype->trends;
	item->status = item_prototype->status;
	discover = item_prototype->discover;

	zbx_vector_db_tag_ptr_create(&item->override_tags);

	lld_override_item(&lld_row->overrides, item->name, &delay, &history, &trends, &item->override_tags,
			&item->status, &discover);

	item->key = zbx_strdup(NULL, item_prototype->key);
	item->key_orig = NULL;

	if (FAIL == (ret = substitute_key_macros(&item->key, NULL, NULL, jp_row, lld_macro_paths, MACRO_TYPE_ITEM_KEY,
			err, sizeof(err))))
	{
		*error = zbx_strdcatf(*error, "Cannot create item, error in item key parameters %s.\n", err);
	}

	item->delay = zbx_strdup(NULL, delay);
	item->delay_orig = NULL;
	substitute_lld_macros(&item->delay, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->delay, ZBX_WHITESPACE);

	item->history = zbx_strdup(NULL, history);
	item->history_orig = NULL;
	substitute_lld_macros(&item->history, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->history, ZBX_WHITESPACE);

	item->trends = zbx_strdup(NULL, trends);
	item->trends_orig = NULL;
	substitute_lld_macros(&item->trends, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->trends, ZBX_WHITESPACE);

	item->units = zbx_strdup(NULL, item_prototype->units);
	item->units_orig = NULL;
	substitute_lld_macros(&item->units, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->units, ZBX_WHITESPACE);

	item->params = zbx_strdup(NULL, item_prototype->params);
	item->params_orig = NULL;

	if (ITEM_TYPE_CALCULATED == item_prototype->type)
	{
		char	*errmsg = NULL;
		if (SUCCEED == ret && FAIL == (ret = substitute_formula_macros(&item->params, jp_row, lld_macro_paths,
				&errmsg)))
		{
			*error = zbx_strdcatf(*error, "Cannot create item, error in formula: %s.\n", errmsg);
			zbx_free(errmsg);
		}
	}
	else
		substitute_lld_macros(&item->params, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

	zbx_lrtrim(item->params, ZBX_WHITESPACE);

	item->ipmi_sensor = zbx_strdup(NULL, item_prototype->ipmi_sensor);
	item->ipmi_sensor_orig = NULL;
	substitute_lld_macros(&item->ipmi_sensor, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->snmp_oid = zbx_strdup(NULL, item_prototype->snmp_oid);
	item->snmp_oid_orig = NULL;

	if (SUCCEED == ret && ITEM_TYPE_SNMP == item_prototype->type &&
			FAIL == (ret = substitute_key_macros(&item->snmp_oid, NULL, NULL, jp_row, lld_macro_paths,
			MACRO_TYPE_SNMP_OID, err, sizeof(err))))
	{
		*error = zbx_strdcatf(*error, "Cannot create item, error in SNMP OID key parameters: %s.\n", err);
	}

	zbx_lrtrim(item->snmp_oid, ZBX_WHITESPACE);

	item->username = zbx_strdup(NULL, item_prototype->username);
	item->username_orig = NULL;
	substitute_lld_macros(&item->username, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->username, ZBX_WHITESPACE); is not missing here */

	item->password = zbx_strdup(NULL, item_prototype->password);
	item->password_orig = NULL;
	substitute_lld_macros(&item->password, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->password, ZBX_WHITESPACE); is not missing here */

	item->description = zbx_strdup(NULL, item_prototype->description);
	item->description_orig = NULL;
	substitute_lld_macros(&item->description, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->description, ZBX_WHITESPACE);

	item->jmx_endpoint = zbx_strdup(NULL, item_prototype->jmx_endpoint);
	item->jmx_endpoint_orig = NULL;
	substitute_lld_macros(&item->jmx_endpoint, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->timeout = zbx_strdup(NULL, item_prototype->timeout);
	item->timeout_orig = NULL;
	substitute_lld_macros(&item->timeout, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->timeout, ZBX_WHITESPACE);

	item->url = zbx_strdup(NULL, item_prototype->url);
	item->url_orig = NULL;
	substitute_lld_macros(&item->url, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->url, ZBX_WHITESPACE);

	item->query_fields = zbx_strdup(NULL, item_prototype->query_fields);
	item->query_fields_orig = NULL;

	if (SUCCEED == ret && FAIL == (ret = substitute_macros_in_json_pairs(&item->query_fields, jp_row,
			lld_macro_paths, err, sizeof(err))))
	{
		*error = zbx_strdcatf(*error, "Cannot create item, error in JSON: %s.\n", err);
	}

	item->posts = zbx_strdup(NULL, item_prototype->posts);
	item->posts_orig = NULL;

	switch (item_prototype->post_type)
	{
		case ZBX_POSTTYPE_JSON:
			substitute_lld_macros(&item->posts, jp_row, lld_macro_paths, ZBX_MACRO_JSON, NULL, 0);
			break;
		case ZBX_POSTTYPE_XML:
			if (SUCCEED == ret && FAIL == (ret = substitute_macros_xml(&item->posts, NULL, jp_row,
					lld_macro_paths, err, sizeof(err))))
			{
				zbx_lrtrim(err, ZBX_WHITESPACE);
				*error = zbx_strdcatf(*error, "Cannot create item, error in XML: %s.\n", err);
			}
			break;
		default:
			substitute_lld_macros(&item->posts, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
			/* zbx_lrtrim(item->posts, ZBX_WHITESPACE); is not missing here */
			break;
	}

	item->status_codes = zbx_strdup(NULL, item_prototype->status_codes);
	item->status_codes_orig = NULL;
	substitute_lld_macros(&item->status_codes, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->status_codes, ZBX_WHITESPACE);

	item->http_proxy = zbx_strdup(NULL, item_prototype->http_proxy);
	item->http_proxy_orig = NULL;
	substitute_lld_macros(&item->http_proxy, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->http_proxy, ZBX_WHITESPACE);

	item->headers = zbx_strdup(NULL, item_prototype->headers);
	item->headers_orig = NULL;
	substitute_lld_macros(&item->headers, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->headers, ZBX_WHITESPACE); is not missing here */

	item->ssl_cert_file = zbx_strdup(NULL, item_prototype->ssl_cert_file);
	item->ssl_cert_file_orig = NULL;
	substitute_lld_macros(&item->ssl_cert_file, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->ssl_key_file = zbx_strdup(NULL, item_prototype->ssl_key_file);
	item->ssl_key_file_orig = NULL;
	substitute_lld_macros(&item->ssl_key_file, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->ssl_key_password = zbx_strdup(NULL, item_prototype->ssl_key_password);
	item->ssl_key_password_orig = NULL;
	substitute_lld_macros(&item->ssl_key_password, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->flags = ZBX_FLAG_LLD_ITEM_DISCOVERED;
	item->lld_row = lld_row;

	zbx_vector_ptr_create(&item->preproc_ops);
	zbx_vector_ptr_create(&item->dependent_items);
	zbx_vector_ptr_create(&item->item_params);
	zbx_vector_ptr_create(&item->item_tags);

	if (SUCCEED != ret || ZBX_PROTOTYPE_NO_DISCOVER == discover)
	{
		lld_item_free(item);
		item = NULL;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return item;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_update                                                  *
 *                                                                            *
 * Purpose: updates an existing item based on item prototype and lld data row *
 *                                                                            *
 * Parameters: item_prototype  - [IN] the item prototype                      *
 *             lld_row         - [IN] the lld row                             *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             item            - [IN] an existing item or NULL                *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_update(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row,
		const zbx_vector_ptr_t *lld_macro_paths, zbx_lld_item_t *item, char **error)
{
	char			*buffer = NULL, err[MAX_STRING_LEN];
	struct zbx_json_parse	*jp_row = (struct zbx_json_parse *)&lld_row->jp_row;
	const char		*delay, *history, *trends;
	unsigned char		discover;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	buffer = zbx_strdup(buffer, item_prototype->name);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->name, buffer))
	{
		item->name_proto = item->name;
		item->name = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_NAME;
	}

	delay = item_prototype->delay;
	history = item_prototype->history;
	trends = item_prototype->trends;
	discover = item_prototype->discover;

	lld_override_item(&lld_row->overrides, item->name, &delay, &history, &trends, &item->override_tags, NULL,
			&discover);

	if (0 != strcmp(item->key_proto, item_prototype->key))
	{
		buffer = zbx_strdup(buffer, item_prototype->key);

		if (SUCCEED == substitute_key_macros(&buffer, NULL, NULL, jp_row, lld_macro_paths,
				MACRO_TYPE_ITEM_KEY, err, sizeof(err)))
		{
			item->key_orig = item->key;
			item->key = buffer;
			buffer = NULL;
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_KEY;
		}
		else
			*error = zbx_strdcatf(*error, "Cannot update item, error in item key parameters: %s.\n", err);
	}

	buffer = zbx_strdup(buffer, delay);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->delay, buffer))
	{
		item->delay_orig = item->delay;
		item->delay = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_DELAY;
	}

	buffer = zbx_strdup(buffer, history);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->history, buffer))
	{
		item->history_orig = item->history;
		item->history = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY;
	}

	buffer = zbx_strdup(buffer, trends);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->trends, buffer))
	{
		item->trends_orig = item->trends;
		item->trends = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS;
	}

	buffer = zbx_strdup(buffer, item_prototype->units);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->units, buffer))
	{
		item->units_orig = item->units;
		item->units = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_UNITS;
	}

	buffer = zbx_strdup(buffer, item_prototype->params);

	if (ITEM_TYPE_CALCULATED == item_prototype->type)
	{
		char	*errmsg = NULL;

		if (SUCCEED == substitute_formula_macros(&buffer, jp_row, lld_macro_paths, &errmsg))
		{
			zbx_lrtrim(buffer, ZBX_WHITESPACE);

			if (0 != strcmp(item->params, buffer))
			{
				item->params_orig = item->params;
				item->params = buffer;
				buffer = NULL;
				item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS;
			}
		}
		else
		{
			*error = zbx_strdcatf(*error, "Cannot update item, error in formula: %s.\n", errmsg);
			zbx_free(errmsg);
		}
	}
	else
	{
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		if (0 != strcmp(item->params, buffer))
		{
			item->params_orig = item->params;
			item->params = buffer;
			buffer = NULL;
			item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS;
		}
	}

	buffer = zbx_strdup(buffer, item_prototype->ipmi_sensor);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ipmi_sensor, buffer))
	{
		item->ipmi_sensor_orig = item->ipmi_sensor;
		item->ipmi_sensor = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR;
	}

	buffer = zbx_strdup(buffer, item_prototype->snmp_oid);

	if (ITEM_TYPE_SNMP == item_prototype->type && FAIL == substitute_key_macros(&buffer, NULL, NULL, jp_row,
			lld_macro_paths, MACRO_TYPE_SNMP_OID, err, sizeof(err)))
	{
		*error = zbx_strdcatf(*error, "Cannot update item, error in SNMP OID key parameters: %s.\n", err);
	}

	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->snmp_oid, buffer))
	{
		item->snmp_oid_orig = item->snmp_oid;
		item->snmp_oid = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_OID;
	}

	buffer = zbx_strdup(buffer, item_prototype->username);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->username, buffer))
	{
		item->username_orig = item->username;
		item->username = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_USERNAME;
	}

	buffer = zbx_strdup(buffer, item_prototype->password);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->password, buffer))
	{
		item->password_orig = item->password;
		item->password = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_PASSWORD;
	}

	buffer = zbx_strdup(buffer, item_prototype->description);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->description, buffer))
	{
		item->description_orig = item->description;
		item->description = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION;
	}

	buffer = zbx_strdup(buffer, item_prototype->jmx_endpoint);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->jmx_endpoint, buffer))
	{
		item->jmx_endpoint_orig = item->jmx_endpoint;
		item->jmx_endpoint = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT;
	}

	buffer = zbx_strdup(buffer, item_prototype->timeout);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->timeout, buffer))
	{
		item->timeout_orig = item->timeout;
		item->timeout = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_TIMEOUT;
	}

	buffer = zbx_strdup(buffer, item_prototype->url);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->url, buffer))
	{
		item->url_orig = item->url;
		item->url = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_URL;
	}

	buffer = zbx_strdup(buffer, item_prototype->query_fields);

	if (FAIL == substitute_macros_in_json_pairs(&buffer, jp_row, lld_macro_paths, err, sizeof(err)))
		*error = zbx_strdcatf(*error, "Cannot update item, error in JSON: %s.\n", err);

	if (0 != strcmp(item->query_fields, buffer))
	{
		item->query_fields_orig = item->query_fields;
		item->query_fields = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS;
	}

	buffer = zbx_strdup(buffer, item_prototype->posts);

	if (ZBX_POSTTYPE_JSON == item_prototype->post_type)
	{
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_JSON, NULL, 0);
	}
	else if (ZBX_POSTTYPE_XML == item_prototype->post_type)
	{
		if (FAIL == substitute_macros_xml(&buffer, NULL, jp_row, lld_macro_paths, err, sizeof(err)))
		{
			zbx_lrtrim(err, ZBX_WHITESPACE);
			*error = zbx_strdcatf(*error, "Cannot update item, error in XML: %s.\n", err);
		}
	}
	else
		substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->posts, buffer))
	{
		item->posts_orig = item->posts;
		item->posts = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_POSTS;
	}

	buffer = zbx_strdup(buffer, item_prototype->status_codes);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->status_codes, buffer))
	{
		item->status_codes_orig = item->status_codes;
		item->status_codes = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES;
	}

	buffer = zbx_strdup(buffer, item_prototype->http_proxy);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(buffer, ZBX_WHITESPACE);
	if (0 != strcmp(item->http_proxy, buffer))
	{
		item->http_proxy_orig = item->http_proxy;
		item->http_proxy = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY;
	}

	buffer = zbx_strdup(buffer, item_prototype->headers);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/*zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->headers, buffer))
	{
		item->headers_orig = item->headers;
		item->headers = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_HEADERS;
	}

	buffer = zbx_strdup(buffer, item_prototype->ssl_cert_file);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_cert_file, buffer))
	{
		item->ssl_cert_file_orig = item->ssl_cert_file;
		item->ssl_cert_file = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE;
	}

	buffer = zbx_strdup(buffer, item_prototype->ssl_key_file);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_key_file, buffer))
	{
		item->ssl_key_file_orig = item->ssl_key_file;
		item->ssl_key_file = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE;
	}

	buffer = zbx_strdup(buffer, item_prototype->ssl_key_password);
	substitute_lld_macros(&buffer, jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(buffer, ZBX_WHITESPACE); is not missing here */
	if (0 != strcmp(item->ssl_key_password, buffer))
	{
		item->ssl_key_password_orig = item->ssl_key_password;
		item->ssl_key_password = buffer;
		buffer = NULL;
		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD;
	}

	if (ZBX_PROTOTYPE_NO_DISCOVER != discover)
		item->flags |= ZBX_FLAG_LLD_ITEM_DISCOVERED;

	item->lld_row = lld_row;

	zbx_free(buffer);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_make                                                   *
 *                                                                            *
 * Purpose: updates existing items and creates new ones based on item         *
 *          item prototypes and lld data                                      *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_rows        - [IN] the lld data rows                       *
 *             lld_macro_paths - [IN] use json path to extract from jp_row  *
 *             items           - [IN/OUT] sorted list of items                *
 *             items_index     - [OUT] index of items based on prototype ids  *
 *                                     and lld rows. Used to quckly find an   *
 *                                     item by prototype and lld_row.         *
 *             error           - [IN/OUT] the lld error message               *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_make(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *lld_rows,
		const zbx_vector_ptr_t *lld_macro_paths, zbx_vector_ptr_t *items, zbx_hashset_t *items_index,
		char **error)
{
	int				i, j, index;
	zbx_lld_item_prototype_t	*item_prototype;
	zbx_lld_item_t			*item;
	zbx_lld_row_t			*lld_row;
	zbx_lld_item_index_t		*item_index, item_index_local;
	char				*buffer = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* create the items index */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
			zbx_vector_ptr_append(&item_prototype->lld_rows, lld_rows->values[j]);
	}

	/* Iterate in reverse order because usually the items are created in the same order as     */
	/* incoming lld rows. Iterating in reverse optimizes lld_row removal from item prototypes. */
	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		for (j = item_prototype->lld_rows.values_num - 1; j >= 0; j--)
		{
			lld_row = (zbx_lld_row_t *)item_prototype->lld_rows.values[j];

			buffer = zbx_strdup(buffer, item->key_proto);

			if (SUCCEED != substitute_key_macros(&buffer, NULL, NULL, &lld_row->jp_row, lld_macro_paths,
					MACRO_TYPE_ITEM_KEY, NULL, 0))
			{
				continue;
			}

			if (0 == strcmp(item->key, buffer) &&
					SUCCEED == lld_validate_item_override_no_discover(&lld_row->overrides,
					item->name, item_prototype->discover))
			{
				item_index_local.parent_itemid = item->parent_itemid;
				item_index_local.lld_row = lld_row;
				item_index_local.item = item;
				zbx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));

				zbx_vector_ptr_remove_noorder(&item_prototype->lld_rows, j);
				break;
			}
		}
	}

	zbx_free(buffer);

	/* update/create discovered items */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		item_index_local.parent_itemid = item_prototype->itemid;

		for (j = 0; j < lld_rows->values_num; j++)
		{
			item_index_local.lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			if (NULL == (item_index = (zbx_lld_item_index_t *)zbx_hashset_search(items_index, &item_index_local)))
			{
				if (NULL != (item = lld_item_make(item_prototype, item_index_local.lld_row,
						lld_macro_paths, error)))
				{
					/* add the created item to items vector and update index */
					zbx_vector_ptr_append(items, item);
					item_index_local.item = item;
					zbx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));
				}
			}
			else
				lld_item_update(item_prototype, item_index_local.lld_row, lld_macro_paths, item_index->item, error);
		}
	}

	zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d items", __func__, items->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_lld_macors_in_preproc_params                          *
 *                                                                            *
 * Purpose: escaping of a symbols in items preprocessing steps for discovery  *
 *          process                                                           *
 *                                                                            *
 * Parameters: type            - [IN] the item preprocessing step type        *
 *             lld_row         - [IN] lld source value                        *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             sub_params      - [IN/OUT] the preprocessing parameters        *
 *                                                                            *
 ******************************************************************************/
static void	substitute_lld_macros_in_preproc_params(int type, const zbx_lld_row_t *lld_row,
		const zbx_vector_ptr_t *lld_macro_paths, char **sub_params)
{
	int	params_num = 1, flags1, flags2;

	switch (type)
	{
		case ZBX_PREPROC_REGSUB:
		case ZBX_PREPROC_ERROR_FIELD_REGEX:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_REGEXP;
			flags2 = ZBX_MACRO_ANY | ZBX_TOKEN_REGEXP_OUTPUT;
			params_num = 2;
			break;
		case ZBX_PREPROC_VALIDATE_REGEX:
		case ZBX_PREPROC_VALIDATE_NOT_REGEX:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_REGEXP;
			params_num = 1;
			break;
		case ZBX_PREPROC_XPATH:
		case ZBX_PREPROC_ERROR_FIELD_XML:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_XPATH;
			params_num = 1;
			break;
		case ZBX_PREPROC_PROMETHEUS_PATTERN:
		case ZBX_PREPROC_PROMETHEUS_TO_JSON:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_PROMETHEUS;
			params_num = 1;
			break;
		case ZBX_PREPROC_JSONPATH:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_JSONPATH;
			params_num = 1;
			break;
		case ZBX_PREPROC_STR_REPLACE:
			flags1 = ZBX_MACRO_ANY | ZBX_TOKEN_STR_REPLACE;
			flags2 = ZBX_MACRO_ANY | ZBX_TOKEN_STR_REPLACE;
			params_num = 2;
			break;
		default:
			flags1 = ZBX_MACRO_ANY;
			params_num = 1;
	}

	if (2 == params_num)
	{
		char	*param1, *param2;
		size_t	params_alloc, params_offset = 0;

		zbx_strsplit(*sub_params, '\n', &param1, &param2);

		if (NULL == param2)
		{
			zbx_free(param1);
			zabbix_log(LOG_LEVEL_ERR, "Invalid preprocessing parameters: %s.", *sub_params);
			THIS_SHOULD_NEVER_HAPPEN;
			return;
		}

		substitute_lld_macros(&param1, &lld_row->jp_row, lld_macro_paths, flags1, NULL, 0);
		substitute_lld_macros(&param2, &lld_row->jp_row, lld_macro_paths, flags2, NULL, 0);

		params_alloc = strlen(param1) + strlen(param2) + 2;
		*sub_params = (char*)zbx_realloc(*sub_params, params_alloc);

		zbx_strcpy_alloc(sub_params, &params_alloc, &params_offset, param1);
		zbx_chrcpy_alloc(sub_params, &params_alloc, &params_offset, '\n');
		zbx_strcpy_alloc(sub_params, &params_alloc, &params_offset, param2);

		zbx_free(param1);
		zbx_free(param2);
	}
	else
		substitute_lld_macros(sub_params, &lld_row->jp_row, lld_macro_paths, flags1, NULL, 0);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_make                                           *
 *                                                                            *
 * Purpose: updates existing items preprocessing operations and create new    *
 *          based on item item prototypes                                     *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             items           - [IN/OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_preproc_make(const zbx_vector_ptr_t *item_prototypes,
		const zbx_vector_ptr_t *lld_macro_paths, zbx_vector_ptr_t *items)
{
	int				i, j, index, preproc_num;
	zbx_lld_item_t			*item;
	zbx_lld_item_prototype_t	*item_proto;
	zbx_lld_item_preproc_t		*ppsrc, *ppdst;
	char				*buffer = NULL;

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_ptr_sort(&item->preproc_ops, lld_item_preproc_sort_by_step);

		item_proto = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		preproc_num = MAX(item->preproc_ops.values_num, item_proto->preproc_ops.values_num);

		for (j = 0; j < preproc_num; j++)
		{
			if (j >= item->preproc_ops.values_num)
			{
				ppsrc = (zbx_lld_item_preproc_t *)item_proto->preproc_ops.values[j];
				ppdst = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
				ppdst->item_preprocid = 0;
				ppdst->flags = ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED | ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE;
				ppdst->step = ppsrc->step;
				ppdst->type = ppsrc->type;
				ppdst->params = zbx_strdup(NULL, ppsrc->params);
				ppdst->error_handler = ppsrc->error_handler;
				ppdst->error_handler_params = zbx_strdup(NULL, ppsrc->error_handler_params);

				substitute_lld_macros_in_preproc_params(ppsrc->type, item->lld_row, lld_macro_paths,
						&ppdst->params);
				substitute_lld_macros(&ppdst->error_handler_params, &item->lld_row->jp_row,
						lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

				zbx_vector_ptr_append(&item->preproc_ops, ppdst);
				continue;
			}

			ppdst = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			if (j >= item_proto->preproc_ops.values_num)
			{
				ppdst->flags &= ~ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;
				continue;
			}

			ppsrc = (zbx_lld_item_preproc_t *)item_proto->preproc_ops.values[j];

			ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;

			if (ppdst->type != ppsrc->type)
			{
				ppdst->type = ppsrc->type;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE;
			}

			if (ppdst->step != ppsrc->step)
			{
				/* this should never happen */
				ppdst->step = ppsrc->step;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP;
			}

			buffer = zbx_strdup(buffer, ppsrc->params);
			substitute_lld_macros_in_preproc_params(ppsrc->type, item->lld_row, lld_macro_paths, &buffer);

			if (0 != strcmp(ppdst->params, buffer))
			{
				zbx_free(ppdst->params);
				ppdst->params = buffer;
				buffer = NULL;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS;
			}

			if (ppdst->error_handler != ppsrc->error_handler)
			{
				ppdst->error_handler = ppsrc->error_handler;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER;
			}

			buffer = zbx_strdup(buffer, ppsrc->error_handler_params);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(ppdst->error_handler_params, buffer))
			{
				zbx_free(ppdst->error_handler_params);
				ppdst->error_handler_params = buffer;
				buffer = NULL;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS;
			}
			else
				zbx_free(buffer);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_param_make                                             *
 *                                                                            *
 * Purpose: updates existing items parameters and create new based on item    *
 *          prototypes                                                        *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             items           - [IN/OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_param_make(const zbx_vector_ptr_t *item_prototypes,
		const zbx_vector_ptr_t *lld_macro_paths, zbx_vector_ptr_t *items)
{
	int				i, j, index, item_param_num;
	zbx_lld_item_t			*item;
	zbx_lld_item_prototype_t	*item_proto;
	zbx_lld_item_param_t		*ipsrc, *ipdst;
	char				*buffer = NULL;

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_ptr_sort(&item->item_params, lld_item_param_sort_by_name);

		item_proto = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		item_param_num = MAX(item->item_params.values_num, item_proto->item_params.values_num);

		for (j = 0; j < item_param_num; j++)
		{
			if (j >= item->item_params.values_num)
			{
				ipsrc = (zbx_lld_item_param_t *)item_proto->item_params.values[j];
				ipdst = (zbx_lld_item_param_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_param_t));
				ipdst->item_parameterid = 0;
				ipdst->flags = ZBX_FLAG_LLD_ITEM_PARAM_DISCOVERED | ZBX_FLAG_LLD_ITEM_PARAM_UPDATE;
				ipdst->name = zbx_strdup(NULL, ipsrc->name);
				ipdst->value = zbx_strdup(NULL, ipsrc->value);

				substitute_lld_macros(&ipdst->name, &item->lld_row->jp_row,
						lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
				substitute_lld_macros(&ipdst->value, &item->lld_row->jp_row,
						lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

				zbx_vector_ptr_append(&item->item_params, ipdst);
				continue;
			}

			ipdst = (zbx_lld_item_param_t *)item->item_params.values[j];

			if (j >= item_proto->item_params.values_num)
			{
				ipdst->flags &= ~ZBX_FLAG_LLD_ITEM_PARAM_DISCOVERED;
				continue;
			}

			ipsrc = (zbx_lld_item_param_t *)item_proto->item_params.values[j];

			ipdst->flags |= ZBX_FLAG_LLD_ITEM_PARAM_DISCOVERED;

			buffer = zbx_strdup(buffer, ipsrc->name);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(ipdst->name, buffer))
			{
				zbx_free(ipdst->name);
				ipdst->name = buffer;
				buffer = NULL;
				ipdst->flags |= ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_NAME;
			}
			else
				zbx_free(buffer);

			buffer = zbx_strdup(buffer, ipsrc->value);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(ipdst->value, buffer))
			{
				zbx_free(ipdst->value);
				ipdst->value = buffer;
				buffer = NULL;
				ipdst->flags |= ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_VALUE;
			}
			else
				zbx_free(buffer);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_tags_make                                              *
 *                                                                            *
 * Purpose: updates existing items tags and create new based on item          *
 *          prototypes                                                        *
 *                                                                            *
 * Parameters: item_prototypes - [IN] the item prototypes                     *
 *             lld_macro_paths - [IN] use json path to extract from jp_row    *
 *             items           - [IN/OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
static void	lld_items_tags_make(const zbx_vector_ptr_t *item_prototypes, const zbx_vector_ptr_t *lld_macro_paths,
		zbx_vector_ptr_t *items)
{
	int				i, j, index, item_tag_num;
	zbx_lld_item_t			*item;
	zbx_lld_item_prototype_t	*item_proto;
	zbx_lld_item_tag_t		*itsrc, *itdst;
	zbx_db_tag_t			*override_tags;
	char				*buffer = NULL;
	const char			*name, *value;

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_ptr_sort(&item->item_tags, lld_item_tag_sort_by_tag);
		zbx_vector_db_tag_ptr_sort(&item->override_tags, zbx_db_tag_compare_func);

		item_proto = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		item_tag_num = MAX(item->item_tags.values_num,
				item_proto->item_tags.values_num + item->override_tags.values_num);

		for (j = 0; j < item_tag_num; j++)
		{
			if (j < item->item_tags.values_num &&
					j >= item_proto->item_tags.values_num + item->override_tags.values_num)
			{
				itdst = (zbx_lld_item_tag_t *)item->item_tags.values[j];
				itdst->flags &= ~ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED;
				continue;
			}

			if (j >= item_proto->item_tags.values_num)
			{
				override_tags = item->override_tags.values[j - item_proto->item_tags.values_num];
				name = override_tags->tag;
				value = override_tags->value;
			}
			else
			{
				itsrc = (zbx_lld_item_tag_t *)item_proto->item_tags.values[j];
				name = itsrc->tag;
				value = itsrc->value;
			}

			if (j >= item->item_tags.values_num)
			{
				itdst = (zbx_lld_item_tag_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_tag_t));
				itdst->item_tagid = 0;
				itdst->flags = ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED | ZBX_FLAG_LLD_ITEM_TAG_UPDATE;
				itdst->tag = zbx_strdup(NULL, name);
				itdst->value = zbx_strdup(NULL, value);

				substitute_lld_macros(&itdst->tag, &item->lld_row->jp_row,
						lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);
				substitute_lld_macros(&itdst->value, &item->lld_row->jp_row,
						lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

				zbx_vector_ptr_append(&item->item_tags, itdst);
				continue;
			}

			itdst = (zbx_lld_item_tag_t *)item->item_tags.values[j];
			itdst->flags |= ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED;

			buffer = zbx_strdup(buffer, name);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(itdst->tag, buffer))
			{
				zbx_free(itdst->tag);
				itdst->tag = buffer;
				buffer = NULL;
				itdst->flags |= ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_NAME;
			}
			else
				zbx_free(buffer);

			buffer = zbx_strdup(buffer, value);
			substitute_lld_macros(&buffer, &item->lld_row->jp_row, lld_macro_paths, ZBX_MACRO_ANY, NULL, 0);

			if (0 != strcmp(itdst->value, buffer))
			{
				zbx_free(itdst->value);
				itdst->value = buffer;
				buffer = NULL;
				itdst->flags |= ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_VALUE;
			}
			else
				zbx_free(buffer);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_save                                                    *
 *                                                                            *
 * Purpose: recursively prepare LLD item bulk insert if any and               *
 *          update dependent items with their masters                         *
 *                                                                            *
 * Parameters: hostid               - [IN] parent host id                     *
 *             item_prototypes      - [IN] item prototypes                    *
 *             item                 - [IN/OUT] item to be saved and set       *
 *                                             master for dependentent items  *
 *             itemid               - [IN/OUT] item id used for insert        *
 *                                             operations                     *
 *             itemdiscoveryid      - [IN/OUT] item discovery id used for     *
 *                                             insert operations              *
 *             db_insert_items      - [IN] prepared item bulk insert          *
 *             db_insert_idiscovery - [IN] prepared item discovery bulk       *
 *                                         insert                             *
 *             db_insert_irtdata    - [IN] prepared item real-time data bulk  *
 *                                         insert                             *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *item_prototypes, zbx_lld_item_t *item,
		zbx_uint64_t *itemid, zbx_uint64_t *itemdiscoveryid, zbx_db_insert_t *db_insert_items,
		zbx_db_insert_t *db_insert_idiscovery, zbx_db_insert_t *db_insert_irtdata)
{
	int	index;
	glb_changeset_t cset;
	
	changeset_prepare(&cset);

	if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
		return;

	if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
			ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	if (0 == item->itemid)
	{
		const zbx_lld_item_prototype_t	*item_prototype;

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];
		item->itemid = (*itemid)++;
		
		zbx_db_insert_add_values(db_insert_items, item->itemid, item->name, item->key, hostid,
				(int)item_prototype->type, (int)item_prototype->value_type,
				item->delay, item->history, item->trends,
				(int)item->status, item_prototype->trapper_hosts, item->units,
				item_prototype->formula, item_prototype->logtimefmt, item_prototype->valuemapid,
				item->params, item->ipmi_sensor, item->snmp_oid, (int)item_prototype->authtype,
				item->username, item->password, item_prototype->publickey, item_prototype->privatekey,
				item->description, item_prototype->interfaceid, (int)ZBX_FLAG_DISCOVERY_CREATED,
				item->jmx_endpoint, item->master_itemid,
				item->timeout, item->url, item->query_fields, item->posts, item->status_codes,
				item_prototype->follow_redirects, item_prototype->post_type, item->http_proxy,
				item->headers, item_prototype->retrieve_mode, item_prototype->request_method,
				item_prototype->output_format, item->ssl_cert_file, item->ssl_key_file,
				item->ssl_key_password, item_prototype->verify_peer, item_prototype->verify_host,
				item_prototype->allow_traps);

		zbx_db_insert_add_values(db_insert_idiscovery, (*itemdiscoveryid)++, item->itemid,
				item->parent_itemid, item_prototype->key);

		zbx_db_insert_add_values(db_insert_irtdata, item->itemid, time(NULL));
		changeset_add_to_cache(&cset, OBJ_ITEMS, &item->itemid, DB_CREATE, 1);
	}
	changeset_flush(&cset);

	for (index = 0; index < item->dependent_items.values_num; index++)
	{
		zbx_lld_item_t	*dependent;

		dependent = (zbx_lld_item_t *)item->dependent_items.values[index];
		dependent->master_itemid = item->itemid;
		lld_item_save(hostid, item_prototypes, dependent, itemid, itemdiscoveryid, db_insert_items,
				db_insert_idiscovery, db_insert_irtdata);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_prepare_update                                          *
 *                                                                            *
 * Purpose: prepare sql to update LLD item                                    *
 *                                                                            *
 * Parameters: item_prototype       - [IN] item prototype                     *
 *             item                 - [IN] item to be updated                 *
 *             sql                  - [IN/OUT] sql buffer pointer used for    *
 *                                             update operations              *
 *             sql_alloc            - [IN/OUT] sql buffer already allocated   *
 *                                             memory                         *
 *             sql_offset           - [IN/OUT] offset for writing within sql  *
 *                                             buffer                         *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_prepare_update(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_item_t *item,
		char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	char				*value_esc;
	const char			*d = "";

	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "update items set ");
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_NAME))
	{
		value_esc = DBdyn_escape_string(item->name);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "name='%s'", value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
	{
		value_esc = DBdyn_escape_string(item->key);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%skey_='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TYPE))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%stype=%d", d, (int)item_prototype->type);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%svalue_type=%d", d, (int)item_prototype->value_type);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_DELAY))
	{
		value_esc = DBdyn_escape_string(item->delay);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sdelay='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY))
	{
		value_esc = DBdyn_escape_string(item->history);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%shistory='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS))
	{
		value_esc = DBdyn_escape_string(item->trends);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%strends='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS))
	{
		value_esc = DBdyn_escape_string(item_prototype->trapper_hosts);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%strapper_hosts='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_UNITS))
	{
		value_esc = DBdyn_escape_string(item->units);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sunits='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_FORMULA))
	{
		value_esc = DBdyn_escape_string(item_prototype->formula);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sformula='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT))
	{
		value_esc = DBdyn_escape_string(item_prototype->logtimefmt);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%slogtimefmt='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%svaluemapid=%s",
				d, DBsql_id_ins(item_prototype->valuemapid));
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS))
	{
		value_esc = DBdyn_escape_string(item->params);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sparams='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR))
	{
		value_esc = DBdyn_escape_string(item->ipmi_sensor);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sipmi_sensor='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_OID))
	{
		value_esc = DBdyn_escape_string(item->snmp_oid);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%ssnmp_oid='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sauthtype=%d", d, (int)item_prototype->authtype);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_USERNAME))
	{
		value_esc = DBdyn_escape_string(item->username);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%susername='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_PASSWORD))
	{
		value_esc = DBdyn_escape_string(item->password);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spassword='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->publickey);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spublickey='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->privatekey);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sprivatekey='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION))
	{
		value_esc = DBdyn_escape_string(item->description);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sdescription='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";

	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_INTERFACEID))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sinterfaceid=%s",
				d, DBsql_id_ins(item_prototype->interfaceid));
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT))
	{
		value_esc = DBdyn_escape_string(item->jmx_endpoint);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sjmx_endpoint='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%smaster_itemid=%s",
				d, DBsql_id_ins(item->master_itemid));
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TIMEOUT))
	{
		value_esc = DBdyn_escape_string(item->timeout);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%stimeout='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_URL))
	{
		value_esc = DBdyn_escape_string(item->url);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%surl='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS))
	{
		value_esc = DBdyn_escape_string(item->query_fields);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%squery_fields='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_POSTS))
	{
		value_esc = DBdyn_escape_string(item->posts);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sposts='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES))
	{
		value_esc = DBdyn_escape_string(item->status_codes);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sstatus_codes='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sfollow_redirects=%d", d,
				(int)item_prototype->follow_redirects);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_POST_TYPE))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%spost_type=%d", d, (int)item_prototype->post_type);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY))
	{
		value_esc = DBdyn_escape_string(item->http_proxy);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%shttp_proxy='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_HEADERS))
	{
		value_esc = DBdyn_escape_string(item->headers);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sheaders='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sretrieve_mode=%d", d,
				(int)item_prototype->retrieve_mode);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%srequest_method=%d", d,
				(int)item_prototype->request_method);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%soutput_format=%d", d,
				(int)item_prototype->output_format);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE))
	{
		value_esc = DBdyn_escape_string(item->ssl_cert_file);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_cert_file='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE))
	{
		value_esc = DBdyn_escape_string(item->ssl_key_file);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_key_file='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD))
	{
		value_esc = DBdyn_escape_string(item->ssl_key_password);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_key_password='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_peer=%d", d, (int)item_prototype->verify_peer);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_host=%d", d, (int)item_prototype->verify_host);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sallow_traps=%d", d, (int)item_prototype->allow_traps);
	}

	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " where itemid=" ZBX_FS_UI64 ";\n", item->itemid);
	
	DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_discovery_prepare_update                                *
 *                                                                            *
 * Purpose: prepare sql to update key in LLD item discovery                   *
 *                                                                            *
 * Parameters: item_prototype       - [IN] item prototype                     *
 *             item                 - [IN] item to be updated                 *
 *             sql                  - [IN/OUT] sql buffer pointer used for    *
 *                                             update operations              *
 *             sql_alloc            - [IN/OUT] sql buffer already allocated   *
 *                                             memory                         *
 *             sql_offset           - [IN/OUT] offset for writing within sql  *
 *                                             buffer                         *
 *                                                                            *
 ******************************************************************************/
static void lld_item_discovery_prepare_update(const zbx_lld_item_prototype_t *item_prototype,
		const zbx_lld_item_t *item, char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	char	*value_esc;

	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
	{
		value_esc = DBdyn_escape_string(item_prototype->key);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update item_discovery"
				" set key_='%s'"
				" where itemid=" ZBX_FS_UI64 ";\n",
				value_esc, item->itemid);
		zbx_free(value_esc);

		DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_save                                                   *
 *                                                                            *
 * Parameters: hostid          - [IN] parent host id                          *
 *             item_prototypes - [IN] item prototypes                         *
 *             items           - [IN/OUT] items to save                       *
 *             items_index     - [IN] LLD item index                          *
 *             host_locked     - [IN/OUT] host record is locked               *
 *                                                                            *
 * Return value: SUCCEED - if items were successfully saved or saving was not *
 *                         necessary                                          *
 *               FAIL    - items cannot be saved                              *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items,
		zbx_hashset_t *items_index, int *host_locked)
{
	int				ret = SUCCEED, i, new_items = 0, upd_items = 0;
	zbx_lld_item_t			*item;
	zbx_uint64_t			itemid, itemdiscoveryid;
	zbx_db_insert_t			db_insert_items, db_insert_idiscovery, db_insert_irtdata;
	zbx_lld_item_index_t		item_index_local;
	zbx_vector_uint64_t		upd_keys, item_protoids;
	char				*sql = NULL;
	size_t				sql_alloc = 8 * ZBX_KIBIBYTE, sql_offset = 0;
	zbx_lld_item_prototype_t	*item_prototype;
	glb_changeset_t cset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&upd_keys);
	zbx_vector_uint64_create(&item_protoids);
	changeset_prepare(&cset);
	
	if (0 == items->values_num)
		goto out;

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		if (0 == item->itemid)
		{
			new_items++;
		}
		else if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE))
		{
			upd_items++;
			if(0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
				zbx_vector_uint64_append(&upd_keys, item->itemid);
		}
	}

	if (0 == new_items && 0 == upd_items)
		goto out;

	if (0 == *host_locked)
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_uint64_append(&item_protoids, item_prototype->itemid);
	}

	if (SUCCEED != DBlock_itemids(&item_protoids))
	{
		/* the item prototype was removed while processing lld rule */
		ret = FAIL;
		goto out;
	}

	if (0 != upd_items)
		sql = (char*)zbx_malloc(NULL, sql_alloc);

	if (0 != upd_keys.values_num)
	{
		sql_offset = 0;

		zbx_vector_uint64_sort(&upd_keys, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

#ifdef HAVE_MYSQL
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set key_=concat('#',key_) where");
#else
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update items set key_='#'||key_ where");
#endif
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", upd_keys.values,
				upd_keys.values_num);

		if (ZBX_DB_OK > DBexecute("%s", sql))
		{
			ret = FAIL;
			goto out;
		}
		changeset_add_to_cache(&cset, OBJ_ITEMS, upd_keys.values, DB_UPDATE, upd_keys.values_num);	
		changeset_flush(&cset);
	}

	if (0 != new_items)
	{
		itemid = DBget_maxid_num("items", new_items);
		itemdiscoveryid = DBget_maxid_num("item_discovery", new_items);

		zbx_db_insert_prepare(&db_insert_items, "items", "itemid", "name", "key_", "hostid", "type",
				"value_type", "delay", "history", "trends", "status", "trapper_hosts",
				"units", "formula", "logtimefmt", "valuemapid", "params",
				"ipmi_sensor", "snmp_oid", "authtype", "username", "password",
				"publickey", "privatekey", "description", "interfaceid", "flags",
				"jmx_endpoint", "master_itemid", "timeout", "url", "query_fields", "posts",
				"status_codes", "follow_redirects", "post_type", "http_proxy", "headers",
				"retrieve_mode", "request_method", "output_format", "ssl_cert_file", "ssl_key_file",
				"ssl_key_password", "verify_peer", "verify_host", "allow_traps", NULL);

		zbx_db_insert_prepare(&db_insert_idiscovery, "item_discovery", "itemdiscoveryid", "itemid",
				"parent_itemid", "key_", NULL);

		zbx_db_insert_prepare(&db_insert_irtdata, "item_rtdata", "itemid", "mtime", NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		/* dependent items based on item prototypes are saved within recursive lld_item_save calls while */
		/* saving master item */
		if (0 == item->master_itemid)
		{
			lld_item_save(hostid, item_prototypes, item, &itemid, &itemdiscoveryid, &db_insert_items,
					&db_insert_idiscovery, &db_insert_irtdata);
		}
		else
		{
			item_index_local.parent_itemid = item->master_itemid;
			item_index_local.lld_row = (zbx_lld_row_t *)item->lld_row;

			/* dependent item based on host item should be saved */
			if (NULL == zbx_hashset_search(items_index, &item_index_local))
			{
				lld_item_save(hostid, item_prototypes, item, &itemid, &itemdiscoveryid,
						&db_insert_items, &db_insert_idiscovery, &db_insert_irtdata);
			}
		}

	}

	if (0 != new_items)
	{
		zbx_db_insert_execute(&db_insert_items);
		zbx_db_insert_clean(&db_insert_items);

		zbx_db_insert_execute(&db_insert_idiscovery);
		zbx_db_insert_clean(&db_insert_idiscovery);

		zbx_db_insert_execute(&db_insert_irtdata);
		zbx_db_insert_clean(&db_insert_irtdata);

		zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	if (0 != upd_items)
	{
		int	index;

		sql_offset = 0;

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < items->values_num; i++)
		{
			item = (zbx_lld_item_t *)items->values[i];

			if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED) ||
					0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE))
			{
				continue;
			}

			if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			item_prototype = item_prototypes->values[index];

			lld_item_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
			lld_item_discovery_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		if (sql_offset > 16)
			DBexecute("%s", sql);
	}
out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&item_protoids);
	zbx_vector_uint64_destroy(&upd_keys);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_save                                           *
 *                                                                            *
 * Purpose: saves/updates/removes item preprocessing operations               *
 *                                                                            *
 * Parameters: hostid      - [IN] parent host id                              *
 *             items       - [IN] items                                       *
 *             host_locked - [IN/OUT] host record is locked                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_preproc_save(zbx_uint64_t hostid, zbx_vector_ptr_t *items, int *host_locked)
{
	int			ret = SUCCEED, i, j, new_preproc_num = 0, update_preproc_num = 0, delete_preproc_num = 0;
	zbx_lld_item_t		*item;
	zbx_lld_item_preproc_t	*preproc_op;
	zbx_vector_uint64_t	deleteids;
	zbx_db_insert_t		db_insert;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	glb_changeset_t cset;
	
	changeset_prepare(&cset);

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&deleteids);

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			preproc_op = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED))
			{
				zbx_vector_uint64_append(&deleteids, preproc_op->item_preprocid);
				changeset_add_to_cache(&cset, OBJ_PREPROCS, &preproc_op->item_preprocid, DB_DELETE, 1);
				continue;
			}

			if (0 == preproc_op->item_preprocid)
			{
				new_preproc_num++;
				continue;
			}

			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			update_preproc_num++;
		}
	}

	if (0 == *host_locked && (0 != update_preproc_num || 0 != new_preproc_num || 0 != deleteids.values_num))
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	if (0 != update_preproc_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != new_preproc_num)
	{
		zbx_db_insert_prepare(&db_insert, "item_preproc", "item_preprocid", "itemid", "step", "type", "params",
				"error_handler", "error_handler_params", NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			char	delim = ' ';

			preproc_op = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			if (0 == preproc_op->item_preprocid)
			{
				zbx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, preproc_op->step,
						preproc_op->type, preproc_op->params, preproc_op->error_handler,
						preproc_op->error_handler_params);
				continue;
			}

			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_preproc set");

			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%ctype=%d", delim, preproc_op->type);
				delim = ',';
			}

			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstep=%d", delim, preproc_op->step);
				delim = ',';
			}

			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS))
			{
				char	*params_esc;

				params_esc = DBdyn_escape_string(preproc_op->params);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cparams='%s'", delim, params_esc);

				zbx_free(params_esc);
				delim = ',';
			}

			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror_handler=%d", delim,
						preproc_op->error_handler);
				delim = ',';
			}

			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_ERROR_HANDLER_PARAMS))
			{
				char	*params_esc;

				params_esc = DBdyn_escape_string(preproc_op->error_handler_params);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror_handler_params='%s'", delim,
						params_esc);

				zbx_free(params_esc);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where item_preprocid=" ZBX_FS_UI64 ";\n",
					preproc_op->item_preprocid);
			changeset_add_to_cache(&cset, OBJ_PREPROCS, &preproc_op->item_preprocid, DB_UPDATE, 1);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}
	}
	changeset_flush(&cset);

	if (0 != update_preproc_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);
	}

	if (0 != new_preproc_num)
	{
		zbx_db_insert_autoincrement(&db_insert, "item_preprocid");
		zbx_db_insert_execute(&db_insert);
		
		for (i = 0; i < db_insert.rows.values_num; i++)
		{
			zbx_db_value_t	*values = (zbx_db_value_t *)db_insert.rows.values[i];
			changeset_add_to_cache(&cset, OBJ_PREPROCS, &values[db_insert.autoincrement].ui64, DB_CREATE, 1); 
		}
		zbx_db_insert_clean(&db_insert);
		changeset_flush(&cset);
	}

	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_preproc where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_preprocid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);
		
		changeset_add_to_cache(&cset, OBJ_PREPROCS, deleteids.values, DB_DELETE, deleteids.values_num);
		changeset_flush(&cset);
		
		delete_preproc_num = deleteids.values_num;
	}
out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&deleteids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() added:%d updated:%d removed:%d", __func__, new_preproc_num,
			update_preproc_num, delete_preproc_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_param_save                                             *
 *                                                                            *
 * Purpose: saves/updates/removes item parameters                             *
 *                                                                            *
 * Parameters: hostid      - [IN] parent host id                              *
 *             items       - [IN] items                                       *
 *             host_locked - [IN/OUT] host record is locked                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_param_save(zbx_uint64_t hostid, zbx_vector_ptr_t *items, int *host_locked)
{
	int			ret = SUCCEED, i, j, new_param_num = 0, update_param_num = 0, delete_param_num = 0;
	zbx_lld_item_t		*item;
	zbx_lld_item_param_t	*item_param;
	zbx_vector_uint64_t	deleteids;
	zbx_db_insert_t		db_insert;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&deleteids);

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_params.values_num; j++)
		{
			item_param = (zbx_lld_item_param_t *)item->item_params.values[j];

			if (0 == (item_param->flags & ZBX_FLAG_LLD_ITEM_PARAM_DISCOVERED))
			{
				zbx_vector_uint64_append(&deleteids, item_param->item_parameterid);
				continue;
			}

			if (0 == item_param->item_parameterid)
			{
				new_param_num++;
				continue;
			}

			if (0 == (item_param->flags & ZBX_FLAG_LLD_ITEM_PARAM_UPDATE))
				continue;

			update_param_num++;
		}
	}

	if (0 == *host_locked && (0 != update_param_num || 0 != new_param_num || 0 != deleteids.values_num))
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	if (0 != update_param_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != new_param_num)
	{
		zbx_db_insert_prepare(&db_insert, "item_parameter", "item_parameterid", "itemid", "name", "value",
				NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_params.values_num; j++)
		{
			char	delim = ' ';

			item_param = (zbx_lld_item_param_t *)item->item_params.values[j];

			if (0 == item_param->item_parameterid)
			{
				zbx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, item_param->name,
						item_param->value);
				continue;
			}

			if (0 == (item_param->flags & ZBX_FLAG_LLD_ITEM_PARAM_UPDATE))
				continue;

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_parameter set");

			if (0 != (item_param->flags & ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_NAME))
			{
				char	*name_esc;

				name_esc = DBdyn_escape_string(item_param->name);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cname='%s'", delim, name_esc);

				zbx_free(name_esc);
				delim = ',';
			}

			if (0 != (item_param->flags & ZBX_FLAG_LLD_ITEM_PARAM_UPDATE_VALUE))
			{
				char	*value_esc;

				value_esc = DBdyn_escape_string(item_param->value);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cvalue='%s'", delim, value_esc);

				zbx_free(value_esc);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where item_parameterid=" ZBX_FS_UI64 ";\n",
					item_param->item_parameterid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}
	}

	if (0 != update_param_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);
	}

	if (0 != new_param_num)
	{
		zbx_db_insert_autoincrement(&db_insert, "item_parameterid");
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_parameter where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_parameterid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);

		delete_param_num = deleteids.values_num;
	}
out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&deleteids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() added:%d updated:%d removed:%d", __func__, new_param_num,
			update_param_num, delete_param_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_tags_save                                              *
 *                                                                            *
 * Purpose: saves/updates/removes item tags                                   *
 *                                                                            *
 * Parameters: hostid      - [IN] parent host id                              *
 *             items       - [IN] items                                       *
 *             host_locked - [IN/OUT] host record is locked                   *
 *                                                                            *
 ******************************************************************************/
static int	lld_items_tags_save(zbx_uint64_t hostid, zbx_vector_ptr_t *items, int *host_locked)
{
	int			ret = SUCCEED, i, j, new_tag_num = 0, update_tag_num = 0, delete_tag_num = 0;
	zbx_lld_item_t		*item;
	zbx_lld_item_tag_t	*item_tag;
	zbx_vector_uint64_t	deleteids;
	zbx_db_insert_t		db_insert;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	glb_changeset_t cset;
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	changeset_prepare(&cset);
	zbx_vector_uint64_create(&deleteids);

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_tags.values_num; j++)
		{
			item_tag = (zbx_lld_item_tag_t *)item->item_tags.values[j];

			if (0 == (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_DISCOVERED))
			{
				zbx_vector_uint64_append(&deleteids, item_tag->item_tagid);
				continue;
			}

			if (0 == item_tag->item_tagid)
			{
				new_tag_num++;
				continue;
			}

			if (0 == (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_UPDATE))
				continue;

			update_tag_num++;
		}
	}

	if (0 == *host_locked && (0 != update_tag_num || 0 != new_tag_num || 0 != deleteids.values_num))
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	if (0 != update_tag_num)
	{
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != new_tag_num)
	{
		zbx_db_insert_prepare(&db_insert, "item_tag", "itemtagid", "itemid", "tag", "value",
				NULL);
	}

	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		for (j = 0; j < item->item_tags.values_num; j++)
		{
			char	delim = ' ';

			item_tag = (zbx_lld_item_tag_t *)item->item_tags.values[j];

			if (0 == item_tag->item_tagid)
			{
				zbx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, item_tag->tag,
						item_tag->value);
				continue;
			}

			if (0 == (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_UPDATE))
				continue;

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_tag set");

			if (0 != (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_UPDATE_TAG))
			{
				char	*tag_esc;

				tag_esc = DBdyn_escape_string(item_tag->tag);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%ctag='%s'", delim, tag_esc);

				zbx_free(tag_esc);
				delim = ',';
			}

			if (0 != (item_tag->flags & ZBX_FLAG_LLD_ITEM_TAG_UPDATE_VALUE))
			{
				char	*value_esc;

				value_esc = DBdyn_escape_string(item_tag->value);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cvalue='%s'", delim, value_esc);

				zbx_free(value_esc);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where itemtagid=" ZBX_FS_UI64 ";\n",
					item_tag->item_tagid);
			changeset_add_to_cache(&cset, OBJ_ITEMTAGS, &item_tag->item_tagid, DB_UPDATE, 1);
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}
	}
	
	if (0 != update_tag_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);
		
		changeset_flush(&cset);
	}

	if (0 != new_tag_num)
	{
		zbx_db_insert_autoincrement(&db_insert, "itemtagid");
		zbx_db_insert_execute(&db_insert);
		
		for (i = 0; i < db_insert.rows.values_num; i++)
		{
			zbx_db_value_t	*values = (zbx_db_value_t *)db_insert.rows.values[i];
			changeset_add_to_cache(&cset, OBJ_ITEMTAGS, &values[db_insert.autoincrement].ui64, DB_CREATE, 1); 
		}
		
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemtagid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);
		changeset_add_to_cache(&cset, OBJ_ITEMTAGS, deleteids.values, DB_DELETE, deleteids.values_num);
		
		delete_tag_num = deleteids.values_num;
	}
out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&deleteids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() added:%d updated:%d removed:%d", __func__, new_tag_num,
			update_tag_num, delete_tag_num);

	return ret;
}

static	void	get_item_info(const void *object, zbx_uint64_t *id, int *discovery_flag, int *lastcheck,
		int *ts_delete)
{
	zbx_lld_item_t	*item;

	item = (zbx_lld_item_t *)object;

	*id = item->itemid;
	*discovery_flag = item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED;
	*lastcheck = item->lastcheck;
	*ts_delete = item->ts_delete;
}

static void	lld_item_links_populate(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *lld_rows,
		zbx_hashset_t *items_index)
{
	int				i, j;
	zbx_lld_item_prototype_t	*item_prototype;
	zbx_lld_item_index_t		*item_index, item_index_local;
	zbx_lld_item_link_t		*item_link;

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		item_index_local.parent_itemid = item_prototype->itemid;

		for (j = 0; j < lld_rows->values_num; j++)
		{
			item_index_local.lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			if (NULL == (item_index = (zbx_lld_item_index_t *)zbx_hashset_search(items_index, &item_index_local)))
				continue;

			if (0 == (item_index->item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
				continue;

			item_link = (zbx_lld_item_link_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_link_t));

			item_link->parent_itemid = item_index->item->parent_itemid;
			item_link->itemid = item_index->item->itemid;

			zbx_vector_ptr_append(&item_index_local.lld_row->item_links, item_link);
		}
	}
}

void	lld_item_links_sort(zbx_vector_ptr_t *lld_rows)
{
	int	i;

	for (i = 0; i < lld_rows->values_num; i++)
	{
		zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[i];

		zbx_vector_ptr_sort(&lld_row->item_links, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_prototypes_get                                          *
 *                                                                            *
 * Purpose: load discovery rule item prototypes                               *
 *                                                                            *
 * Parameters: lld_ruleid      - [IN] the discovery rule id                   *
 *             item_prototypes - [OUT] the item prototypes                    *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_prototypes_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *item_prototypes)
{
	DB_RESULT			result;
	DB_ROW				row;
	zbx_lld_item_prototype_t	*item_prototype;
	zbx_lld_item_preproc_t		*preproc_op;
	zbx_lld_item_param_t		*item_param;
	zbx_lld_item_tag_t		*item_tag;
	zbx_uint64_t			itemid;
	int				index, i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select i.itemid,i.name,i.key_,i.type,i.value_type,i.delay,"
				"i.history,i.trends,i.status,i.trapper_hosts,i.units,i.formula,"
				"i.logtimefmt,i.valuemapid,i.params,i.ipmi_sensor,i.snmp_oid,i.authtype,"
				"i.username,i.password,i.publickey,i.privatekey,i.description,i.interfaceid,"
				"i.jmx_endpoint,i.master_itemid,i.timeout,i.url,i.query_fields,"
				"i.posts,i.status_codes,i.follow_redirects,i.post_type,i.http_proxy,i.headers,"
				"i.retrieve_mode,i.request_method,i.output_format,i.ssl_cert_file,i.ssl_key_file,"
				"i.ssl_key_password,i.verify_peer,i.verify_host,i.allow_traps,i.discover"
			" from items i,item_discovery id"
			" where i.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		item_prototype = (zbx_lld_item_prototype_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_prototype_t));

		ZBX_STR2UINT64(item_prototype->itemid, row[0]);
		item_prototype->name = zbx_strdup(NULL, row[1]);
		item_prototype->key = zbx_strdup(NULL, row[2]);
		ZBX_STR2UCHAR(item_prototype->type, row[3]);
		ZBX_STR2UCHAR(item_prototype->value_type, row[4]);
		item_prototype->delay = zbx_strdup(NULL, row[5]);
		item_prototype->history = zbx_strdup(NULL, row[6]);
		item_prototype->trends = zbx_strdup(NULL, row[7]);
		ZBX_STR2UCHAR(item_prototype->status, row[8]);
		item_prototype->trapper_hosts = zbx_strdup(NULL, row[9]);
		item_prototype->units = zbx_strdup(NULL, row[10]);
		item_prototype->formula = zbx_strdup(NULL, row[11]);
		item_prototype->logtimefmt = zbx_strdup(NULL, row[12]);
		ZBX_DBROW2UINT64(item_prototype->valuemapid, row[13]);
		item_prototype->params = zbx_strdup(NULL, row[14]);
		item_prototype->ipmi_sensor = zbx_strdup(NULL, row[15]);
		item_prototype->snmp_oid = zbx_strdup(NULL, row[16]);
		ZBX_STR2UCHAR(item_prototype->authtype, row[17]);
		item_prototype->username = zbx_strdup(NULL, row[18]);
		item_prototype->password = zbx_strdup(NULL, row[19]);
		item_prototype->publickey = zbx_strdup(NULL, row[20]);
		item_prototype->privatekey = zbx_strdup(NULL, row[21]);
		item_prototype->description = zbx_strdup(NULL, row[22]);
		ZBX_DBROW2UINT64(item_prototype->interfaceid, row[23]);
		item_prototype->jmx_endpoint = zbx_strdup(NULL, row[24]);
		ZBX_DBROW2UINT64(item_prototype->master_itemid, row[25]);

		item_prototype->timeout = zbx_strdup(NULL, row[26]);
		item_prototype->url = zbx_strdup(NULL, row[27]);
		item_prototype->query_fields = zbx_strdup(NULL, row[28]);
		item_prototype->posts = zbx_strdup(NULL, row[29]);
		item_prototype->status_codes = zbx_strdup(NULL, row[30]);
		ZBX_STR2UCHAR(item_prototype->follow_redirects, row[31]);
		ZBX_STR2UCHAR(item_prototype->post_type, row[32]);
		item_prototype->http_proxy = zbx_strdup(NULL, row[33]);
		item_prototype->headers = zbx_strdup(NULL, row[34]);
		ZBX_STR2UCHAR(item_prototype->retrieve_mode, row[35]);
		ZBX_STR2UCHAR(item_prototype->request_method, row[36]);
		ZBX_STR2UCHAR(item_prototype->output_format, row[37]);
		item_prototype->ssl_cert_file = zbx_strdup(NULL, row[38]);
		item_prototype->ssl_key_file = zbx_strdup(NULL, row[39]);
		item_prototype->ssl_key_password = zbx_strdup(NULL, row[40]);
		ZBX_STR2UCHAR(item_prototype->verify_peer, row[41]);
		ZBX_STR2UCHAR(item_prototype->verify_host, row[42]);
		ZBX_STR2UCHAR(item_prototype->allow_traps, row[43]);
		ZBX_STR2UCHAR(item_prototype->discover, row[44]);

		zbx_vector_ptr_create(&item_prototype->lld_rows);
		zbx_vector_ptr_create(&item_prototype->preproc_ops);
		zbx_vector_ptr_create(&item_prototype->item_params);
		zbx_vector_ptr_create(&item_prototype->item_tags);

		zbx_vector_ptr_append(item_prototypes, item_prototype);
	}
	DBfree_result(result);

	zbx_vector_ptr_sort(item_prototypes, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	if (0 == item_prototypes->values_num)
		goto out;

	/* get item prototype preprocessing options */

	result = DBselect(
			"select ip.itemid,ip.step,ip.type,ip.params,ip.error_handler,ip.error_handler_params"
			" from item_preproc ip,item_discovery id"
			" where ip.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		preproc_op = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
		preproc_op->step = atoi(row[1]);
		preproc_op->type = atoi(row[2]);
		preproc_op->params = zbx_strdup(NULL, row[3]);
		preproc_op->error_handler = atoi(row[4]);
		preproc_op->error_handler_params = zbx_strdup(NULL, row[5]);
		zbx_vector_ptr_append(&item_prototype->preproc_ops, preproc_op);
	}
	DBfree_result(result);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_ptr_sort(&item_prototype->preproc_ops, lld_item_preproc_sort_by_step);
	}

	/* get item prototype parameters */

	result = DBselect(
			"select ip.itemid,ip.name,ip.value"
			" from item_parameter ip,item_discovery id"
			" where ip.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		item_param = (zbx_lld_item_param_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_param_t));
		item_param->name = zbx_strdup(NULL, row[1]);
		item_param->value = zbx_strdup(NULL, row[2]);
		zbx_vector_ptr_append(&item_prototype->item_params, item_param);
	}
	DBfree_result(result);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_ptr_sort(&item_prototype->item_params, lld_item_param_sort_by_name);
	}

	/* get item prototype tags */

	result = DBselect(
			"select it.itemid,it.tag,it.value"
			" from item_tag it,item_discovery id"
			" where it.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		item_tag = (zbx_lld_item_tag_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_tag_t));
		item_tag->tag = zbx_strdup(NULL, row[1]);
		item_tag->value = zbx_strdup(NULL, row[2]);
		zbx_vector_ptr_append(&item_prototype->item_tags, item_tag);
	}
	DBfree_result(result);

	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_ptr_sort(&item_prototype->item_tags, lld_item_tag_sort_by_tag);
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d prototypes", __func__, item_prototypes->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_link_dependent_items                                         *
 *                                                                            *
 * Purpose: create dependent item index in master item data                   *
 *                                                                            *
 * Parameters: items       - [IN/OUT] the lld items                           *
 *             items_index - [IN] lld item index                              *
 *                                                                            *
 ******************************************************************************/
static void	lld_link_dependent_items(zbx_vector_ptr_t *items, zbx_hashset_t *items_index)
{
	zbx_lld_item_t		*item, *master;
	zbx_lld_item_index_t	*item_index, item_index_local;
	int			i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = items->values_num - 1; i >= 0; i--)
	{
		item = (zbx_lld_item_t *)items->values[i];
		/* only discovered dependent items should be linked */
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED) || 0 == item->master_itemid)
			continue;

		item_index_local.parent_itemid = item->master_itemid;
		item_index_local.lld_row = (zbx_lld_row_t *)item->lld_row;

		if (NULL != (item_index = (zbx_lld_item_index_t *)zbx_hashset_search(items_index, &item_index_local)))
		{
			master = item_index->item;
			zbx_vector_ptr_append(&master->dependent_items, item);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_update_items                                                 *
 *                                                                            *
 * Purpose: add or update discovered items                                    *
 *                                                                            *
 * Return value: SUCCEED - if items were successfully added/updated or        *
 *                         adding/updating was not necessary                  *
 *               FAIL    - items cannot be added/updated                      *
 *                                                                            *
 ******************************************************************************/
int	lld_update_items(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *lld_rows,
		const zbx_vector_ptr_t *lld_macro_paths, char **error, int lifetime, int lastcheck)
{
	zbx_vector_ptr_t	items, item_prototypes, item_dependencies;
	zbx_hashset_t		items_index;
	int			ret = SUCCEED, host_record_is_locked = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&item_prototypes);

	lld_item_prototypes_get(lld_ruleid, &item_prototypes);

	if (0 == item_prototypes.values_num)
		goto out;

	zbx_vector_ptr_create(&items);
	zbx_hashset_create(&items_index, item_prototypes.values_num * lld_rows->values_num, lld_item_index_hash_func,
			lld_item_index_compare_func);
	//this will load from the database all the items related to the discovery rule, all, fucking _ALL_ items
	//this is done to mark unupdated items (set ts_delete)
	//however, setting ts_delete might be done right at the time when item is saved
	//and then it should be done periodically (say, once in an hour)
	//then we would be able to use the following logic: 
	//for any arrived string updating the LLD Cache ( remebering the arrived row as well as id of the objects - we can find the objects in the existing config cache)
	//if object hasn't been found - save it immediately
	//if it was found - update LLD cache for the object (ts_delete info)
	//housekeeper will delete items from the cache as well as from the database
	//in case of frequent LLD updates we'll mostly exec a couple of searches in the config memory
	//actually it's just finding the discovery info having ts_delete marked. 

	lld_items_get(&item_prototypes, &items);

	lld_items_make(&item_prototypes, lld_rows, lld_macro_paths, &items, &items_index, error);
	lld_items_preproc_make(&item_prototypes, lld_macro_paths, &items);
	lld_items_param_make(&item_prototypes, lld_macro_paths, &items);
	lld_items_tags_make(&item_prototypes, lld_macro_paths, &items);

	lld_link_dependent_items(&items, &items_index);

	zbx_vector_ptr_create(&item_dependencies);
	lld_item_dependencies_get(&item_prototypes, &item_dependencies);

	lld_items_validate(hostid, &items, &item_prototypes, &item_dependencies, error);

	DBbegin();

	if (SUCCEED == lld_items_save(hostid, &item_prototypes, &items, &items_index, &host_record_is_locked) &&
			SUCCEED == lld_items_preproc_save(hostid, &items, &host_record_is_locked) &&
			SUCCEED == lld_items_param_save(hostid, &items, &host_record_is_locked) &&
			SUCCEED == lld_items_tags_save(hostid, &items, &host_record_is_locked))
	{
		if (ZBX_DB_OK != DBcommit())
		{
			ret = FAIL;
			goto clean;
		}
	}
	else
	{
		ret = FAIL;
		DBrollback();
		goto clean;
	}

	lld_item_links_populate(&item_prototypes, lld_rows, &items_index);
	lld_remove_lost_objects("item_discovery", "itemid", &items, lifetime, lastcheck, DBdelete_items, get_item_info);
clean:
	zbx_hashset_destroy(&items_index);

	zbx_vector_ptr_clear_ext(&item_dependencies, zbx_ptr_free);
	zbx_vector_ptr_destroy(&item_dependencies);

	zbx_vector_ptr_clear_ext(&items, (zbx_clean_func_t)lld_item_free);
	zbx_vector_ptr_destroy(&items);

	zbx_vector_ptr_clear_ext(&item_prototypes, (zbx_clean_func_t)lld_item_prototype_free);
out:
	zbx_vector_ptr_destroy(&item_prototypes);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}
