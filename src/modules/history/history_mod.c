/*
Since i am writing this from scratch, perhaps, threr is no need to leave Zabbix annotation here
//todo: figure legal staff about copyright message from Zabbix - is it worth leving in the code along with license or not
**/
#include "../../../include/common.h"
#include "../../../include/log.h"
#include "../../../include/zbxalgo.h"
#include "../../../include/zbxhistory.h"
#include "../../../include/zbxjson.h"
#include "../../libs/zbxhistory/history.h"
#include "../../../include/module.h"
//#include "../../libs/glbrunner/runner.h"
#include "../../../include/dbcache.h"

#include <time.h>
#include <string.h>


extern char	*CONFIG_HISTORY_STORAGE_URL;
extern int	CONFIG_HISTORY_STORAGE_PIPELINES;

extern int CONFIG_SERVER_STARTUP_TIME;
extern int CONFIG_CLICKHOUSE_VALUECACHE_FILL_TIME;
extern int CONFIG_HISTORY_PRELOAD_VALUES;

typedef struct  {
	//char *config_line;
    glb_runner_t runner;
} glb_history_mod_conf_t;



static history_value_t	history_str2value(char *str, unsigned char value_type)
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

int  zbx_module_api_version() {
	return ZBX_MODULE_API_VERSION_GLABER;
}
/*
int	History_Read_Aggregate(void *cfg_data, zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int steps, int end,
		zbx_vector_history_record_t *values)
{
	
	char request[MAX_STRING_LEN];
	char *response=NULL;
	int ret;
	zbx_history_record_t	hr;
	glb_history_mod_conf_t *config = (glb_history_mod_conf_t *)cfg_data;
	struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	int valuecount=0;

	//creating the request
	zbx_snprintf(request,MAX_STRING_LEN, "{\"itemid\":%d, \"start\": %d, \"steps\":%d, \"end\":%d }\n",
				itemid,start,steps,end);
	//requesting, we'll get multiline responce simple json from there
	ret=glb_process_runner_request(&config->runner, request, &response);
	
	zbx_json_open(response, &jp);
    zbx_json_brackets_by_name(&jp, "data", &jp_data);
    
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
        char *clck = NULL, *ns = NULL, *avg_value = NULL, *max_value = NULL, *min_value = NULL;
        size_t clck_alloc=0, ns_alloc = 0, value_alloc = 0;
        struct zbx_json_parse	jp_row;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
		//the reason for parsing the data is to make sure we have the proper JSON responce from the 
		//script. Sure it would be much simple just copy and paste the entire data section of
		//the responce	
            if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "clock", &clck, &clck_alloc) &&
                SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "max", &max_value, &value_alloc) && 
				SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "avg", &avg_value, &avg_alloc) && 
				SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "min", &min_value, &min_alloc) && 
				SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "ns", &ns, &ns_alloc && 
				SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "i", &i, &ns_alloc)   )
				{
               			   	
               	hr.timestamp.sec = atoi(clck);
				zabbix_log(LOG_LEVEL_DEBUG,"read: Clock: %s, ns: %s, avg_value: %s, min_value: %s max_value: %s ",
																				clck,ns,avg_value,min_value,max_value);

                switch (hist->value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",value);
			    		hr.value = history_str2value(value, hist->value_type);
						zbx_vector_history_record_append_ptr(values, &hr);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",value);
			    		hr.value = history_str2value(value, hist->value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:

						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value);
						hr.value = history_str2value(value, hist->value_type);
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
    

	return SUCCEED;
}
*/

int	History_Read(void *cfg_data, zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int count, int end,
		zbx_vector_history_record_t *values)
{
	
	char request[MAX_STRING_LEN];
	char *response=NULL;
	int ret;
	zbx_history_record_t	hr;
	glb_history_mod_conf_t *config = (glb_history_mod_conf_t *)cfg_data;
	struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	int valuecount=0;

	//creating the request
	zbx_snprintf(request,MAX_STRING_LEN, "{\"itemid\":%d, \"start\": %d, \"count\":%d, \"end\":%d }\n",
				itemid,start,count,end);
	//requesting, we'll get multiline responce simple json from there
	ret=glb_process_runner_request(&config->runner, request, &response);
	
	zbx_json_open(response, &jp);
    zbx_json_brackets_by_name(&jp, "data", &jp_data);
    
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
        char *clck = NULL, *ns = NULL, *value = NULL;
        size_t clck_alloc=0, ns_alloc = 0, value_alloc = 0;
        struct zbx_json_parse	jp_row;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
			
            if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "clock", &clck, &clck_alloc) &&
                SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value", &value, &value_alloc) && 
				SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "ns", &ns, &ns_alloc)   )
				{
               			   	
               	hr.timestamp.sec = atoi(clck);
				zabbix_log(LOG_LEVEL_DEBUG,"read: Clock: %s, ns: %s, value: %s, ",clck,ns,value);

                switch (hist->value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",value);
			    		hr.value = history_str2value(value, hist->value_type);
						zbx_vector_history_record_append_ptr(values, &hr);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",value);
			    		hr.value = history_str2value(value, hist->value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:

						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value);
						hr.value = history_str2value(value, hist->value_type);
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
    

	return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: History_Write callback                                                  *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  data    - [IN] the module config data		                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
int	History_Write(void *cfg_data, const zbx_vector_ptr_t *history)
{
    char *response=NULL;
    char *req_buffer=NULL;
    size_t	req_alloc = 0, req_offset = 0;
    ZBX_DC_HISTORY		*h;
    glb_history_mod_conf_t *config = (glb_history_mod_conf_t *)cfg_data;
    int i,j;
    int ret=FAIL;

	char buffer [MAX_STRING_LEN*2];
	
	zabbix_log(LOG_LEVEL_DEBUG,"History write values called, %d items to write",history->values_num);
    //converitng the data to string

    for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];
			
		//if (hist->value_type != h->value_type)	
		if ( ITEM_VALUE_TYPE_LOG == h->value_type) continue;
		glb_escape_runner_string(h->host_name,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"{\"hostname\":\"%s\",", buffer);
		
		buffer[0]='\0';
		glb_escape_runner_string(h->item_key,buffer);
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset," \"item_key\":\"%s\",", buffer);
		
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"itemid\":%ld, \"time_sec\":%ld, \"time_ns\":%ld, \"value_type\":%d, ", h->itemid,h->ts.sec,h->ts.ns, h->value_type);
    	
		//type-dependent part
		if (ITEM_VALUE_TYPE_UINT64 == h->value_type) 
	           zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_int\":%ld, \"value_dbl\":0, \"value_str\":\"\"",h->value.ui64);
    	
		if (ITEM_VALUE_TYPE_FLOAT == h->value_type) 
           zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_int\":0, \"value_dbl\":%f, \"value_str\":\"\"",h->value.dbl);
        
		if (ITEM_VALUE_TYPE_STR == h->value_type || ITEM_VALUE_TYPE_TEXT == h->value_type ) {
			buffer[0]='\0';
			buffer[1]='\0';
			glb_escape_runner_string(h->value.str,buffer);    		
        
		    zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\"value_int\":0, \"value_dbl\":0, \"value_str\":\"%s\"", buffer);
		}
		zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"}\n");
		//num++;
	}
    
	//adding empty line to the request's end to signal end of request;
	zbx_snprintf_alloc(&req_buffer,&req_alloc,&req_offset,"\n");

    //zabbix_log(LOG_LEVEL_INFORMATION,"%s",req_buffer);

	ret=glb_process_runner_request(&config->runner, req_buffer, &response);

    zbx_free(req_buffer);
    zbx_free(response);

	return ret;
}
	


int glb_module_init(char *config_line, void **data)
{
	glb_history_mod_conf_t *config;
	char path[MAX_STRING_LEN], params[MAX_STRING_LEN], timeout_str[MAX_ID_LEN] , max_calls_str[MAX_ID_LEN];
    
    path[0]=0;
    params[0]=0;
    timeout_str[0]=0;
    max_calls_str[0]=0;

    int timeout = GLB_DEFAULT_RUNNER_TIMEOUT;
    int max_calls = GLB_DEFAULT_RUNNER_MAX_CALLS;

    zabbix_log(LOG_LEVEL_INFORMATION, "%s: got config: '%s'", __func__, config_line);   

    //history mode expects old good JSON as a config, let's parse it
    struct zbx_json_parse jp, jp_config;
    
    if ( SUCCEED != zbx_json_open(config_line, &jp_config)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: '%s', most likely not a valid JSON");
		return FAIL;
	}

    zabbix_log(LOG_LEVEL_INFORMATION, "before path serach: %s",jp_config.start );
    if 	( SUCCEED != zbx_json_value_by_name(&jp_config,"path", path, MAX_STRING_LEN))  {
        zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: couldn't find 'path' parameter");
		return FAIL;
    } else {
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed path: '%s'", __func__, path);
    }

	if (SUCCEED == zbx_json_value_by_name(&jp_config,"params", params, MAX_STRING_LEN))
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed params: '%s'", __func__, params);

	if (SUCCEED == zbx_json_value_by_name(&jp_config,"timeout", timeout_str, MAX_ID_LEN)) {
        timeout=strtol(timeout_str,NULL,10);
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed timeout: '%d'",timeout);
    }

    if (SUCCEED == zbx_json_value_by_name(&jp_config,"max_calls", max_calls_str, MAX_ID_LEN)) {
        max_calls=strtol(max_calls_str,NULL,10);
        zabbix_log(LOG_LEVEL_INFORMATION, "%s: parsed max_calls: '%d'",timeout);
    }

	config=zbx_malloc(NULL,sizeof( glb_history_mod_conf_t ));
	*data=(void *)config;
    zabbix_log(LOG_LEVEL_INFORMATION, "Parsed config: path: '%s'\n params:'%s' \n timeout: %d\n max_calls: %d",
                                   path,params,timeout,max_calls);
    
    glb_init_runner( &config->runner, path, params, timeout, max_calls, GLB_MODULE_MULTILINE, GLB_MODULE_SILENT );

	return ZBX_MODULE_OK;
}

int	history_modules_destroy(void *config)
{
	zbx_free(config);
	return SUCCEED;
}

