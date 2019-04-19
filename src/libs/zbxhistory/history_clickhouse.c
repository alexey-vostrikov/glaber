/*
** Zabbix
** Copyright (C) 2001-2018 Zabbix SIA
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
#include "zbxjson.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "zbxhistory.h"
#include "zbxself.h"
#include "history.h"

/* curl_multi_wait() is supported starting with version 7.28.0 (0x071c00) */
#if defined(HAVE_LIBCURL) && LIBCURL_VERSION_NUM >= 0x071c00

#define		ZBX_HISTORY_STORAGE_DOWN	10000 /* Timeout in milliseconds */

//#define		ZBX_IDX_JSON_ALLOCATE		256
//#define		ZBX_JSON_ALLOCATE		2048
#define     ZBX_VALUECACHE_FILL_TIME 300
//#define     ZBX_HISTORY_AGE_TIME    15*60-1 //we don't request 15-minutes old data from history it's better to
                                            //wait till value cache fills
#define		MAX_HISTORY_CLICKHOUSE_FIELDS	5 /* How many fields to parse from clickhouse output */

//const char	*value_type_str[] = {"dbl", "str", "log", "uint", "text"};

extern char	*CONFIG_HISTORY_STORAGE_URL;
extern int	CONFIG_HISTORY_STORAGE_PIPELINES;
extern char *CONFIG_HISTORY_STORAGE_TABLE_NAME;

typedef struct
{
	char	*base_url;
	char	*post_url;
	char	*buf;
	CURL	*handle;
}
zbx_clickhouse_data_t;

typedef struct
{
	unsigned char		initialized;
	zbx_vector_ptr_t	ifaces;

	CURLM			*handle;
}
zbx_clickhouse_writer_t;

static zbx_clickhouse_writer_t	writer;

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
zbx_httppage_t;



typedef struct
{
	zbx_httppage_t	page;
	char		errbuf[CURL_ERROR_SIZE];
}
zbx_curlpage_t;

static zbx_curlpage_t	page_w[ITEM_VALUE_TYPE_MAX];

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t	r_size = size * nmemb;

	zbx_httppage_t	*page = (zbx_httppage_t	*)userdata;
	zbx_strncpy_alloc(&page->data, &page->alloc, &page->offset, ptr, r_size);

	return r_size;
}

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

static const char	*history_value2str(const ZBX_DC_HISTORY *h)
{
	static char	buffer[MAX_ID_LEN + 1];

	switch (h->value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			return h->value.str;
		case ITEM_VALUE_TYPE_LOG:
			return h->value.log->value;
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_DBL, h->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_UI64, h->value.ui64);
			break;
	}

	return buffer;
}

static int	history_parse_value(struct zbx_json_parse *jp, unsigned char value_type, zbx_history_record_t *hr)
{
	char	*value = NULL;
	size_t	value_alloc = 0;
	int	ret = FAIL;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "clock", &value, &value_alloc))
		goto out;

	hr->timestamp.sec = atoi(value);

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "ns", &value, &value_alloc))
		goto out;

	hr->timestamp.ns = atoi(value);

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "value", &value, &value_alloc))
		goto out;

	hr->value = history_str2value(value, value_type);

	if (ITEM_VALUE_TYPE_LOG == value_type)
	{

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "timestamp", &value, &value_alloc))
			goto out;

		hr->value.log->timestamp = atoi(value);

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "logeventid", &value, &value_alloc))
			goto out;

		hr->value.log->logeventid = atoi(value);

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "severity", &value, &value_alloc))
			goto out;

		hr->value.log->severity = atoi(value);

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "source", &value, &value_alloc))
			goto out;

		hr->value.log->source = zbx_strdup(NULL, value);
	}

	ret = SUCCEED;

out:
	zbx_free(value);

	return ret;
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
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from clickhousesearch, HTTP error: %ld", http_code);
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot get values from clickhousesearch: %s",
				'\0' != *errbuf ? errbuf : curl_easy_strerror(error));
	}
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_close                                                          *
 *                                                                                  *
 * Purpose: closes connection and releases allocated resources                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_close(zbx_history_iface_t *hist)
{
	zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;

	zbx_free(data->buf);
	zbx_free(data->post_url);

	if (NULL != data->handle)
	{
		if (NULL != writer.handle)
			curl_multi_remove_handle(writer.handle, data->handle);

		curl_easy_cleanup(data->handle);
		data->handle = NULL;
	}
}


/******************************************************************************************************************
 *                                                                                                                *
 * common sql service support                                                                                     *
 *                                                                                                                *
 ******************************************************************************************************************/



/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_writer_init                                                    *
 *                                                                                  *
 * Purpose: initializes clickhouse writer for a new batch of history values            *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_writer_init(void)
{
	if (0 != writer.initialized)
		return;

	zbx_vector_ptr_create(&writer.ifaces);

	if (NULL == (writer.handle = curl_multi_init()))
	{
		zbx_error("Cannot initialize cURL multi session");
		exit(EXIT_FAILURE);
	}

	writer.initialized = 1;
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_writer_release                                                 *
 *                                                                                  *
 * Purpose: releases initialized clickhouse writer by freeing allocated resources and  *
 *          setting its state to uninitialized.                                     *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_writer_release(void)
{
	int	i;

	for (i = 0; i < writer.ifaces.values_num; i++)
		clickhouse_close((zbx_history_iface_t *)writer.ifaces.values[i]);

	curl_multi_cleanup(writer.handle);
	writer.handle = NULL;

	zbx_vector_ptr_destroy(&writer.ifaces);

	writer.initialized = 0;
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_writer_add_iface                                               *
 *                                                                                  *
 * Purpose: adds history storage interface to be flushed later                      *
 *                                                                                  *
 * Parameters: db_insert - [IN] bulk insert data                                    *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_writer_add_iface(zbx_history_iface_t *hist)
{
	zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;

	clickhouse_writer_init();

	if (NULL == (data->handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		return;
	}

	curl_easy_setopt(data->handle, CURLOPT_URL, data->base_url);
	curl_easy_setopt(data->handle, CURLOPT_POST, 1L);
	curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, data->buf);
	curl_easy_setopt(data->handle, CURLOPT_WRITEFUNCTION, curl_write_cb);

	curl_easy_setopt(data->handle, CURLOPT_WRITEDATA, &page_w[hist->value_type].page);
	curl_easy_setopt(data->handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(data->handle, CURLOPT_ERRORBUFFER, page_w[hist->value_type].errbuf);
	*page_w[hist->value_type].errbuf = '\0';
	curl_easy_setopt(data->handle, CURLOPT_PRIVATE, &page_w[hist->value_type]);
	page_w[hist->value_type].page.offset = 0;
	if (0 < page_w[hist->value_type].page.alloc)
		*page_w[hist->value_type].page.data = '\0';

	curl_multi_add_handle(writer.handle, data->handle);

	zbx_vector_ptr_append(&writer.ifaces, hist);
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_writer_flush                                                   *
 *                                                                                  *
 * Purpose: posts historical data to clickhouse storage                                *
 *                                                                                  *
 ************************************************************************************/
/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_writer_flush                                                   *
 *                                                                                  *
 * Purpose: posts historical data to clickhouse storage                                *
 *                                                                                  *
 ************************************************************************************/
static int	clickhouse_writer_flush(void)
{
	const char		*__function_name = "clickhouse_writer_flush";

	struct curl_slist	*curl_headers = NULL;
	int			i, running, previous, msgnum;
	CURLMsg			*msg;
	zbx_vector_ptr_t	retries;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* The writer might be uninitialized only if the history */
	/* was already flushed. In that case, return SUCCEED */
	if (0 == writer.initialized)
		return SUCCEED;

	zbx_vector_ptr_create(&retries);

	curl_headers = curl_slist_append(curl_headers, "Content-Type: application/x-ndjson");

	for (i = 0; i < writer.ifaces.values_num; i++)
	{
		zbx_history_iface_t	*hist = (zbx_history_iface_t *)writer.ifaces.values[i];
		zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;

		(void)curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);

	}

try_again:
	previous = 0;

	do
	{
		int		fds;
		CURLMcode	code;
		char 		*error;
		zbx_curlpage_t	*curl_page;

		if (CURLM_OK != (code = curl_multi_perform(writer.handle, &running)))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot perform on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		if (CURLM_OK != (code = curl_multi_wait(writer.handle, NULL, 0, ZBX_HISTORY_STORAGE_DOWN, &fds)))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot wait on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		if (previous == running)
			continue;

		while (NULL != (msg = curl_multi_info_read(writer.handle, &msgnum)))
		{
			/* If the error is due to malformed data, there is no sense on re-trying to send. */
			/* That's why we actually check for transport and curl errors separately */
			if (CURLE_HTTP_RETURNED_ERROR == msg->data.result)
			{
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
						(char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					zabbix_log(LOG_LEVEL_ERR, "cannot send data to clickhousesearch, HTTP error"
							" message: %s", curl_page->errbuf);
				}
				else
				{
					long int	err;

					curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &err);
					zabbix_log(LOG_LEVEL_ERR, "cannot send data to clickhouse, HTTP error code:"
							" %ld", err);
				}
			}
			else if (CURLE_OK != msg->data.result)
			{
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
						(char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to clickhouse: %s",
							curl_page->errbuf);
				}
				else
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to clickhouse: %s",
							curl_easy_strerror(msg->data.result));
				}

				/* If the error is due to curl internal problems or unrelated */
				/* problems with HTTP, we put the handle in a retry list and */
				/* remove it from the current execution loop */
				zbx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
//			else if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&curl_page)
//					&& SUCCEED == clickhouse_is_error_present(&curl_page->page, &error))
//			{
//				zabbix_log(LOG_LEVEL_WARNING, "%s() cannot send data to clickhousesearch: %s",
//						__function_name, error);
//				zbx_free(error);

				/* If the error is due to clickhouse internal problems (for example an index */
				/* became read-only), we put the handle in a retry list and */
//				/* remove it from the current execution loop */
//				zbx_vector_ptr_append(&retries, msg->easy_handle);
//				curl_multi_remove_handle(writer.handle, msg->easy_handle);
//			}
		}

		previous = running;
	}
	while (running);

	/* We check if we have handles to retry. If yes, we put them back in the multi */
	/* handle and go to the beginning of the do while() for try sending the data again */
	/* after sleeping for ZBX_HISTORY_STORAGE_DOWN / 1000 (seconds) */
	if (0 < retries.values_num)
	{
		for (i = 0; i < retries.values_num; i++)
			curl_multi_add_handle(writer.handle, retries.values[i]);

		zbx_vector_ptr_clear(&retries);

		sleep(ZBX_HISTORY_STORAGE_DOWN / 1000);
		goto try_again;
	}

	curl_slist_free_all(curl_headers);

	zbx_vector_ptr_destroy(&retries);

	clickhouse_writer_release();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return SUCCEED;
}


/******************************************************************************************************************
 *                                                                                                                *
 * history interface support                                                                                      *
 *                                                                                                                *
 ******************************************************************************************************************/

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_destroy                                                        *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void	clickhouse_destroy(zbx_history_iface_t *hist)
{
	zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;

	clickhouse_close(hist);

	zbx_free(data->base_url);
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
static int	clickhouse_get_values(zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int count, int end,
		zbx_vector_history_record_t *values)
{
	const char		*__function_name = "clickhouse_get_values";

	zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;
	size_t			url_alloc = 0, url_offset = 0, id_alloc = 0;
    // scroll_alloc = 0, scroll_offset = 0;
	int			total, empty, ret;
	CURLcode		err;
	//struct zbx_json		query;
	struct curl_slist	*curl_headers = NULL;
	//char			*scroll_id = NULL, *scroll_query = NULL,
    char  errbuf[CURL_ERROR_SIZE];
    char	*sql_buffer=NULL;
    size_t			buf_alloc = 0, buf_offset = 0;
    static int first_run=0;
    int i;
    zbx_history_record_t	hr;
    zbx_httppage_t	*page_r;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    if ( NULL == (page_r=zbx_malloc(NULL,sizeof(zbx_httppage_t)))) {
        zabbix_log(LOG_LEVEL_ERR,"Couldn't alocate pager_r");
        return (FAIL);    
    } else 
        bzero(page_r,sizeof(zbx_httppage_t));

    if (0 == first_run) 
		first_run = time(NULL);

    //for checks with small intervalls it's better to wait item
    //collection then to request SLOOW history cache, so 
    if (time(NULL)- ZBX_VALUECACHE_FILL_TIME < first_run) {
            ret = SUCCEED;
            goto out;

	}

	ret=SUCCEED;
	goto out;

	ret = FAIL;

	if (NULL == (data->handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");

		return FAIL;
	} 
	
    zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, 
		"SELECT  toUInt32(clock),ns,value,value_dbl,value_str FROM %s WHERE itemid=%ld ",
		CONFIG_HISTORY_STORAGE_TABLE_NAME,itemid);

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
	    zbx_snprintf_alloc(&sql_buffer, &buf_alloc, &buf_offset, "LIMIT %d", count);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "sending query to clickhouse: %s", sql_buffer);


    
      
//    zbx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/%s*/values/_search?scroll=10s", data->base_url,
//			value_type_str[hist->value_type]);

	
	//curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

	curl_easy_setopt(data->handle, CURLOPT_URL, data->base_url);
	curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, sql_buffer);
	curl_easy_setopt(data->handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(data->handle, CURLOPT_WRITEDATA, page_r);
	curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(data->handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(data->handle, CURLOPT_ERRORBUFFER, errbuf);

	zabbix_log(LOG_LEVEL_DEBUG, "sending query to %s; post data: %s", data->base_url, sql_buffer);

	page_r->offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(data->handle)))
	{
		clickhouse_log_error(data->handle, err, errbuf,page_r);
        zabbix_log(LOG_LEVEL_INFORMATION, "Failed query");
		goto out;
	}

    zabbix_log(LOG_LEVEL_DEBUG, "recieved from clickhouse: %s", page_r->data);
		
	
	char *end_str;
	if (NULL !=page_r->data && page_r->data[0]!=0) {

	    zabbix_log(LOG_LEVEL_DEBUG, "Parsing line by line");
	    int line_count=0, field_count=0;


	    char *line_ptr = strtok_r(page_r->data, "\n", &end_str);

	    while (line_ptr != NULL)
	    {
			char *end_field;
			char *field_ptr = strtok_r(line_ptr, "\t", &end_field);
			char *fields[MAX_HISTORY_CLICKHOUSE_FIELDS];

			zabbix_log(LOG_LEVEL_DEBUG, "Parsing line '%s'", line_ptr);

			for (i=0; i++; i<field_count)  fields[i]=NULL; 

			while (field_ptr != NULL && MAX_HISTORY_CLICKHOUSE_FIELDS>field_count) 
			{	
				fields[field_count++]=field_ptr;
				field_ptr = strtok_r(NULL, "\t", &end_field);
			}
			
			//the fields order  must be in sync with SQL query above
			//OR TODO: make it via some proper interface, perhaps JSON or whatever clickhouse supports to 
			//be able to distingiosh wich value is from what field name, not depending on the order in SQL request


			zabbix_log(LOG_LEVEL_DEBUG, "Parsed line %d clock:'%s', ns:'%s', value:'%s', value_dbl:'%s' '",line_count, fields[0],fields[1],fields[2],fields[3]);

			if (NULL != fields[4]) 
			{
				//we've got at least three fields
				hr.timestamp.sec = atoi(fields[0]);
				hr.timestamp.ns = atoi(fields[1]);
				switch (hist->value_type)
				{
					case ITEM_VALUE_TYPE_UINT64:
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as UINT64 %s",fields[2]);
			    		hr.value = history_str2value(fields[2], hist->value_type);
						break;

					case ITEM_VALUE_TYPE_FLOAT: 
						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as DBL field %s",fields[3]);
			    		hr.value = history_str2value(fields[3], hist->value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
						break;
					case ITEM_VALUE_TYPE_STR:
					case ITEM_VALUE_TYPE_TEXT:
						//!!!! for some reason there are major memory leak when reading 
						//string values from history.
						//remove the following goto statement if you really need it
						//break;

						zabbix_log(LOG_LEVEL_DEBUG, "Parsed  as STR/TEXT type %s",fields[4]);
						hr.value = history_str2value(fields[4], hist->value_type);
                        zbx_vector_history_record_append_ptr(values, &hr);
                        break;

					case ITEM_VALUE_TYPE_LOG:
						//todo: if i ever need this, but for now there is no log write to clickhouse
						//goto out;
                        break;
				}				
				//adding to zabbix vector
				//zbx_vector_history_record_append_ptr(values, &hr);

				ret=SUCCEED;
				line_count++;
			} else {
				zabbix_log(LOG_LEVEL_DEBUG, "Skipping the result, not enough fields");
			}
			
			line_ptr = strtok_r(NULL, "\n", &end_str);
	    }
	    page_r->data[0]=0;	
	} else 
	{
	    zabbix_log(LOG_LEVEL_DEBUG, "No data from clickhouse");
	    ret = SUCCEED;
	}



out:
	clickhouse_close(hist);
	curl_slist_free_all(curl_headers);
    zbx_free(sql_buffer);
    zbx_free(page_r->data);
    zbx_free(page_r);


	zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
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
static int	clickhouse_add_values(zbx_history_iface_t *hist, const zbx_vector_ptr_t *history)
{
	const char	*__function_name = "clickhouse_add_values";

	zbx_clickhouse_data_t	*data = (zbx_clickhouse_data_t *)hist->data;
	int			i,j, num = 0;
	ZBX_DC_HISTORY		*h;
	struct zbx_json		json_idx, json;
	size_t			buf_alloc = 0, buf_offset = 0;
	
    char *tmp_buffer=NULL;	
	size_t tmp_alloc=0, tmp_offset=0;


	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    
	zbx_snprintf_alloc(&tmp_buffer,&tmp_alloc,&tmp_offset,"INSERT INTO %s VALUES ", CONFIG_HISTORY_STORAGE_TABLE_NAME );
    
	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];

		if (hist->value_type != h->value_type)
			continue;

		
		if (ITEM_VALUE_TYPE_UINT64 == h->value_type) {
	           zbx_snprintf_alloc(&tmp_buffer,&tmp_alloc,&tmp_offset,"(CAST(%d as date) ,%ld,%d,%d,%ld,0,''),",
				h->ts.sec,h->itemid,h->ts.sec,h->ts.ns,h->value.ui64);
    	}

		if (ITEM_VALUE_TYPE_FLOAT == h->value_type) {
           zbx_snprintf_alloc(&tmp_buffer,&tmp_alloc,&tmp_offset,"(CAST(%d as date) ,%ld,%d,%d,0,%f,''),",
					h->ts.sec,h->itemid,h->ts.sec,h->ts.ns,h->value.dbl);
        }

		if (ITEM_VALUE_TYPE_STR == h->value_type || 
		 	        ITEM_VALUE_TYPE_TEXT == h->value_type ) {
		    zabbix_log(LOG_LEVEL_DEBUG, "Parsing value as string or text type");
			
                //todo: make more sensible string quotation
            for (j = 0; j < strlen(h->value.str); j++) {
		        if ('\'' == h->value.str[j]) { 
				    h->value.str[j]=' ';
			    }
			}

		    zbx_snprintf_alloc(&tmp_buffer,&tmp_alloc,&tmp_offset,"(CAST(%d as date) ,%ld,%d,%d,0,0,'%s'),",
					h->ts.sec,h->itemid,h->ts.sec,h->ts.ns,h->value.str);
		}

		if (ITEM_VALUE_TYPE_LOG == h->value_type)
		{
		//    const zbx_log_value_t	*log;
		//    log = h->value.log;
		}

		num++;
	}

	if (num > 0)
	{ 
        zbx_snprintf_alloc(&data->buf, &buf_alloc, &buf_offset, "%s\n", tmp_buffer);

		clickhouse_writer_add_iface(hist);
	}

	zbx_free(tmp_buffer);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return num;
}

/************************************************************************************
 *                                                                                  *
 * Function: clickhouse_flush                                                          *
 *                                                                                  *
 * Purpose: flushes the history data to storage                                     *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *                                                                                  *
 * Comments: This function will try to flush the data until it succeeds or          *
 *           unrecoverable error occurs                                             *
 *                                                                                  *
 ************************************************************************************/
static int	clickhouse_flush(zbx_history_iface_t *hist)
{
	ZBX_UNUSED(hist);

	return clickhouse_writer_flush();
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
int	zbx_history_clickhouse_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	zbx_clickhouse_data_t	*data;

	if (0 != curl_global_init(CURL_GLOBAL_ALL))
	{
		*error = zbx_strdup(*error, "Cannot initialize cURL library");
		return FAIL;
	}

	data = (zbx_clickhouse_data_t *)zbx_malloc(NULL, sizeof(zbx_clickhouse_data_t));
	memset(data, 0, sizeof(zbx_clickhouse_data_t));
	data->base_url = zbx_strdup(NULL, CONFIG_HISTORY_STORAGE_URL);
	zbx_rtrim(data->base_url, "/");
	data->buf = NULL;
	data->post_url = NULL;
	data->handle = NULL;

	hist->value_type = value_type;
	hist->data = data;
	hist->destroy = clickhouse_destroy;
	hist->add_values = clickhouse_add_values;
	hist->flush = clickhouse_flush;
	hist->get_values = clickhouse_get_values;
	hist->requires_trends = 0;

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
