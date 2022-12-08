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

#include "common.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxhistory.h"
#include "history.h"
#include "module.h"
#include "../zbxalgo/vectorimpl.h"


ZBX_VECTOR_IMPL(history_record, zbx_history_record_t);

extern int CONFIG_VALUECACHE_FILL_TIME;
extern int CONFIG_SERVER_STARTUP_TIME;
extern zbx_vector_ptr_t *API_CALLBACKS[GLB_MODULE_API_TOTAL_CALLBACKS];
const char	*value_type_names[] = {"dbl", "str", "log", "uint", "text"};
/************************************************************************************
 *                                                                                  *
 * Function: glb_load_history_module                                                *
 *                                                                                  *
 * Purpose: initializes history storage for the single storage type                 *
 *                                                                                  *
 * Comments:  glaber approach: each module decides which data type to write and read*
*                              based on the module's configuration                  *
 ************************************************************************************/
int glb_load_history_module(char *history_module) {
	
	char *params;
	void *data;

	
	if (NULL != ( params = strchr(history_module, ';'))) {
		*params++ = '\0';
	} else {
		params="";
	}

	LOG_INF("loading history module \"%s\", module params \"%s\"", history_module, params);

	if (NULL != strstr(history_module,"worker")) 
		return glb_history_worker_init(params);
	
	if (NULL != strstr(history_module,"clickhouse")) 
		return glb_history_clickhouse_init(params);
		
	zabbix_log(LOG_LEVEL_WARNING,"Unknown history module type: '%s', exiting",history_module);
	return FAIL;

}


/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_init                                                       *
 *                                                                                  *
 * Purpose: initializes history storage                                             *
 *                                                                                  *
 * Comments: History interfaces are created for all values types based on           *
 *           configuration. Every value type can have different history storage     *
 *           backend.                                                               *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_init(char **history_modules, char **error)
{
	char	**history_module;
	int	ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
	
	if (NULL == *history_modules)
		return SUCCEED;

	for (history_module = history_modules; NULL != *history_module; history_module++)
	{
		if (SUCCEED != (ret = glb_load_history_module(*history_module)))
			return FAIL;
	}

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_destroy                                                    *
 *                                                                                  *
 * Purpose: destroys history storage                                                *
 *                                                                                  *
 * Comments: All interfaces created by zbx_history_init() function are destroyed    *
 *           here.                                                                  *
 *                                                                                  *
 ************************************************************************************/
void glb_history_destroy(void)
{

	int	j,  ret = SUCCEED;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_DESTROY]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_DESTROY]->values[j];
		glb_history_destroy_func_t destroy_module = callback->callback;
		
		destroy_module(callback->callbackData);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}



/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_add_values                                                 *
 *                                                                                  *
 * Purpose: Sends values to the history storage                                     *
 *                                                                                  *
 * Parameters: history - [IN] the values to store                                   *
 *                                                                                  *
 * Comments: add history values to the configured storage backends                  *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_add_history(ZBX_DC_HISTORY *history, int history_num)
{
	int	j,  ret = SUCCEED;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE]->values_num; j++) {
		
		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE]->values[j];
		glb_history_add_func_t write_values = callback->callback;
		
		write_values(callback->callbackData, history, history_num);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: glb_history_add_trends                                                 *
 *                                                                                  *
 * Purpose: Sends trends to the history storage                                     *
 *                                                                                  *
 * Parameters: trends - [IN] the trends data to store                               *
 *                                                                                  *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_add_trends(ZBX_DC_TREND *trends, int trends_num)
{
	int	j,  ret = SUCCEED;
	
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE_TRENDS]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE_TRENDS]->values[j];
		glb_history_add_trends_func_t write_trends = callback->callback;
		
		if (SUCCEED == write_trends(callback->callbackData, trends, trends_num) ) return SUCCEED;
	}

	return SUCCEED;
}


/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_get_values                                                 *
 *                                                                                  *
 * Purpose: gets item values from history storage                                   *
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              count      - [IN] the number of values to read                      *
 *              end        - [IN] the period end timestamp                          *
 *              values     - [OUT] the item history data values 
 * 	            aggregate  - [IN] set to 1 to get aggregated data 					*
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
#define GET_ACCOUNT_INTERVAL 5
int	glb_history_get_history(zbx_uint64_t itemid, int value_type, int start, int count, int end, unsigned char interactive,  zbx_vector_history_record_t *values)
{
	int			j;
	double last_run;
	static int next_account_time=0;
	static double get_runtime = 0.0;
	static char enabled_gets = 1;

	if (time(NULL) > next_account_time) {
		
		//resetting counters
		enabled_gets = 1;
		get_runtime = 0.0;
		next_account_time = time(NULL) + GET_ACCOUNT_INTERVAL;
	} 

	if (enabled_gets && ( get_runtime > GET_ACCOUNT_INTERVAL * 0.5 )) {
		enabled_gets = 0;
		LOG_WRN("Suppressing getting history for %ld sec due to too long get time", next_account_time - time(NULL));
	}

	if ( !enabled_gets && GLB_HISTORY_GET_NON_INTERACTIVE == interactive   ) {
		return FAIL;
	}

	//whoever first gets the data, it's rusult is used 
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ]->values[j];
		glb_history_get_func_t get_values = callback->callback;
		last_run = zbx_time();
		
		if (SUCCEED == get_values(callback->callbackData , value_type, itemid, start, count, end, interactive, values)) {
			get_runtime += zbx_time() - last_run;	
			zabbix_log(LOG_LEVEL_DEBUG,"Current runtime is %f stat is %d",get_runtime,enabled_gets);
			return SUCCEED;
		}
		
		get_runtime += zbx_time() - last_run;	
	}

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_get_agg_buff                                               *
 *                                                                                  *
 * Purpose: gets aggregated item values from history 				                *
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              count      - [IN] the number of values to read                      *
 *              end        - [IN] the period end timestamp                          *
 *              buffer     - [OUT] buffer that hold all the aggregated metrics		*
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_get_history_aggregates_json(zbx_uint64_t itemid, int value_type, int start, int aggregates, int end, struct zbx_json* json)
{
	int			j, ret=FAIL;
	
	//zabbix_log(LOG_LEVEL_INFORMATION,"Starting %s",__func__);
	
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_AGG_JSON]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_AGG_JSON]->values[j];
		glb_history_get_agg_buff_func_t get_values = callback->callback;
		
		if (SUCCEED == get_values(callback->callbackData , value_type, itemid, start, aggregates ,end, json)) 
			return SUCCEED;
	}

	return ret;
}
/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_get_agg_buff                                               *
 *                                                                                  *
 * Purpose: gets aggregated item values from history 				                *
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              count      - [IN] the number of values to read                      *
 *              end        - [IN] the period end timestamp                          *
 *              buffer     - [OUT] buffer that hold all the aggregated metrics		*
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
int	glb_history_get_trends_aggregates_json(zbx_uint64_t itemid, int value_type, int start, int steps, int end, struct zbx_json *json)
{
	int			j, ret=FAIL;
	
	//zabbix_log(LOG_LEVEL_INFORMATION,"Starting %s",__func__);
	
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_TRENDS_AGG_JSON]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_TRENDS_AGG_JSON]->values[j];
		glb_history_get_agg_buff_func_t get_values = callback->callback;
		
		if (SUCCEED == get_values(callback->callbackData, value_type, itemid, start, steps, end, json)) 
			return SUCCEED;
	}

	return ret;
}


/************************************************************************************
 *                                                                                  *
 * Function: glb_history_get_trends                                                 *
 *                                                                                  *
 * Purpose: gets trends form a history storage										*
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              end        - [IN] the period end timestamp                          *
 *              buffer     - [OUT] buffer containing string with all fetched trends *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
  ************************************************************************************/
int	glb_history_get_trends_json(zbx_uint64_t itemid, int value_type, int start, int end, struct zbx_json* json)
{
	int			j, ret=FAIL;

	//we'll ask dor data for every registered module
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_TRENDS_JSON]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_TRENDS_JSON]->values[j];
		glb_history_get_trends_json_func_t get_trends = callback->callback;
		
		if (SUCCEED == get_trends(callback->callbackData,value_type,itemid,start,end, json)) 
			ret = SUCCEED;
	}

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: history_logfree                                                  *
 *                                                                            *
 * Purpose: frees history log and all resources allocated for it              *
 *                                                                            *
 * Parameters: log   - [IN] the history log to free                           *
 *                                                                            *
 ******************************************************************************/
static void	history_logfree(zbx_log_value_t *log)
{
	zbx_free(log->source);
	zbx_free(log->value);
	zbx_free(log);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_vector_destroy                                *
 *                                                                            *
 * Purpose: destroys value vector and frees resources allocated for it        *
 *                                                                            *
 * Parameters: vector    - [IN] the value vector                              *
 *                                                                            *
 * Comments: Use this function to destroy value vectors created by            *
 *           zbx_vc_get_values_by_* functions.                                *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_record_vector_destroy(zbx_vector_history_record_t *vector, int value_type)
{
	if (NULL != vector->values)
	{
		zbx_history_record_vector_clean(vector, value_type);
		zbx_vector_history_record_destroy(vector);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_clear                                         *
 *                                                                            *
 * Purpose: frees resources allocated by a cached value                       *
 *                                                                            *
 * Parameters: value      - [IN] the cached value to clear                    *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_record_clear(zbx_history_record_t *value, int value_type)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_free(value->value.str);
			break;
		case ITEM_VALUE_TYPE_LOG:
			history_logfree(value->value.log);
	}
}

/*********************************************************************
 * parses json, looks for value_type and itemid fields 
 * also parses value field according to value_type
 * if value_type is log, then additional log fields are searched and 
 * parsed too
 * ******************************************************************/
int glb_history_json2val(struct zbx_json_parse *jp, char value_type, zbx_history_record_t * value){
	char  timestamp_str[MAX_ID_LEN], value_str[MAX_STRING_LEN];
	zbx_json_type_t type;

	if ( SUCCEED != zbx_json_value_by_name(jp,"value",value_str,MAX_STRING_LEN, &type) || 
		 SUCCEED != zbx_json_value_by_name(jp,"ts",timestamp_str,MAX_ID_LEN, &type) ) {
			return FAIL;
		}
		
	value->timestamp.sec = strtol(timestamp_str,NULL,10);
	value->timestamp.ns = 0;

	switch (value_type) {
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			value->value.str=zbx_strdup(NULL,value_str);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			value->value.ui64 = strtol(value_str,NULL,10);
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			value->value.dbl = strtof(value_str,NULL);
			break;
		case ITEM_VALUE_TYPE_LOG:
			value->value.log = zbx_malloc(NULL, sizeof(zbx_log_value_t));
			if ( NULL == value->value.log) return FAIL;
			char tmp_str[MAX_STRING_LEN];

			if (SUCCEED == zbx_json_value_by_name(jp,"logeventid",tmp_str,MAX_STRING_LEN, &type)) {
				value->value.log->logeventid=strtol(tmp_str,NULL,10);
			} else value->value.log->logeventid = 0;
			
			if (SUCCEED == zbx_json_value_by_name(jp,"source",tmp_str,MAX_STRING_LEN, &type)) {
				value->value.log->source=zbx_strdup(NULL,tmp_str);
			} else value->value.log->source = zbx_strdup(NULL,"");
			

			if (SUCCEED == zbx_json_value_by_name(jp,"severity",tmp_str,MAX_STRING_LEN, &type)) {
				value->value.log->severity=strtol(tmp_str,NULL,10);
			} else value->value.log->severity = 0;
			
			value->value.log->value=zbx_strdup(NULL,value_str);
			break;
		default:
			zabbix_log(LOG_LEVEL_WARNING,"Unknown item_value_type %d",value_type);
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_value2str                                            *
 *                                                                            *
 * Purpose: converts history value to string format                           *
 *                                                                            *
 * Parameters: buffer     - [OUT] the output buffer                           *
 *             size       - [IN] the output buffer size                       *
 *             value      - [IN] the value to convert                         *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_value2str(char *buffer, size_t size, const history_value_t *value, int value_type)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_snprintf(buffer, size, ZBX_FS_DBL64, value->dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_snprintf(buffer, size, ZBX_FS_UI64, value->ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_strlcpy_utf8(buffer, value->str, size);
			break;
		case ITEM_VALUE_TYPE_LOG:
			zbx_strlcpy_utf8(buffer, value->log->value, size);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_value2str_dyn                                        *
 *                                                                            *
 * Purpose: converts history value to string format (with dynamic buffer)     *
 *                                                                            *
 * Parameters: value      - [IN] the value to convert                         *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 * Return value: The value in text format.                                    *
 *                                                                            *
 ******************************************************************************/
char	*zbx_history_value2str_dyn(const history_value_t *value, int value_type)
{
	char	*str = NULL;
	size_t	str_alloc = 0, str_offset = 0;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_DBL, value->dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_snprintf_alloc(&str, &str_alloc, &str_offset, ZBX_FS_UI64, value->ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			str = zbx_strdup(NULL, value->str);
			break;
		case ITEM_VALUE_TYPE_LOG:
			str = zbx_strdup(NULL, value->log->value);
	}
	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_value_print                                          *
 *                                                                            *
 * Purpose: converts history value to string format (double type printed in   *
 *          human friendly format)                                            *
 *                                                                            *
 * Parameters: buffer     - [OUT] the output buffer                           *
 *             size       - [IN] the output buffer size                       *
 *             value      - [IN] the value to convert                         *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_value_print(char *buffer, size_t size, const history_value_t *value, int value_type)
{
	if (ITEM_VALUE_TYPE_FLOAT == value_type)
		zbx_print_double(buffer, size, value->dbl);
	else
		zbx_history_value2str(buffer, size, value, value_type);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_vector_clean                                  *
 *                                                                            *
 * Purpose: releases resources allocated to store history records             *
 *                                                                            *
 * Parameters: vector      - [IN] the history record vector                   *
 *             value_type  - [IN] the type of vector values                   *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_record_vector_clean(zbx_vector_history_record_t *vector, int value_type)
{
	int	i;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			for (i = 0; i < vector->values_num; i++)
				zbx_free(vector->values[i].value.str);

			break;
		case ITEM_VALUE_TYPE_LOG:
			for (i = 0; i < vector->values_num; i++)
				history_logfree(vector->values[i].value.log);
	}

	zbx_vector_history_record_clear(vector);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_compare_asc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   <0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 >0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in ascending *
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
int	zbx_history_record_compare_asc_func(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	if (d1->timestamp.sec == d2->timestamp.sec)
		return d1->timestamp.ns - d2->timestamp.ns;

	return d1->timestamp.sec - d2->timestamp.sec;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_history_record_compare_desc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   >0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 <0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in descending*
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
int	zbx_history_record_compare_desc_func(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	if (d1->timestamp.sec == d2->timestamp.sec)
		return d2->timestamp.ns - d1->timestamp.ns;

	return d2->timestamp.sec - d1->timestamp.sec;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_value2variant                                        *
 *                                                                            *
 * Purpose: converts history value to variant value                           *
 *                                                                            *
 * Parameters: value      - [IN] the value to convert                         *
 *             value_type - [IN] the history value type                       *
 *             var        - [IN] the output value                             *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_value2variant(const history_value_t *value, unsigned char value_type, zbx_variant_t *var)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_variant_set_dbl(var, value->dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_variant_set_ui64(var, value->ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_variant_set_str(var, zbx_strdup(NULL, value->str));
			break;
		case ITEM_VALUE_TYPE_LOG:
			zbx_variant_set_str(var, zbx_strdup(NULL, value->log->value));
	}
}


/******************************************************************************
 *                                                                            *
 * Function: glb_set_process_types				                              *
 *                                                                            *
 * Purpose: sets types arryay if the type names are present in the setting    *
 *                                                                            *
 * Parameters: types_array   - [out] array to set                             *
 * 				    setting   - [int] string containing the names             *
 ******************************************************************************/

int glb_set_process_types(u_int8_t *types_array, char *setting) {
	
	int i;
	//zabbix_log(LOG_LEVEL_INFORMATION,"Processing types: %s",setting);
	for (i=0; i< ITEM_VALUE_TYPE_MAX; i++) {
		if ( NULL != strstr(setting,value_type_names[i])) {
			//zabbix_log(LOG_LEVEL_INFORMATION,"Enabling value type:%s", value_type_names[i]);
			types_array[i]=1; 
		
		}
		else types_array[i]=0; 
	}
}

/******************************************************************************
 *                                                                            *
 * Function: glb_types_array_sum				                              *
 *                                                                            *
 * Purpose: guess? :) to shorten the code to calc types processing logic      *
 *                                                                            *
******************************************************************************/
int glb_types_array_sum(u_int8_t *types_array) {
	int i, sum=0;
	for (i=0; i< ITEM_VALUE_TYPE_MAX; i++) sum+=types_array[i];
	return sum;
}

history_value_t	history_str2value(char *str, unsigned char value_type)
{
	history_value_t	value;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_LOG:
			value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
			memset(value.log, 0, sizeof(zbx_log_value_t));
			value.log->value = zbx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			value.str = zbx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			value.dbl = atof(str);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			ZBX_STR2UINT64(value.ui64, str);
			break;
	}

	return value;
}


int glb_history_history_record_to_json(u_int64_t itemid, int value_type, zbx_history_record_t *record, struct zbx_json *json) {
			
	zbx_json_addobject(json,NULL);
	zbx_json_adduint64string (json, "itemid", itemid);
	zbx_json_addint64string (json,"clock", record->timestamp.sec);
	zbx_json_addint64string (json,"ns", record->timestamp.ns);
	
	switch (value_type) {
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_json_addfloatstring(json,"value", record->value.dbl);
		 	break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_json_adduint64string(json,"value", record->value.ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_json_addstring(json,"value", record->value.str,ZBX_JSON_TYPE_STRING);
			break;
		case ITEM_VALUE_TYPE_LOG:
			zbx_json_addstring(json,"value", record->value.log->value,  ZBX_JSON_TYPE_STRING);
			zbx_json_addint64string(json,"logeventid", record->value.log->logeventid);
			zbx_json_addint64string(json,"severity", record->value.log->severity);
			
			if (NULL != record->value.log->source)
				zbx_json_addstring(json,"source", record->value.log->source, ZBX_JSON_TYPE_STRING);
			
			break;
	}
	zbx_json_close(json);
}