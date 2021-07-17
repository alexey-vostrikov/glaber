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
	GLB_EXT_WORKER *worker;
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
static int	worker_get_trends(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int aggregates, char **buffer)
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
	//creating the request
	*buffer=NULL;

	if (0 == conf->read_trend_types[value_type])	
			return FAIL;
	
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_trends\", \"itemid\":%ld, \"value_type\":%d, \"start\": %d, \"count\":%d, \"end\":%d }\n",
				itemid,value_type,start,aggregates,end);
	
	
	if (SUCCEED != glb_process_worker_request(conf->worker, request, &response)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Failed to get info from worker");
		return FAIL;
	}	
	
	zabbix_log(LOG_LEVEL_DEBUG, "Got aggregation responce :%s",response);


	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't ropen JSON response from worker %s %s:",conf->worker->path, response);
		return FAIL;
	}
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "aggmetrics", &jp_data)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't find data sesction in the worker %s  responce %s:",conf->worker->path, response);
		return FAIL;
	};
    
	zbx_snprintf_alloc(buffer,&allocd,&offset,"[");
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN];
        
        struct zbx_json_parse	jp_row;
		
        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
							
            if (SUCCEED == zbx_json_value_by_name(&jp_row, "clock", clck,MAX_ID_LEN, &type) &&
               	SUCCEED == zbx_json_value_by_name(&jp_row, "count", count, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_row, "max", max_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "avg", avg_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "min", min_value, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "i", i, MAX_ID_LEN,&type)
			  )
			{
				//all attributes are here, so we know that jp_row holds the values, we just add jp_row 
				//to the output buffer (or we might be generating own )
				size_t buf_size=jp_row.end-jp_row.start+1;
				if (valuecount > 0) zbx_snprintf_alloc(buffer,&allocd,&offset,",");
				zbx_strncpy_alloc(buffer,&allocd,&offset,jp_row.start,buf_size);
		
				valuecount++;
			} 
            
        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"Couldn't parse JSON row: %s",p);
        };
	}
	zbx_snprintf_alloc (buffer,&allocd,&offset,"]");
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
static int	worker_get_agg(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int aggregates, char **buffer)
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
	//creating the request
	*buffer=NULL;
	
	if (0 == conf->read_agg_types[value_type])	
			return FAIL;
	
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_agg\", \"itemid\":%ld, \"value_type\":%d, \"start\": %d, \"count\":%d, \"end\":%d }\n",
				itemid,value_type,start,aggregates,end);
	
	zabbix_log(LOG_LEVEL_DEBUG, "Sending request %s",request);
	if (SUCCEED !=glb_process_worker_request(conf->worker, request, &response)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Failed to get info from worker");
		return FAIL;
	}	
	
	zabbix_log(LOG_LEVEL_DEBUG, "Got aggregation responce :%s",response);


	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't ropen JSON response from worker %s %s:",conf->worker->path, response);
		return FAIL;
	}
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "aggmetrics", &jp_data)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't find data sesction in the worker %s  responce %s:",conf->worker->path, response);
		return FAIL;
	};
    
	zbx_snprintf_alloc(buffer,&allocd,&offset,"[");
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
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
				//all attributes are here, so we know that jp_row holds the values, we just add jp_row 
				//to the output buffer (or we might be generating own )
				size_t buf_size=jp_row.end-jp_row.start+1;
				if (valuecount > 0) 
					zbx_snprintf_alloc(buffer,&allocd,&offset,",");
				
				zbx_strncpy_alloc(buffer,&allocd,&offset,jp_row.start,buf_size);
		
				valuecount++;
			} 
            
        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"Couldn't parse JSON row: %s",p);
        };
	}
	zbx_snprintf_alloc (buffer,&allocd,&offset,"]");
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
	
	if (0 == conf->read_types[value_type])	
			return SUCCEED;
	
	//this isn't really nice idea since we might be getting some get_values from the zabbix frontend
	if ( GLB_HISTORY_GET_NON_INTERACTIVE == interactive && 
		 time(NULL)- conf->disable_read_timeout < CONFIG_SERVER_STARTUP_TIME) {
		zabbix_log(LOG_LEVEL_DEBUG, "waiting for cache load, exiting");
      	return SUCCEED;
	}
	
	//creating the request
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_history\", \"itemid\":%ld, \"start\": %d, \"count\":%d, \"end\":%d, \"value_type\":%d }\n",
				itemid,start,count,end,value_type);
	
	glb_process_worker_request(conf->worker, request, &response);
	
	if (NULL == response)
			return SUCCEED;
	if (ITEM_VALUE_TYPE_LOG == value_type)
		 zabbix_log(LOG_LEVEL_INFORMATION, "Got the LOG response: '%s'", response);
	
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
        char *itemid=NULL,*source=NULL;
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN], logeventid[MAX_ID_LEN],severity[MAX_ID_LEN];
		
        
        struct zbx_json_parse	jp_row;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
							
            if (SUCCEED == zbx_json_value_by_name(&jp_row, "time_sec", clck,MAX_ID_LEN, &type) )
				{
				hr.timestamp.sec = atoi(clck);
				hr.timestamp.ns = atoi(ns);
				zabbix_log(LOG_LEVEL_DEBUG,"read: Clock: %s, ns: %s, value: %s, ",clck,ns,value);

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
static int	worker_add_history(void *data, const zbx_vector_ptr_t *history)
{
	char *response=NULL;
    char *req_buffer=NULL;
    size_t	req_alloc = 0, req_offset = 0;
    ZBX_DC_HISTORY		*h;
    int i,j,num=0;
    int ret=FAIL;
	char buffer [MAX_STRING_LEN*20];
	
	zabbix_log(LOG_LEVEL_DEBUG, "Started %s()", __func__);	
		
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
	
	if (0 == history->values_num) 
		return SUCCEED;
	
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"request\":\"put_history\", \"metrics\":[");
    //converitng the data to string
	zabbix_log(LOG_LEVEL_DEBUG, "Started 1 %s()", __func__);	
    for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];
	
		if (0 == conf->write_types[h->value_type]) 
			continue;
		
		
		
		if (num) zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,",");
		
		glb_escape_worker_string(h->host_name,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"hostname\":\"%s\",", buffer);
		buffer[0]='\0';
		
		glb_escape_worker_string(h->item_key,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset," \"item_key\":\"%s\",", buffer);
		
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"itemid\":%ld, \"time_sec\":%d, \"time_ns\":%d, \"value_type\":%d, ", 
				h->itemid,h->ts.sec,h->ts.ns, h->value_type);
    		
		//type-dependent part
		switch (h->value_type) {
			case ITEM_VALUE_TYPE_UINT64:
	    		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_int\":%ld" ,h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_FLOAT: 
		   		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_dbl\":%f",h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_LOG:
				//zabbix_log(LOG_LEVEL_INFORMATION,"Writing log data: %s",h->value.log->value);
				if (h->value.log) {
					buffer[0]=0;

					zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"logeventid\":%d, \"severity\":%d",h->value.log->logeventid,h->value.log->severity);
					
					if ( NULL != h->value.log->source) {
						glb_escape_worker_string(h->value.log->source,buffer);  
						zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,",\"source\":\"%s\"",h->value.log->source, buffer);
					}	
				
					//zabbix_log(LOG_LEVEL_INFORMATION,"Writing log data 3"); 
					if ( NULL != h->value.log->value) {
						glb_escape_worker_string(h->value.log->value,buffer);  
					//	zabbix_log(LOG_LEVEL_INFORMATION,"Writing log data 4"); 
						zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,",\"value_str\":\"%s\"", buffer);
					}
				}
				//zabbix_log(LOG_LEVEL_INFORMATION,"Writing log data 5"); 
				//zabbix_log(LOG_LEVEL_INFORMATION,"Will send %s",req_buffer);
				break;
				
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				buffer[0]=0;
		
				glb_escape_worker_string(h->value.str,buffer);  
				//zabbix_log(LOG_LEVEL_INFORMATION,"Transform: %s -> %s",h->value.str,buffer);
				
				zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_str\":\"%s\"", buffer);
				break;

		}
		
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"}");
		//zabbix_log(LOG_LEVEL_INFORMATION,"Will send %s",req_buffer);
		num++;
	}
  
  	//adding empty line to the request's end to signal end of request;
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"]}\n");

    zabbix_log(LOG_LEVEL_DEBUG,"sending to the worker: %s",req_buffer);
	if (num > 0)
		ret=glb_process_worker_request(conf->worker, req_buffer, &response);
	
    zbx_free(req_buffer);
    zbx_free(response);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
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
    char *req_buffer=NULL;
    size_t	req_alloc = 0, req_offset = 0;
       
	int i,j,num=0;
    int ret=FAIL;
	char buffer [MAX_STRING_LEN*2];
	char *precision="%0.6f,";
	
	zabbix_log(LOG_LEVEL_DEBUG, "Started %s()", __func__);	
		
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
	
	if (0 == trends_num) 
		return SUCCEED;
	
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"request\":\"put_trends\", \"aggmetrics\":[");
    
    for (i = 0; i < trends_num; i++)
	{
		if (0 == conf->write_trend_types[trends[i].value_type]) 
			continue;
		
		glb_escape_worker_string(trends[i].host_name,buffer);
		
		if (num) zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,",");
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"hostname\":\"%s\",", buffer);
		
		buffer[0]='\0';
		glb_escape_worker_string(trends[i].item_key,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset," \"item_key\":\"%s\",", buffer);
		
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"itemid\":%ld, \"time\":%d, \"value_type\":%d, ", trends[i].itemid,trends[i].clock, trends[i].value_type);
    	
		switch (trends[i].value_type) {
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"min\":%0.4f, \"max\":%0.4f,\"avg\":%0.4f,",
					trends[i].value_min.dbl,
					trends[i].value_max.dbl,
					trends[i].value_avg.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"minint\":%ld,\"maxint\":%ld,\"avgint\":%ld,",
			   		trends[i].value_min.ui64,
					trends[i].value_max.ui64,
					(trends[i].value_avg.ui64.lo / trends[i].num) );
				break;
		}
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset," \"count\":%d", trends[i].num);

		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"}");
		num++;
	}
    zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"]}\n");

    //zabbix_log(LOG_LEVEL_INFORMATION,"TRENDS DATA %s",req_buffer);
	if (num > 0)
		ret=glb_process_worker_request(conf->worker, req_buffer, &response);
	
    zbx_free(req_buffer);
    zbx_free(response);

	return ret;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
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

	//for worker there are following params are parsed:
	//url : by default http://localhost:8123
	//write_types: (str, text, ui64, double)
	//read_types: (str, text, ui64, double)
	//preload_types: (str, text, ui64, double)
	//preload_values: 10
	//read_aggregate_types: (str, text, ui64, double) 
	//disable_reads: 0 sec by default (how long not to do readings)

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

	conf->worker->mode_from_worker=GLB_WORKER_MODE_EMPTYLINE;
	conf->worker->mode_to_worker=GLB_WORKER_MODE_NEWLINE;
	
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
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_TRENDS,(void (*)(void))worker_get_trends,conf);
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
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGG,(void (*)(void))worker_get_agg,conf);
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

