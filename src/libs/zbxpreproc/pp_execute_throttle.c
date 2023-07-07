/*
** Glaber
** Copyright (C) 2001-2100 Glaber
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

#include "zbxcommon.h"
#include "zbxjson.h"
#include "zbxalgo.h"
#include "zbxstr.h"
#include "zbxtime.h"
#include "zbxvariant.h"

#define GLB_FUNC_UNKNOWN 0
#define GLB_FUNC_SUM 1
#define GLB_FUNC_COUNT 2
#define GLB_FUNC_AVG 3
#define GLB_FUNC_MAX 4
#define GLB_FUNC_MIN 5


static char get_agg_func(const char *func_name)
{
	if (0 == strcmp(func_name, "sum"))
		return GLB_FUNC_SUM;
	if (0 == strcmp(func_name, "count"))
		return GLB_FUNC_COUNT;
	if (0 == strcmp(func_name, "avg"))
		return GLB_FUNC_AVG;
	if (0 == strcmp(func_name, "max"))
		return GLB_FUNC_MAX;
	if (0 == strcmp(func_name, "min"))
		return GLB_FUNC_MIN;

	return GLB_FUNC_UNKNOWN;
}

int pp_execute_throttle_value_agg(zbx_variant_t *value, const zbx_timespec_t *ts, const char *params,
										   zbx_variant_t *history_value, zbx_timespec_t *history_ts, char value_type)
{
	int ret, timeout, period = 0, len_time, len_aggf;
	char *ptr;
	const char *time, *aggf_name;
	char func = GLB_FUNC_UNKNOWN;

	// one special case, if previous step fails, it expected to return string "NaN"
	// in such just discard the value
	if (ZBX_VARIANT_STR == value->type && 0 == strcmp(value->data.str, "NaN"))
	{
		zbx_variant_clear(value);
		return SUCCEED;
	}

	// there are two params - aggregation time and aggregation function
	time = params;

	if (NULL == (ptr = strchr(params, '\n')))
	{
		zbx_variant_set_error(value, zbx_strdup(NULL, "cannot find second parameter"));
		return FAIL;
	}

	if (0 == (len_time = ptr - params))
	{
		zbx_variant_set_error(value, zbx_strdup(NULL, "first parameter must be defined"));
		return FAIL;
	}

	aggf_name = ptr + 1;
	len_aggf = strlen(ptr + 1);

	func = get_agg_func(aggf_name);

	if (GLB_FUNC_UNKNOWN == func)
	{
		char *error = NULL;
		
		error = zbx_dsprintf(NULL, "invalid function name: %s", aggf_name);
		zbx_variant_set_error(value, error);
		
		return FAIL;
	}

	if (FAIL == zbx_is_time_suffix(time, &timeout, len_time))
	{
		char *error = NULL;

		error = zbx_dsprintf(NULL, "invalid time period: %s", params);
		zbx_variant_set_error(value, error);
		return FAIL;
	}

	// all type of metrics except count require numerical values, converting
	if (GLB_FUNC_COUNT != func && ZBX_VARIANT_DBL != value->type && ZBX_VARIANT_UI64 != value->type)
	{

		if (ZBX_VARIANT_STR != value->type)
		{
			zbx_variant_clear(history_value);
			zbx_variant_clear(value);
			return FAIL;
		}

		errno = 0;
		char *p = value->data.str;

		zbx_variant_convert(value, ZBX_VARIANT_DBL);
	}

	// history value hasn't been init - means we're starting
	if (ZBX_VARIANT_NONE == history_value->type)
	{
		if (GLB_FUNC_COUNT == func)
		{
			zbx_variant_set_ui64(history_value, 0);
		}
		else
		{
			zbx_variant_set_dbl(history_value, value->data.dbl);
		}

		history_ts->ns = 0;
		history_ts->sec = ts->sec;
	}

	if (GLB_FUNC_MIN == func)
	{
		zbx_variant_set_dbl(history_value, MIN(history_value->data.dbl, value->data.dbl));
	}
	else if (GLB_FUNC_MAX == func)
	{
		zbx_variant_set_dbl(history_value, MAX(history_value->data.dbl, value->data.dbl));
	}
	else if (GLB_FUNC_AVG == func || GLB_FUNC_SUM == func)
	{
		if (0 != history_ts->ns)
		{
			zbx_variant_set_dbl(history_value, history_value->data.dbl + value->data.dbl);
		}
	}
	else if (GLB_FUNC_COUNT == func)
	{
		// noop here
	}
	else
	{
		char *error = NULL;

		error = zbx_dsprintf(NULL, "Unknown function %d for aggregation", func);
		zbx_variant_set_error(value, error);
		
		return FAIL;
	}
	history_ts->ns++;
	zbx_variant_clear(value);

	if (history_ts->sec + timeout < ts->sec)
	{

		if (GLB_FUNC_MIN == func || GLB_FUNC_MAX == func || GLB_FUNC_SUM == func)
		{
			zbx_variant_copy(value, history_value);
		}
		else if (GLB_FUNC_AVG == func)
		{
			zbx_variant_set_dbl(value, history_value->data.dbl / history_ts->ns);
		}
		else if (GLB_FUNC_COUNT == func)
		{

			zbx_variant_set_ui64(value, history_ts->ns);
		}

		zbx_variant_clear(history_value);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Result is not ready to be emmited, %d seconds left, %d values collected", history_ts->sec + timeout - ts->sec, history_ts->ns);
	}

	return SUCCEED;
}
