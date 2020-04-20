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


#define GLB_DEFAULT_VMETRICS_TYPES "dbl, uint"
#define GLB_DEFAULT_VMETRICS_DBNAME	"glaber"

#define MAX_VMETRICS_VALUES 1024
#define MAX_VMETRICS_POINTS 16386
#define MAX_VMETRICS_TIMEFRAME 86400

/* curl_multi_wait() is supported starting with version 7.28.0 (0x071c00) */
#if defined(HAVE_LIBCURL) && LIBCURL_VERSION_NUM >= 0x071c00
size_t DCconfig_get_itemids_by_valuetype(int value_type, zbx_vector_uint64_t *vector_itemids);
int zbx_vc_simple_add(zbx_uint64_t itemids, zbx_history_record_t *record);

extern int CONFIG_SERVER_STARTUP_TIME;
//extern int CONFIG_VALUECACHE_FILL_TIME;

extern u_int64_t CONFIG_DEBUG_ITEM;

typedef struct
{
	char	*read_url;
	char	*write_url;
//	char	*buf;
	char 	dbname[MAX_ID_LEN];
	char 	*username;
	char 	*password;
	u_int8_t read_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t write_types[ITEM_VALUE_TYPE_MAX];
	u_int8_t read_aggregate_types[ITEM_VALUE_TYPE_MAX];

}
zbx_vmetrics_data_t;

typedef struct
{
	char *data;
	size_t alloc;
	size_t offset;
} zbx_httppage_t;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t r_size = size * nmemb;

	zbx_httppage_t *page = (zbx_httppage_t *)userdata;
	zbx_strncpy_alloc(&page->data, &page->alloc, &page->offset, ptr, r_size);

	return r_size;
}

static void vmetrics_log_error(CURL *handle, CURLcode error, const char *errbuf, zbx_httppage_t *page_r)
{
	long http_code;

	if (CURLE_HTTP_RETURNED_ERROR == error)
	{
		curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
		if (0 != page_r->offset)
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from vmetrics, HTTP error: %ld, message: %s",
					   http_code, page_r->data);
		}
		else
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from vmetrics, HTTP error: %ld", http_code);
	}
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot get values from vmetricssearch: %s",
				   '\0' != *errbuf ? errbuf : curl_easy_strerror(error));
	}
}

/************************************************************************************
 *                                                                                  *
 * Function: vmetrics_destroy                                                        *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
static void vmetrics_destroy(void *data)
{
	zbx_vmetrics_data_t *conf = (zbx_vmetrics_data_t *)data;

	//zbx_free(conf->buf);
	zbx_free(conf->read_url);
	zbx_free(conf->write_url);
	zbx_free(conf->username);
	zbx_free(conf->password);
	
	zbx_free(data);
}
/************************************************************************************
 *                                                                                  *
 * Function: vmetrics_get_values                                                     *
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
static int vmetrics_get_agg_values(void *data, int value_type, zbx_uint64_t itemid, int start, int end, int count, char **buffer)
{
	const char *__function_name = "vmetrics_get_values";
	int valuecount = 0;

	zbx_vmetrics_data_t *conf = (zbx_vmetrics_data_t *)data;
	size_t url_alloc = 0, url_offset = 0;

	CURLcode err;
	CURL *handle = NULL;

	struct curl_slist *curl_headers = NULL;

	char errbuf[CURL_ERROR_SIZE];
	char url[MAX_STRING_LEN];

	char *sql_buffer = NULL;
	size_t buf_alloc = 0, buf_offset = 0;
	zbx_httppage_t page_r;

	zbx_history_record_t hr;
	int step = 1;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (CONFIG_DEBUG_ITEM == itemid)
		zabbix_log(LOG_LEVEL_INFORMATION, "Debug item: %ld at %s:requesting average history data from %d to %d count is %d",
				   itemid, __func__, start, end, count);

	if (ITEM_VALUE_TYPE_FLOAT != value_type && ITEM_VALUE_TYPE_UINT64 != value_type)
	{
		return FAIL;
	}
	bzero(&page_r, sizeof(zbx_httppage_t));

	if (NULL == (handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		goto out;
	}

// victoria and prometheus endpoints:
// /api/v1/query
// /api/v1/query_range
// /api/v1/series
// /api/v1/labels
// /api/v1/label/…/values

//example of query:
//according to this https://prometheus.io/docs/prometheus/latest/querying/api/#range-queries
//timestamps might be used either
//$ curl 'http://localhost:9090/api/v1/query_range?query=up&start=2015-07-01T20:10:30.781Z&end=2015-07-01T20:11:00.781Z&step=15s'

//retrun format descriptions
//https://prometheus.io/docs/prometheus/latest/querying/api/#range-vectors

//fetching logic:

//if (0 == count)
//	return db_read_values_by_time(itemid, hist->value_type, values, end - start, end);

// read all data between start and end
//if (0 == start)
//	return db_read_values_by_count(itemid, hist->value_type, values, count, end);
// i.e - read n values before end date....

//return db_read_values_by_time_and_count(itemid, hist->value_type, values, end - start, count, end);

//there is no such thing as "raw" point extractions - so we just limit the step to 1 second
//however it's still unclear why whould one need this - it's possible that 2-day's data will be requestd
//so we linit this to MAX_VMETRICS_VALUES
#define MAX_VMETRICS_VALUES 1024
#define MAX_VMETRICS_TIMEFRAME 86400

	if (!count && (MAX_VMETRICS_VALUES > end - start))
	{
		step = abs((end - start) / MAX_VMETRICS_VALUES);
	}

	if (0 == start)
	{
		//OMG, we have a problem again - in the SQL world we would just fetch  n metrics
		//which  has been collected before the end date
		//however we need to set a timeframe for the selection here
		start = end - MAX_VMETRICS_TIMEFRAME;
		step = MAX_VMETRICS_TIMEFRAME / count;
	}

	if (!step || step < 0)
		step = 1;

	//one more check, if there are more then MAX_VMETRIC_POINTS, then change the step to fit
	if (MAX_VMETRICS_POINTS < (end - start / step))
	{
		step = abs((end - start) / MAX_VMETRICS_POINTS);
	}

	//don't want more than one point per sec
	//todo: add label filtering here 
	zbx_snprintf(url, MAX_STRING_LEN, "%s/api/v1/query_range?query=rollup(item_%ld)&start=%d&end=%d&step=%ds",
				 conf->read_url, itemid, start, end, step);

	curl_easy_setopt(handle, CURLOPT_URL, url);
	//curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

	if (NULL != conf->username && NULL != conf->password)
	{
		curl_easy_setopt(handle, CURLOPT_USERNAME, conf->username);
		curl_easy_setopt(handle, CURLOPT_PASSWORD, conf->password);
	}

	//todo: include auth fields here

	zabbix_log(LOG_LEVEL_INFORMATION, "vmetrics query: %s", url);

	page_r.offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(handle)))
	{
		vmetrics_log_error(handle, err, errbuf, &page_r);
		zabbix_log(LOG_LEVEL_WARNING, "Failed query");
		//seting zero responce for the buffer
		zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "[]");
		goto out;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s Recieved from vmetrics: %s", __func__, page_r.data);

	struct zbx_json_parse jp, jp_row, jp_data, jp_result;
	struct zbx_json_parse jp_min_values = {NULL, NULL}, jp_max_values = {NULL, NULL}, jp_avg_values = {NULL, NULL};

	const char *p = NULL, *metric = NULL;
	char item_name[MAX_ID_LEN], rollup[MAX_ID_LEN];

	zbx_json_open(page_r.data, &jp);
	zbx_json_type_t type;

	if (SUCCEED != zbx_json_brackets_by_name(&jp, "data", &jp_data) ||
		SUCCEED != zbx_json_brackets_by_name(&jp_data, "result", &jp_result))
	{

		zabbix_log(LOG_LEVEL_INFORMATION, "Empty responce");
		zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "[]");
		goto out;
	}
	//zabbix_log(LOG_LEVEL_INFORMATION, "Data section is %s",jp_data.start);
	//opening result array

	//zabbix_log(LOG_LEVEL_INFORMATION, "Result section is %s",jp_result.start);
	//jp_avg_values = (NULL,NULL);
	//jp_min_values = { NULL,NULL};

	while (NULL != (metric = zbx_json_next(&jp_result, metric)))
	{
		struct zbx_json_parse jp_metric, jp_temp_values;
		zbx_json_brackets_open(metric, &jp_metric);

		if (SUCCEED == zbx_json_brackets_by_name(&jp_metric, "values", &jp_temp_values))
		{

			const char *value = NULL;

			zbx_json_brackets_by_name(&jp_metric, "metric", &jp_metric);
			//zabbix_log(LOG_LEVEL_INFORMATION,"metric prased: %s",jp_metric.start);

			if (SUCCEED != zbx_json_value_by_name(&jp_metric, "__name__", item_name, MAX_ID_LEN,&type))
				continue; //   ""result",&jp_result);
			//zabbix_log(LOG_LEVEL_INFORMATION,"found metric %s",item_name);

			if (SUCCEED != zbx_json_value_by_name(&jp_metric, "rollup", rollup, MAX_ID_LEN,&type))
				continue; //   ""result",&jp_result);
			//zabbix_log(LOG_LEVEL_INFORMATION, "Values: %s", jp_values.start);

			if (!strcmp("avg", rollup))
			{
				jp_avg_values.start = jp_temp_values.start;
				jp_avg_values.end = jp_temp_values.end;
				// zabbix_log(LOG_LEVEL_INFORMATION, "Avg values is found");
			}
			if (!strcmp("min", rollup))
			{
				jp_min_values.start = jp_temp_values.start;
				jp_min_values.end = jp_temp_values.end;
				//  zabbix_log(LOG_LEVEL_INFORMATION, "Min values is found");
			}
			if (!strcmp("max", rollup))
			{
				jp_max_values.start = jp_temp_values.start;
				jp_max_values.end = jp_temp_values.end;
				//  zabbix_log(LOG_LEVEL_INFORMATION, "Max values is found: %s",jp_max_values.start);
			}
		}
	}

	if (NULL == jp_avg_values.end || NULL == jp_max_values.end || NULL == jp_min_values.end)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "couldn't find min,max,avg data in the responce from vmetrics");
		zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "[]");
		goto out;
	}

	//need to respond with the buffer containing the data:
	//"data":
	//  [
	//            {
	//                    "itemid": "250746764",
	//                    "i": "126",
	//                    "clock": 1582699244,
	//                    "avg": 1,
	//                    "count": "1",
	//                    "min": "1",
	//                    "max": "1"
	//           },

	struct zbx_json json;
	//size_t buf_alloc=0, buf_offset=0;
	zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "[");
	//zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	//zbx_json_addarray(&json,"data");

	zbx_json_brackets_open(jp_max_values.start, &jp_max_values);
	zbx_json_brackets_open(jp_min_values.start, &jp_min_values);
	zbx_json_brackets_open(jp_avg_values.start, &jp_avg_values);
	//adding data section to the json

	const char *min_value = NULL, *max_value = NULL, *avg_value = NULL;
	char tmp_buf[MAX_ID_LEN];
	int valcount = 0;
	//parsing all the values
	while (NULL != (min_value = zbx_json_next(&jp_min_values, min_value)) &&
		   NULL != (max_value = zbx_json_next(&jp_max_values, max_value)) &&
		   NULL != (avg_value = zbx_json_next(&jp_avg_values, avg_value)))
	{

		//zabbix_log(LOG_LEVEL_INFORMATION,"asdwq %d",valuecount);`
		//it's a shit work parsing jsons in c, so lets go
		const char *min_val = NULL, *max_val = NULL, *avg_val = NULL;
		struct zbx_json_parse jp_min_tuple, jp_max_tuple, jp_avg_tuple;
		int t_min, t_max, t_avg;
		zbx_json_brackets_open(min_value, &jp_min_tuple);
		zbx_json_brackets_open(max_value, &jp_max_tuple);
		zbx_json_brackets_open(avg_value, &jp_avg_tuple);

		//zabbix_log(LOG_LEVEL_INFORMATION,"asdwq1 %d",valuecount);
		min_val = zbx_json_next(&jp_min_tuple, min_val);
		max_val = zbx_json_next(&jp_min_tuple, max_val);
		avg_val = zbx_json_next(&jp_min_tuple, avg_val);

		//abbix_log(LOG_LEVEL_INFORMATION,"asdwq2 %d",valuecount);
		//now we've got timestamps for min max avg
		t_max = strtol(max_val, NULL, 10);
		t_min = strtol(min_val, NULL, 10);
		t_avg = strtol(avg_val, NULL, 10);
		//making the common part of the json

		if (0 < valuecount)
			zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, ",");
		//i is the point number, so we'll calc it by time
		int idx = 1;
		if (start != end)
			idx = ((t_max - start) * count) / (end - start);

		zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "{\"itemid\":\"%ld\",\"i\":\"%d\",\"clock\": %d,\"count\":\"3\",",
						   itemid, idx, t_max);

		//zabbix_log(LOG_LEVEL_INFORMATION, "Got valcount %d, timestamps %d %d %d",valuecount, t_max,t_min,t_avg);
		//strstr(min_val,"\"")
		//moving to the next number
		min_val = zbx_json_next(&jp_min_tuple, min_val);
		max_val = zbx_json_next(&jp_min_tuple, max_val);
		avg_val = zbx_json_next(&jp_min_tuple, avg_val);

		uint64_t min_val_ui, max_val_ui, avg_val_ui;
		double min_val_dbl, max_val_dbl, avg_val_dbl;

		switch (value_type)
		{
		case ITEM_VALUE_TYPE_UINT64:

			min_val_ui = strtol(min_val + 1, NULL, 10);
			max_val_ui = strtol(max_val + 1, NULL, 10);
			avg_val_ui = strtol(avg_val + 1, NULL, 10);
		
			zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "\"max\":\"%ld\",\"min\":\"%ld\",\"avg\":\"%ld\"}", max_val_ui, min_val_ui, avg_val_ui);
			break;
		case ITEM_VALUE_TYPE_FLOAT:

			min_val_dbl = strtof(min_val + 1, NULL);
			max_val_dbl = strtof(max_val + 1, NULL);
			avg_val_dbl = strtof(avg_val + 1, NULL);
			/*zbx_snprintf(tmp_buf,MAX_ID_LEN,"%f",min_val_dbl);
			zbx_json_addstring(&json,"min",tmp_buf,ZBX_JSON_TYPE_STRING);
			zbx_snprintf(tmp_buf,MAX_ID_LEN,"%f",max_val_dbl);
			zbx_json_addstring(&json,"max",tmp_buf,ZBX_JSON_TYPE_STRING);
			zbx_snprintf(tmp_buf,MAX_ID_LEN,"%f",avg_val_dbl);
			zbx_json_addstring(&json,"avg",tmp_buf,ZBX_JSON_TYPE_STRING);*/
			zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "\"max\":\"%f\",\"min\":\"%f\",\"avg\":\"%f\"}", max_val_dbl, min_val_dbl, avg_val_dbl);
			//zabbix_log(LOG_LEVEL_INFORMATION, "Got parsed double metrics values: %f %f %f",min_val_dbl,max_val_dbl,avg_val_dbl);
			break;
		}

		valuecount++;
	}
	zbx_snprintf_alloc(buffer, &buf_alloc, &buf_offset, "]");

	//zbx_json_close(&json);
	size_t allocd = 0, offset = 0;
	//zbx_strncpy_alloc(buffer,&allocd,&offset,json.buffer,json.buffer_size);
	zabbix_log(LOG_LEVEL_INFORMATION, "Built responce %s", *buffer);

out:
//	zbx_free(conf->buf);
	curl_easy_cleanup(handle);
	curl_slist_free_all(curl_headers);

	zbx_free(page_r.data);
	//	zbx_json_free(&json);

	zabbix_log(LOG_LEVEL_INFORMATION, "End of %s()", __func__);

	//retrun succeeds ander any circumstances
	//since otherwise history sincers will try to repeate the query
	return SUCCEED;
}
static int comma_escape(char *in_string, char *out_buffer)
{
	int in = 0, out = 0;
	while (in_string[in] != '\0')
	{
		//we don't want some special chars to appear
		if (in_string[in] == ',' || in_string[in] == '\"') // || in_string[in] == ']' || in_string[in] == '[')
			out_buffer[out++] = '\\';
		if (in_string[in] == ' ')
		{
			out_buffer[out++] = '_';
			in++;
			continue;
		}
		out_buffer[out++] = in_string[in++];
	}
	out_buffer[out] = '\0';

	return out;
}

/************************************************************************************
 *                                                                                  *
 * Function: vmetrics_get_values                                                     *
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
static int vmetrics_get_values(void *data, int value_type, zbx_uint64_t itemid, int start, int count, int end,
							   zbx_vector_history_record_t *values)
{
	const char *__function_name = "vmetrics_get_values";
	int valuecount = 0;

	zbx_vmetrics_data_t *conf = (zbx_vmetrics_data_t *)data;
	
	CURLcode err;
	CURL *handle = NULL;

	struct curl_slist *curl_headers = NULL;

	char errbuf[CURL_ERROR_SIZE];
	char url[MAX_STRING_LEN];
	zbx_httppage_t page_r;

	zbx_history_record_t hr;
	int step;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	bzero(&page_r, sizeof(zbx_httppage_t));

	if (NULL == (handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		goto out;
	}

	//the proper way of using the export
	// curl  'http://localhost:8428/api/v1/export' -d 'match[]={__name__="item_2098384",label="zabbix1"}&start=0&end=1584157644'

	if (CONFIG_DEBUG_ITEM == itemid)
		zabbix_log(LOG_LEVEL_INFORMATION, "Debug item: %ld at %s:requesting history data from %d to %d count is %d",
				   itemid, __func__, start, end, count);

	if (!count && (MAX_VMETRICS_VALUES > end - start))
	{
		step = abs((end - start) / MAX_VMETRICS_VALUES);
	}

	if (0 == start)
	{
		//todo: set approximate range based on the item's delay
		start = end - MAX_VMETRICS_TIMEFRAME;
		if (count)
			step = MAX_VMETRICS_TIMEFRAME / count;
		else
			step = MAX_VMETRICS_TIMEFRAME;
	}
	if (!step || step < 1)
		step = 1;
	//one more check, if there are more then MAX_VMETRIC_POINTS, then change the step to fit
	if (MAX_VMETRICS_POINTS < (end - start / step))
	{
		step = abs((end - start) / MAX_VMETRICS_POINTS);
	}
	if (!step || step < 1)
		step = 1;

	zbx_snprintf(url, MAX_STRING_LEN, "%s/api/v1/export?match[]={__name__=\"item_%ld\",label=\"%s\"}&start=%d&end=%d",
				 conf->read_url, itemid, conf->dbname, start, end);

	curl_easy_setopt(handle, CURLOPT_URL, url);
	//curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);
	//todo: include auth fields here

	if (NULL !=conf->username && NULL !=conf->password)
	{
		curl_easy_setopt(handle, CURLOPT_USERNAME, conf->username);
		curl_easy_setopt(handle, CURLOPT_PASSWORD, conf->password);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "vmetrics query: %s", url);

	page_r.offset = 0;
	*errbuf = '\0';

	if (CURLE_OK != (err = curl_easy_perform(handle)))
	{
		vmetrics_log_error(handle, err, errbuf, &page_r);
		zabbix_log(LOG_LEVEL_WARNING, "Failed query");
		goto out;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "Recieved from vmetrics: %s", page_r.data);

	if (NULL == page_r.data)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Empty responce from victoria metrics (no data for the requested period");
		goto out;
	}
	struct zbx_json_parse jp, jp_values, jp_timestamps;
	const char *p = NULL, *value = NULL, *timestamp = NULL;
	char item_name[MAX_ID_LEN];

	zbx_json_open(page_r.data, &jp);
	if (SUCCEED != zbx_json_brackets_by_name(&jp, "values", &jp_values) ||
		SUCCEED != zbx_json_brackets_by_name(&jp, "timestamps", &jp_timestamps))
	{
		zabbix_log(LOG_LEVEL_WARNING, "%s : couldn't parse responce json from vmetrics either values or timestamps arrays are missing", __func__);
		goto out;
	};

	zabbix_log(LOG_LEVEL_DEBUG, "%s: Found values and timestamps: %s %s", __func__, jp_values.start, jp_timestamps.start);

	while (NULL != (value = zbx_json_next(&jp_values, value)) && NULL != (timestamp = zbx_json_next(&jp_timestamps, timestamp)))
	{
		u_int64_t timestamp_int;
		//victoria returns time is ms
		
		timestamp_int=strtol(timestamp, NULL, 10);
		hr.timestamp.sec = timestamp_int/1000;
		hr.timestamp.ns =  (timestamp_int % 1000) * 1000000;

		switch (value_type)
		{
		case ITEM_VALUE_TYPE_UINT64:
			hr.value.ui64 = strtol(value, NULL, 10);
			//zabbix_log(LOG_LEVEL_INFORMATION,"Parsed as ui %ld",hr.value.ui64);
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			hr.value.ui64 = strtof(value, NULL);
			//zabbix_log(LOG_LEVEL_INFORMATION,"Parsed as float  %f",hr.value.dbl);
			break;
		}

		zbx_vector_history_record_append_ptr(values, &hr);
		valuecount++;
	}

out: 
//zbx_free(conf->buf);
curl_easy_cleanup(handle);
curl_slist_free_all(curl_headers);

zbx_free(page_r.data);

zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);
zabbix_log(LOG_LEVEL_DEBUG, "End of %s: read %d items()", __func__, valuecount);

//retrun succeeds ander any circumstances
//since otherwise history syncers will try to repeate the query
return SUCCEED;
}

/************************************************************************************
 *                                                                                  *
 * Function: vmetrics_add_values                                                     *
 *                                                                                  *
 * Purpose: sends history data to the storage                                       *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *              history - [IN] the history data vector (may have mixed value types) *
 *                                                                                  *
 ************************************************************************************/
static int vmetrics_add_values(void *data, const zbx_vector_ptr_t *history)
{
	zbx_vmetrics_data_t *conf = (zbx_vmetrics_data_t *)data;
	int i, j, num = 0;
	ZBX_DC_HISTORY *h;
	struct zbx_json json_idx, json;
	size_t buf_alloc = 0, buf_offset = 0;
	char hostname_quoted[MAX_STRING_LEN], item_key_quoted[MAX_STRING_LEN];

	char *sql_buffer = NULL;
	size_t sql_alloc = 0, sql_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];

		if (0 == conf->write_types[h->value_type])
			continue;

		if (ITEM_VALUE_TYPE_UINT64 != h->value_type && ITEM_VALUE_TYPE_FLOAT != h->value_type)
			continue;

		comma_escape(h->host_name, hostname_quoted);
		comma_escape(h->item_key, item_key_quoted);

		if (CONFIG_DEBUG_ITEM == h->itemid)
			zabbix_log(LOG_LEVEL_INFORMATION, "Debug item: %ld at %s: writing item value to the vmetrics", h->itemid, __func__);

		zbx_snprintf_alloc(&sql_buffer, &sql_alloc, &sql_offset, "item,label=%s,hostname=%s,itemname=%s %ld=",
						   conf->dbname,
						   hostname_quoted, item_key_quoted, h->itemid);

		//type-dependent part
		if (ITEM_VALUE_TYPE_UINT64 == h->value_type)
			zbx_snprintf_alloc(&sql_buffer, &sql_alloc, &sql_offset, "%ld %ld%09ld\n", h->value.ui64, h->ts.sec, h->ts.ns);

		if (ITEM_VALUE_TYPE_FLOAT == h->value_type)
			zbx_snprintf_alloc(&sql_buffer, &sql_alloc, &sql_offset, "%f %ld%09ld\n", h->value.dbl, h->ts.sec, h->ts.ns);

		num++;
	}

	if (num > 0)
	{

		zbx_httppage_t page_r;
		bzero(&page_r, sizeof(zbx_httppage_t));
		struct curl_slist *curl_headers = NULL;
		char errbuf[CURL_ERROR_SIZE];
		CURLcode err;
		CURL *handle = NULL;

		if (NULL == (handle = curl_easy_init()))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
		}
		else
		{

			curl_easy_setopt(handle, CURLOPT_URL, conf->write_url);
			curl_easy_setopt(handle, CURLOPT_POSTFIELDS, sql_buffer);
			curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
			curl_easy_setopt(handle, CURLOPT_WRITEDATA, page_r);
			curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
			curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

			if (NULL != conf->username && NULL != conf->password)
			{
				curl_easy_setopt(handle, CURLOPT_USERNAME, conf->username);
				curl_easy_setopt(handle, CURLOPT_PASSWORD, conf->password);
			}

			if (CURLE_OK != (err = curl_easy_perform(handle)))
			{
				vmetrics_log_error(handle, err, errbuf, &page_r);
				zabbix_log(LOG_LEVEL_INFORMATION, "Failed query '%s'", sql_buffer);
			} else  {
					//zabbix_log(LOG_LEVEL_INFORMATION, "OK query '%s'", sql_buffer);
			}
		}

		zbx_free(page_r.data);
		curl_slist_free_all(curl_headers);
		curl_easy_cleanup(handle);
	}

	zbx_free(sql_buffer);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return num;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_vmetrics_init                                               *
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
int	glb_history_vmetrics_init(char *params)
{
	zbx_vmetrics_data_t	*conf;

//for clickhouse there are following params are parsed:
//url : by default http://localhost:8123
//dbname : by default zabbix
//username and passsword  - by default unset
//write_types: (ui64, double) by default
//read_types: (ui64, double)	by default
//read_aggregate_types: (ui64, double) by default
//disable_reads: 0 by default (how long not to do readings)

	 //history mode expects old good JSON as a config, let's parse it
    struct zbx_json_parse jp, jp_config;
	char url[MAX_STRING_LEN], username[MAX_ID_LEN],password[MAX_ID_LEN],tmp_str[MAX_ID_LEN];

	size_t alloc=0,offset=0;
	zbx_json_type_t type;
    
	conf = (zbx_vmetrics_data_t *)zbx_malloc(NULL, sizeof(zbx_vmetrics_data_t));
	memset(conf, 0, sizeof(zbx_vmetrics_data_t));
	
	if ( SUCCEED != zbx_json_open(params, &jp_config)) {
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't parse configureation: '%s', most likely not a valid JSON");
		return FAIL;
	}
	
	zbx_strlcpy(url,"http://localhost:8428",MAX_STRING_LEN);
	
	zbx_json_value_by_name(&jp_config,"url", url, MAX_STRING_LEN,&type);
	zbx_rtrim(url, "/");
	
	zbx_snprintf_alloc(&conf->write_url, &alloc, &offset, "%s/write", url);
	
	alloc=0; offset=0;
	zbx_snprintf_alloc(&conf->read_url, &alloc, &offset, "%s", url);

	conf->username = NULL;
	conf->password = NULL;

	if (SUCCEED == zbx_json_value_by_name(&jp_config,"username", tmp_str, MAX_ID_LEN,&type))  {
		alloc=0; offset=0;
		zbx_snprintf_alloc(&conf->username,&alloc,&offset,"%s",tmp_str);
	}
	
	if (SUCCEED == zbx_json_value_by_name(&jp_config,"password", tmp_str, MAX_ID_LEN,&type)) {
		alloc=0; offset=0;
		zbx_snprintf_alloc(&conf->password,&alloc,&offset,"%s",tmp_str);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_VMETRICS_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"write_types", tmp_str, MAX_ID_LEN,&type);

	zbx_strlcpy(conf->dbname,GLB_DEFAULT_VMETRICS_DBNAME,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"dbname", conf->dbname, MAX_ID_LEN,&type);

	glb_set_process_types(conf->write_types, tmp_str);
	
	if (glb_types_array_sum(conf->write_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init victoriametrics module: WRITE types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE,(void (*)(void))vmetrics_add_values,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_VMETRICS_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init victoriametrics module: READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ,(void (*)(void))vmetrics_get_values,conf);
	}

	zbx_strlcpy(tmp_str,GLB_DEFAULT_VMETRICS_TYPES,MAX_ID_LEN);
	zbx_json_value_by_name(&jp_config,"read_aggregate_types", tmp_str, MAX_ID_LEN,&type);
	glb_set_process_types(conf->read_aggregate_types, tmp_str);
	
	if (glb_types_array_sum(conf->read_aggregate_types) > 0) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Init victoriametrics module: AGGREGATE READ types '%s'",tmp_str);
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGGREGATED,(void (*)(void))vmetrics_get_agg_values,conf);
	}
	//now, need to setup callbacks for the functions we're involved
	glb_register_callback(GLB_MODULE_API_DESTROY,(void (*)(void))vmetrics_destroy,conf);
	
	if (glb_types_array_sum(conf->write_types) > 0) {
		glb_register_callback(GLB_MODULE_API_HISTORY_WRITE,(void (*)(void))vmetrics_add_values,conf);
	}
	if (glb_types_array_sum(conf->read_types) > 0) {
		glb_register_callback(GLB_MODULE_API_HISTORY_READ,(void (*)(void))vmetrics_get_values,conf);
	}
	if (glb_types_array_sum(conf->read_aggregate_types) > 0) {
		glb_register_callback(GLB_MODULE_API_HISTORY_READ_AGGREGATED,(void (*)(void))vmetrics_get_agg_values,conf);
	}
		
	return SUCCEED;
}

#else

int zbx_history_vmetrics_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	ZBX_UNUSED(hist);
	ZBX_UNUSED(value_type);

	*error = zbx_strdup(*error, "cURL library support >= 7.28.0 is required for vmetricssearch history backend");
	return FAIL;
}

#endif
