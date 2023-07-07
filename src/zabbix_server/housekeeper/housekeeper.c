/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "housekeeper.h"

#include "log.h"
#include "zbxnix.h"
#include "zbxself.h"
#include "zbxserver.h"
#include "zbxrtc.h"
#include "zbxnum.h"
#include "zbxtime.h"
#include "zbx_rtc_constants.h"
#include "zbx_host_constants.h"
#include "../../libs/glb_state/glb_state.h"

static struct zbx_db_version_info_t	*db_version_info;


static int	hk_period;

#define HK_INITIAL_DELETE_QUEUE_SIZE	4096

/* the maximum number of housekeeping periods to be removed per single housekeeping cycle */
#define HK_MAX_DELETE_PERIODS		4

/* global configuration data containing housekeeping configuration */
static zbx_config_t	cfg;

/* Housekeeping rule definition.                                */
/* A housekeeping rule describes table from which records older */
/* than history setting must be removed according to optional   */
/* filter.                                                      */
typedef struct
{
	/* target table name */
	const char	*table;

	/* ID field name, required to select IDs of records that must be deleted */
	char	*field_name;

	/* Optional filter, must be empty string if not used. Only the records matching */
	/* filter are subject to housekeeping procedures.                               */
	const char	*filter;

	/* The oldest record in table (with filter in effect). The min_clock value is   */
	/* read from the database when accessed for the first time and then during      */
	/* housekeeping procedures updated to the last 'cutoff' value.                  */
	int		min_clock;

	/* a reference to the settings value specifying number of seconds the records must be kept */
	int		*phistory;
}
zbx_hk_rule_t;

/* housekeeper table => configuration data mapping.                       */
/* This structure is used to map table names used in housekeeper table to */
/* configuration data.                                                    */
typedef struct
{
	/* housekeeper table name */
	const char		*name;

	/* a reference to housekeeping configuration enable value for this table */
	unsigned char		*poption_mode;

	/* a reference to the housekeeping configuration overwrite option for this table */
	unsigned char		*poption_global;
}
zbx_hk_cleanup_table_t;

static unsigned char poption_mode_regular	= ZBX_HK_MODE_REGULAR;
static unsigned char poption_global_disabled	= ZBX_HK_OPTION_DISABLED;

/* Housekeeper table mapping to housekeeping configuration values.    */
/* This mapping is used to exclude disabled tables from housekeeping  */
/* cleanup procedure.                                                 */
static zbx_hk_cleanup_table_t	hk_cleanup_tables[] = {
//	{"history",		&cfg.hk.history_mode,	&cfg.hk.history_global},
//	{"history_log",		&cfg.hk.history_mode,	&cfg.hk.history_global},
//	{"history_str",		&cfg.hk.history_mode,	&cfg.hk.history_global},
//	{"history_text",	&cfg.hk.history_mode,	&cfg.hk.history_global},
//	{"history_uint",	&cfg.hk.history_mode,	&cfg.hk.history_global},
//	{"trends",		&cfg.hk.trends_mode,	&cfg.hk.trends_global},
//	{"trends_uint",		&cfg.hk.trends_mode,	&cfg.hk.trends_global},
	/* force events housekeeping mode on to perform problem cleanup when events housekeeping is disabled */
	{"events",		&poption_mode_regular,	&poption_global_disabled},
	{NULL}
};

/* trends table offsets in the hk_cleanup_tables[] mapping  */
#define HK_UPDATE_CACHE_OFFSET_TREND_FLOAT	ITEM_VALUE_TYPE_MAX
#define HK_UPDATE_CACHE_OFFSET_TREND_UINT	(HK_UPDATE_CACHE_OFFSET_TREND_FLOAT + 1)
#define HK_UPDATE_CACHE_TREND_COUNT		2

void DCget_unknown_triggers(int *unknown_triggers, int *not_calculated, int *total_triggers);
/* the oldest record timestamp cache for items in history tables */
typedef struct
{
	zbx_uint64_t	itemid;
	int		min_clock;
}
zbx_hk_item_cache_t;

/* Delete queue item definition.                                     */
/* The delete queue item defines an item that should be processed by */
/* housekeeping procedure (records older than min_clock seconds      */
/* must be removed from database).                                   */
typedef struct
{
	zbx_uint64_t	itemid;
	int		min_clock;
}
zbx_hk_delete_queue_t;

/* this structure is used to remove old records from history (trends) tables */
typedef struct
{
	/* the target table name */
	const char		*table;

	/* history setting field name in items table (history|trends) */
	const char		*history;

	/* a reference to the housekeeping configuration mode (enable) option for this table */
	unsigned char		*poption_mode;

	/* a reference to the housekeeping configuration overwrite option for this table */
	unsigned char		*poption_global;

	/* a reference to the housekeeping configuration history value for this table */
	int			*poption;

	/* type for checking which values are sent to the history storage */
	unsigned char		type;

	/* the oldest item record timestamp cache for target table */
	zbx_hashset_t		item_cache;

	/* the item delete queue */
	zbx_vector_ptr_t	delete_queue;
}
zbx_hk_history_rule_t;

/* The history item rules, used for housekeeping history and trends tables */
/* The order of the rules must match the order of value types in zbx_item_value_type_t. */
//static zbx_hk_history_rule_t	hk_history_rules[] = {
//	{.table = "history",		.history = "history",	.poption_mode = &cfg.hk.history_mode,
//			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
//			.type = ITEM_VALUE_TYPE_FLOAT},
	// {.table = "history_str",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
	// 		.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
	// 		.type = ITEM_VALUE_TYPE_STR},
	// {.table = "history_log",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
	// 		.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
	// 		.type = ITEM_VALUE_TYPE_LOG},
	// {.table = "history_uint",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
	// 		.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
	// 		.type = ITEM_VALUE_TYPE_UINT64},
	// {.table = "history_text",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
	// 		.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
	// 		.type = ITEM_VALUE_TYPE_TEXT},
	// {.table = "trends",		.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
	// 		.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
	// 		.type = ITEM_VALUE_TYPE_FLOAT},
	// {.table = "trends_uint",	.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
	// 		.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
	// 		.type = ITEM_VALUE_TYPE_UINT64},
//	{NULL}
//};

/******************************************************************************
 *                                                                            *
 * Purpose: compare two delete queue items by their itemid                    *
 *                                                                            *
 * Parameters: d1 - [IN] the first delete queue item to compare               *
 *             d2 - [IN] the second delete queue item to compare              *
 *                                                                            *
 * Return value: <0 - the first item is less than the second                  *
 *               >0 - the first item is greater than the second               *
 *               =0 - the items are the same                                  *
 *                                                                            *
 * Comments: this function is used to sort delete queue by itemids            *
 *                                                                            *
 ******************************************************************************/
static int	hk_item_update_cache_compare(const void *d1, const void *d2)
{
	zbx_hk_delete_queue_t	*r1 = *(zbx_hk_delete_queue_t **)d1;
	zbx_hk_delete_queue_t	*r2 = *(zbx_hk_delete_queue_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(r1->itemid, r2->itemid);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: add item to the delete queue if necessary                         *
 *                                                                            *
 * Parameters: rule        - [IN/OUT] the history housekeeping rule           *
 *             now         - [IN] the current timestamp                       *
 *             item_record - [IN/OUT] the record from item cache containing   *
 *                           item to process and its oldest record timestamp  *
 *             history     - [IN] a number of seconds the history data for    *
 *                           item_record must be kept.                        *
 *                                                                            *
 * Comments: If item is added to delete queue, its oldest record timestamp    *
 *           (min_clock) is updated to the calculated 'cutoff' value.         *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_append(zbx_hk_history_rule_t *rule, int now,
		zbx_hk_item_cache_t *item_record, int history)
{
	int	keep_from;

	if (history > now)
		return;	/* there shouldn't be any records with negative timestamps, nothing to do */

	keep_from = now - history;

	if (keep_from > item_record->min_clock)
	{
		zbx_hk_delete_queue_t	*update_record;

		/* update oldest timestamp in item cache */
		item_record->min_clock = MIN(keep_from, item_record->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		update_record = (zbx_hk_delete_queue_t *)zbx_malloc(NULL, sizeof(zbx_hk_delete_queue_t));
		update_record->itemid = item_record->itemid;
		update_record->min_clock = item_record->min_clock;
		zbx_vector_ptr_append(&rule->delete_queue, update_record);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: prepares history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule        - [IN/OUT] the history housekeeping rule           *
 *                                                                            *
 * Comments: This function is called to initialize history rule data either   *
 *           at start or when housekeeping is enabled for this rule.          *
 *           It caches item history data and also prepares delete queue to be *
 *           processed during the first run.                                  *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_prepare(zbx_hk_history_rule_t *rule)
{
	DB_RESULT	result;
	DB_ROW		row;

	zbx_hashset_create(&rule->item_cache, 1024, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_ptr_create(&rule->delete_queue);
	zbx_vector_ptr_reserve(&rule->delete_queue, HK_INITIAL_DELETE_QUEUE_SIZE);

	result = zbx_db_select("select itemid,min(clock) from %s group by itemid", rule->table);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t		itemid;
		int			min_clock;
		zbx_hk_item_cache_t	item_record;

		ZBX_STR2UINT64(itemid, row[0]);
		min_clock = atoi(row[1]);

		item_record.itemid = itemid;
		item_record.min_clock = min_clock;

		zbx_hashset_insert(&rule->item_cache, &item_record, sizeof(zbx_hk_item_cache_t));
	}

	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: releases history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule  - [IN/OUT] the history housekeeping rule                 *
 *                                                                            *
 * Comments: This function is called to release resources allocated by        *
 *           history housekeeping rule after housekeeping was disabled        *
 *           for the table referred by this rule.                             *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_release(zbx_hk_history_rule_t *rule)
{
	if (0 == rule->item_cache.num_slots)
		return;

	zbx_hashset_destroy(&rule->item_cache);
	zbx_vector_ptr_destroy(&rule->delete_queue);
}

/******************************************************************************
 *                                                                            *
 * Purpose: updates history housekeeping rule with item history setting and   *
 *          adds item to the delete queue if necessary                        *
 *                                                                            *
 * Parameters: rule    - [IN/OUT] the history housekeeping rule               *
 *             now     - [IN] the current timestamp                           *
 *             itemid  - [IN] the item to update                              *
 *             history - [IN] the number of seconds the item data             *
 *                       should be kept in history                            *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_item_update(zbx_hk_history_rule_t *rules, zbx_hk_history_rule_t *rule_add, int count,
		int now, zbx_uint64_t itemid, int history)
{
	zbx_hk_history_rule_t	*rule;

	/* item can be cached in multiple rules when value type has been changed */
	for (rule = rules; rule - rules < count; rule++)
	{
		zbx_hk_item_cache_t	*item_record;

		if (0 == rule->item_cache.num_slots)
			continue;

		if (NULL == (item_record = (zbx_hk_item_cache_t *)zbx_hashset_search(&rule->item_cache, &itemid)))
		{
			zbx_hk_item_cache_t	item_data = {itemid, now};

			if (rule_add != rule)
				continue;

			if (NULL == (item_record = (zbx_hk_item_cache_t *)zbx_hashset_insert(&rule->item_cache,
					&item_data, sizeof(zbx_hk_item_cache_t))))
			{
				continue;
			}
		}

		hk_history_delete_queue_append(rule, now, item_record, history);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: updates history housekeeping rule with the latest item history    *
 *          settings and prepares delete queue                                *
 *                                                                            *
 * Parameters: rule  - [IN/OUT] the history housekeeping rule                 *
 *             now   - [IN] the current timestamp                             *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_update(zbx_hk_history_rule_t *rules, int now)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*tmp = NULL;
	zbx_dc_um_handle_t	*um_handle;

	result = zbx_db_select(
			"select i.itemid,i.value_type,i.history,i.trends,h.hostid"
			" from items i,hosts h"
			" where i.flags in (%d,%d)"
				" and i.hostid=h.hostid"
				" and h.status in (%d,%d)",
			ZBX_FLAG_DISCOVERY_NORMAL, ZBX_FLAG_DISCOVERY_CREATED,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

	um_handle = zbx_dc_open_user_macros();

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t		itemid, hostid;
		int			history, trends, value_type;
		zbx_hk_history_rule_t	*rule;

		ZBX_STR2UINT64(itemid, row[0]);
		value_type = atoi(row[1]);
		ZBX_STR2UINT64(hostid, row[4]);

		if (value_type < ITEM_VALUE_TYPE_MAX &&
				ZBX_HK_MODE_REGULAR == *(rule = rules + value_type)->poption_mode)
		{
			tmp = zbx_strdup(tmp, row[2]);
			zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, NULL, NULL,
					NULL, &tmp, MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != zbx_is_time_suffix(tmp, &history, ZBX_LENGTH_UNLIMITED))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}

			if (0 != history && (ZBX_HK_HISTORY_MIN > history || ZBX_HK_PERIOD_MAX < history))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != history && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				history = *rule->poption;

			hk_history_item_update(rules, rule, ITEM_VALUE_TYPE_MAX, now, itemid, history);
		}

		if (ITEM_VALUE_TYPE_FLOAT == value_type || ITEM_VALUE_TYPE_UINT64 == value_type)
		{
			rule = rules + (value_type == ITEM_VALUE_TYPE_FLOAT ?
					HK_UPDATE_CACHE_OFFSET_TREND_FLOAT : HK_UPDATE_CACHE_OFFSET_TREND_UINT);

			if (ZBX_HK_MODE_REGULAR != *rule->poption_mode)
				continue;

			tmp = zbx_strdup(tmp, row[3]);
			zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, NULL, NULL,
					NULL, &tmp, MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != zbx_is_time_suffix(tmp, &trends, ZBX_LENGTH_UNLIMITED))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}
			else if (0 != trends && (ZBX_HK_TRENDS_MIN > trends || ZBX_HK_PERIOD_MAX < trends))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != trends && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				trends = *rule->poption;

			hk_history_item_update(rules + HK_UPDATE_CACHE_OFFSET_TREND_FLOAT, rule,
					HK_UPDATE_CACHE_TREND_COUNT, now, itemid, trends);
		}
	}
	zbx_db_free_result(result);

	zbx_dc_close_user_macros(um_handle);

	zbx_free(tmp);
}

/******************************************************************************
 *                                                                            *
 * Purpose: prepares history housekeeping delete queues for all defined       *
 *          history rules.                                                    *
 *                                                                            *
 * Parameters: rules  - [IN/OUT] the history housekeeping rules               *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 * Comments: This function also handles history rule initializing/releasing   *
 *           when the rule just became enabled/disabled.                      *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_prepare_all(zbx_hk_history_rule_t *rules, int now)
{
	zbx_hk_history_rule_t	*rule;
	unsigned char		items_update = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* prepare history item cache (hashset containing itemid:min_clock values) */
	for (rule = rules; NULL != rule->table; rule++)
	{
		if (ZBX_HK_MODE_REGULAR == *rule->poption_mode)
		{
			if (0 == rule->item_cache.num_slots)
				hk_history_prepare(rule);

			items_update = 1;
		}
		else if (0 != rule->item_cache.num_slots)
			hk_history_release(rule);
	}

	/* Since we maintain two separate global period settings - for history and for trends */
	/* we need to scan items table if either of these is off. Thus setting both global periods */
	/* to override is very beneficial for performance. */
	if (0 != items_update)
		hk_history_update(rules, now);	/* scan items and update min_clock using per item settings */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: clears the history housekeeping delete queue                      *
 *                                                                            *
 * Parameters: rule   - [IN/OUT] the history housekeeping rule                *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 ******************************************************************************/
static void	hk_history_delete_queue_clear(zbx_hk_history_rule_t *rule)
{
	zbx_vector_ptr_clear_ext(&rule->delete_queue, zbx_ptr_free);
}

/******************************************************************************
 *                                                                            *
 * Purpose: drop appropriate partitions from the history and trends tables    *
 *                                                                            *
 * Parameters: rules - [IN/OUT] history housekeeping rules                    *
 *             now   - [IN] the current timestamp                             *
 *                                                                            *
 ******************************************************************************/

#if defined(HAVE_POSTGRESQL)

static void 	hk_update_dbversion_status(void)
{
	struct zbx_json	db_version_json;

	zbx_json_initarray(&db_version_json, ZBX_JSON_STAT_BUF_LEN);

	zbx_db_version_json_create(&db_version_json, db_version_info);
	zbx_db_flush_version_requirements(db_version_json.buffer);

	zbx_json_free(&db_version_json);
}

#endif

/******************************************************************************
 *                                                                            *
 * Purpose: removes old records from a table according to the specified rule  *
 *                                                                            *
 * Parameters: now  - [IN] the current time in seconds                        *
 *             rule - [IN/OUT] the housekeeping rule specifying table to      *
 *                    clean and the required data (fields, filters, time)     *
 *                                                                            *
 * Return value: the number of deleted records                                *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_process_rule(int now, zbx_hk_rule_t *rule)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		keep_from, id_field_str_type, deleted = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' field_name:'%s' filter:'%s' min_clock:%d now:%d",
			__func__, rule->table, rule->field_name, rule->filter, rule->min_clock, now);

	/* NOTE: Do not forget to add here tables whose id column is string-type.                    */
	/* Now only audit field has string id, if in the future this list of exceptions is increased */
	/* DBget_table() and DBget_field() functions with cache could be used to determine if string */
	/* or int version is required.                                                               */
	id_field_str_type = (0 == strcmp("auditid", rule->field_name)) ? 1 : 0;

	/* initialize min_clock with the oldest record timestamp from database */
	if (0 == rule->min_clock)
	{
		result = zbx_db_select("select min(clock) from %s%s%s", rule->table,
				('\0' != *rule->filter ? " where " : ""), rule->filter);
		if (NULL != (row = zbx_db_fetch(result)) && SUCCEED != zbx_db_is_null(row[0]))
			rule->min_clock = atoi(row[0]);
		else
			rule->min_clock = now;

		zbx_db_free_result(result);
	}

	/* Delete the old records from database. Don't remove more than 4 x housekeeping */
	/* periods worth of data to prevent database stalling.                           */
	keep_from = now - *rule->phistory;
	if (keep_from > rule->min_clock)
	{
		char			buffer[MAX_STRING_LEN];
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset;
		zbx_vector_uint64_t	ids_uint64;
		zbx_vector_str_t	ids_str;
		int			ret;

		if (0 == id_field_str_type)
			zbx_vector_uint64_create(&ids_uint64);
		else
			zbx_vector_str_create(&ids_str);

		rule->min_clock = MIN(keep_from, rule->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		zbx_snprintf(buffer, sizeof(buffer),
			"select %s"
			" from %s"
			" where clock<%d%s%s"
			" order by %s",
			rule->field_name, rule->table, rule->min_clock, '\0' != *rule->filter ? " and " : "",
			rule->filter, rule->field_name);

		while (1)
		{
			/* Select IDs of records that must be deleted, this allows to avoid locking for every   */
			/* record the search encounters when using delete statement, thus eliminates deadlocks. */
			if (0 == CONFIG_MAX_HOUSEKEEPER_DELETE)
				result = zbx_db_select("%s", buffer);
			else
				result = zbx_db_select_n(buffer, CONFIG_MAX_HOUSEKEEPER_DELETE);

			while (NULL != (row = zbx_db_fetch(result)))
			{
				if (0 == id_field_str_type)
				{
					zbx_uint64_t	id;

					ZBX_STR2UINT64(id, row[0]);
					zbx_vector_uint64_append(&ids_uint64, id);
				}
				else
					zbx_vector_str_append(&ids_str, zbx_strdup(NULL, row[0]));
			}

			zbx_db_free_result(result);

			if (0 == id_field_str_type)
			{
				if (0 == ids_uint64.values_num)
					break;
			}
			else
			{
				if (0 == ids_str.values_num)
					break;
			}

			sql_offset = 0;
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where", rule->table);

			if (0 == id_field_str_type)
			{
				zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, rule->field_name,
						ids_uint64.values, ids_uint64.values_num);
			}
			else
			{
				zbx_db_add_str_condition_alloc(&sql, &sql_alloc, &sql_offset, rule->field_name,
						(const char**)ids_str.values, ids_str.values_num);
			}

			ret = zbx_db_execute("%s", sql);

			if (0 == id_field_str_type)
				zbx_vector_uint64_clear(&ids_uint64);
			else
				zbx_vector_str_clear_ext(&ids_str, zbx_str_free);

			if (ZBX_DB_OK > ret)
				break;

			deleted += ret;
		}

		zbx_free(sql);

		if (0 == id_field_str_type)
			zbx_vector_uint64_destroy(&ids_uint64);
		else
			zbx_vector_str_destroy(&ids_str);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

/******************************************************************************
 *                                                                            *
 * Purpose: delete limited count of rows from table                           *
 *                                                                            *
 * Return value: number of deleted rows or less than 0 if an error occurred   *
 *                                                                            *
 ******************************************************************************/
static int	DBdelete_from_table(const char *tablename, const char *filter, int limit)
{
	if (0 == limit)
	{
		return zbx_db_execute(
				"delete from %s"
				" where %s",
				tablename,
				filter);
	}
	else
	{
#if defined(HAVE_ORACLE)
		return zbx_db_execute(
				"delete from %s"
				" where %s"
					" and rownum<=%d",
				tablename,
				filter,
				limit);
#elif defined(HAVE_MYSQL)
		return zbx_db_execute(
				"delete from %s"
				" where %s limit %d",
				tablename,
				filter,
				limit);
#elif defined(HAVE_POSTGRESQL)
		return zbx_db_execute(
				"delete from %s"
				" where %s and ctid = any(array(select ctid from %s"
					" where %s limit %d))",
				tablename,
				filter,
				tablename,
				filter,
				limit);
#elif defined(HAVE_SQLITE3)
		return zbx_db_execute(
				"delete from %s"
				" where %s",
				tablename,
				filter);
#endif
	}

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: perform problem table cleanup                                     *
 *                                                                            *
 * Parameters: table    - [IN] the problem table name                         *
 * Parameters: source   - [IN] the event source                               *
 *             object   - [IN] the event object type                          *
 *             objectid - [IN] the event object identifier                    *
 *             more     - [OUT] 1 if there might be more data to remove,      *
 *                              otherwise the value is not changed            *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 ******************************************************************************/
static int	hk_problem_cleanup(const char *table, int source, int object, zbx_uint64_t objectid, int *more)
{
	char	filter[MAX_STRING_LEN];
	int	ret;

	zbx_snprintf(filter, sizeof(filter), "source=%d and object=%d and objectid=" ZBX_FS_UI64,
			source, object, objectid);

	ret = DBdelete_from_table(table, filter, CONFIG_MAX_HOUSEKEEPER_DELETE);

	if (ZBX_DB_OK > ret || (0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE))
		*more = 1;

	return ZBX_DB_OK <= ret ? ret : 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: perform generic table cleanup                                     *
 *                                                                            *
 * Parameters: table    - [IN] the table name                                 *
 *             field    - [IN] the field name                                 *
 *             objectid - [IN] the field value                                *
 *             more     - [OUT] 1 if there might be more data to remove,      *
 *                              otherwise the value is not changed            *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 ******************************************************************************/
static int	hk_table_cleanup(const char *table, const char *field, zbx_uint64_t id, int *more)
{
	char	filter[MAX_STRING_LEN];
	int	ret;

	zbx_snprintf(filter, sizeof(filter), "%s=" ZBX_FS_UI64, field, id);

	ret = DBdelete_from_table(table, filter, CONFIG_MAX_HOUSEKEEPER_DELETE);

	if (ZBX_DB_OK > ret || (0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE))
		*more = 1;

	return ZBX_DB_OK <= ret ? ret : 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove deleted items/triggers data                                *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 * Comments: sqlite3 does not use CONFIG_MAX_HOUSEKEEPER_DELETE, deletes all  *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_cleanup(void)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			deleted = 0;
	zbx_vector_uint64_t	housekeeperids;
	char			*sql = NULL, *table_name_esc;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_hk_cleanup_table_t *table;
	zbx_uint64_t		housekeeperid, objectid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&housekeeperids);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select housekeeperid,tablename,field,value"
			" from housekeeper"
			" where tablename in (");

	/* assemble list of tables included in the housekeeping procedure */
	for (table = hk_cleanup_tables; NULL != table->name; table++)
	{
		if (ZBX_HK_MODE_REGULAR != *table->poption_mode)
			continue;

		table_name_esc = zbx_db_dyn_escape_string(table->name);

		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table_name_esc);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "',");

		zbx_free(table_name_esc);
	}
	sql_offset--;

	/* order by tablename to effectively use DB cache */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ") order by tablename");

	result = zbx_db_select("%s", sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		int	more = 0;

		ZBX_STR2UINT64(housekeeperid, row[0]);
		ZBX_STR2UINT64(objectid, row[3]);

		if (0 == strcmp(row[1], "events")) /* events name is used for backwards compatibility with frontend */
		{
			const char	*table_name = "problem";

			if (0 == strcmp(row[2], "triggerid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "itemid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "lldruleid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "serviceid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_SERVICE, EVENT_OBJECT_SERVICE,
						objectid, &more);
			}
		}
		else
			deleted += hk_table_cleanup(row[1], row[2], objectid, &more);

		if (0 == more)
			zbx_vector_uint64_append(&housekeeperids, housekeeperid);
	}
	zbx_db_free_result(result);

	if (0 != housekeeperids.values_num)
	{
		zbx_vector_uint64_sort(&housekeeperids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_db_execute_multiple_query("delete from housekeeper where", "housekeeperid", &housekeeperids);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&housekeeperids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_sessions(int now)
{
	int	deleted = 0, rc;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	if (ZBX_HK_OPTION_ENABLED == cfg.hk.sessions_mode)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "lastaccess<%d", now - cfg.hk.sessions);
		rc = DBdelete_from_table("sessions", sql, CONFIG_MAX_HOUSEKEEPER_DELETE);
		zbx_free(sql);

		if (ZBX_DB_OK <= rc)
			deleted = rc;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_services(int now)
{
	static zbx_hk_rule_t	rule = {"service_alarms", "servicealarmid", "", 0, &cfg.hk.services};

	if (ZBX_HK_OPTION_ENABLED == cfg.hk.services_mode)
		return housekeeping_process_rule(now, &rule);

	return 0;
}

static int	housekeeping_audit(int now)
{
	static zbx_hk_rule_t	rule = {"auditlog", "auditid", "", 0, &cfg.hk.audit};

	if (ZBX_HK_OPTION_ENABLED == cfg.hk.audit_mode)
		return housekeeping_process_rule(now, &rule);

	return 0;
}

static int	housekeeping_events(int now)
{
#define ZBX_HK_EVENT_RULE		" and not exists(" \
						"select null" \
						" from problem" \
						" where events.eventid=problem.eventid" \
					")" \
					" and not exists(" \
						"select null" \
						" from problem" \
						" where events.eventid=problem.r_eventid" \
					")"
#define ZBX_HK_TRIGGER_EVENT_RULE	" and not exists(" \
						"select null" \
						" from event_symptom" \
						" where events.eventid=event_symptom.cause_eventid" \
					")"

	static zbx_hk_rule_t	rules[] = {
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_TRIGGERS)
			" and events.object=" ZBX_STR(EVENT_OBJECT_TRIGGER)
			ZBX_HK_EVENT_RULE ZBX_HK_TRIGGER_EVENT_RULE, 0, &cfg.hk.events_trigger},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" ZBX_STR(EVENT_OBJECT_TRIGGER)
			ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" ZBX_STR(EVENT_OBJECT_ITEM)
			ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_INTERNAL)
			" and events.object=" ZBX_STR(EVENT_OBJECT_LLDRULE)
			ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_DISCOVERY)
			" and events.object=" ZBX_STR(EVENT_OBJECT_DHOST), 0, &cfg.hk.events_discovery},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_DISCOVERY)
			" and events.object=" ZBX_STR(EVENT_OBJECT_DSERVICE), 0, &cfg.hk.events_discovery},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_AUTOREGISTRATION)
			" and events.object=" ZBX_STR(EVENT_OBJECT_ZABBIX_ACTIVE), 0, &cfg.hk.events_autoreg},
		{"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_SERVICE)
			" and events.object=" ZBX_STR(EVENT_OBJECT_SERVICE)
			ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_service},
		{NULL}
	};

	int		deleted = 0;
	zbx_hk_rule_t	*rule;

	if (ZBX_HK_OPTION_ENABLED != cfg.hk.events_mode)
		return 0;

	for (rule = rules; NULL != rule->table; rule++)
		deleted += housekeeping_process_rule(now, rule);

	return deleted;
#undef ZBX_HK_EVENT_RULE
#undef ZBX_HK_TRIGGER_EVENT_RULE
}

static int	housekeeping_problems(int now)
{
	int			deleted = 0, rc;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	ids_uint64;
	size_t			sql_alloc = 0, sql_offset;
	char			*sql = NULL;
	char			buffer[MAX_STRING_LEN];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	zbx_vector_uint64_create(&ids_uint64);

	zbx_snprintf(buffer, sizeof(buffer),
		"select p1.eventid from problem p1"
		" where p1.r_clock<>0 and p1.r_clock<%d and p1.eventid not in ("
			" select cause_eventid"
			" from problem p2"
			" where p1.eventid=p2.cause_eventid"
		")", now - SEC_PER_DAY);

	while (1)
	{
		if (0 == CONFIG_MAX_HOUSEKEEPER_DELETE)
			result = zbx_db_select("%s", buffer);
		else
			result = zbx_db_select_n(buffer, CONFIG_MAX_HOUSEKEEPER_DELETE);

		while (NULL != (row = zbx_db_fetch(result)))
		{
			zbx_uint64_t	id;

			ZBX_STR2UINT64(id, row[0]);
			zbx_vector_uint64_append(&ids_uint64, id);
		}

		zbx_db_free_result(result);

		if (0 == ids_uint64.values_num)
			break;

		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from problem where");
		zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", ids_uint64.values,
				ids_uint64.values_num);

		rc = zbx_db_execute("%s", sql);

		zbx_vector_uint64_clear(&ids_uint64);

		if (ZBX_DB_OK > rc)
			break;

		deleted += rc;
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&ids_uint64);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	housekeeping_proxy_dhistory(int now)
{
	int	deleted = 0, rc;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __func__, now);

	rc = zbx_db_execute("delete from proxy_dhistory where clock<%d", now - SEC_PER_DAY);

	if (ZBX_DB_OK <= rc)
		deleted = rc;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, deleted);

	return deleted;
}

static int	get_housekeeping_period(double time_slept)
{
	if (SEC_PER_HOUR > time_slept)
		return SEC_PER_HOUR;
	else if (24 * SEC_PER_HOUR < time_slept)
		return 24 * SEC_PER_HOUR;
	else
		return (int)time_slept;
}

ZBX_THREAD_ENTRY(housekeeper_thread, args)
{
	zbx_thread_housekeeper_args	*housekeeper_args_in = (zbx_thread_housekeeper_args *)
							(((zbx_thread_args_t *)args)->args);
	int				now, d_history_and_trends, d_cleanup, d_events, d_problems, d_sessions,
					d_services, d_audit, sleeptime, records;
	double				sec, time_slept, time_now;
	char				sleeptext[25];
	zbx_ipc_async_socket_t		rtc;
	const zbx_thread_info_t	*info = &((zbx_thread_args_t *)args)->info;
	int			server_num = ((zbx_thread_args_t *)args)->info.server_num;
	int			process_num = ((zbx_thread_args_t *)args)->info.process_num;
	unsigned char		process_type = ((zbx_thread_args_t *)args)->info.process_type;

	db_version_info = housekeeper_args_in->db_version_info;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(info->program_type),
			server_num, get_process_type_string(process_type), process_num);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		sleeptime = ZBX_IPC_WAIT_FOREVER;
		zbx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		zbx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		zbx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		zbx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	zbx_rtc_subscribe(process_type, process_num, housekeeper_args_in->config_timeout, &rtc);

#if defined(HAVE_POSTGRESQL)
	zbx_db_connect(ZBX_DB_CONNECT_NORMAL);

	zbx_db_close();

#endif

	while (ZBX_IS_RUNNING())
	{
		zbx_uint32_t	rtc_cmd;
		unsigned char	*rtc_data;
		int		hk_execute = 0;

		sec = zbx_time();

		while (SUCCEED == zbx_rtc_wait(&rtc, info, &rtc_cmd, &rtc_data, sleeptime) && 0 != rtc_cmd)
		{
			switch (rtc_cmd)
			{
				case ZBX_RTC_HOUSEKEEPER_EXECUTE:
					if (0 == hk_execute)
					{
						zabbix_log(LOG_LEVEL_WARNING, "forced execution of the housekeeper");
						hk_execute = 1;
					}
					else
						zabbix_log(LOG_LEVEL_WARNING, "housekeeping procedure is already in"
								" progress");
					break;
				case ZBX_RTC_SHUTDOWN:
					goto out;
				default:
					continue;
			}

			sleeptime = 0;
		}

		if (!ZBX_IS_RUNNING())
			break;

		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = ZBX_IPC_WAIT_FOREVER;
		else
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;

		time_now = zbx_time();
		time_slept = time_now - sec;
		zbx_update_env(get_process_type_string(process_type), time_now);

		hk_period = get_housekeeping_period(time_slept);

		zabbix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		now = time(NULL);

		zbx_setproctitle("%s [Housekeeping state objects in memory]", get_process_type_string(process_type));
		glb_state_housekeep();

		zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
		zbx_db_connect(ZBX_DB_CONNECT_NORMAL);

		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_HOUSEKEEPER | ZBX_CONFIG_FLAGS_DB_EXTENSION);

		zbx_setproctitle("%s [removing old problems]", get_process_type_string(process_type));
		d_problems = housekeeping_problems(now);

		zbx_setproctitle("%s [removing old events]", get_process_type_string(process_type));
		d_events = housekeeping_events(now);

		zbx_setproctitle("%s [removing old sessions]", get_process_type_string(process_type));
		d_sessions = housekeeping_sessions(now);

		zbx_setproctitle("%s [removing old service alarms]", get_process_type_string(process_type));
		d_services = housekeeping_services(now);

		zbx_setproctitle("%s [removing old audit log items]", get_process_type_string(process_type));
		d_audit = housekeeping_audit(now);

		zbx_setproctitle("%s [removing old records]", get_process_type_string(process_type));
		records = housekeeping_proxy_dhistory(now);

		zbx_setproctitle("%s [removing deleted items data]", get_process_type_string(process_type));
		d_cleanup = housekeeping_cleanup();
		sec = zbx_time() - sec;

		zabbix_log(LOG_LEVEL_WARNING, "%s [deleted %d hist/trends, %d items/triggers, %d events, %d problems,"
				" %d sessions, %d alarms, %d audit, %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_problems, d_sessions, d_services, d_audit, records, sec, sleeptext);

		zbx_config_clean(&cfg);

		zbx_db_close();

		zbx_dc_cleanup_sessions();

		zbx_setproctitle("%s [deleted %d hist/trends, %d items/triggers, %d events, %d sessions, %d alarms,"
				" %d audit items, %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_sessions, d_services, d_audit, records, sec, sleeptext);
	}
out:
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
