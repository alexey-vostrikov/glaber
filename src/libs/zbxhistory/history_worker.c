/*
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


//TODO :check that api fields conform to the api.go in field names 
#include "common.h"
#include "log.h"
#include "zbxjson.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "zbxhistory.h"
#include "zbxself.h"
#include "history.h"
#include "module.h"
#include "../../libs/zbxexec/worker.h"


size_t	DCconfig_get_trigger_itemids_by_valuetype( int value_type, zbx_vector_uint64_t *vector_itemids);
//int	zbx_vc_simple_add(zbx_uint64_t itemids, zbx_history_record_t *record);

extern int CONFIG_SERVER_STARTUP_TIME;

#define GLB_DEFAULT_WORKER_PRELOAD_VALUES 0
#define GLB_DEFAULT_WORKER_DISABLE_READ	0
#define GLB_DEFAULT_WORKER_WRITE_TYPES "dbl, str, uint, text, log"
#define GLB_DEFAULT_WORKER_READ_TYPES "dbl, str, uint, text, log"

#define GLB_DEFAULT_WORKER_TREND_TYPES "dbl, uint"
#define GLB_DEFAULT_WORKER_READ_AGG_TYPES "dbl, uint"

typedef struct
{
	ext_worker_t *worker;
	u_int8_t read_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t read_agg_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t write_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t read_trend_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t write_trend_types[ITEM_VALUE_TYPE_MAX];	
	u_int8_t preload_types[ITEM_VALUE_TYPE_MAX];
	u_int16_t preload_values;
	u_int16_t disable_read_timeout;
	
}
zbx_worker_data_t;

/************************************************************************************
 *                                                                                  *
 * Function: worker_destroy                                                        *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	worker_destroy(void *data)
{
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
		
	glb_destroy_worker(conf->worker);
	zbx_free(conf->worker);
	zbx_free(data);
}
/************************************************************************************
 *                                                                                  *
 * Function: worker_get_trends                                                      *
 *                                                                                  *
 * Purpose: gets trends  data from the history storage                              *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              itemid  - [IN] the itemid                                           *
 *              start   - [IN] the period start timestamp                           *
 *              count   - [IN] the number of values to read                         *
 *              end     - [IN] the period end timestamp                             *
 *              values  - [OUT] the item history data values                        *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	worker_get_trends_json(void *data, int value_type, zbx_uint64_t itemid, int start, int end, struct zbx_json *json)
{
	
	zabbix_log(LOG_LEVEL_DEBUG, "Start %s()", __func__);
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;

	char request[MAX_STRING_LEN],min_value[MAX_ID_LEN],max_value[MAX_ID_LEN],avg_value[MAX_ID_LEN], i[MAX_ID_LEN], count[MAX_ID_LEN];
	char *response=NULL;

	struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	int valuecount=0;
	zbx_history_record_t	hr;
	zbx_json_type_t type;
	size_t allocd=0,offset=0;

	if (0 == conf->read_trend_types[value_type])	
			return FAIL;
	
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_trends\", \"itemid\":%ld, \"value_type\":%d, \"start\": %d, \"end\":%d }",
				itemid, value_type, start, end);
	
	
	if (SUCCEED != glb_process_worker_request(conf->worker, request, &response)) {
		LOG_DBG("Failed to get info from worker");
		return FAIL;
	}	
	
	zabbix_log(LOG_LEVEL_DEBUG, "Got aggregation responce :%s",response);


	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't ropen JSON response from worker %s %s:", worker_get_path(conf->worker), response);
		return FAIL;
	}
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "aggmetrics", &jp_data)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't find data section in the worker %s  responce %s:",worker_get_path(conf->worker), response);
		return FAIL;
	};
    
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN];
        
        struct zbx_json_parse	jp_row;
		
        if (SUCCEED == zbx_json_brackets_open(p, &jp_row) &&
			SUCCEED == zbx_json_value_by_name(&jp_row, "clock", clck,MAX_ID_LEN, &type) &&
            SUCCEED == zbx_json_value_by_name(&jp_row, "count", count, MAX_ID_LEN,&type) &&
			SUCCEED == zbx_json_value_by_name(&jp_row, "max", max_value, MAX_ID_LEN,&type) && 
			SUCCEED == zbx_json_value_by_name(&jp_row, "avg", avg_value, MAX_ID_LEN,&type) && 
			SUCCEED == zbx_json_value_by_name(&jp_row, "min", min_value, MAX_ID_LEN,&type)
		)
		{
			zbx_json_addobject(json,NULL);
			zbx_json_adduint64 (json, "itemid", itemid);
			zbx_json_addstring( json, "clock", clck, ZBX_JSON_TYPE_INT);
			zbx_json_addstring( json, "value_max", max_value, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring( json, "value_min", min_value, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring( json, "value_avg", avg_value, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring( json, "num", count, ZBX_JSON_TYPE_INT);
			zbx_json_close(json);
		} else {
            LOG_INF( "Couldn't parse JSON row: %s",jp_row.start);
        };
	}
	zbx_free(response);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
	return SUCCEED;
}
/************************************************************************************
 *                                                                                  *
 * Function: worker_get_agg                                                         *
 *                                                                                  *
 * Purpose: gets trends  data from the history storage                              *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              itemid  - [IN] the itemid                                           *
 *              start   - [IN] the period start timestamp                           *
 *              count   - [IN] the number of values to read                         *
 *              end     - [IN] the period end timestamp                             *
 *              buffer  - [OUT] the item history data values                        *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	worker_get_agg_json(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int aggregates, struct zbx_json *json)
{
	
	zabbix_log(LOG_LEVEL_DEBUG, "Start %s()", __func__);
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;

	char request[MAX_STRING_LEN],min_value[MAX_ID_LEN],max_value[MAX_ID_LEN],avg_value[MAX_ID_LEN], i[MAX_ID_LEN], count[MAX_ID_LEN];
	char *response=NULL;

	struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	int valuecount=0;
	zbx_history_record_t	hr;
	zbx_json_type_t type;
	size_t allocd=0,offset=0;

	if (0 == conf->read_agg_types[value_type])	
			return FAIL;
	
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_agg\", \"itemid\":%ld, \"value_type\":%d, \"start\": %d, \"count\":%d, \"end\":%d }",
				itemid,value_type,start,aggregates,end);
	
	zabbix_log(LOG_LEVEL_DEBUG, "Sending request %s",request);
	if (SUCCEED !=glb_process_worker_request(conf->worker, request, &response)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Failed to get info from worker");
		return FAIL;
	}	
	
	zabbix_log(LOG_LEVEL_DEBUG, "Got aggregation responce :%s",response);

	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't ropen JSON response from worker %s %s:",worker_get_path(conf->worker), response);
		return FAIL;
	}
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "aggmetrics", &jp_data)) {
		LOG_DBG("Couldn't find data section in the worker %s  responce %s:",worker_get_path(conf->worker), response);
		return FAIL;
	};
    
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN];
        
        struct zbx_json_parse	jp_row;
		//this is just a json check value by value
        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
							
            if (SUCCEED == zbx_json_value_by_name(&jp_row, "clock", clck,MAX_ID_LEN, &type) &&
               	SUCCEED == zbx_json_value_by_name(&jp_row, "count", count, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_row, "max", max_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "avg", avg_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "min", min_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "i", i, MAX_ID_LEN,&type)
			  )
			{
				zbx_json_addobject(json,NULL);
				zbx_json_adduint64 (json, "itemid", itemid);
				zbx_json_addstring( json, "clock", clck, ZBX_JSON_TYPE_INT);
				zbx_json_addstring( json, "value_max", max_value, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring( json, "value_min", min_value, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring( json, "value_avg", avg_value, ZBX_JSON_TYPE_STRING);
				zbx_json_addstring( json, "i", i, ZBX_JSON_TYPE_INT);
				zbx_json_close(json);
			} 
            
        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"Couldn't parse JSON row: %s",p);
        };
	}
	zbx_free(response);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: worker_get_values                                                     *
 *                                                                                  *
 * Purpose: gets item history data from history storage                             *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              itemid  - [IN] the itemid                                           *
 *              start   - [IN] the period start timestamp                           *
 *              count   - [IN] the number of values to read                         *
 *              end     - [IN] the period end timestamp                             *
 *              values  - [OUT] the item history data values                        *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	worker_get_history(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end, unsigned char interactive,
		zbx_vector_history_record_t *values) 
// TODO : create a possibility to fetch log data by setting logeventid and/or severity and/or source 
{
	zabbix_log(LOG_LEVEL_DEBUG, "Start %s()", __func__);
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;

	char request[MAX_STRING_LEN];
	char *response=NULL;

	struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	int valuecount=0;
	zbx_history_record_t	hr;
	zbx_json_type_t type;
	
	if (0 == conf->read_types[value_type])	{
	
		LOG_DBG("Value type %d isn't handled by this worker",value_type);
		DEBUG_ITEM(itemid, "Value type %d isn't handled by this worker",value_type);

		return FAIL;
	}
	
	if ( GLB_HISTORY_GET_NON_INTERACTIVE == interactive && 
		 time(NULL) - conf->disable_read_timeout < CONFIG_SERVER_STARTUP_TIME) {
		LOG_DBG("waiting for cache load, exiting");
      	return FAIL;
	}
	
	//creating the request
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_history\", \"itemid\":%ld, \"start\": %d, \"count\":%d, \"end\":%d, \"value_type\":%d }",
				itemid,start,count,end,value_type);

	DEBUG_ITEM(itemid, "Requested from the history via history worker, start:%d, count:%d, end:%d ",start, count, end);

	glb_process_worker_request(conf->worker, request, &response);
	
	if (NULL != response)
		DEBUG_ITEM(itemid, "Got response:%s ",response);

	//LOG_INF("Got response: %s",response);	
	if (NULL == response)
			return SUCCEED;
			
	if (ITEM_VALUE_TYPE_LOG == value_type)
		 zabbix_log(LOG_LEVEL_DEBUG, "Got the LOG response: '%s'", response);
	
	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse responce from worker: '%s'",response);
		return SUCCEED;
	}

	zabbix_log(LOG_LEVEL_DEBUG,"parsing worker response '%s':",response);

    if (SUCCEED != zbx_json_brackets_by_name(&jp, "metrics", &jp_data)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"NO data object in the responce JSON");
		return SUCCEED;
	}   

    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *source=NULL;
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN], logeventid[MAX_ID_LEN],severity[MAX_ID_LEN];
		
        
        struct zbx_json_parse	jp_row;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
							
            if (SUCCEED == zbx_json_value_by_name(&jp_row, "time_sec", clck,MAX_ID_LEN, &type) )
				{
				hr.timestamp.sec = atoi(clck);
				hr.timestamp.ns = atoi(ns);
				zabbix_log(LOG_LEVEL_DEBUG,"read: Clock: %s, ns: %s, value: %s, ",clck,ns,value);
				
				//DEBUG_ITEM(itemid, "Got history response: %s -> %s", clck, value );
                
				switch (value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed as UINT64 %s",value);
						if (SUCCEED != zbx_json_value_by_name(&jp_row, "value_int", value, MAX_STRING_LEN,&type) ) continue;
			    		hr.value = history_str2value(value, value_type);
						zbx_vector_history_record_append_ptr(values, &hr);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						if (SUCCEED != zbx_json_value_by_name(&jp_row, "value_dbl", value, MAX_STRING_LEN,&type)) continue;
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed as DBL field %s",value);
			    		hr.value = history_str2value(value, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					
					case ITEM_VALUE_TYPE_LOG:
							hr.value.log = zbx_malloc(NULL,sizeof(zbx_log_value_t));
							
						if (SUCCEED != zbx_json_value_by_name(&jp_row, "value_str", value, MAX_STRING_LEN,&type)) continue;
							hr.value.log->value = zbx_strdup(NULL,value);
						
						if (SUCCEED == zbx_json_value_by_name(&jp_row, "logeventid", logeventid, MAX_ID_LEN,&type)) 
							hr.value.log->logeventid=atoi(logeventid);
						else 
							hr.value.log->logeventid=0;
						
						if (SUCCEED == zbx_json_value_by_name(&jp_row, "severity", severity, MAX_ID_LEN,&type))
							hr.value.log->severity=atoi(severity);
						else 
							hr.value.log->severity=0;
						
						source=zbx_malloc(NULL,MAX_ID_LEN);
						if (SUCCEED == zbx_json_value_by_name(&jp_row, "source", source, MAX_ID_LEN,&type)) 
							hr.value.log->source=source; 
						else {
							hr.value.log->source=NULL;
							zbx_free(source);
						}
						zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:
						if (SUCCEED != zbx_json_value_by_name(&jp_row, "value_str", value, MAX_STRING_LEN,&type)) continue;
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value);
						hr.value = history_str2value(value, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
                        break;
				
					}				
				
				valuecount++;
			} 
          
        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"Couldn't parse JSON row: %s",p);
        };
	}
	zbx_free(response);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: worker_add_history                                                     *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	worker_add_history(void *data, ZBX_DC_HISTORY *hist, int history_num)
{
	char *response=NULL;

    ZBX_DC_HISTORY		*h;
    int i,j,num=0;
    int ret=FAIL;
	
	struct zbx_json json;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	
	zabbix_log(LOG_LEVEL_DEBUG, "Started %s(), %d values", __func__,history_num);	
		
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
	
	if (0 == history_num) 
		return SUCCEED;
	zbx_json_addstring(&json,"request","put_history",ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(&json,"metrics");
	
	for (i = 0; i < history_num; i++)
	{
		h = (ZBX_DC_HISTORY *)&hist[i];
	
		if (0 == conf->write_types[h->value_type]) 
			continue;
		
		if (ITEM_STATE_NORMAL != h->state || 0 != (h->flags & (ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY))) {
			DEBUG_ITEM(h->itemid, "Not saving item's history to worker, flags are %u", h->flags);
			continue;
		}
		
		zbx_json_addobject(&json, NULL);
		zbx_json_addstring(&json,"hostname",h->host_name,ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json,"item_key",h->item_key,ZBX_JSON_TYPE_STRING);
		
		zbx_json_adduint64(&json,"itemid",h->itemid);
		zbx_json_adduint64(&json,"time_sec",h->ts.sec);
		zbx_json_adduint64(&json,"time_ns",h->ts.ns);
		zbx_json_addint64(&json,"value_type", h->value_type);

		switch (h->value_type) {
			case ITEM_VALUE_TYPE_UINT64:
	    		zbx_json_addint64(&json, "value_int", h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_FLOAT: 
		   		zbx_json_addfloat(&json, "value_dbl", h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_LOG:
				if (h->value.log) {
					zbx_json_adduint64(&json,"logeventid",h->value.log->logeventid);
					zbx_json_adduint64(&json,"severity",h->value.log->severity);
					
					if ( NULL != h->value.log->source) 
						zbx_json_addstring(&json,"source",h->value.log->source,ZBX_JSON_TYPE_STRING);

					if ( NULL != h->value.log->value) 
						zbx_json_addstring(&json, "value_str", h->value.log->value, ZBX_JSON_TYPE_STRING);
				}
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				zbx_json_addstring(&json, "value_str", h->value.str, ZBX_JSON_TYPE_STRING);
				break;
			default:
				LOG_WRN("Wrong value type supplied");
				THIS_SHOULD_NEVER_HAPPEN;
				exit(-1);

		}
		zbx_json_close(&json);
		num++;
	}
  	zbx_json_close(&json);

  	LOG_DBG("sending to the worker: %s", json.buffer);

	if (num > 0)
		ret=glb_process_worker_request(conf->worker, json.buffer, &response);
	
    zbx_free(response);
	zbx_json_free(&json);
	
	LOG_DBG("End of %s()", __func__);
	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: worker_add_trends                                                      *
 *                                                                                  *
 * Purpose: sends trends data to the  history storage                               *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	worker_add_trends(void *data, ZBX_DC_TREND *trends, int trends_num)
{
	char *response=NULL;
	int i,num=0;

	struct zbx_json json;

	LOG_DBG("Started %s()", __func__);	
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
		
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
	
	if (0 == trends_num) 
		return SUCCEED;

	zbx_json_addstring(&json,"request","put_history",ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(&json,"aggmetrics");
	
    for (i = 0; i < trends_num; i++)
	{
		if (0 == conf->write_trend_types[trends[i].value_type]) 
			continue;
		
		zbx_json_addobject(&json, NULL);
		zbx_json_addstring(&json,"hostname",trends[i].host_name,ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json,"item_key",trends[i].item_key,ZBX_JSON_TYPE_STRING);
		
	
		zbx_json_adduint64(&json,"itemid",trends[i].itemid);
		zbx_json_adduint64(&json,"time",trends[i].clock);
		zbx_json_adduint64(&json,"value_type", trends[i].value_type);

		switch (trends[i].value_type) {
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_json_addfloat(&json,"min",trends[i].value_min.dbl);
				zbx_json_addfloat(&json,"max",trends[i].value_max.dbl);
				zbx_json_addfloat(&json,"avg",trends[i].value_avg.dbl);
				break;

			case ITEM_VALUE_TYPE_UINT64:
				zbx_json_adduint64(&json,"minint",trends[i].value_min.ui64);
				zbx_json_adduint64(&json,"maxint",trends[i].value_max.ui64);
				zbx_json_adduint64(&json,"avgint", (trends[i].value_avg.ui64.lo /  trends[i].num) );
				break;

		}
		zbx_json_adduint64(&json, "count",trends[i].num );
		zbx_json_close(&json);
		num++;
	}
	zbx_json_close(&json);
	
	LOG_DBG("Syncing %d trend values", num);
   // zbx_json_addraw(&json,NULL,"\n");
	//zbx_snprintf_alloc(&json.buffer,&json.buffer_allocated,&json.buffer_offset + 1,"\n");

    LOG_DBG("Sending trends data %s", json.buffer);
	
	if (num > 0)
		glb_process_worker_request(conf->worker, json.buffer, &response);
    
	zbx_json_free(&json);
    zbx_free(response);
	
	LOG_DBG("End of %s()", __func__);
	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_worker_init                                               *
 *                                                                                  *
 * Purpose: initializes history storage interface                                   *
 *                                                                                  *
 * Parameters:  hist       - [IN] the history storage interface                     *
 *              value_type - [IN] the target value type                             *
 *              error      - [OUT] the error message                                *
 *                                                                                  *
 * Return value: SUCCEED - the history storage interface was initialized            *
 *               FAIL    - otherwise                                                *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_worker_init(char *params)
{
	zbx_worker_data_t	*conf;

	 //history mode expects old good JSON as a config, let's parse it
    struct zbx_json_parse jp, jp_config;
	char  cmd[MAX_STRING_LEN],tmp_str[MAX_STRING_LEN];
	size_t alloc=0,offset=0;
	zbx_json_type_t type;
    
	conf = (zbx_worker_data_t *)zbx_malloc(NULL, sizeof(zbx_worker_data_t));
	memset(conf, 0, sizeof(zbx_worker_data_t));
	
	zabbix_log(LOG_LEVEL_DEBUG,"in %s: starting init", __func__);

    if ( SUCCEED != zbx_json_open(params, &jp_config)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configuration '%s', most likely not a valid JSON",params);
		return FAIL;
	}

	if (NULL == (conf->worker=glb_init_worker(params)) ) {
		zabbix_log(LOG_LEVEL_WARNING,"Load worker history couldn't create new worker");
		return FAIL;
	}

	worker_set_mode_from_worker(conf->worker, GLB_WORKER_MODE_EMPTYLINE);
	worker_set_mode_to_worker(conf->worker, GLB_WORKER_MODE_NEWLINE);
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_WRITE_TYPES,MAX_STRING_LEN);
	zbx_json_value_by_name(&jp_config,"write_types", tmp_str, MAX_STRING_LEN,&type);
	glb_set_process_types(conf->write_types, tmp_str);

	if (glb_types_array_sum(conf->write_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: WRITE types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE,(void (*)(void))worker_add_history,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_READ_TYPES,MAX_STRING_LEN);
	zbx_json_value_by_name(&jp_config,"read_types", tmp_str, MAX_STRING_LEN,&type);
	glb_set_process_types(conf->read_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ,(void (*)(void))worker_get_history,conf);
	}
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_TREND_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_trend_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_trend_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_trend_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: read TREND types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_TRENDS_JSON,(void (*)(void))worker_get_trends_json,conf);
	}
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_TREND_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"write_trend_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->write_trend_types, tmp_str);
	
	if (glb_types_array_sum(conf->write_trend_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: write TREND types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE_TRENDS,(void (*)(void))worker_add_trends,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_READ_AGG_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_agg_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_agg_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_agg_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: read agg types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGG_JSON,(void (*)(void))worker_get_agg_json,conf);
	}

	conf->preload_values=GLB_DEFAULT_WORKER_PRELOAD_VALUES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"preload_values", tmp_str, MAX_ID_LEN,&type) ) 
			conf->preload_values =strtol(tmp_str,NULL,10);
	
	conf->disable_read_timeout=GLB_DEFAULT_WORKER_DISABLE_READ;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"disable_reads", tmp_str, MAX_ID_LEN,&type) ) 
			conf->disable_read_timeout =strtol(tmp_str,NULL,10);

	glb_register_callback(GLB_MODULE_API_DESTROY,(void (*)(void))worker_destroy,conf);
		
	return SUCCEED;
}

