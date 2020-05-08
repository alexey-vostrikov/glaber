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
int	zbx_vc_simple_add(zbx_uint64_t itemids, zbx_history_record_t *record);

extern int CONFIG_SERVER_STARTUP_TIME;

#define GLB_DEFAULT_WORKER_PRELOAD_VALUES 0
#define GLB_DEFAULT_WORKER_DISABLE_READ	1800
#define GLB_DEFAULT_WORKER_TYPES "dbl, str, uint, text, log"

typedef struct
{
	//char	*cmd;
	//char	*params;
	DC_EXT_WORKER *worker;
	u_int8_t read_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t write_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t read_aggregate_types[ITEM_VALUE_TYPE_MAX];
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
static int	worker_get_agg_values(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int aggregates,
		char **buffer)
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

	if (0 == conf->read_aggregate_types[value_type])	
			return SUCCEED;
	
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get_aggregated\", \"itemid\":%d, \"start\": %d, \"steps\":%d, \"end\":%d }\n",
				itemid,start,aggregates,end);
	
	//requesting, we'll get multiline responce simple json from there
	glb_process_worker_request(conf->worker, request, &response);	//requesting, we'll get multiline responce simple json from there
	
	zabbix_log(LOG_LEVEL_DEBUG, "Got aggregation responce :%s",response);

	if (SUCCEED != zbx_json_open(response, &jp)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON response from worker %s %s:",conf->worker->path, response);
		return FAIL;
	}
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "data", &jp_data)) {
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
                SUCCEED == zbx_json_value_by_name(&jp_row, "ns", ns, MAX_ID_LEN,&type) &&
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

	zabbix_log(LOG_LEVEL_INFORMATION, "End of %s()", __func__);
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
static int	worker_get_values(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end,
		zbx_vector_history_record_t *values)
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
	if (time(NULL)- conf->disable_read_timeout < CONFIG_SERVER_STARTUP_TIME) {
		zabbix_log(LOG_LEVEL_DEBUG, "waiting for cache load, exiting");
      	return SUCCEED;
	}
	
	//creating the request
	zbx_snprintf(request,MAX_STRING_LEN, "{\"request\":\"get\", \"itemid\":%d, \"start\": %d, \"count\":%d, \"end\":%d }\n",
				itemid,start,count,end);
	
	glb_process_worker_request(conf->worker, request, &response);
	zabbix_log(LOG_LEVEL_DEBUG,"worker pid is %d",conf->worker->pid);

	if (NULL == response  || SUCCEED != zbx_json_open(response, &jp))
		return SUCCEED;
	
	zabbix_log(LOG_LEVEL_DEBUG,"parsing worker response '%s':",response);

    if (SUCCEED != zbx_json_brackets_by_name(&jp, "data", &jp_data)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"NO data object in the responce JSON");
		return SUCCEED;
	}   

    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
        char clck[MAX_ID_LEN], ns[MAX_ID_LEN],value[MAX_STRING_LEN];
        
        struct zbx_json_parse	jp_row;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
							
            if (SUCCEED == zbx_json_value_by_name(&jp_row, "time_sec", clck,MAX_ID_LEN, &type) &&
                SUCCEED == zbx_json_value_by_name(&jp_row, "value", value, MAX_STRING_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_row, "time_ns", ns, MAX_ID_LEN,&type)   )
				{
				hr.timestamp.sec = atoi(clck);
				hr.timestamp.ns = atoi(ns);
				zabbix_log(LOG_LEVEL_INFORMATION,"read: Clock: %s, ns: %s, value: %s, ",clck,ns,value);

                switch (value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",value);
			    		hr.value = history_str2value(value, value_type);
						zbx_vector_history_record_append_ptr(values, &hr);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",value);
			    		hr.value = history_str2value(value, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:

						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value);
						hr.value = history_str2value(value, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
                        break;

					case ITEM_VALUE_TYPE_LOG:
						//todo: implement log data parisng 
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
 * Function: worker_add_values                                                     *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	worker_add_values(void *data, const zbx_vector_ptr_t *history)
{
	char *response=NULL;
    char *req_buffer=NULL;
    size_t	req_alloc = 0, req_offset = 0;
    ZBX_DC_HISTORY		*h;
    int i,j,num=0;
    int ret=FAIL;
	char buffer [MAX_STRING_LEN*2];

	zabbix_log(LOG_LEVEL_DEBUG, "Started %s()", __func__);	
	zbx_worker_data_t	*conf = (zbx_worker_data_t *)data;
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"request\":\"put\", \"data\":[", buffer);
    //converitng the data to string
    for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];
	
		if (0 == conf->write_types[h->value_type]) 
			continue;
	
		if ( ITEM_VALUE_TYPE_LOG == h->value_type) continue;
		glb_escape_worker_string(h->host_name,buffer);
		
		if (num) zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,",", buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"request\":\"put\", \"hostname\":\"%s\",", buffer);
		
		buffer[0]='\0';
		glb_escape_worker_string(h->item_key,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset," \"item_key\":\"%s\",", buffer);
		
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"itemid\":%ld, \"time_sec\":%ld, \"time_ns\":%ld, \"value_type\":%d, ", h->itemid,h->ts.sec,h->ts.ns, h->value_type);
    	
		//type-dependent part
		if (ITEM_VALUE_TYPE_UINT64 == h->value_type) 
	           zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value\":%ld" ,h->value.ui64);
    	
		if (ITEM_VALUE_TYPE_FLOAT == h->value_type) 
           zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value\":%f",h->value.dbl);
        
		if (ITEM_VALUE_TYPE_STR == h->value_type || ITEM_VALUE_TYPE_TEXT == h->value_type ) {
			buffer[0]='\0';
			buffer[1]='\0';
			
			glb_escape_worker_string(h->value.str,buffer);    		
        	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value\":\"%s\"", buffer);
		}
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"}");
		num++;
	}
    //adding empty line to the request's end to signal end of request;
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"]}\n");

    //zabbix_log(LOG_LEVEL_INFORMATION,"%s",req_buffer);
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
	//disable_reads: 1800 by default (how long not to do readings)

	 //history mode expects old good JSON as a config, let's parse it
    struct zbx_json_parse jp, jp_config;
	char  cmd[MAX_STRING_LEN],tmp_str[MAX_ID_LEN];
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
	
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"write_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->write_types, tmp_str);

	if (glb_types_array_sum(conf->write_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: WRITE types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE,(void (*)(void))worker_add_values,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ,(void (*)(void))worker_get_values,conf);
	}
	

	zbx_strlcpy(tmp_str,GLB_DEFAULT_WORKER_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_aggregated_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_aggregate_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_aggregate_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init worker module: AGGREGATE READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGGREGATED,(void (*)(void))worker_get_agg_values,conf);
	}
	
	conf->preload_values=GLB_DEFAULT_WORKER_PRELOAD_VALUES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"read_aggregate_types", tmp_str, MAX_ID_LEN,&type) ) 
			conf->preload_values =strtol(tmp_str,NULL,10);
	
	conf->preload_values=GLB_DEFAULT_WORKER_DISABLE_READ;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"disable_reads", tmp_str, MAX_ID_LEN,&type) ) 
			conf->disable_read_timeout =strtol(tmp_str,NULL,10);

	conf->preload_values=GLB_DEFAULT_WORKER_PRELOAD_VALUES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"preload_values", tmp_str, MAX_ID_LEN,&type) ) 
			conf->preload_values=strtol(tmp_str,NULL,10);
	
	glb_register_callback(GLB_MODULE_API_DESTROY,(void (*)(void))worker_destroy,conf);
		
	return SUCCEED;
}

