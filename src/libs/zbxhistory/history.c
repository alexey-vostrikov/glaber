/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

//extern char	*CONFIG_HISTORY_STORAGE_URL;
//extern char	*CONFIG_HISTORY_STORAGE_OPTS;
//extern char	*CONFIG_HISTORY_STORAGE_TYPE;
extern int CONFIG_VALUECACHE_FILL_TIME;
extern int CONFIG_SERVER_STARTUP_TIME;

//zbx_history_iface_t	history_ifaces[ITEM_VALUE_TYPE_MAX];

extern zbx_vector_ptr_t *API_CALLBACKS[GLB_MODULE_API_TOTAL_CALLBACKS];
const char	*value_type_names[] = {"dbl", "str", "log", "uint", "text"};

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_preload                                                  	*
 *                                                                                  *
 * preloads history into the value cache 											*
 * returns number of values preloaded												*
  ************************************************************************************/
int	zbx_history_preload()
{
	int j;

	//zbx_history_iface_t	*h_writer = &history_ifaces[value_type];
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_VC_PRELOAD]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_VC_PRELOAD]->values[j];
		glb_history_preload_values_func_t preload_values = callback->callback;
		
		preload_values(callback->callbackData);
	}

	return SUCCEED;
}
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
	//first, splitting to module name and params:
	char *params="";
	void *data;

	//looking if name also has params (after ;)
	if (NULL!=(params = strchr(history_module, ';'))) {
			*params++ = '\0';
	} else {
		params="";
	}

	zabbix_log(LOG_LEVEL_INFORMATION, "loading history module \"%s\", module params \"%s\"", history_module, params);
	//there are three steps to make a new module:
	//1. - call init so the the module will parse it's data and return pointer to it
	//2. - register api callbacks (this is what the module will do during the load)

	//parsing some common vars
	if (NULL != strstr(history_module,"clickhouse")) 
		return glb_history_clickhouse_init(params);
	
	if (NULL != strstr(history_module,"victoriametrics")) 
		return glb_history_vmetrics_init(params);
	
	if (NULL != strstr(history_module,"worker")) 
		return glb_history_worker_init(params);
		
	
/*

	if (NULL != strstr(history_module,"sql")) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Doing SQL history storage init (why do you use glaber at all? )");
	//	glb_sql_history_init(params);
	} else  if (NULL != strstr(history_module,"elastics")) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Doing elastics storage init");
	//	glb_elastics_history_init(params);
	}
*/
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
	//int		i, ret;
	//history modules is registered via HistoryModule=<name>;<params> 
	//dirctive where name suggest on of existing history modules:
	//clickkhouse, victoriametrics, worker, elastics, sql
	//worker modules is out of process runners for better compatibility
	char	**history_module;
	int	ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
	
	if (NULL == *history_modules)
		return SUCCEED;

	for (history_module = history_modules; NULL != *history_module; history_module++)
	{
		//zabbix_log(LOG_LEVEL_INFORMATION, "Loading module %s", *history_module);
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
void zbx_history_destroy(void)
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
int	zbx_history_add_values(const zbx_vector_ptr_t *history)
{
	int	j,  ret = SUCCEED;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);


	//sending everyone the agregated data 
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE]->values[j];
		glb_history_add_values_func_t write_values = callback->callback;
		
		write_values(callback->callbackData, history);
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
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);


	//sending everyone the agregated data 
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE_TRENDS]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE_TRENDS]->values[j];
		glb_history_add_trends_func_t write_trends = callback->callback;
		
		if (SUCCEED == write_trends(callback->callbackData, trends, trends_num) ) return SUCCEED;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_get_aggregate_values                                             *
 *                                                                                  *
 * Purpose: gets buffer of aggregated values from history storage                   *
 *                                                                                  *
 * Parameters:  itemid     - [IN] the itemid                                        *
 *              value_type - [IN] the item value type                               *
 *              start      - [IN] the period start timestamp                        *
 *              count      - [IN] the number of values to read                      *
 *              end        - [IN] the period end timestamp   
 * 				aggregates - [IN] number of aggregation steps	                    *
 *              data       - [OUT] buffer of json data values 	                    *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
int	zbx_history_get_aggregated_values(zbx_uint64_t itemid, int value_type, int start,  int end, int agggregates,
		char **buffer)
{
	int j;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64 " value_type:%d start:%d end:%d",
			__func__, itemid, value_type, start,  end);

	//whoever first retruns the agregated data, it's rusult is used 
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_AGGREGATED]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ_AGGREGATED]->values[j];
		glb_history_get_agg_values_func_t get_agg_values = callback->callback;
		
		if (SUCCEED == get_agg_values(callback->callbackData,value_type, itemid,start,end,agggregates,buffer))
			 return SUCCEED;
	}
	
	return FAIL;
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
 *              values     - [OUT] the item history data values                     *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
int	zbx_history_get_values(zbx_uint64_t itemid, int value_type, int start, int count, int end, 	zbx_vector_history_record_t *values)
{
	int			j, ret;

	//whoever first gets the data, it's rusult is used 
	for (j = 0; j < API_CALLBACKS[GLB_MODULE_API_HISTORY_READ]->values_num; j++) {

		glb_api_callback_t *callback = API_CALLBACKS[GLB_MODULE_API_HISTORY_READ]->values[j];
		glb_history_get_values_func_t get_values = callback->callback;
		
		if (SUCCEED == get_values(callback->callbackData , value_type, itemid,start,count,end,values)) 
			return SUCCEED;
	}

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_requires_trends                                            *
 *                                                                                  *
 * Purpose: checks if the value type requires trends data calculations              *
 *                                                                                  *
 * Parameters: value_type - [IN] the value type                                     *
 *                                                                                  *
 * Return value: SUCCEED - trends must be calculated for this value type            *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function is used to check if the trends must be calculated for    *
 *           the specified value type based on the history storage used.            *
 *                                                                                  *
 ************************************************************************************/
int	zbx_history_requires_trends(int value_type)
{
	//if at least one history module was registered for writing trends, allow trends calculation
	if (0< API_CALLBACKS[GLB_MODULE_API_HISTORY_WRITE_TRENDS]->values_num) 
		return SUCCEED;
	
	//zabbix_log(LOG_LEVEL_INFORMATION,"There is no callbacks for trends");
	return FAIL;
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
			zbx_snprintf(buffer, size, ZBX_FS_DBL, value->dbl);
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
 * Function: glb_set_rpcess_types				                              *
 *                                                                            *
 * Purpose: sets types arryay if the type names are present in the setting    *
 *                                                                            *
 * Parameters: types_array   - [out] array to set                             *
 * 				    setting   - [int] string containing the names             *
 ******************************************************************************/

int glb_set_process_types(u_int8_t *types_array, char *setting) {
	
	int i;
	
	for (i=0; i< ITEM_VALUE_TYPE_MAX; i++) {
		if ( NULL != strstr(setting,value_type_names[i])) 
			types_array[i]=1; 
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
