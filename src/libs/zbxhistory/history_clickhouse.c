#include "common.h"
#include "log.h"
#include "zbxjson.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "zbxhistory.h"
#include "zbxself.h"
#include "history.h"
#include "module.h"


/* curl_multi_wait() is supported starting with version 7.28.0 (0x071c00) */
#if defined(HAVE_LIBCURL) && LIBCURL_VERSION_NUM >= 0x071c00

size_t	DCconfig_get_trigger_itemids_by_valuetype( int value_type, zbx_vector_uint64_t *vector_itemids);

int	zbx_vc_simple_add(zbx_uint64_t itemids, int value_type, zbx_history_record_t *record);

extern int CONFIG_SERVER_STARTUP_TIME;

#define GLB_DEFAULT_CLICKHOUSE_TYPES "dbl, str, uint, text"
#define GLB_DEFAULT_CLICKHOUSE_DISABLE_HOST_ITEMS_NAMES	1
#define GLB_DEFAULT_CLICKHOUSE_DISABLE_NANOSECONDS	1
#define GLB_DEFAULT_CLICKHOUSE_DBNAME	"glaber"
#define GLB_DEFAULT_CLICKHOUSE_PRELOAD_VALUES 0
#define GLB_DEFAULT_CLICKHOUSE_DISABLE_READ	1800
#define GLB_DEFAULT_CLICKHOUSE_DISABLE_TRENDS 0

typedef struct
{
	char	*url;
	char 	dbname[MAX_STRING_LEN];
	u_int8_t read_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t write_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t read_aggregate_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t preload_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t disable_host_item_names;
	u_int8_t disable_nanoseconds;
	u_int16_t preload_values;
	u_int16_t disable_read_timeout;

}
glb_clickhouse_data_t;

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
zbx_httppage_t;

static char *trend_tables[]={"trends_dbl","","","trends_uint"};

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t	r_size = size * nmemb;

	zbx_httppage_t	*page = (zbx_httppage_t	*)userdata;
	zbx_strncpy_alloc(&page->data, &page->alloc, &page->offset, ptr, r_size);

	return r_size;
}


static void	clickhouse_log_error(CURL *handle, CURLcode error, const char *errbuf,zbx_httppage_t *page_r)
{
	long	http_code;

	if (CURLE_HTTP_RETURNED_ERROR == error)
	{
		curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
		if (0 != page_r->offset)
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from clickhouse, HTTP error: %ld, message: %s",
					http_code, page_r->data);
		}
		else
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from clickhouse, HTTP error: %ld", http_code);
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot get values from clickhousesearch: %s",
				'\0' != *errbuf ? errbuf : curl_easy_strerror(error));
	}
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_destroy                                                        *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_destroy(void *data)
{
	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;
	//todo: free all the buffers as well
	zbx_free(conf->url);
	zbx_free(data);
}
/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_get_values                                                     *
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
static int	clickhouse_get_agg_values(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int aggregates,
		char **buffer)
{
	const char		*__function_name = "clickhouse_get_agg_values";
	int valuecount=0;

	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;
	size_t			url_alloc = 0, url_offset = 0;
    
	CURLcode		err;
	CURL	*handle = NULL;
	
	struct curl_slist	*curl_headers = NULL;
	
    char  errbuf[CURL_ERROR_SIZE];
    char	*sql_buffer=NULL;
    size_t			buf_alloc = 0, buf_offset = 0;
    zbx_httppage_t page_r;
	int ret = FAIL;
    char *field_name="value";
	

	if (0 == conf->read_aggregate_types[value_type])	
			return SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (end < start || aggregates <1 ) {
		zabbix_log(LOG_LEVEL_WARNING,"%s: wrong params requested: start:%d end:%d, aggregates: %ld",__func__,start,end,aggregates);
		goto out;
	}
	
	if ( value_type == ITEM_VALUE_TYPE_FLOAT) {
		field_name="value_dbl";
	}

    bzero(&page_r,sizeof(zbx_httppage_t));

	if (NULL == (handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		goto out;
	} 

	
	zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, 
	"SELECT itemid, \
		round( multiply((toUnixTimestamp(clock)-%ld), %ld) / %ld ,0) as i,\
		max(toUnixTimestamp(clock)) as clcck ,\
		avg(%s) as avg, \
		count(%s) as count, \
		min(%s) as min , \
		max(%s) as max \
	FROM %s.history h \
	WHERE clock BETWEEN %ld AND %ld AND \
	itemid = %ld \
	GROUP BY itemid, i \
	ORDER BY i \
	FORMAT JSON", start,aggregates, end-start, 
				field_name,field_name,field_name,field_name,
				conf->dbname, start, end, itemid);

	zabbix_log(LOG_LEVEL_DEBUG, "CLICKHOUSE: sending query to clickhouse: %s", sql_buffer);

	curl_easy_setopt(handle, CURLOPT_URL, conf->url);
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

	zabbix_log(LOG_LEVEL_DEBUG, "sending query to %s; post data: %s", conf->url, sql_buffer);

	page_r.offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(handle)))
	{
		clickhouse_log_error(handle, err, errbuf,&page_r);
        zabbix_log(LOG_LEVEL_WARNING, "Failed query '%s'", sql_buffer);
		goto out;
	}

    zabbix_log(LOG_LEVEL_DEBUG, "Recieved from clickhouse: %s", page_r.data);
	//buffer=zbx_strdup(NULL,page_r.data);
		
    struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
	size_t offset=0, allocd=0;
    
    zbx_json_open(page_r.data, &jp);
	
	if (SUCCEED == zbx_json_brackets_by_name(&jp, "data", &jp_data) ) {
		//adding one more byte for the trailing zero
		size_t buf_size=jp_data.end-jp_data.start+1;
		zbx_strncpy_alloc(buffer,&allocd,&offset,jp_data.start,buf_size);
		
		
		//lets fix the field naming
		//this better must be accomplished by fixing sql so that column name would be clock,
		//but so far i haven't done it yet //todo:
		//this code changes all clcck to clock, since i am sure
		//there is only numerical data and fixed column name, must work ok
		//does evth in one pass
		char *pos=*buffer;
		while (NULL != (pos=strstr(pos,"clcck"))) {
			 pos+=2;
			 pos[0]='o';
		}

		ret=SUCCEED;
	} else {
		zbx_snprintf_alloc (buffer,&allocd,&offset,"[]");
	}
  
out: 

	curl_easy_cleanup(handle);
	curl_slist_free_all(curl_headers);
    zbx_free(sql_buffer);
    zbx_free(page_r.data);


	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_get_values                                                     *
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
static int	clickhouse_get_values(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end,
		zbx_vector_history_record_t *values)
{
	const char		*__function_name = "clickhouse_get_values";
	int valuecount=0;

	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;
	size_t			url_alloc = 0, url_offset = 0;
    
	CURLcode		err;
	CURL	*handle = NULL;
	
	struct curl_slist	*curl_headers = NULL;
	
    char  errbuf[CURL_ERROR_SIZE];
    char	*sql_buffer=NULL;
    size_t			buf_alloc = 0, buf_offset = 0;
    zbx_httppage_t page_r;
 
	zbx_history_record_t	hr;


	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
	
	if (0 == conf->read_types[value_type])	
			return SUCCEED;

    bzero(&page_r,sizeof(zbx_httppage_t));

	//this isn't really nice idea since we might be getting some get_values from the zabbix frontend
	if (time(NULL)- conf->disable_read_timeout < CONFIG_SERVER_STARTUP_TIME) {
		zabbix_log(LOG_LEVEL_DEBUG, "waiting for cache load, exiting");
      	return SUCCEED;
	}

    if (NULL == (handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		goto out;
	} 
	
	 zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, 
			"SELECT  toUInt32(clock) clock,value,value_dbl,value_str");

	if ( 0 == conf->disable_nanoseconds ) {
		zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, ",ns");
	}
	
	zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, " FROM %s.history_buffer WHERE itemid=%ld ",
		conf->dbname,itemid);

	if (1 == end-start) {
		zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "AND clock = %d ", end);
	} else {
		if (0 < start) {
			zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "AND clock > %d ", start);
		}
		if (0 < end ) {
			zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "AND clock <= %d ", end);
		}
	}

	zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "ORDER BY clock DESC ");

	if (0 < count) 
	{
	    zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "LIMIT %d ", count);
	}

    zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "format JSON ");

	zabbix_log(LOG_LEVEL_DEBUG, "CLICKHOUSE: sending query to clickhouse: %s", sql_buffer);

	curl_easy_setopt(handle, CURLOPT_URL, conf->url);
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

	zabbix_log(LOG_LEVEL_DEBUG, "sending query to %s; post data: %s", conf->url, sql_buffer);

	page_r.offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(handle)))
	{
		clickhouse_log_error(handle, err, errbuf,&page_r);
        zabbix_log(LOG_LEVEL_WARNING, "Failed query '%s'", sql_buffer);
		goto out;
	}

    zabbix_log(LOG_LEVEL_DEBUG, "Recieved from clickhouse: %s", page_r.data);
		
    struct zbx_json_parse	jp, jp_row, jp_data;
	const char		*p = NULL;
    
    zbx_json_open(page_r.data, &jp);
    zbx_json_brackets_by_name(&jp, "data", &jp_data);
    
    while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
        char *itemid=NULL;
        char *clck = NULL, *ns = NULL, *value = NULL, *value_dbl = NULL, *value_str = NULL;
        size_t clck_alloc=0, ns_alloc = 0, value_alloc = 0, value_dbl_alloc = 0, value_str_alloc = 0;
        struct zbx_json_parse	jp_row;
		zbx_json_type_t type;

        if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
			
            if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "clock", &clck, &clck_alloc,&type) &&
                SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value", &value, &value_alloc, &type) &&
                SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value_dbl", &value_dbl, &value_dbl_alloc, &type) &&
                SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value_str", &value_str, &value_str_alloc, &type)) 
            {
               
			   	if ( 0 == conf->disable_nanoseconds &&
					 SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "ns", &ns, &ns_alloc,&type) ) {
							hr.timestamp.ns = atoi(ns); 
				} else hr.timestamp.ns = 0;

               	hr.timestamp.sec = atoi(clck);
				zabbix_log(LOG_LEVEL_DEBUG,"CLICKHOSUE read: Clock: %s, ns: %s, value: %s, value_dbl: %s, value_str:%s ",clck,ns,value,value_dbl,value_str);

                switch (value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",value);
			    		hr.value = history_str2value(value, value_type);
						zbx_vector_history_record_append_ptr(values, &hr);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",value_dbl);
			    		hr.value = history_str2value(value_dbl, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:

						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value_str);
						hr.value = history_str2value(value_str, value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
                        break;

					case ITEM_VALUE_TYPE_LOG:
						//todo: does server really need's to read logs????
                        break;
				}				
				
				valuecount++;
			} 
            
        } else {
            zabbix_log(LOG_LEVEL_DEBUG,"CLICCKHOUSE: Couldn't parse JSON row: %s",p);
        };

		if ( !valuecount) zabbix_log(LOG_LEVEL_DEBUG,"No data returned form request");
        zbx_free(clck);
        zbx_free(ns);
        zbx_free(value);
        zbx_free(value_dbl);
        zbx_free(value_str);            
    } 
out:
	curl_easy_cleanup(handle);
	curl_slist_free_all(curl_headers);
    zbx_free(sql_buffer);
    zbx_free(page_r.data);

	zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	//retrun succeeds ander any circumstances 
	//since otherwise history sincers will try to repeate the query 
	return SUCCEED;
}
/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_get_trends                                                     *
 *                                                                                  *
 * Purpose: gets item history data from history storage                             *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              itemid  - [IN] the itemid                                           *
 *              start   - [IN] the period start timestamp                           *
 *              end     - [IN] the period end timestamp                             *
 *              values  - [OUT] the item trends  data values                        *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	clickhouse_get_trends(void *data, int value_type, zbx_uint64_t itemid, int start, int end,
		zbx_vector_history_record_t *values)
{
	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;

	//retrun succeeds ander any circumstances 
	//since otherwise history syncers will try to repeat the query 
	return SUCCEED;
}
/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_add_values                                                     *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	clickhouse_add_values(void *data, const zbx_vector_ptr_t *history)
{
	const char	*__function_name = "clickhouse_add_values";

	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;
	int			i,j, num = 0;
	ZBX_DC_HISTORY		*h;
	struct zbx_json		json_idx, json;
	size_t			buf_alloc = 0, buf_offset = 0;
	
    char *sql_buffer=NULL;	
	size_t sql_alloc=0, sql_offset=0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
    
	zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,"INSERT INTO %s.history_buffer (day,itemid,clock,value,value_dbl,value_str", conf->dbname);

	if ( 0 == conf->disable_nanoseconds ) {
		zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",ns");
	}
	
	if ( 0 == conf->disable_host_item_names ) {
		zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",hostname, itemname");
	}
	

	zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,") VALUES");

	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];
			
		if (0 == conf->write_types[h->value_type])	
			continue;
		
		//common part
		zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,"(CAST(%d as date) ,%ld,%d",
				h->ts.sec,h->itemid,h->ts.sec);
    	
		//type-dependent part
		if (ITEM_VALUE_TYPE_UINT64 == h->value_type) 
	           zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",%ld,0,''",h->value.ui64);
    	
		if (ITEM_VALUE_TYPE_FLOAT == h->value_type) 
           zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",0,%f,''",h->value.dbl);
        

		if (ITEM_VALUE_TYPE_STR == h->value_type || ITEM_VALUE_TYPE_TEXT == h->value_type ) {
		    		
            //todo: make more sensible string quotation
			// like this host_name = zbx_dyn_escape_string();   
            for (j = 0; j < strlen(h->value.str); j++) {
		        if ('\'' == h->value.str[j]) { 
				    h->value.str[j]=' ';
			    }
			}
		    zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",0,0,'%s'",h->value.str);
		}
	
		if ( 0 == conf->disable_nanoseconds) {
			zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",%d", h->ts.ns);
		}
		
		if ( 0 == conf->disable_host_item_names ) {
			zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,",'%s','%s'", h->host_name, h->item_key);
		}
		
		zbx_snprintf_alloc(&sql_buffer,&sql_alloc,&sql_offset,"),");

		num++;
	}

	if (num > 0)
	{ 
    
		zbx_httppage_t	page_r;
		bzero(&page_r,sizeof(zbx_httppage_t));
		struct curl_slist	*curl_headers = NULL;
		char  errbuf[CURL_ERROR_SIZE];
		CURLcode		err;
		CURL	*handle = NULL;
		
		if (NULL == (handle = curl_easy_init()))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		} else {

			curl_easy_setopt(handle, CURLOPT_URL, conf->url);
			curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
			curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
			curl_easy_setopt(handle, CURLOPT_WRITEDATA, page_r);
			curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
			curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);
	
			if (CURLE_OK != (err = curl_easy_perform(handle)))
			{
				clickhouse_log_error(handle, err, errbuf,&page_r);
        		zabbix_log(LOG_LEVEL_WARNING, "Failed query '%s'", sql_buffer);
	
			} else {
				zabbix_log(LOG_LEVEL_DEBUG, "CLICKHOUSE: succeeded query: %s",sql_buffer);
			}
		}
		
	 	zbx_free(page_r.data);
		curl_slist_free_all(curl_headers);
		curl_easy_cleanup(handle);
	}

	zbx_free(sql_buffer);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return num;
}


/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_add_trends                                                  *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int	clickhouse_add_trends(void *data, ZBX_DC_TREND *trends, int trends_num)
{
	glb_clickhouse_data_t	*conf = (glb_clickhouse_data_t *)data;
	int			i,j, value_type, num = 0;
	
	ZBX_DC_TREND *tr;


	static struct {
		char *buffer;
		size_t alloc;
		size_t offset;
		int lastflush;
		int trends_count;
	} trh[ITEM_VALUE_TYPE_MAX] = {0};


	char *host_name, *item_key;	
	char *precision="%0.4f,";

	if (0 == trends_num ) 
		return SUCCEED;
    
	for (i = 0; i < trends_num; i++)
	{  
		value_type=trends[i].value_type;
		
		zabbix_log(LOG_LEVEL_INFORMATION, "Got trend data: itemid %ld %s:%s", trends[i].itemid, trends[i].host_name, trends[i].item_key);
		if ( 0 == trends[i].num ) 
		 	continue;

		tr=&trends[i];

		if (0 == trh[value_type].trends_count) {
			zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,"INSERT INTO %s.%s (day,itemid,clock,value_min,value_max,value_avg,hostname,itemname) VALUES", conf->dbname, trend_tables[value_type]);
		} else {
			zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,",");
		}

		zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,"(CAST(%d as date) ,%ld,%d,", trends[i].clock,trends[i].itemid,trends[i].clock);
    	
		switch (tr->value_type) {
		
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,precision,trends[i].value_min.dbl);
				zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,precision,trends[i].value_max.dbl);
				zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,precision,trends[i].value_avg.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,"%ld,%ld,%ld,",
			   			trends[i].value_min.ui64,
						trends[i].value_max.ui64,
						(trends[i].value_avg.ui64.lo / trends[i].num) );
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}
	
		host_name = zbx_dyn_escape_string(trends[i].host_name, "'");   
		item_key = zbx_dyn_escape_string(trends[i].item_key, "'");   

		zbx_snprintf_alloc(&trh[value_type].buffer,&trh[value_type].alloc,&trh[value_type].offset,"'%s','%s')",host_name,item_key);
		
		zbx_free(host_name);
		zbx_free(item_key);

        trh[value_type].trends_count++;
	}

    for (value_type=0; value_type <ITEM_VALUE_TYPE_UINT64; value_type ++ ) {

		if ((trh[value_type].trends_count > 8192 || trh[value_type].lastflush + 20 < time(NULL)) && trh[value_type].trends_count > 0 )
		{ 
			zbx_httppage_t	page_r;
			bzero(&page_r,sizeof(zbx_httppage_t));
			struct curl_slist	*curl_headers = NULL;
			char  errbuf[CURL_ERROR_SIZE];
			CURLcode		err;
			CURL	*handle = NULL;

			zabbix_log(LOG_LEVEL_INFORMATION, "It's a trends flush time for %d value type",value_type);		
		
			if (NULL == (handle = curl_easy_init()))
			{
				zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
			} else {

			curl_easy_setopt(handle, CURLOPT_URL, conf->url);
			curl_easy_setopt(handle, CURLOPT_POSTFIELDS, trh[value_type].buffer);
			curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
			curl_easy_setopt(handle, CURLOPT_WRITEDATA, page_r);
			curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
			curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);
	
			if (CURLE_OK != (err = curl_easy_perform(handle)))
			{
				clickhouse_log_error(handle, err, errbuf,&page_r);
        		zabbix_log(LOG_LEVEL_WARNING, "Failed query '%s' ,trends count is %d", trh[value_type].buffer, trh[value_type].trends_count);
	
			} else {
				zabbix_log(LOG_LEVEL_DEBUG, "CLICKHOUSE: succeeded query: %s",trh[value_type].buffer);
			}
		}
		
	 	zbx_free(page_r.data);
		curl_slist_free_all(curl_headers);
		curl_easy_cleanup(handle);
		
		trh[value_type].offset=0;
		trh[value_type].trends_count=0;
		trh[value_type].lastflush = time(NULL);

	}

	}
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return num;
}

static int zbx_history_add_vc(char* url, int value_type, char *query) {
	
	CURL	*handle = NULL;
	zbx_httppage_t	page_r;
	bzero(&page_r,sizeof(zbx_httppage_t));
	struct curl_slist	*curl_headers = NULL;
	char  errbuf[CURL_ERROR_SIZE];
	CURLcode		err;
	zbx_history_record_t	hr;
	int valuecount = 0;
	const char		*p = NULL;

	if (NULL == (handle = curl_easy_init())) {
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		goto out;
	};

	page_r.offset = 0;
	*errbuf = '\0';

	curl_easy_setopt(handle, CURLOPT_URL, url);
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, query);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);


	if (CURLE_OK != (err = curl_easy_perform(handle))) {
			clickhouse_log_error(handle, err, errbuf,&page_r);
        	zabbix_log(LOG_LEVEL_WARNING, "Failed query %s",query);
			goto out;
	}	

	if (NULL != page_r.data) {
    	zabbix_log(LOG_LEVEL_DEBUG, "Query '%s' completed\n, filling value cache, data: %s",query, page_r.data);
		
		struct zbx_json_parse	jp, jp_row, jp_data;
		
    	
		zbx_json_open(page_r.data, &jp);
    	zbx_json_brackets_by_name(&jp, "data", &jp_data);
    
    	while (NULL != (p = zbx_json_next(&jp_data, p))) {
        	
			char *clck = NULL,  *value = NULL, *value_dbl = NULL, *value_str = NULL , *itemid_str = NULL;
        	size_t clck_alloc=0,  value_alloc = 0, value_dbl_alloc = 0, value_str_alloc = 0, itemid_alloc = 0;
        	struct zbx_json_parse	jp_row;
			zbx_uint64_t itemid=0;
			zbx_json_type_t type;

			if (SUCCEED == zbx_json_brackets_open(p, &jp_row)) {
					
            	if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "itemid", &itemid_str, &itemid_alloc,&type) &&
					SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "clck", &clck, &clck_alloc,&type) &&
           			SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value", &value, &value_alloc,&type) &&
                	SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value_dbl", &value_dbl, &value_dbl_alloc,&type) &&
                	SUCCEED == zbx_json_value_by_name_dyn(&jp_row, "value_str", &value_str, &value_str_alloc,&type)) {
               
			   		hr.timestamp.sec = atoi(clck);
					itemid=atoi(itemid_str);
					
					switch (value_type) {
						case ITEM_VALUE_TYPE_UINT64:
							zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",value);
			    			hr.value = history_str2value(value, value_type);
							break;

						case ITEM_VALUE_TYPE_FLOAT: 
							zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",value_dbl);
				    		hr.value = history_str2value(value_dbl, value_type);
							break;
					
						case ITEM_VALUE_TYPE_STR:
						case ITEM_VALUE_TYPE_TEXT:
							zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",value_str);
							hr.value = history_str2value(value_str, value_type);
                        	break;

						default:
							//todo: does server really need's to read logs????
							goto out;
                    }	
						
					
					if (FAIL == zbx_vc_simple_add(itemid,value_type, &hr)) {
						zabbix_log(LOG_LEVEL_INFORMATION,"Couldn't add value to vc after %ld items", valuecount);
						
					} else {
						valuecount++;
					}
										
				}
			}

			zbx_free(itemid_str);
			zbx_free(clck);
        	zbx_free(value);
        	zbx_free(value_dbl);
        	zbx_free(value_str);            
             
        }
	} else {
        zabbix_log(LOG_LEVEL_DEBUG,"CLICKHOUSE: Couldn't parse JSON row: %s",p);
	};
	
out:
	zbx_free(page_r.data);
	curl_slist_free_all(curl_headers);
	curl_easy_cleanup(handle);
	zabbix_log(LOG_LEVEL_INFORMATION,"History preload: %ld values loaded to the value cache", valuecount);
	return valuecount;
}


static int clickhouse_preload_values(void *data) {
	
	int valuecount=0;
	glb_clickhouse_data_t *conf = data;
	unsigned long rows_num;
	zbx_vector_uint64_t vector_itemids;
	zbx_vector_history_record_t values;
	size_t i=0;
	int k=0;
	char *query=NULL;
	size_t q_len = 0, q_offset = 0;
	int value_type;
		
	if (conf->preload_values > 0 ) {
		
		zabbix_log(LOG_LEVEL_INFORMATION,"Prefetching items to value cache");
		//iterating over valuetypes here
		for (value_type=0; value_type < ITEM_VALUE_TYPE_MAX; value_type++) {
			i=0;k=0;
			if (0 == conf->preload_types[value_type]) 
				continue;
			zbx_vector_uint64_create(&vector_itemids);			

			zabbix_log(LOG_LEVEL_INFORMATION,"Prefetching type %d", value_type);
			size_t items=DCconfig_get_trigger_itemids_by_valuetype (value_type, &vector_itemids);
			zabbix_log(LOG_LEVEL_INFORMATION,"Got %ld items for value type %d",items, value_type);
			
			if ( items > 0 ) {
			
				while( i < vector_itemids.values_num ) {
					zbx_snprintf_alloc(&query,&q_len,&q_offset,"SELECT itemid, toUnixTimestamp(clock) as clck, value, value_dbl, value_str FROM %s.history_buffer WHERE (itemid IN  (",
						conf->dbname);

#define MAX_ITEMS_PER_QUERY 20000
#define MAX_QUERY_LENGTH 200*1024

					while (i-k < MAX_ITEMS_PER_QUERY && q_len < MAX_QUERY_LENGTH && i < vector_itemids.values_num) {
						if ( i-k == 1 || ( 0==i && 0==k)) 
							zbx_snprintf_alloc(&query,&q_len,&q_offset,"%ld",vector_itemids.values[i]);
						else 
							zbx_snprintf_alloc(&query,&q_len,&q_offset,",%ld",vector_itemids.values[i]);
						i++;
					}
				
					zbx_snprintf_alloc(&query,&q_len,&q_offset,")) AND (day = today() OR day = today()-1 ) AND clock > subtractDays(now(),1)	ORDER BY itemid ASC, clock DESC	LIMIT 10 BY itemid");
					zbx_snprintf_alloc(&query, &q_len, &q_offset, " format JSON ");

					zabbix_log(LOG_LEVEL_DEBUG,"Length of the query: '%ld'",strlen(query));
					zabbix_log(LOG_LEVEL_DEBUG,"History preloading: Perfroming query for items %ld - %ld out of %ld ",k,i,vector_itemids.values_num);
					
					zabbix_log(LOG_LEVEL_DEBUG,"query: %s",query);
					//zbx_history_fill_value_cache();
					valuecount += zbx_history_add_vc(conf->url, value_type, query);
					
					zbx_free(query);
					q_len=0;
					q_offset=0;

					k=i;
					i++;
				}
			}
			zbx_vector_uint64_destroy(&vector_itemids);
		}

	}
	zabbix_log(LOG_LEVEL_INFORMATION,"Finished history preloading");
	return valuecount;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_clickhouse_init                                               *
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
int	glb_history_clickhouse_init(char *params)
{
	glb_clickhouse_data_t	*conf;

	//for clickhouse there are following params are parsed:
	//url :  http://localhost:8123
	//dbname :  zabbix
	//username and passsword  -  unset
	//write_types: (str, text, ui64, double) 
	//read_types: (str, text, ui64, double)	
	//preload_types: (str, text, ui64, double) 
	//preload_values: 0 
	//read_aggregate_types: (str, text, ui64, double) 
	//disable_reads: 0 by default (how long not to do readings)

	 //history mode expects old good JSON as a config, let's parse it
    struct zbx_json_parse jp, jp_config;
	char  username[MAX_ID_LEN],password[MAX_ID_LEN],tmp_str[MAX_ID_LEN];
	size_t alloc=0,offset=0;
	zbx_json_type_t type;

	conf = (glb_clickhouse_data_t *)zbx_malloc(NULL, sizeof(glb_clickhouse_data_t));
	memset(conf, 0, sizeof(glb_clickhouse_data_t));
	
	zabbix_log(LOG_LEVEL_DEBUG,"in %s: starting init", __func__);

    if ( SUCCEED != zbx_json_open(params, &jp_config)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: '%s', most likely not a valid JSON");
		return FAIL;
	}

	zbx_strlcpy(tmp_str,"http://localhost:8123",MAX_STRING_LEN);

	if ( SUCCEED != zbx_json_value_by_name(&jp_config,"url", tmp_str, MAX_STRING_LEN,&type))  
    	zabbix_log(LOG_LEVEL_INFORMATION, "%s: Couldn't find url param, using defaul '%s'",__func__,tmp_str);
	
	zbx_rtrim(tmp_str, "/");
	zbx_snprintf_alloc(&conf->url,&alloc,&offset,"%s",tmp_str);
		    
	if (SUCCEED == zbx_json_value_by_name(&jp_config,"username", username, MAX_ID_LEN,&type)  && 
		SUCCEED == zbx_json_value_by_name(&jp_config,"password", password, MAX_ID_LEN,&type) ) {
		
		zbx_snprintf_alloc(&conf->url,&alloc,&offset,"/?user=%s&password=%s",username,password);
	}

	if (SUCCEED == zbx_json_value_by_name(&jp_config,"dbname", tmp_str, MAX_ID_LEN,&type) )
		zbx_strlcpy(conf->dbname,tmp_str,MAX_STRING_LEN);
	else 	zbx_strlcpy(conf->dbname,GLB_DEFAULT_CLICKHOUSE_DBNAME,MAX_STRING_LEN);

	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_CLICKHOUSE_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"write_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->write_types, tmp_str);
	
	if (glb_types_array_sum(conf->write_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init clickhouse module: WRITE types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE,(void (*)(void))clickhouse_add_values,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_CLICKHOUSE_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init clickhouse module: READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ,(void (*)(void))clickhouse_get_values,conf);
	}
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_CLICKHOUSE_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"preload_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->preload_types, tmp_str);
	
	if (glb_types_array_sum(conf->preload_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init clickhouse module: PRELOAD types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_VC_PRELOAD,(void (*)(void))clickhouse_preload_values,conf);
	}
	
	zbx_strlcpy(tmp_str,GLB_DEFAULT_CLICKHOUSE_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_aggregate_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_aggregate_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_aggregate_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init clickhouse module: AGGREGATE READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGGREGATED,(void (*)(void))clickhouse_get_agg_values,conf);
	}
	
	if ( (SUCCEED == zbx_json_value_by_name(&jp_config,"enable_trends", tmp_str, MAX_ID_LEN,&type))  &&
		 ( strcmp(tmp_str,"0") == 0 || strcmp(tmp_str,"false")==0 )) {
			zabbix_log(LOG_LEVEL_INFORMATION, "Trends are disabled");
	} else {
		zabbix_log(LOG_LEVEL_INFORMATION, "Trends are enabled");
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE_TRENDS,(void (*)(void))clickhouse_add_trends,conf);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_TRENDS,(void (*)(void))clickhouse_get_trends,conf);
	}

	conf->preload_values=GLB_DEFAULT_CLICKHOUSE_PRELOAD_VALUES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"read_aggregate_types", tmp_str, MAX_ID_LEN,&type) ) 
			conf->preload_values =strtol(tmp_str,NULL,10);

	conf->disable_read_timeout=GLB_DEFAULT_CLICKHOUSE_DISABLE_READ;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"disable_reads", tmp_str, MAX_ID_LEN,&type) ) 
			conf->disable_read_timeout =strtol(tmp_str,NULL,10);

	conf->disable_nanoseconds = GLB_DEFAULT_CLICKHOUSE_DISABLE_NANOSECONDS;

	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"read_aggregate_types", tmp_str, MAX_ID_LEN,&type) ) 
			conf->disable_nanoseconds=strtol(tmp_str,NULL,10);
	
	conf->disable_host_item_names = GLB_DEFAULT_CLICKHOUSE_DISABLE_HOST_ITEMS_NAMES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"disable_host_item_names", tmp_str, MAX_ID_LEN,&type) ) 
			conf->disable_host_item_names=strtol(tmp_str,NULL,10);
	
	conf->preload_values=GLB_DEFAULT_CLICKHOUSE_PRELOAD_VALUES;
	if (SUCCEED ==zbx_json_value_by_name(&jp_config,"preload_values", tmp_str, MAX_ID_LEN,&type) ) 
			conf->preload_values=strtol(tmp_str,NULL,10);

	if (0 != curl_global_init(CURL_GLOBAL_ALL)) {
		zabbix_log(LOG_LEVEL_INFORMATION,"Cannot initialize cURL library");
		return FAIL;
	}

	//now, need to setup callbacks for the functions we're involved
	glb_register_callback(GLB_MODULE_API_DESTROY,(void (*)(void))clickhouse_destroy,conf);
	
	return SUCCEED;
}

#else

int	zbx_history_clickhouse_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	ZBX_UNUSED(hist);
	ZBX_UNUSED(value_type);

	*error = zbx_strdup(*error, "cURL library support >= 7.28.0 is required for clickhousesearch history backend");
	return FAIL;
}

#endif
