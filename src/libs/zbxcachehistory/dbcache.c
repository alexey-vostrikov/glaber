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

#include "zbxcachehistory.h"

#include "log.h"
#include "zbxmutexs.h"
#include "zbxserver.h"
#include "glb_events.h"
#include "zbxmodules.h"
#include "module.h"
#include "zbxexport.h"
#include "zbxnix.h"
#include "zbxavailability.h"
#include "zbxtrends.h"
#include "zbxnum.h"
#include "zbxsysinfo.h"
#include "zbx_host_constants.h"
#include "zbx_trigger_constants.h"
#include "zbx_item_constants.h"
#include "../glb_state/glb_state_items.h"
#include "../glb_state/glb_state_triggers.h"
#include "glb_preproc.h"
#include "metric.h"
#include "../zabbix_server/dbsyncer/trends.h"
#include "zbxconnector.h"

static zbx_shmem_info_t *hc_index_mem = NULL;
//static zbx_shmem_info_t *hc_mem = NULL;
//static zbx_shmem_info_t *trend_mem = NULL;

//#define LOCK_CACHE zbx_mutex_lock(cache_lock)
//#define UNLOCK_CACHE zbx_mutex_unlock(cache_lock)
//#define LOCK_TRENDS zbx_mutex_lock(trends_lock)
//#define UNLOCK_TRENDS zbx_mutex_unlock(trends_lock)
#define LOCK_CACHE_IDS zbx_mutex_lock(cache_ids_lock)
#define UNLOCK_CACHE_IDS zbx_mutex_unlock(cache_ids_lock)

//static zbx_mutex_t cache_lock = ZBX_MUTEX_NULL;
//static zbx_mutex_t trends_lock = ZBX_MUTEX_NULL;
static zbx_mutex_t cache_ids_lock = ZBX_MUTEX_NULL;

static char *sql = NULL;
static size_t sql_alloc = 4 * ZBX_KIBIBYTE;

extern unsigned char program_type;
extern int CONFIG_DOUBLE_PRECISION;

#define ZBX_IDS_SIZE 10

#define ZBX_HC_ITEMS_INIT_SIZE 1000

#define ZBX_TRENDS_CLEANUP_TIME (SEC_PER_MIN * 55)

/* the maximum time spent synchronizing history */
#define ZBX_HC_SYNC_TIME_MAX 10

/* the maximum number of items in one synchronization batch */
#define ZBX_HC_SYNC_MAX 1000
#define ZBX_HC_TIMER_MAX (ZBX_HC_SYNC_MAX / 2)
#define ZBX_HC_TIMER_SOFT_MAX (ZBX_HC_TIMER_MAX - 10)

/* the minimum processed item percentage of item candidates to continue synchronizing */
#define ZBX_HC_SYNC_MIN_PCNT 10

/* the maximum number of characters for history cache values */
#define ZBX_HISTORY_VALUE_LEN (1024 * 64)

#define ZBX_DC_FLAGS_NOT_FOR_HISTORY (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)
#define ZBX_DC_FLAGS_NOT_FOR_TRENDS (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOTRENDS)
#define ZBX_DC_FLAGS_NOT_FOR_MODULES (ZBX_DC_FLAGS_NOT_FOR_HISTORY | ZBX_DC_FLAG_LLD)
#define ZBX_DC_FLAGS_NOT_FOR_EXPORT (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF)

#define ZBX_HC_PROXYQUEUE_STATE_NORMAL 0
#define ZBX_HC_PROXYQUEUE_STATE_WAIT 1

typedef struct
{
	char table_name[ZBX_TABLENAME_LEN_MAX];
	zbx_uint64_t lastid;
} ZBX_DC_ID;

typedef struct
{
	ZBX_DC_ID id[ZBX_IDS_SIZE];
} ZBX_DC_IDS;

static ZBX_DC_IDS *ids = NULL;

typedef struct
{
	zbx_list_t list;
	zbx_hashset_t index;
	int state;
} zbx_hc_proxyqueue_t;

/* local history cache */
#define ZBX_MAX_VALUES_LOCAL 256
#define ZBX_STRUCT_REALLOC_STEP 8
#define ZBX_STRING_REALLOC_STEP ZBX_KIBIBYTE

#define GLB_MIN_FLUSH_VALUES 32768
#define GLB_MAX_FLUSH_TIMEOUT 3

typedef struct
{
	size_t pvalue;
	size_t len;
} dc_value_str_t;

typedef struct
{
	double value_dbl;
	zbx_uint64_t value_uint;
	dc_value_str_t value_str;
} dc_value_t;

typedef struct
{
	zbx_uint64_t itemid;
	dc_value_t value;
	zbx_timespec_t ts;
	dc_value_str_t source; /* for log items only */
	zbx_uint64_t lastlogsize;
	int timestamp;	/* for log items only */
	int severity;	/* for log items only */
	int logeventid; /* for log items only */
	int mtime;
	unsigned char item_value_type;
	unsigned char value_type;
	unsigned char state;
	unsigned char flags; /* see ZBX_DC_FLAG_* above */
} dc_item_value_t;

static char *string_values = NULL;
static size_t string_values_alloc = 0, string_values_offset = 0;
static dc_item_value_t *item_values = NULL;
static size_t item_values_alloc = 0, item_values_num = 0;

ZBX_PTR_VECTOR_DECL(item_tag, zbx_tag_t)
ZBX_PTR_VECTOR_IMPL(item_tag, zbx_tag_t)
ZBX_PTR_VECTOR_IMPL(tags, zbx_tag_t *)

/******************************************************************************
 *                                                                            *
 * Purpose: update trends cache and get list of trends to flush into database *
 *                                                                            *
 * Parameters: history         - [IN]  array of history data                  *
 *             history_num     - [IN]  number of history structures           *
 *             trends          - [OUT] list of trends to flush into database  *
 *             trends_num      - [OUT] number of trends                       *
 *             compression_age - [IN]  history compression age                *
 *                                                                            *
 ******************************************************************************/
static void DCmass_update_trends(const ZBX_DC_HISTORY *history, int history_num)
{
	int i; 

	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY *h = &history[i];

		if (0 != (ZBX_DC_FLAGS_NOT_FOR_TRENDS & h->flags))
			continue;

		//LOG_INF("Updating trends");
		trends_account_metric(h);
		//LOG_INF("Trend updated");
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

typedef struct
{
	zbx_uint64_t hostid;
	zbx_vector_ptr_t groups;
} zbx_host_info_t;

/******************************************************************************
 *                                                                            *
 * Purpose: frees resources allocated to store host groups names              *
 *                                                                            *
 * Parameters: host_info - [IN] host information                              *
 *                                                                            *
 ******************************************************************************/
static void zbx_host_info_clean(zbx_host_info_t *host_info)
{
	zbx_vector_ptr_clear_ext(&host_info->groups, zbx_ptr_free);
	zbx_vector_ptr_destroy(&host_info->groups);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get hosts groups names                                            *
 *                                                                            *
 * Parameters: hosts_info - [IN/OUT] output names of host groups for a host   *
 *             hostids    - [IN] hosts identifiers                            *
 *                                                                            *
 ******************************************************************************/
static void db_get_hosts_info_by_hostid(zbx_hashset_t *hosts_info, const zbx_vector_uint64_t *hostids)
{
	int i;
	size_t sql_offset = 0;
	DB_RESULT result;
	DB_ROW row;

	for (i = 0; i < hostids->values_num; i++)
	{
		zbx_host_info_t host_info = {.hostid = hostids->values[i]};

		zbx_vector_ptr_create(&host_info.groups);
		zbx_hashset_insert(hosts_info, &host_info, sizeof(host_info));
	}

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					   "select distinct hg.hostid,g.name"
					   " from hstgrp g,hosts_groups hg"
					   " where g.groupid=hg.groupid"
					   " and");

	zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids->values, hostids->values_num);

	result = zbx_db_select("%s", sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t hostid;
		zbx_host_info_t *host_info;

		ZBX_DBROW2UINT64(hostid, row[0]);

		if (NULL == (host_info = (zbx_host_info_t *)zbx_hashset_search(hosts_info, &hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_ptr_append(&host_info->groups, zbx_strdup(NULL, row[1]));
	}
	zbx_db_free_result(result);
}

typedef struct
{
	zbx_uint64_t itemid;
	char *name;
	zbx_history_sync_item_t *item;
	zbx_vector_tags_t	item_tags;
} zbx_item_info_t;

/******************************************************************************
 *                                                                            *
 * Purpose: get item names                                                    *
 *                                                                            *
 * Parameters: items_info - [IN/OUT] output item names                        *
 *             itemids    - [IN] the item identifiers                         *
 *                                                                            *
 ******************************************************************************/
static void	db_get_item_names_by_itemid(zbx_hashset_t *items_info, const zbx_vector_uint64_t *itemids)
{
	size_t		sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,name from items where");
	zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);

	result = zbx_db_select("%s", sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t	itemid;
		zbx_item_info_t	*item_info;

		ZBX_DBROW2UINT64(itemid, row[0]);

		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_info->name = zbx_strdup(item_info->name, row[1]);
	}

	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get item tags                                                     *
 *                                                                            *
 * Parameters: items_info - [IN/OUT] output item tags                         *
 *             itemids    - [IN] the item identifiers                         *
 *                                                                            *
 ******************************************************************************/
static void	db_get_item_tags_by_itemid(zbx_hashset_t *items_info, const zbx_vector_uint64_t *itemids)
{
	size_t		sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;
	zbx_item_info_t	*item_info = NULL;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,tag,value from item_tag where");
	zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by itemid");

	result = zbx_db_select("%s", sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t	itemid;
		zbx_tag_t	*item_tag;

		ZBX_DBROW2UINT64(itemid, row[0]);

		if (NULL == item_info || item_info->itemid != itemid)
		{
			if (NULL != item_info)
			{
				zbx_vector_tags_sort(&item_info->item_tags, zbx_compare_tags);
			}
			if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &itemid)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}
		}

		item_tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(*item_tag));
		item_tag->tag = zbx_strdup(NULL, row[1]);
		item_tag->value = zbx_strdup(NULL, row[2]);
		zbx_vector_tags_append(&item_info->item_tags, item_tag);
	}

	if (NULL != item_info)
	{
		zbx_vector_tags_sort(&item_info->item_tags, zbx_compare_tags);
	}

	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get item names and item tags                                      *
 *                                                                            *
 * Parameters: items_info - [IN/OUT] output item name and item tags           *
 *             itemids    - [IN] the item identifiers                         *
 *                                                                            *
 ******************************************************************************/
static void	db_get_items_info_by_itemid(zbx_hashset_t *items_info, const zbx_vector_uint64_t *itemids)
{
	db_get_item_names_by_itemid(items_info, itemids);
	db_get_item_tags_by_itemid(items_info, itemids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees resources allocated to store item tags and name             *
 *                                                                            *
 * Parameters: item_info - [IN] item information                              *
 *                                                                            *
 ******************************************************************************/
static void zbx_item_info_clean(zbx_item_info_t *item_info)
{
	zbx_vector_tags_clear_ext(&item_info->item_tags, zbx_free_tag);
	zbx_vector_tags_destroy(&item_info->item_tags);
	zbx_free(item_info->name);
}

/******************************************************************************
 *                                                                            *
 * Purpose: export history                                                    *
 *                                                                            *
 * Parameters: history     - [IN/OUT] array of history data                   *
 *             history_num - [IN] number of history structures                *
 *             hosts_info  - [IN] hosts groups names                          *
 *             items_info  - [IN] item names and tags                         *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_history(const ZBX_DC_HISTORY *history, int history_num, zbx_hashset_t *hosts_info,
		zbx_hashset_t *items_info, int history_export_enabled, zbx_vector_connector_filter_t *connector_filters,
		unsigned char **data, size_t *data_alloc, size_t *data_offset)
{
	const ZBX_DC_HISTORY		*h;
	const zbx_history_sync_item_t	*item;
	int				i, j;
	zbx_host_info_t			*host_info;
	zbx_item_info_t			*item_info;
	struct zbx_json			json;
	zbx_connector_object_t		connector_object;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_vector_uint64_create(&connector_object.ids);

	for (i = 0; i < history_num; i++)
	{
		h = &history[i];

		if (0 != (ZBX_DC_FLAGS_NOT_FOR_MODULES & h->flags))
			continue;

		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &h->itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = item_info->item;

		if (NULL == (host_info = (zbx_host_info_t *)zbx_hashset_search(hosts_info, &item->host.hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		if (0 != connector_filters->values_num)
		{
			int	k;

			for (k = 0; k < connector_filters->values_num; k++)
			{
				if (SUCCEED == zbx_match_tags(connector_filters->values[k].tags_evaltype,
						&connector_filters->values[k].connector_tags, &item_info->item_tags))
				{
					zbx_vector_uint64_append(&connector_object.ids,
							connector_filters->values[k].connectorid);
				}
			}

			if (0 == connector_object.ids.values_num && FAIL == history_export_enabled)
				continue;
		}

		zbx_json_clean(&json);

		zbx_json_addobject(&json,ZBX_PROTO_TAG_HOST);
		zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, item->host.host, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, item->host.name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json);

		zbx_json_addarray(&json, ZBX_PROTO_TAG_GROUPS);

		for (j = 0; j < host_info->groups.values_num; j++)
			zbx_json_addstring(&json, NULL, host_info->groups.values[j], ZBX_JSON_TYPE_STRING);

		zbx_json_close(&json);

		zbx_json_addarray(&json, ZBX_PROTO_TAG_ITEM_TAGS);

		for (j = 0; j < item_info->item_tags.values_num; j++)
		{
			zbx_tag_t	*item_tag = item_info->item_tags.values[j];

			zbx_json_addobject(&json, NULL);
			zbx_json_addstring(&json, ZBX_PROTO_TAG_TAG, item_tag->tag, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, item_tag->value, ZBX_JSON_TYPE_STRING);
			zbx_json_close(&json);
		}

		zbx_json_close(&json);
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_ITEMID, item->itemid);

		if (NULL != item_info->name)
			zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, item_info->name, ZBX_JSON_TYPE_STRING);

		zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, h->ts.sec);
		zbx_json_addint64(&json, ZBX_PROTO_TAG_NS, h->ts.ns);

		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_json_adddouble(&json, ZBX_PROTO_TAG_VALUE, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_VALUE, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.str, ZBX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_TEXT:
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.str, ZBX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_LOG:
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGTIMESTAMP, h->value.log->timestamp);
				zbx_json_addstring(&json, ZBX_PROTO_TAG_LOGSOURCE,
						ZBX_NULL2EMPTY_STR(h->value.log->source), ZBX_JSON_TYPE_STRING);
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGSEVERITY, h->value.log->severity);
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGEVENTID, h->value.log->logeventid);
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.log->value,
						ZBX_JSON_TYPE_STRING);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		zbx_json_adduint64(&json, ZBX_PROTO_TAG_TYPE, h->value_type);

		if (0 != connector_object.ids.values_num)
		{
			connector_object.objectid = item->itemid;
			connector_object.ts = h->ts;
			connector_object.str = json.buffer;

			zbx_connector_serialize_object(data, data_alloc, data_offset, &connector_object);

			zbx_vector_uint64_clear(&connector_object.ids);
		}

		if (SUCCEED == history_export_enabled)
			zbx_history_export_write(json.buffer, json.buffer_size);
	}

	if (SUCCEED == history_export_enabled)
		zbx_history_export_flush();

	zbx_vector_uint64_destroy(&connector_object.ids);
	zbx_json_free(&json);
}


/******************************************************************************
 *                                                                            *
 * Purpose: 1) calculate changeset of trigger fields to be updated            *
 *          2) generate events                                                *
 *                                                                            *
 * Parameters: trigger - [IN] the trigger to process                          *
 *             diffs   - [OUT] the vector with trigger changes                *
 *                                                                            *
 * Return value: SUCCEED - trigger processed successfully                     *
 *               FAIL    - no changes                                         *
 *                                                                            *
 * Comments: Trigger dependency checks will be done during event processing.  *
 *                                                                            *
 * Event generation depending on trigger value/state changes:                 *
 *                                                                            *
 * From \ To  | OK         | OK(?)      | PROBLEM    | PROBLEM(?) | NONE      *
 *----------------------------------------------------------------------------*
 * OK         | .          | I          | E          | I          | .         *
 *            |            |            |            |            |           *
 * OK(?)      | I          | .          | E,I        | -          | I         *
 *            |            |            |            |            |           *
 * PROBLEM    | E          | I          | E(m)       | I          | .         *
 *            |            |            |            |            |           *
 * PROBLEM(?) | E,I        | -          | E(m),I     | .          | I         *
 *                                                                            *
 * Legend:                                                                    *
 *        'E' - trigger event                                                 *
 *        'I' - internal event                                                *
 *        '.' - nothing                                                       *
 *        '-' - should never happen                                           *
 *                                                                            *
 ******************************************************************************/
static int zbx_process_trigger(struct _DC_TRIGGER *trigger, zbx_vector_ptr_t *diffs, ZBX_DC_HISTORY *history)
{
	LOG_DBG("In %s() triggerid:" ZBX_FS_UI64 " value:%d new_value:%d",
			__func__, trigger->triggerid, trigger->value, trigger->new_value);

	DEBUG_TRIGGER(trigger->triggerid, "Processing trigger, value is %d, new value is %d", trigger->value, trigger->new_value);

	// updating state anyway
	zbx_append_trigger_diff(diffs, trigger->triggerid, trigger->priority, ZBX_FLAGS_TRIGGER_DIFF_UPDATE, trigger->new_value,
							trigger->timespec.sec, trigger->new_error);

	if (trigger->new_value == TRIGGER_VALUE_OK || trigger->new_value == TRIGGER_VALUE_PROBLEM)
	{
		// creating recovery/problem event, to handle existing/not yet existing problems if they already are, the event will be discarede

		DEBUG_TRIGGER(trigger->triggerid, "Creating event for the trigger %ld create for item %ld",
					  trigger->triggerid, history[trigger->history_idx].itemid);

		zbx_add_event(EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid,
					  &trigger->timespec, trigger->new_value, trigger->description,
					  trigger->expression, trigger->recovery_expression,
					  trigger->priority, trigger->type, &trigger->tags,
					  trigger->correlation_mode, trigger->correlation_tag, trigger->value, trigger->opdata,
					  trigger->event_name, NULL, trigger->history_idx);
	}

	// if state hasn't changed, and not problem in multi-problem gen config, nothing to do
	if (trigger->value == trigger->new_value)
	{
		if (TRIGGER_VALUE_PROBLEM != trigger->new_value ||
			TRIGGER_TYPE_MULTIPLE_TRUE != trigger->type)
			return SUCCEED;
	}

	if (TRIGGER_VALUE_UNKNOWN == trigger->value || TRIGGER_VALUE_UNKNOWN == trigger->new_value)
	{
		DEBUG_TRIGGER(trigger->triggerid, "Creating internal event for the trigger");

		zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER, trigger->triggerid,
					  &trigger->timespec, trigger->new_value, NULL, trigger->expression,
					  trigger->recovery_expression, 0, 0, &trigger->tags, 0, NULL, 0, NULL, NULL,
					  trigger->error, trigger->history_idx);
	}

	if (trigger->new_value == TRIGGER_VALUE_UNKNOWN)
		return SUCCEED;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Comments: helper function for zbx_process_triggers()                       *
 *                                                                            *
 ******************************************************************************/
static int zbx_trigger_topoindex_compare(const void *d1, const void *d2)
{
	const DC_TRIGGER *t1 = *(const DC_TRIGGER *const *)d1;
	const DC_TRIGGER *t2 = *(const DC_TRIGGER *const *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(t1->topoindex, t2->topoindex);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: process triggers - calculates property changeset and generates    *
 *          events                                                            *
 *                                                                            *
 * Parameters: triggers     - [IN] the triggers to process                    *
 *             trigger_diff - [OUT] the trigger changeset                     *
 *                                                                            *
 * Comments: The trigger_diff changeset must be cleaned by the caller:        *
 *                zbx_vector_ptr_clear_ext(trigger_diff,                      *
 *                              (zbx_clean_func_t)zbx_trigger_diff_free);     *
 *                                                                            *
 ******************************************************************************/
static void zbx_process_triggers(zbx_vector_ptr_t *triggers, zbx_vector_ptr_t *trigger_diff, ZBX_DC_HISTORY *history)
{
	int i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __func__, triggers->values_num);

	if (0 == triggers->values_num)
		goto out;

	zbx_vector_ptr_sort(triggers, zbx_trigger_topoindex_compare);

	for (i = 0; i < triggers->values_num; i++)
		zbx_process_trigger((struct _DC_TRIGGER *)triggers->values[i], trigger_diff, history);

	zbx_vector_ptr_sort(trigger_diff, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: re-calculate and update values of triggers related to the items   *
 *                                                                            *
 * Parameters: history           - [IN] array of history data                 *
 *             history_num       - [IN] number of history structures          *
 *             history_itemids   - [IN] the item identifiers                  *
 *                                      (used for item lookup)                *
 *             history_items     - [IN] the items                             *
 *             history_errcodes  - [IN] item error codes                      *
 *             timers            - [IN] the trigger timers                    *
 *             trigger_diff      - [OUT] trigger updates                      *
 *                                                                            *
 ******************************************************************************/
static int recalculate_triggers(ZBX_DC_HISTORY *history, int history_num,
								 const zbx_vector_uint64_t *history_itemids, 
								 const zbx_history_sync_item_t *history_items, const int *history_errcodes,
								 const zbx_vector_ptr_t *timers, zbx_vector_ptr_t *trigger_diff)
{
	int i, item_num = 0, timers_num = 0, trigger_num = 0;
	zbx_uint64_t *itemids = NULL;
	zbx_timespec_t *timespecs = NULL;
	zbx_hashset_t trigger_info;
	zbx_vector_ptr_t trigger_order;
	zbx_vector_ptr_t trigger_items;

	LOG_DBG("In %s()", __func__);

	if (0 != history_num)
	{
		itemids = (zbx_uint64_t *)zbx_malloc(itemids, sizeof(zbx_uint64_t) * (size_t)history_num);
		timespecs = (zbx_timespec_t *)zbx_malloc(timespecs, sizeof(zbx_timespec_t) * (size_t)history_num);

		for (i = 0; i < history_num; i++)
		{
			ZBX_DC_HISTORY *h = &history[i];

			if (0 != (ZBX_DC_FLAG_NOVALUE & h->flags))
				continue;

			itemids[item_num] = h->itemid;
			timespecs[item_num] = h->ts;
			item_num++;
		}
	}

	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)timers->values[i];

		if (0 != timer->lock)
			timers_num++;
	}

	if (0 == item_num && 0 == timers_num)
		goto out;

	zbx_hashset_create(&trigger_info, MAX(100, 2 * item_num + timers_num),
					   ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_ptr_create(&trigger_order);
	zbx_vector_ptr_reserve(&trigger_order, trigger_info.num_slots);

	zbx_vector_ptr_create(&trigger_items);

	if (0 != item_num)
	{
		zbx_dc_config_history_sync_get_triggers_by_itemids(&trigger_info, &trigger_order, itemids, timespecs, item_num);
		zbx_prepare_triggers((DC_TRIGGER **)trigger_order.values, trigger_order.values_num);
	//	zbx_determine_items_in_expressions(&trigger_order, itemids, item_num);
	}

	if (0 != timers_num)
	{
		int offset = trigger_order.values_num;

		zbx_dc_get_triggers_by_timers(&trigger_info, &trigger_order, timers);

		if (offset != trigger_order.values_num)
		{
			zbx_prepare_triggers((DC_TRIGGER **)trigger_order.values + offset,
								 trigger_order.values_num - offset);
		}
	}

	zbx_vector_ptr_sort(&trigger_order, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	zbx_evaluate_expressions(&trigger_order, history_itemids, history_items, history_errcodes);
	zbx_process_triggers(&trigger_order, trigger_diff, history);
	
	trigger_num = trigger_order.values_num;

	DCfree_triggers(&trigger_order);

	zbx_vector_ptr_destroy(&trigger_items);

	zbx_hashset_destroy(&trigger_info);
	zbx_vector_ptr_destroy(&trigger_order);
out:
	zbx_free(timespecs);
	zbx_free(itemids);
	
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return trigger_num;
}

static void DCinventory_value_add(zbx_vector_ptr_t *inventory_values, const zbx_history_sync_item_t *item,
								  ZBX_DC_HISTORY *h)
{
	char value[MAX_BUFFER_LEN];
	const char *inventory_field;
	zbx_inventory_value_t *inventory_value;

	if (ITEM_STATE_NOTSUPPORTED == h->state)
		return;

	if (HOST_INVENTORY_AUTOMATIC != item->host.inventory_mode)
		return;

	if (0 != (ZBX_DC_FLAG_UNDEF & h->flags) || 0 != (ZBX_DC_FLAG_NOVALUE & h->flags) ||
		NULL == (inventory_field = zbx_db_get_inventory_field(item->inventory_link)))
	{
		return;
	}

	switch (h->value_type)
	{
	case ITEM_VALUE_TYPE_FLOAT:
		zbx_print_double(value, sizeof(value), h->value.dbl);
		break;
	case ITEM_VALUE_TYPE_UINT64:
		zbx_snprintf(value, sizeof(value), ZBX_FS_UI64, h->value.ui64);
		break;
	case ITEM_VALUE_TYPE_STR:
	case ITEM_VALUE_TYPE_TEXT:
		zbx_strscpy(value, h->value.str);
		break;
	default:
		return;
	}

	zbx_format_value(value, sizeof(value), item->valuemapid, ZBX_NULL2EMPTY_STR(item->units), h->value_type);

	inventory_value = (zbx_inventory_value_t *)zbx_malloc(NULL, sizeof(zbx_inventory_value_t));

	inventory_value->hostid = item->host.hostid;
	inventory_value->idx = item->inventory_link - 1;
	inventory_value->field_name = inventory_field;
	inventory_value->value = zbx_strdup(NULL, value);

	zbx_vector_ptr_append(inventory_values, inventory_value);
}

static void DCadd_update_inventory_sql(size_t *sql_offset, const zbx_vector_ptr_t *inventory_values)
{
	char *value_esc;
	int i;

	for (i = 0; i < inventory_values->values_num; i++)
	{
		const zbx_inventory_value_t *inventory_value = (zbx_inventory_value_t *)inventory_values->values[i];

		value_esc = zbx_db_dyn_escape_field("host_inventory", inventory_value->field_name, inventory_value->value);

		zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
						   "update host_inventory set %s='%s' where hostid=" ZBX_FS_UI64 ";\n",
						   inventory_value->field_name, value_esc, inventory_value->hostid);

		zbx_db_execute_overflowed_sql(&sql, &sql_alloc, sql_offset);

		zbx_free(value_esc);
	}
}

static void DCinventory_value_free(zbx_inventory_value_t *inventory_value)
{
	zbx_free(inventory_value->value);
	zbx_free(inventory_value);
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees resources allocated to store str/text/log value             *
 *                                                                            *
 * Parameters: history     - [IN] the history data                            *
 *             history_num - [IN] the number of values in history data        *
 *                                                                            *
 ******************************************************************************/
static void dc_history_clean_value(ZBX_DC_HISTORY *history)
{

	if (ITEM_STATE_NOTSUPPORTED == history->state)
	{
		if (NULL != history->value.err)
			zbx_free(history->value.err);
		return;
	}

	if (ITEM_VALUE_TYPE_NONE == history->value_type) 
		return;

	if (0 != (ZBX_DC_FLAG_NOVALUE & history->flags))
		return;

	switch (history->value_type)
	{
	case ITEM_VALUE_TYPE_LOG:
		zbx_free(history->value.log->value);
		zbx_free(history->value.log->source);
		zbx_free(history->value.log);
		break;
	case ITEM_VALUE_TYPE_STR:
	case ITEM_VALUE_TYPE_TEXT:
		zbx_free(history->value.str);
		break;
	}

}

/******************************************************************************
 *                                                                            *
 * Purpose: frees resources allocated to store str/text/log values            *
 *                                                                            *
 * Parameters: history     - [IN] the history data                            *
 *             history_num - [IN] the number of values in history data        *
 *                                                                            *
 ******************************************************************************/
static void hc_free_item_values(ZBX_DC_HISTORY *history, int history_num)
{
	int i;

	for (i = 0; i < history_num; i++) {
		//LOG_INF("Freeind hist id %ld", history[i].itemid);
		dc_history_clean_value(&history[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: sets history data to notsupported                                 *
 *                                                                            *
 * Parameters: history  - [IN] the history data                               *
 *             errmsg   - [IN] the error message                              *
 *                                                                            *
 * Comments: The error message is stored directly and freed with when history *
 *           data is cleaned.                                                 *
 *                                                                            *
 ******************************************************************************/
static void dc_history_set_error(ZBX_DC_HISTORY *hdata, char *errmsg)
{
	dc_history_clean_value(hdata);
	hdata->value.err = errmsg;
	hdata->state = ITEM_STATE_NOTSUPPORTED;
	hdata->flags |= ZBX_DC_FLAG_UNDEF;
}

/******************************************************************************
 *                                                                            *
 * Purpose: sets history data value                                           *
 *                                                                            *
 * Parameters: hdata      - [IN/OUT] the history data                         *
 *             value_type - [IN] the item value type                          *
 *             value      - [IN] the value to set                             *
 *                                                                            *
 ******************************************************************************/
static void dc_history_set_value(ZBX_DC_HISTORY *hdata, unsigned char value_type, zbx_variant_t *value)
{
	char *errmsg = NULL;

	if (FAIL == zbx_variant_to_value_type(value, value_type, CONFIG_DOUBLE_PRECISION, &errmsg))
	{
		DEBUG_ITEM(hdata->itemid, "Item type coversion error: %s", errmsg);
		glb_state_item_set_error(hdata->itemid, "Item type conversion error");
		dc_history_set_error(hdata, errmsg);
		return;
	}

	switch (value_type)
	{
	case ITEM_VALUE_TYPE_FLOAT:
		dc_history_clean_value(hdata);
		hdata->value.dbl = value->data.dbl;
		break;
	case ITEM_VALUE_TYPE_UINT64:
		dc_history_clean_value(hdata);
		hdata->value.ui64 = value->data.ui64;
		break;
	case ITEM_VALUE_TYPE_STR:
		dc_history_clean_value(hdata);
		hdata->value.str = value->data.str;
		hdata->value.str[zbx_db_strlen_n(hdata->value.str, ZBX_HISTORY_STR_VALUE_LEN)] = '\0';
		break;
	case ITEM_VALUE_TYPE_TEXT:
		dc_history_clean_value(hdata);
		hdata->value.str = value->data.str;
		hdata->value.str[zbx_db_strlen_n(hdata->value.str, ZBX_HISTORY_TEXT_VALUE_LEN)] = '\0';
		break;
	case ITEM_VALUE_TYPE_LOG:
		if (ITEM_VALUE_TYPE_LOG != hdata->value_type)
		{
			dc_history_clean_value(hdata);
			hdata->value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
			memset(hdata->value.log, 0, sizeof(zbx_log_value_t));
			hdata->value.log->severity = TRIGGER_SEVERITY_UNDEFINED;
		}
		hdata->value.log->value = value->data.str;
		hdata->value.str[zbx_db_strlen_n(hdata->value.str, ZBX_HISTORY_LOG_VALUE_LEN)] = '\0';
	}

	hdata->value_type = value_type;
	zbx_variant_set_none(value);
}

/******************************************************************************
 *                                                                            *
 * Purpose: normalize item value by performing truncation of long text        *
 *          values and changes value format according to the item value type  *
 *                                                                            *
 * Parameters: item          - [IN] the item                                  *
 *             hdata         - [IN/OUT] the historical data to process        *
 *                                                                            *
 ******************************************************************************/
static void normalize_item_value(const zbx_history_sync_item_t *item, ZBX_DC_HISTORY *hdata)
{
	char *logvalue;
	zbx_variant_t value_var;

	if (0 != (hdata->flags & ZBX_DC_FLAG_NOVALUE))
		return;

	if (hdata->value_type == ITEM_VALUE_TYPE_NONE) {
		hdata->flags |= ZBX_DC_FLAG_NOVALUE;
		return;
	}

	if (ITEM_STATE_NOTSUPPORTED == hdata->state)
		return;

	DEBUG_ITEM(hdata->itemid, "in Normalizing item, state is %d", hdata->state);

	if (item->value_type == hdata->value_type)
	{
		/* truncate text based values if necessary */
		switch (hdata->value_type)
		{
		case ITEM_VALUE_TYPE_STR:
			hdata->value.str[zbx_db_strlen_n(hdata->value.str, ZBX_HISTORY_STR_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_TEXT:
			hdata->value.str[zbx_db_strlen_n(hdata->value.str, ZBX_HISTORY_TEXT_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_LOG:
			logvalue = hdata->value.log->value;
			logvalue[zbx_db_strlen_n(logvalue, ZBX_HISTORY_LOG_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			if (FAIL == zbx_validate_value_dbl(hdata->value.dbl, CONFIG_DOUBLE_PRECISION))
			{
				char buffer[ZBX_MAX_DOUBLE_LEN + 1], buff_str[MAX_STRING_LEN];

				DEBUG_ITEM(hdata->itemid, "Value is detected to bee too small or too large");
				zbx_snprintf(buff_str, MAX_STRING_LEN, "Value %s is too small or too large.", zbx_print_double(buffer, sizeof(buffer), hdata->value.dbl));
				glb_state_item_set_error(hdata->itemid, buff_str);
			}
			break;
		}
		return;
	}

	switch (hdata->value_type)
	{
	case ITEM_VALUE_TYPE_FLOAT:
		zbx_variant_set_dbl(&value_var, hdata->value.dbl);
		break;
	case ITEM_VALUE_TYPE_UINT64:
		zbx_variant_set_ui64(&value_var, hdata->value.ui64);
		break;
	case ITEM_VALUE_TYPE_STR:
	case ITEM_VALUE_TYPE_TEXT:
		zbx_variant_set_str(&value_var, hdata->value.str);
		hdata->value.str = NULL;
		break;
	case ITEM_VALUE_TYPE_LOG:
		zbx_variant_set_str(&value_var, hdata->value.log->value);
		hdata->value.log->value = NULL;
		break;
	case ITEM_VALUE_TYPE_NONE:
		zbx_variant_set_none(&value_var);
		break;
	default:
		LOG_INF("Unknown item type %d", hdata->value_type);
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	dc_history_set_value(hdata, item->value_type, &value_var);
	zbx_variant_clear(&value_var);
}

static int	history_value_compare_func(const void *d1, const void *d2)
{
	const ZBX_DC_HISTORY	*i1 = *(const ZBX_DC_HISTORY * const *)d1;
	const ZBX_DC_HISTORY	*i2 = *(const ZBX_DC_HISTORY * const *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(i1->itemid, i2->itemid);
	ZBX_RETURN_IF_NOT_EQUAL(i1->value_type, i2->value_type);
	ZBX_RETURN_IF_NOT_EQUAL(i1->ts.sec, i2->ts.sec);
	ZBX_RETURN_IF_NOT_EQUAL(i1->ts.ns, i2->ts.ns);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other than       *
 *          ITEM_VALUE_TYPE_LOG not containing meta information in result     *
 *                                                                            *
 ******************************************************************************/
static void dc_add_proxy_history(ZBX_DC_HISTORY *history, int history_num)
{
	int i, now, history_count = 0;
	unsigned int flags;
	char buffer[64], *pvalue;
	zbx_db_insert_t db_insert;

	now = (int)time(NULL);
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "flags", "write_clock",
						  NULL);

	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY *h = &history[i];

		if (0 != (h->flags & ZBX_DC_FLAG_UNDEF))
			continue;

		if (0 != (h->flags & ZBX_DC_FLAG_META))
			continue;

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (0 == (h->flags & ZBX_DC_FLAG_NOVALUE) && h->value_type != ITEM_VALUE_TYPE_NONE)
		{
			switch (h->value_type)
			{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_DBL64, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_UI64, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				pvalue = h->value.str;
				break;
			case ITEM_VALUE_TYPE_LOG:
				continue;
			default:

				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}
			flags = 0;
		}
		else
		{
			flags = PROXY_HISTORY_FLAG_NOVALUE;
			pvalue = (char *)"";
		}

		history_count++;
		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue, flags, now);
	}

//	change_proxy_history_count(history_count);
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other than       *
 *          ITEM_VALUE_TYPE_LOG containing meta information in result         *
 *                                                                            *
 ******************************************************************************/
static void dc_add_proxy_history_meta(ZBX_DC_HISTORY *history, int history_num)
{
	int i, now, history_count = 0;
	char buffer[64], *pvalue;
	zbx_db_insert_t db_insert;

	now = (int)time(NULL);
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "lastlogsize", "mtime",
						  "flags", "write_clock", NULL);

	for (i = 0; i < history_num; i++)
	{
		unsigned int flags = PROXY_HISTORY_FLAG_META;
		const ZBX_DC_HISTORY *h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (0 != (h->flags & ZBX_DC_FLAG_UNDEF))
			continue;

		if (0 == (h->flags & ZBX_DC_FLAG_META))
			continue;

		if (ITEM_VALUE_TYPE_LOG == h->value_type)
			continue;

		if (0 == (h->flags & ZBX_DC_FLAG_NOVALUE))
		{
			switch (h->value_type)
			{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_DBL64, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_UI64, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				pvalue = h->value.str;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}
		}
		else
		{
			flags |= PROXY_HISTORY_FLAG_NOVALUE;
			pvalue = (char *)"";
		}

		history_count++;
		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue, h->lastlogsize, h->mtime,
								 flags, now);
	}

//	change_proxy_history_count(history_count);
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type                  *
 *          ITEM_VALUE_TYPE_LOG                                               *
 *                                                                            *
 ******************************************************************************/
static void dc_add_proxy_history_log(ZBX_DC_HISTORY *history, int history_num)
{
	int i, now, history_count = 0;
	zbx_db_insert_t db_insert;

	now = (int)time(NULL);

	/* see hc_copy_history_data() for fields that might be uninitialized and need special handling here */
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "timestamp", "source", "severity",
						  "value", "logeventid", "lastlogsize", "mtime", "flags", "write_clock", NULL);

	for (i = 0; i < history_num; i++)
	{
		unsigned int flags;
		zbx_uint64_t lastlogsize;
		int mtime;
		const ZBX_DC_HISTORY *h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		if (ITEM_VALUE_TYPE_LOG != h->value_type)
			continue;

		if (0 == (h->flags & ZBX_DC_FLAG_NOVALUE))
		{
			zbx_log_value_t *log = h->value.log;

			if (0 != (h->flags & ZBX_DC_FLAG_META))
			{
				flags = PROXY_HISTORY_FLAG_META;
				lastlogsize = h->lastlogsize;
				mtime = h->mtime;
			}
			else
			{
				flags = 0;
				lastlogsize = 0;
				mtime = 0;
			}

			zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, log->timestamp,
									 ZBX_NULL2EMPTY_STR(log->source), log->severity, log->value, log->logeventid,
									 lastlogsize, mtime, flags, now);
		}
		else
		{
			/* sent to server only if not 0, see proxy_get_history_data() */
			const int unset_if_novalue = 0;

			flags = PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE;

			zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, unset_if_novalue, "",
									 unset_if_novalue, "", unset_if_novalue, h->lastlogsize, h->mtime, flags, now);
		}
		history_count++;
	}

//	change_proxy_history_count(history_count);
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 ******************************************************************************/
static void dc_add_proxy_history_notsupported(ZBX_DC_HISTORY *history, int history_num)
{
	int i, now, history_count = 0;
	zbx_db_insert_t db_insert;

	now = (int)time(NULL);
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "state", "write_clock",
						  NULL);

	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY *h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED != h->state)
			continue;

		history_count++;
		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, ZBX_NULL2EMPTY_STR(h->value.err),
								 (int)h->state, now);
	}

//	change_proxy_history_count(history_count);
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Purpose: inserting new history data after new value is received            *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 ******************************************************************************/
static void DBmass_proxy_add_history(ZBX_DC_HISTORY *history, int history_num)
{
	int i, h_num = 0, h_meta_num = 0, hlog_num = 0, notsupported_num = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY *h = &history[i];

		if (ITEM_STATE_NOTSUPPORTED == h->state)
		{
			notsupported_num++;
			continue;
		}

		switch (h->value_type)
		{
		case ITEM_VALUE_TYPE_LOG:
			hlog_num++;
			break;
		case ITEM_VALUE_TYPE_FLOAT:
		case ITEM_VALUE_TYPE_UINT64:
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			if (0 != (h->flags & ZBX_DC_FLAG_META))
				h_meta_num++;
			else
				h_num++;
			break;
		case ITEM_VALUE_TYPE_NONE:
			h_num++;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	if (0 != h_num)
		dc_add_proxy_history(history, history_num);

	if (0 != h_meta_num)
		dc_add_proxy_history_meta(history, history_num);

	if (0 != hlog_num)
		dc_add_proxy_history_log(history, history_num);

	if (0 != notsupported_num)
		dc_add_proxy_history_notsupported(history, history_num);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: prepare history data using items from configuration cache and     *
 *          generate item changes to be applied and host inventory values to  *
 *          be added                                                          *
 *                                                                            *
 * Parameters: history             - [IN/OUT] array of history data           *
 *             itemids             - [IN] the item identifiers                *
 *                                        (used for item lookup)              *
 *             items               - [IN] the items                           *
 *             errcodes            - [IN] item error codes                    *
 *             history_num         - [IN] number of history structures        *
 *             item_diff           - [OUT] the changes in item data           *
 *             inventory_values    - [OUT] the inventory values to add        *
 *             compression_age     - [IN] history compression age             *
 *             proxy_subscribtions - [IN] history compression age             *
 *                                                                            *
 ******************************************************************************/
static void DCmass_prepare_history(ZBX_DC_HISTORY *history, const zbx_vector_uint64_t *itemids,
								zbx_history_sync_item_t *items, const int *errcodes, int history_num,
								zbx_vector_ptr_t *inventory_values, zbx_vector_uint64_pair_t *proxy_subscribtions)
{
	static time_t last_history_discard = 0;
	time_t now;
	int i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __func__, history_num);

	now = time(NULL);

	for (i = 0; i < history_num; i++)
	{
		ZBX_DC_HISTORY *h = &history[i];
		zbx_history_sync_item_t  *item;

		int index;

		DEBUG_ITEM(h->itemid, "Will do prepare history, state is %d", h->state);
		//LOG_INF("History prepare item %ld type %d state %d", h->itemid, h->value_type, h->state);
		if (FAIL == (index = zbx_vector_uint64_bsearch(itemids, h->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			h->flags |= ZBX_DC_FLAG_UNDEF;
			continue;
		}

		if (SUCCEED != errcodes[index])
		{
			DEBUG_ITEM(h->itemid, "Setting undefined value flag, due to errcode");
			// LOG_INF("Errcode is non zero");
			h->flags |= ZBX_DC_FLAG_UNDEF;
			continue;
		}

		item = &items[index];

		if (ITEM_STATUS_ACTIVE != item->status || HOST_STATUS_MONITORED != item->host.status)
		{
			DEBUG_ITEM(h->itemid, "Setting undefined value flag, due to item status");
			//	LOG_INF("Item status is nonactive or nonmonitored");
			h->flags |= ZBX_DC_FLAG_UNDEF;
			continue;
		}

		if (0 == item->history)
			h->flags |= ZBX_DC_FLAG_NOHISTORY;

		if (0 == item->trends || (ITEM_VALUE_TYPE_FLOAT != item->value_type &&
								  ITEM_VALUE_TYPE_UINT64 != item->value_type))
			h->flags |= ZBX_DC_FLAG_NOTRENDS;

		h->host_name = (char *)item->host.host;
		h->item_key = (char *)item->key_orig;
		
		h->hostid = item->host.hostid;

		DEBUG_ITEM(h->itemid, "Normalizing item, state is %d", h->state);
		//LOG_INF("Normalizing hist item %ld type %d state %d", h->itemid, h->value_type, h->state);
		normalize_item_value(item, h);
		//LOG_INF("Normalizing hist item %ld type %d state %d", h->itemid, h->value_type, h->state);

		DEBUG_ITEM(h->itemid, "Finished normalizing item, state is %d", h->state);

		DCinventory_value_add(inventory_values, item, h);

		if (0 != item->host.proxy_hostid && FAIL == is_item_processed_by_server(item->type, item->key_orig))
		{
			zbx_uint64_pair_t p = {item->host.proxy_hostid, h->ts.sec};

			zbx_vector_uint64_pair_append(proxy_subscribtions, p);
		}
		DEBUG_ITEM(h->itemid, "Finished prepare processing, state is %d", h->state);
	}

	zbx_vector_ptr_sort(inventory_values, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void DCmodule_sync_history(int history_float_num, int history_integer_num, int history_string_num,
								  int history_text_num, int history_log_num, ZBX_HISTORY_FLOAT *history_float,
								  ZBX_HISTORY_INTEGER *history_integer, ZBX_HISTORY_STRING *history_string,
								  ZBX_HISTORY_TEXT *history_text, ZBX_HISTORY_LOG *history_log)
{
	if (0 != history_float_num)
	{
		int i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing float history data with modules...");

		for (i = 0; NULL != history_float_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_float_cbs[i].module->name);
			history_float_cbs[i].history_float_cb(history_float, history_float_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d float values with modules", history_float_num);
	}

	if (0 != history_integer_num)
	{
		int i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing integer history data with modules...");

		for (i = 0; NULL != history_integer_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_integer_cbs[i].module->name);
			history_integer_cbs[i].history_integer_cb(history_integer, history_integer_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d integer values with modules", history_integer_num);
	}

	if (0 != history_string_num)
	{
		int i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing string history data with modules...");

		for (i = 0; NULL != history_string_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_string_cbs[i].module->name);
			history_string_cbs[i].history_string_cb(history_string, history_string_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d string values with modules", history_string_num);
	}

	if (0 != history_text_num)
	{
		int i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing text history data with modules...");

		for (i = 0; NULL != history_text_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_text_cbs[i].module->name);
			history_text_cbs[i].history_text_cb(history_text, history_text_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d text values with modules", history_text_num);
	}

	if (0 != history_log_num)
	{
		int i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing log history data with modules...");

		for (i = 0; NULL != history_log_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_log_cbs[i].module->name);
			history_log_cbs[i].history_log_cb(history_log, history_log_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d log values with modules", history_log_num);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: prepares history update by checking which values must be stored   *
 *                                                                            *
 * Parameters: history     - [IN/OUT] the history values                      *
 *             history_num - [IN] the number of history values                *
 *                                                                            *
 ******************************************************************************/
static void proxy_prepare_history(ZBX_DC_HISTORY *history, int history_num)
{
	int i, *errcodes;
	zbx_history_sync_item_t	*items;
	zbx_vector_uint64_t itemids;

	zbx_vector_uint64_create(&itemids);
	zbx_vector_uint64_reserve(&itemids, history_num);

	for (i = 0; i < history_num; i++)
	{
		zbx_vector_uint64_append(&itemids, history[i].itemid);
		DEBUG_ITEM(history[i].itemid, "Processing in history sync");
	}

	items = zbx_malloc(NULL, sizeof(zbx_history_sync_item_t) * (size_t)history_num);
	errcodes = (int *)zbx_malloc(NULL, sizeof(int) * (size_t)history_num);

	zbx_dc_config_history_sync_get_items_by_itemids(items, itemids.values, errcodes, (size_t)itemids.values_num,
			ZBX_ITEM_GET_SYNC);

	for (i = 0; i < history_num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		/* store items with enabled history  */
		if (0 != items[i].history)
			continue;

		/* store numeric items to handle data conversion errors on server and trends */
		if (ITEM_VALUE_TYPE_FLOAT == items[i].value_type || ITEM_VALUE_TYPE_UINT64 == items[i].value_type)
			continue;

		/* store discovery rules */
		if (0 != (items[i].flags & ZBX_FLAG_DISCOVERY_RULE))
			continue;

		/* store errors or first value after an error */
		// if (ITEM_STATE_NOTSUPPORTED == history[i].state || ITEM_STATE_NOTSUPPORTED == items[i].state)
		//	continue;

		/* store items linked to host inventory */
		if (0 != items[i].inventory_link)
			continue;

		dc_history_clean_value(history + i);

		/* all checks passed, item value must not be stored in proxy history/sent to server */
		history[i].flags |= ZBX_DC_FLAG_NOVALUE;
	}

	zbx_dc_config_clean_history_sync_items(items, errcodes, (size_t)history_num);
	zbx_free(items);
	zbx_free(errcodes);
	zbx_vector_uint64_destroy(&itemids);
}

IPC_PROCESS_CB(metrics_proc_cb) {
	ZBX_DC_HISTORY *history = cb_data;

	ZBX_DC_HISTORY *h = &history[i];
	bzero(h, sizeof(ZBX_DC_HISTORY));

	metric_t *metric = ipc_data;
	
	history->state = ITEM_STATE_NORMAL;
	
	switch (metric->value.type)
	{
		case ZBX_VARIANT_DBL:
			h->value_type = ITEM_VALUE_TYPE_FLOAT;
			h->value.dbl = metric->value.data.dbl;
			break;
		case ZBX_VARIANT_UI64:
			h->value_type = ITEM_VALUE_TYPE_UINT64;
			h->value.ui64 = metric->value.data.ui64;
			break;
		case ZBX_VARIANT_STR:
			h->value_type = ITEM_VALUE_TYPE_TEXT;
			h->value.str = zbx_strdup(NULL, metric->value.data.str);
			break;
		case ZBX_VARIANT_NONE:
			h->value_type = ITEM_VALUE_TYPE_NONE;
			h->flags = ZBX_DC_FLAG_NOVALUE;
			h->state = ITEM_STATE_UNKNOWN;
			break;
		case ZBX_VARIANT_ERR:
			h->value_type = ITEM_VALUE_TYPE_NONE;
			h->state =ITEM_STATE_NOTSUPPORTED;
			h->value.err = zbx_strdup(NULL, metric->value.data.str);
			h->flags = ZBX_DC_FLAG_UNDEF; 
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			break;
	}

	h->hostid = metric->hostid;
	h->itemid = metric->itemid;
	h->ts = metric->ts;
	h->flags = metric->flags;
}

static void sync_proxy_history(int *total_num, int proc_num)
{
	int history_num, txn_rc;
	time_t sync_start;
	ZBX_DC_HISTORY history[ZBX_HC_SYNC_MAX];

	sync_start = time(NULL);

	do
	{
		history_num = process_receive_metrics(proc_num, metrics_proc_cb, history, ZBX_HC_SYNC_MAX );

		if (0 == history_num)
			break;

		proxy_prepare_history(history, history_num);

		do
		{
			zbx_db_begin();
			DBmass_proxy_add_history(history, history_num);
		} while (ZBX_DB_DOWN == (txn_rc = zbx_db_commit()));

		*total_num += history_num;
		
	} while (history_num > 0 && ZBX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);
}



/******************************************************************************
 *                                                                            *
 * Purpose: flush history cache to database, process triggers of flushed      *
 *          and timer triggers from timer queue                               *
 *                                                                            *
 * Parameters: sync_timeout - [IN] the timeout in seconds                     *
 *             values_num   - [IN/OUT] the number of synced values            *
 *             triggers_num - [IN/OUT] the number of processed timers         *
 *             more         - [OUT] a flag indicating the cache emptiness:    *
 *                               ZBX_SYNC_DONE - nothing to sync, go idle     *
 *                               ZBX_SYNC_MORE - more data to sync            *
 *                                                                            *
 * Comments: This function loops syncing history values by 1k batches and     *
 *           processing timer triggers by batches of 500 triggers.            *
 *           Unless full sync is being done the loop is aborted if either     *
 *           timeout has passed or there are no more data to process.         *
 *           The last is assumed when the following is true:                  *
 *            a) history cache is empty or less than 10% of batch values were *
 *               processed (the other items were locked by triggers)          *
 *            b) less than 500 (full batch) timer triggers were processed     *
 *                                                                            *
 ******************************************************************************/
static void sync_server_history(int *values_num, int *triggers_num, int proc_num)
{

	int i, history_num, timers_num,  txn_error;

	unsigned int item_retrieve_mode;
	time_t sync_start;
	zbx_vector_uint64_t triggerids;
	zbx_vector_ptr_t trigger_diff, inventory_values, trigger_timers;
	zbx_vector_uint64_pair_t proxy_subscribtions;
	ZBX_DC_HISTORY history[ZBX_HC_SYNC_MAX];
	zbx_vector_connector_filter_t	connector_filters_history, connector_filters_events;

	item_retrieve_mode = 0 == zbx_has_export_dir() ? ZBX_ITEM_GET_SYNC : ZBX_ITEM_GET_SYNC_EXPORT;

	zbx_vector_connector_filter_create(&connector_filters_history);
	zbx_vector_connector_filter_create(&connector_filters_events);
	zbx_vector_ptr_create(&inventory_values);
	zbx_vector_ptr_create(&trigger_diff);

	zbx_vector_uint64_pair_create(&proxy_subscribtions);

	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_reserve(&triggerids, ZBX_HC_SYNC_MAX);

	zbx_vector_ptr_create(&trigger_timers);
	zbx_vector_ptr_reserve(&trigger_timers, ZBX_HC_TIMER_MAX);

	*values_num = 0;
	*triggers_num = 0;
	sync_start = time(NULL);

	item_retrieve_mode = 0 == zbx_has_export_dir() ? ZBX_ITEM_GET_SYNC : ZBX_ITEM_GET_SYNC_EXPORT;

	do
	{
		zbx_history_sync_item_t		*items = NULL;
		int *errcodes, ret = SUCCEED;
		zbx_vector_uint64_t itemids;

		zbx_vector_uint64_create(&itemids);
		
		zbx_dc_um_handle_t *um_handle;
		um_handle = zbx_dc_open_user_macros();
	
		history_num = process_receive_metrics(proc_num, metrics_proc_cb, history, ZBX_HC_SYNC_MAX );

		if (0 !=  history_num ) {

			items = zbx_malloc(NULL, sizeof(zbx_history_sync_item_t) * (size_t)history_num);
			errcodes = (int *)zbx_malloc(NULL, sizeof(int) * (size_t)history_num);

			zbx_vector_uint64_reserve(&itemids, history_num);

			for (i = 0; i < history_num; i++)
			{
				zbx_vector_uint64_append(&itemids, history[i].itemid);
				DEBUG_ITEM(history[i].itemid, "Processing in history sync");
			//	LOG_INF("Id enum hist item %ld type %d state %d", history[i].itemid, history[i].value_type, history[i].state);
			}

			zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			
			zbx_dc_config_history_sync_get_items_by_itemids(items, itemids.values, errcodes,
					(size_t)history_num, item_retrieve_mode);
			
	
			DCmass_prepare_history(history, &itemids, items, errcodes, history_num,
								   &inventory_values, &proxy_subscribtions);

			glb_state_item_add_values(history, history_num);
		}

		if (FAIL != ret)
		{
			/* don't process trigger timers when server is shutting down */
			if (ZBX_IS_RUNNING())
			{
				zbx_dc_get_trigger_timers(&trigger_timers, time(NULL), ZBX_HC_TIMER_SOFT_MAX,
										  ZBX_HC_TIMER_MAX);
			}

			timers_num = trigger_timers.values_num;

			if (0 != history_num || 0 != timers_num)
			{
				for (i = 0; i < trigger_timers.values_num; i++)
				{
					zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)trigger_timers.values[i];

					if (0 != timer->lock)
						zbx_vector_uint64_append(&triggerids, timer->triggerid);
				}

				do
				{
					zbx_db_begin();

					*triggers_num = *triggers_num + recalculate_triggers(history, history_num, &itemids, items, errcodes,
										 &trigger_timers, &trigger_diff);

					/* process trigger events generated by recalculate_triggers() */
					zbx_process_events(&trigger_diff, &triggerids, history);
					glb_state_triggers_apply_diffs(&trigger_diff);

					if (ZBX_DB_OK != (txn_error = zbx_db_commit()))
						zbx_clean_events();

					zbx_vector_ptr_clear_ext(&trigger_diff, (zbx_clean_func_t)zbx_trigger_diff_free);
				} while (ZBX_DB_DOWN == txn_error);

				// separate story, some kind of SLA and services calculation is
				// done in the db either, should probably go to the state cache either
				if (ZBX_DB_OK == txn_error)
					zbx_events_update_itservices();
			}
		}
		zbx_dc_close_user_macros(um_handle);

		if (0 != trigger_timers.values_num)
		{
			zbx_dc_reschedule_trigger_timers(&trigger_timers, time(NULL));
			zbx_vector_ptr_clear(&trigger_timers);
		}

		if (0 != proxy_subscribtions.values_num)
		{
			zbx_vector_uint64_pair_sort(&proxy_subscribtions, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_dc_proxy_update_nodata(&proxy_subscribtions);
			zbx_vector_uint64_pair_clear(&proxy_subscribtions);
		}

		// if (FAIL != ret)
		// {

		// 	if (SUCCEED == zbx_is_export_enabled(ZBX_FLAG_EXPTYPE_EVENTS))
		// 		zbx_export_events()
		// }

		if (0 != history_num || 0 != timers_num)
			zbx_clean_events();

		if (0 != history_num)
		{
			glb_history_add_history(history, history_num);
			DCmass_update_trends(history, history_num);

			do
			{
				zbx_db_begin();

				zbx_process_events(NULL, NULL, NULL);

				if (ZBX_DB_OK != (txn_error = zbx_db_commit()))
					zbx_reset_event_recovery();
			
			} while (ZBX_DB_DOWN == txn_error);

			zbx_clean_events();
			zbx_vector_ptr_clear_ext(&inventory_values, (zbx_clean_func_t)DCinventory_value_free);


			zbx_dc_config_clean_history_sync_items(items, errcodes, (size_t)history_num);

			zbx_free(errcodes);
			zbx_free(items);

			hc_free_item_values(history, history_num);

		}

		zbx_vector_uint64_destroy(&itemids);
		*values_num += history_num;

		/* Exit from sync loop if we have spent too much time here.       */
		/* This is done to allow syncer process to update its statistics. */
	} while ( (history_num + triggerids.values_num + timers_num) > 0  && ZBX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);

	zbx_vector_ptr_destroy(&inventory_values);
	zbx_vector_ptr_destroy(&trigger_diff);
	zbx_vector_uint64_pair_destroy(&proxy_subscribtions);

	zbx_vector_ptr_destroy(&trigger_timers);
	zbx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: writes updates and new data from history cache to database        *
 *                                                                            *
 * Parameters: values_num - [OUT] the number of synced values                  *
 *             more      - [OUT] a flag indicating the cache emptiness:       *
 *                                ZBX_SYNC_DONE - nothing to sync, go idle    *
 *                                ZBX_SYNC_MORE - more data to sync           *
 *                                                                            *
 ******************************************************************************/
void zbx_sync_history_cache(int *values_num, int *triggers_num, int *more, int proc_num)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num ", __func__);

	*values_num = 0;
	*triggers_num = 0;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		sync_server_history(values_num, triggers_num, proc_num);
	else
		sync_proxy_history(values_num, proc_num);
}

int history_update_log_enty_severity(ZBX_DC_HISTORY *h, int severity, u_int64_t eventid, u_int64_t triggerid, int value) {
	
	if  (ITEM_VALUE_TYPE_LOG != h->value_type)
		return FAIL;
	
	if ( 1 == value) { //remembering max severity
	   if ( TRIGGER_SEVERITY_UNDEFINED != h->value.log->severity &&
		    h->value.log->severity >= severity)
			return FAIL;
		h->value.log->severity = severity;
	} else { //recovery data, for it severity is 0 which is OK
		h->value.log->severity = 0; // indication of recovery or OK value
	}	

	h->value.log->logeventid = eventid;
	char *buff = zbx_malloc(NULL, MAX_ID_LEN);
	zbx_snprintf(buff, MAX_ID_LEN, "%ld", triggerid);
	
	h->value.log->source = buff;
	
	return SUCCEED;
}

ZBX_SHMEM_FUNC_IMPL(__hc_index, hc_index_mem)
/******************************************************************************
 *                                                                            *
 * Purpose: Allocate shared memory for database cache                         *
 *                                                                            *
 ******************************************************************************/
int init_database_cache(char **error)
{
	int ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = zbx_mutex_create(&cache_ids_lock, ZBX_MUTEX_CACHE_IDS, error)))
		goto out;

	if (SUCCEED != (ret = zbx_shmem_create(&hc_index_mem, 16 * ZBX_MEBIBYTE, "history index cache",
	 									   "HistoryIndexCacheSize", 0, error)))
	 {
	 	goto out;
	 }

	ids = (ZBX_DC_IDS *)__hc_index_shmem_malloc_func(NULL, sizeof(ZBX_DC_IDS));
		memset(ids, 0, sizeof(ZBX_DC_IDS));

	if (NULL == sql)
		sql = (char *)zbx_malloc(sql, sql_alloc);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Free memory allocated for database cache                          *
 *                                                                            *
//  ******************************************************************************/
 void free_database_cache(int sync)
 {
 	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

 	zbx_shmem_destroy(hc_index_mem);
 	hc_index_mem = NULL;

 	zbx_mutex_destroy(&cache_ids_lock);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
 }

/******************************************************************************
 *                                                                            *
 * Purpose: Return next id for requested table                                *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t DCget_nextid(const char *table_name, int num)
{
	int i;
	DB_RESULT result;
	DB_ROW row;
	const ZBX_TABLE *table;
	ZBX_DC_ID *id;
	zbx_uint64_t min = 0, max = ZBX_DB_MAX_ID, nextid, lastid;

	LOG_DBG("In %s() table:'%s' num:%d", __func__, table_name, num);

	LOCK_CACHE_IDS;

	for (i = 0; i < ZBX_IDS_SIZE; i++)
	{
		id = &ids->id[i];
		if ('\0' == *id->table_name)
			break;

		if (0 == strcmp(id->table_name, table_name))
		{
			nextid = id->lastid + 1;
			id->lastid += num;
			lastid = id->lastid;

			UNLOCK_CACHE_IDS;

			zabbix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" ZBX_FS_UI64 ":" ZBX_FS_UI64 "]",
					   __func__, table_name, nextid, lastid);

			return nextid;
		}
	}

	if (i == ZBX_IDS_SIZE)
	{
		zabbix_log(LOG_LEVEL_ERR, "insufficient shared memory for ids");
		exit(EXIT_FAILURE);
	}

	table = zbx_db_get_table(table_name);

	result = zbx_db_select("select max(%s) from %s where %s between " ZBX_FS_UI64 " and " ZBX_FS_UI64,
					  table->recid, table_name, table->recid, min, max);

	if (NULL != result)
	{
		zbx_strlcpy(id->table_name, table_name, sizeof(id->table_name));

		if (NULL == (row = zbx_db_fetch(result)) || SUCCEED == zbx_db_is_null(row[0]))
			id->lastid = min;
		else
			ZBX_STR2UINT64(id->lastid, row[0]);

		nextid = id->lastid + 1;
		id->lastid += num;
		lastid = id->lastid;
	}
	else
		nextid = lastid = 0;

	UNLOCK_CACHE_IDS;

	zbx_db_free_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" ZBX_FS_UI64 ":" ZBX_FS_UI64 "]",
			   __func__, table_name, nextid, lastid);

	return nextid;
}

