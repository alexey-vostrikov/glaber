/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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

/*
 * NOTE!!!!!
 *
 * This is the new expression syntax support for trigger functions and calculated/aggregated
 * checks. The old syntax is still used in simple macros. When the new expression syntax
 * support is added to simple macros the evalfunc.c:evaluate_function (and related code)
 * must be removed, this code must be copied over the old implementation and unused code removed.
 */

#include "common.h"
#include "db.h"
#include "log.h"
#include "zbxserver.h"
#include "glb_cache.h"
#include "evalfunc.h"
#include "zbxregexp.h"
#include "zbxtrends.h"

typedef enum
{
	ZBX_PARAM_OPTIONAL,
	ZBX_PARAM_MANDATORY
}
zbx_param_type_t;

typedef enum
{
	ZBX_VALUE_NONE,
	ZBX_VALUE_SECONDS,
	ZBX_VALUE_NVALUES
}
zbx_value_type_t;

static const char	*zbx_type_string(zbx_value_type_t type)
{
	switch (type)
	{
		case ZBX_VALUE_NONE:
			return "none";
		case ZBX_VALUE_SECONDS:
			return "sec";
		case ZBX_VALUE_NVALUES:
			return "num";
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return "unknown";
	}
}

/******************************************************************************
 *                                                                            *
 * Function: get_function_parameter_int                                       *
 *                                                                            *
 * Purpose: get the value of sec|#num trigger function parameter              *
 *                                                                            *
 * Parameters: parameters     - [IN] trigger function parameters              *
 *             Nparam         - [IN] specifies which parameter to extract     *
 *             parameter_type - [IN] specifies whether parameter is mandatory *
 *                              or optional                                   *
 *             value          - [OUT] parameter value (preserved as is if the *
 *                              parameter is optional and empty)              *
 *             type           - [OUT] parameter value type (number of seconds *
 *                              or number of values)                          *
 *                                                                            *
 * Return value: SUCCEED - parameter is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	get_function_parameter_int(const char *parameters, int Nparam, zbx_param_type_t parameter_type,
		int *value, zbx_value_type_t *type)
{
	char	*parameter;
	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __func__, parameters, Nparam);

	if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	if ('\0' == *parameter)
	{
		switch (parameter_type)
		{
			case ZBX_PARAM_OPTIONAL:
				ret = SUCCEED;
				break;
			case ZBX_PARAM_MANDATORY:
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}
	else if ('#' == *parameter)
	{
		*type = ZBX_VALUE_NVALUES;
		if (SUCCEED == is_uint31(parameter + 1, value) && 0 < *value)
			ret = SUCCEED;
	}
	else if ('-' == *parameter)
	{
		if (SUCCEED == is_time_suffix(parameter + 1, value, ZBX_LENGTH_UNLIMITED))
		{
			*value = -(*value);
			*type = ZBX_VALUE_SECONDS;
			ret = SUCCEED;
		}
	}
	else if (SUCCEED == is_time_suffix(parameter, value, ZBX_LENGTH_UNLIMITED))
	{
		*type = ZBX_VALUE_SECONDS;
		ret = SUCCEED;
	}

	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "%s() type:%s value:%d", __func__, zbx_type_string(*type), *value);

	zbx_free(parameter);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	get_function_parameter_uint64(const char *parameters, int Nparam, zbx_uint64_t *value)
{
	char	*parameter;
	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __func__, parameters, Nparam);

	if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	if (SUCCEED == (ret = is_uint64(parameter, value)))
		zabbix_log(LOG_LEVEL_DEBUG, "%s() value:" ZBX_FS_UI64, __func__, *value);

	zbx_free(parameter);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	get_function_parameter_float(const char *parameters, int Nparam, unsigned char flags, double *value)
{
	char	*parameter;
	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __func__, parameters, Nparam);

	if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	if (SUCCEED == (ret = is_double_suffix(parameter, flags)))
	{
		*value = str2double(parameter);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() value:" ZBX_FS_DBL, __func__, *value);
	}

	zbx_free(parameter);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	get_function_parameter_str(const char *parameters, int Nparam, char **value)
{
	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __func__, parameters, Nparam);

	if (NULL == (*value = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	zabbix_log(LOG_LEVEL_DEBUG, "%s() value:'%s'", __func__, *value);
	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_function_parameter_hist_range                                *
 *                                                                            *
 * Purpose: get the value of sec|num + timeshift trigger function parameter   *
 *                                                                            *
 * Parameters: from           - [IN] the function calculation time            *
 *             parameters     - [IN] trigger function parameters              *
 *             Nparam         - [IN] specifies which parameter to extract     *
 *             value          - [OUT] parameter value (preserved as is if the *
 *                              parameter is optional and empty)              *
 *             type           - [OUT] parameter value type (number of seconds *
 *                              or number of values)                          *
 *             timeshift      - [OUT] the timeshift value (0 if absent)       *
 *                                                                            *
 * Return value: SUCCEED - parameter is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	get_function_parameter_hist_range(int from, const char *parameters, int Nparam, int *value,
		zbx_value_type_t *type, int *timeshift)
{
	char	*parameter = NULL, *shift;
	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __func__, parameters, Nparam);

	if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	if (NULL != (shift = strchr(parameter, ':')))
		*shift++ = '\0';

	if ('\0' == *parameter)
	{
		*value = 0;
		*type = ZBX_VALUE_NONE;
	}
	else if ('#' != *parameter)
	{
		if (SUCCEED != is_time_suffix(parameter, value, ZBX_LENGTH_UNLIMITED) || 0 > *value)
			goto out;

		*type = ZBX_VALUE_SECONDS;
	}
	else
	{
		if (SUCCEED != is_uint31(parameter + 1, value) || 0 >= *value)
			goto out;
		*type = ZBX_VALUE_NVALUES;
	}

	if (NULL != shift)
	{
		struct tm	tm;
		char		*error = NULL;
		int		end;

		if (SUCCEED != zbx_parse_timeshift(from, shift, &tm, &error))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() timeshift error:%s", __func__, error);
			zbx_free(error);
			goto out;
		}

		if (-1 == (end = mktime(&tm)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() invalid timeshift value:%s", __func__, zbx_strerror(errno));
			goto out;
		}

		if (end >= from)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() timeshift produced time in future", __func__);
			goto out;
		}

		*timeshift = from - end;
	}
	else
		*timeshift = 0;

	ret = SUCCEED;
	zabbix_log(LOG_LEVEL_DEBUG, "%s() type:%s value:%d timeshift:%d", __func__, zbx_type_string(*type), *value,
			*timeshift);
out:
	zbx_free(parameter);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_last_n_value                                                 *
 *                                                                            *
 * Purpose: get last Nth value defined by #num:now-timeshift first parameter  *
 *                                                                            *
 * Parameters: item       - [IN] item (performance metric)                    *
 *             parameters - [IN] the parameter string with #sec|num/timeshift *
 *                          in first parameter                                *
 *             ts         - [IN] the starting timestamp                       *
 *             value      - [OUT] the Nth value                               *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - value was found successfully copied                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	get_last_n_value(const DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		zbx_history_record_t *value, char **error)
{
	int				arg1 = 1, ret = FAIL, time_shift;
	zbx_value_type_t		arg1_type = ZBX_VALUE_NVALUES;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zbx_history_record_vector_create(&values);

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift))
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (ZBX_VALUE_NVALUES != arg1_type)
		arg1 = 1;	/* time or non parameter is defaulted to "last(0)" */

	ts_end.sec -= time_shift;

	if (SUCCEED != zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, 0, arg1, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (arg1 <= values.values_num)
	{
		*value = values.values[arg1 - 1];
		zbx_vector_history_record_remove(&values, arg1 - 1);
		ret = SUCCEED;
	}
	else
		*error = zbx_strdup(*error, "not enough data");
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_LOGEVENTID                                              *
 *                                                                            *
 * Purpose: evaluate function 'logeventid' for the item                       *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - regex string for event id matching                 *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_LOGEVENTID(zbx_variant_t *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	char			*pattern = NULL;
	int			ret = FAIL, nparams;
	zbx_vector_ptr_t	regexps;
	zbx_history_record_t	vc_value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&regexps);

	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (2 == nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 2, &pattern))
		{
			*error = zbx_strdup(*error, "invalid third parameter");
			goto out;
		}

		if ('@' == *pattern)
		{
			DCget_expressions_by_name(&regexps, pattern + 1);

			if (0 == regexps.values_num)
			{
				*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist",
						pattern + 1);
				goto out;
			}
		}
	}
	else
		pattern = zbx_strdup(NULL, "");

	if (SUCCEED == get_last_n_value(item, parameters, ts, &vc_value, error))
	{
		char	logeventid[16];
		int	regexp_ret;

		zbx_snprintf(logeventid, sizeof(logeventid), "%d", vc_value.value.log->logeventid);

		if (FAIL == (regexp_ret = regexp_match_ex(&regexps, logeventid, pattern, ZBX_CASE_SENSITIVE)))
		{
			*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", pattern);
		}
		else
		{
			if (ZBX_REGEXP_MATCH == regexp_ret)
				zbx_variant_set_dbl(value, 1);
			else if (ZBX_REGEXP_NO_MATCH == regexp_ret)
				zbx_variant_set_dbl(value, 0);

			ret = SUCCEED;
		}

		zbx_history_record_clear(&vc_value, item->value_type);
	}
	else
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGEVENTID is empty");
out:
	zbx_free(pattern);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_LOGSOURCE                                               *
 *                                                                            *
 * Purpose: evaluate function 'logsource' for the item                        *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - ignored                                            *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_LOGSOURCE(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	char			*pattern = NULL;
	int			ret = FAIL, nparams;
	zbx_vector_ptr_t	regexps;
	zbx_history_record_t	vc_value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&regexps);

	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (2 == nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 2, &pattern))
		{
			*error = zbx_strdup(*error, "invalid third parameter");
			goto out;
		}

		if ('@' == *pattern)
		{
			DCget_expressions_by_name(&regexps, pattern + 1);

			if (0 == regexps.values_num)
			{
				*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist",
						pattern + 1);
				goto out;
			}
		}
	}
	else
		pattern = zbx_strdup(NULL, "");

	if (SUCCEED == get_last_n_value(item, parameters, ts, &vc_value, error))
	{
		switch (regexp_match_ex(&regexps, vc_value.value.log->source, pattern, ZBX_CASE_SENSITIVE))
		{
			case ZBX_REGEXP_MATCH:
				zbx_variant_set_dbl(value, 1);
				ret = SUCCEED;
				break;
			case ZBX_REGEXP_NO_MATCH:
				zbx_variant_set_dbl(value, 0);
				ret = SUCCEED;
				break;
			case FAIL:
				*error = zbx_dsprintf(*error, "invalid regular expression");
		}

		zbx_history_record_clear(&vc_value, item->value_type);
	}
	else
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGSOURCE is empty");
out:
	zbx_free(pattern);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_LOGSEVERITY                                             *
 *                                                                            *
 * Purpose: evaluate function 'logseverity' for the item                      *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_LOGSEVERITY(zbx_variant_t *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	int			ret = FAIL;
	zbx_history_record_t	vc_value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED == get_last_n_value(item, parameters, ts, &vc_value, error))
	{
		zbx_variant_set_dbl(value, vc_value.value.log->severity);
		zbx_history_record_clear(&vc_value, item->value_type);

		ret = SUCCEED;
	}
	else
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGSEVERITY is empty");
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	history_record_float_compare(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	ZBX_RETURN_IF_NOT_EQUAL(d1->value.dbl, d2->value.dbl);

	return 0;
}

static int	history_record_uint64_compare(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	ZBX_RETURN_IF_NOT_EQUAL(d1->value.ui64, d2->value.ui64);

	return 0;
}

static int	history_record_str_compare(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	return strcmp(d1->value.str, d2->value.str);
}

static int	history_record_log_compare(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	int	value_match;

	if (0 != (value_match = strcmp(d1->value.log->value, d2->value.log->value)))
		return value_match;

	if (NULL != d1->value.log->source && NULL != d2->value.log->source)
		return strcmp(d1->value.log->source, d2->value.log->source);

	if (NULL != d2->value.log->source)
		return -1;

	if (NULL != d1->value.log->source)
		return 1;

	return 0;
}

/* specialized versions of zbx_vector_history_record_*_uniq() because */
/* standard versions do not release memory occupied by duplicate elements */

static void	zbx_vector_history_record_str_uniq(zbx_vector_history_record_t *vector, zbx_compare_func_t compare_func)
{
	if (2 <= vector->values_num)
	{
		int	i = 0, j = 1;

		while (j < vector->values_num)
		{
			if (0 != compare_func(&vector->values[i], &vector->values[j]))
			{
				i++;
				j++;
			}
			else
			{
				zbx_free(vector->values[j].value.str);
				zbx_vector_history_record_remove(vector, j);
			}
		}
	}
}

static void	zbx_vector_history_record_log_uniq(zbx_vector_history_record_t *vector, zbx_compare_func_t compare_func)
{
	if (2 <= vector->values_num)
	{
		int	i = 0, j = 1;

		while (j < vector->values_num)
		{
			if (0 != compare_func(&vector->values[i], &vector->values[j]))
			{
				i++;
				j++;
			}
			else
			{
				zbx_free(vector->values[j].value.log->source);
				zbx_free(vector->values[j].value.log->value);
				zbx_free(vector->values[j].value.log);
				zbx_vector_history_record_remove(vector, j);
			}
		}
	}
}

#define OP_UNKNOWN	-1
#define OP_EQ		0
#define OP_NE		1
#define OP_GT		2
#define OP_GE		3
#define OP_LT		4
#define OP_LE		5
#define OP_LIKE		6
#define OP_REGEXP	7
#define OP_IREGEXP	8
#define OP_BITAND		9

static void	count_one_ui64(int *count, int op, zbx_uint64_t value, zbx_uint64_t pattern, zbx_uint64_t mask)
{
	switch (op)
	{
		case OP_EQ:
			if (value == pattern)
				(*count)++;
			break;
		case OP_NE:
			if (value != pattern)
				(*count)++;
			break;
		case OP_GT:
			if (value > pattern)
				(*count)++;
			break;
		case OP_GE:
			if (value >= pattern)
				(*count)++;
			break;
		case OP_LT:
			if (value < pattern)
				(*count)++;
			break;
		case OP_LE:
			if (value <= pattern)
				(*count)++;
			break;
		case OP_BITAND:
			if ((value & mask) == pattern)
				(*count)++;
	}
}

static void	count_one_dbl(int *count, int op, double value, double pattern)
{
	switch (op)
	{
		case OP_EQ:
			if (value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_NE:
			if (!(value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON))
				(*count)++;
			break;
		case OP_GT:
			if (value >= pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_GE:
			if (value > pattern - ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_LT:
			if (value <= pattern - ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_LE:
			if (value < pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
	}
}

static void	count_one_str(int *count, int op, const char *value, const char *pattern, zbx_vector_ptr_t *regexps)
{
	int	res;

	switch (op)
	{
		case OP_EQ:
			if (0 == strcmp(value, pattern))
				(*count)++;
			break;
		case OP_NE:
			if (0 != strcmp(value, pattern))
				(*count)++;
			break;
		case OP_LIKE:
			if (NULL != strstr(value, pattern))
				(*count)++;
			break;
		case OP_REGEXP:
			if (ZBX_REGEXP_MATCH == (res = regexp_match_ex(regexps, value, pattern, ZBX_CASE_SENSITIVE)))
				(*count)++;
			else if (FAIL == res)
				*count = FAIL;
			break;
		case OP_IREGEXP:
			if (ZBX_REGEXP_MATCH == (res = regexp_match_ex(regexps, value, pattern, ZBX_IGNORE_CASE)))
				(*count)++;
			else if (FAIL == res)
				*count = FAIL;
	}
}

/* flags for evaluate_COUNT() */
#define COUNT_ALL	0
#define COUNT_UNIQUE	1

/******************************************************************************
 *                                                                            *
 * Function: evaluate_COUNT                                                   *
 *                                                                            *
 * Purpose: evaluate functions 'count' and 'find' for the item                *
 *                                                                            *
 * Parameters: item       - [IN] item (performance metric)                    *
 *             parameters - [IN] up to three comma-separated fields:          *
 *                            (1) number of seconds/values + timeshift        *
 *                            (2) comparison operator (optional)              *
 *                            (3) value to compare with (optional)            *
 *                                Becomes mandatory for numeric items if 3rd  *
 *                                parameter is specified and is not "regexp"  *
 *                                or "iregexp". With "bitand" can take one of *
 *                                2 forms:                                    *
 *                                  - value_to_compare_with/mask              *
 *                                  - mask                                    *
 *             ts         - [IN] the function evaluation time                 *
 *             limit      - [IN] the limit of counted values, will return     *
 *                              when the limit is reached                     *
 *             unique     - [IN] COUNT_ALL - count all values,                *
 *                               COUNT_UNIQUE - count unique values           *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_COUNT(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		int limit, int unique, char **error)
{
	int				arg1, op = OP_UNKNOWN, numeric_search, nparams, count = 0, i, ret = FAIL;
	int				seconds = 0, nvalues = 0, time_shift;
	char				*operator = NULL, *pattern2 = NULL, *pattern = NULL, buf[ZBX_MAX_UINT64_LEN];
	double				arg3_dbl;
	zbx_uint64_t			pattern_ui64, pattern2_ui64;
	zbx_value_type_t		arg1_type;
	zbx_vector_ptr_t		regexps;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&regexps);
	zbx_history_record_vector_create(&values);

	numeric_search = (ITEM_VALUE_TYPE_UINT64 == item->value_type || ITEM_VALUE_TYPE_FLOAT == item->value_type);

	if (3 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift))
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (2 <= nparams && SUCCEED != get_function_parameter_str(parameters, 2, &operator))
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	if (3 <= nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 3, &pattern))
		{
			*error = zbx_strdup(*error, "invalid fourth parameter");
			goto out;
		}
	}
	else
		pattern = zbx_strdup(NULL, "");

	ts_end.sec -= time_shift;

	if (NULL == operator || '\0' == *operator)
		op = (0 != numeric_search ? OP_EQ : OP_LIKE);
	else if (0 == strcmp(operator, "eq"))
		op = OP_EQ;
	else if (0 == strcmp(operator, "ne"))
		op = OP_NE;
	else if (0 == strcmp(operator, "gt"))
		op = OP_GT;
	else if (0 == strcmp(operator, "ge"))
		op = OP_GE;
	else if (0 == strcmp(operator, "lt"))
		op = OP_LT;
	else if (0 == strcmp(operator, "le"))
		op = OP_LE;
	else if (0 == strcmp(operator, "like"))
		op = OP_LIKE;
	else if (0 == strcmp(operator, "regexp"))
		op = OP_REGEXP;
	else if (0 == strcmp(operator, "iregexp"))
		op = OP_IREGEXP;
	else if (0 == strcmp(operator, "bitand"))
		op = OP_BITAND;

	if (OP_UNKNOWN == op)
	{
		*error = zbx_dsprintf(*error, "operator \"%s\" is not supported for function COUNT", operator);
		goto out;
	}

	numeric_search = (0 != numeric_search && OP_REGEXP != op && OP_IREGEXP != op);

	if (0 != numeric_search)
	{
		if (NULL != operator && '\0' != *operator && '\0' == *pattern)
		{
			*error = zbx_strdup(*error, "pattern must be provided along with operator for numeric values");
			goto out;
		}

		if (OP_LIKE == op)
		{
			*error = zbx_dsprintf(*error, "operator \"%s\" is not supported for counting numeric values",
					operator);
			goto out;
		}

		if (OP_BITAND == op && ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			*error = zbx_dsprintf(*error, "operator \"%s\" is not supported for counting float values",
					operator);
			goto out;
		}

		if (OP_BITAND == op && NULL != (pattern2 = strchr(pattern, '/')))
		{
			*pattern2 = '\0';	/* end of the 1st part of the 2nd parameter (number to compare with) */
			pattern2++;	/* start of the 2nd part of the 2nd parameter (mask) */
		}

		if (NULL != pattern && '\0' != *pattern)
		{
			if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
			{
				if (OP_BITAND != op)
				{
					if (SUCCEED != str2uint64(pattern, ZBX_UNIT_SYMBOLS, &pattern_ui64))
					{
						*error = zbx_dsprintf(*error, "\"%s\" is not a valid numeric unsigned"
								" value", pattern);
						goto out;
					}
					pattern2_ui64 = 0;
				}
				else
				{
					if (SUCCEED != is_uint64(pattern, &pattern_ui64))
					{
						*error = zbx_dsprintf(*error, "\"%s\" is not a valid numeric unsigned"
								" value", pattern);
						goto out;
					}

					if (NULL != pattern2)
					{
						if (SUCCEED != is_uint64(pattern2, &pattern2_ui64))
						{
							*error = zbx_dsprintf(*error, "\"%s\" is not a valid numeric"
									" unsigned value", pattern2);
							goto out;
						}
					}
					else
						pattern2_ui64 = pattern_ui64;
				}
			}
			else
			{
				if (SUCCEED != is_double_suffix(pattern, ZBX_FLAG_DOUBLE_SUFFIX))
				{
					*error = zbx_dsprintf(*error, "\"%s\" is not a valid numeric float value",
							pattern);
					goto out;
				}

				arg3_dbl = str2double(pattern);
			}
		}
	}
	else if (OP_LIKE != op && OP_REGEXP != op && OP_IREGEXP != op && OP_EQ != op && OP_NE != op)
	{
		*error = zbx_dsprintf(*error, "operator \"%s\" is not supported for counting textual values", operator);
		goto out;
	}

	if ((OP_REGEXP == op || OP_IREGEXP == op) && NULL != pattern && '@' == *pattern)
	{
		DCget_expressions_by_name(&regexps, pattern + 1);

		if (0 == regexps.values_num)
		{
			*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist", pattern + 1);
			goto out;
		}
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		case ZBX_VALUE_NONE:
			nvalues = 1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (COUNT_UNIQUE == unique)
	{
		switch (item->value_type)
		{
			case ITEM_VALUE_TYPE_UINT64:
				zbx_vector_history_record_sort(&values,
						(zbx_compare_func_t)history_record_uint64_compare);
				zbx_vector_history_record_uniq(&values,
						(zbx_compare_func_t)history_record_uint64_compare);
				break;
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_vector_history_record_sort(&values,
						(zbx_compare_func_t)history_record_float_compare);
				zbx_vector_history_record_uniq(&values,
						(zbx_compare_func_t)history_record_float_compare);
				break;
			case ITEM_VALUE_TYPE_LOG:
				zbx_vector_history_record_sort(&values,
						(zbx_compare_func_t)history_record_log_compare);
				zbx_vector_history_record_log_uniq(&values,
						(zbx_compare_func_t)history_record_log_compare);
				break;
			default:
				zbx_vector_history_record_sort(&values,
						(zbx_compare_func_t)history_record_str_compare);
				zbx_vector_history_record_str_uniq(&values,
						(zbx_compare_func_t)history_record_str_compare);
		}
	}

	/* skip counting values one by one if both pattern and operator are empty or "" is searched in text values */
	if ((NULL != pattern && '\0' != *pattern) || (NULL != operator && '\0' != *operator &&
			OP_LIKE != op && OP_REGEXP != op && OP_IREGEXP != op))
	{
		switch (item->value_type)
		{
			case ITEM_VALUE_TYPE_UINT64:
				if (0 != numeric_search)
				{
					for (i = 0; i < values.values_num && count < limit; i++)
					{
						count_one_ui64(&count, op, values.values[i].value.ui64, pattern_ui64,
								pattern2_ui64);
					}
				}
				else
				{
					for (i = 0; i < values.values_num && FAIL != count && count < limit; i++)
					{
						zbx_snprintf(buf, sizeof(buf), ZBX_FS_UI64,
								values.values[i].value.ui64);
						count_one_str(&count, op, buf, pattern, &regexps);
					}
				}
				break;
			case ITEM_VALUE_TYPE_FLOAT:
				if (0 != numeric_search)
				{
					for (i = 0; i < values.values_num && count < limit; i++)
						count_one_dbl(&count, op, values.values[i].value.dbl, arg3_dbl);
				}
				else
				{
					for (i = 0; i < values.values_num && FAIL != count && count < limit; i++)
					{
						zbx_snprintf(buf, sizeof(buf), ZBX_FS_DBL_EXT(4),
								values.values[i].value.dbl);
						count_one_str(&count, op, buf, pattern, &regexps);
					}
				}
				break;
			case ITEM_VALUE_TYPE_LOG:
				for (i = 0; i < values.values_num && FAIL != count && count < limit; i++)
					count_one_str(&count, op, values.values[i].value.log->value, pattern, &regexps);
				break;
			default:
				for (i = 0; i < values.values_num && FAIL != count && count < limit; i++)
					count_one_str(&count, op, values.values[i].value.str, pattern, &regexps);
		}

		if (FAIL == count)
		{
			*error = zbx_strdup(*error, "invalid regular expression");
			goto out;
		}
	}
	else
	{
		if ((count = values.values_num) > limit)
			count = limit;
	}

	zbx_variant_set_dbl(value, count);

	ret = SUCCEED;
out:
	zbx_free(operator);
	zbx_free(pattern);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

#undef OP_UNKNOWN
#undef OP_EQ
#undef OP_NE
#undef OP_GT
#undef OP_GE
#undef OP_LT
#undef OP_LE
#undef OP_LIKE
#undef OP_REGEXP
#undef OP_IREGEXP
#undef OP_BITAND

/******************************************************************************
 *                                                                            *
 * Function: evaluate_SUM                                                     *
 *                                                                            *
 * Purpose: evaluate function 'sum' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_SUM(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	int				arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	history_value_t			result;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	ts_end.sec -= time_shift;

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
	{
		result.dbl = 0;

		for (i = 0; i < values.values_num; i++)
			result.dbl += values.values[i].value.dbl;
	}
	else
	{
		result.ui64 = 0;

		for (i = 0; i < values.values_num; i++)
			result.ui64 += values.values[i].value.ui64;
	}

	zbx_history_value2variant(&result, item->value_type, value);
	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_AVG                                                     *
 *                                                                            *
 * Purpose: evaluate function 'avg' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_AVG(zbx_variant_t  *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	int				arg1, ret = FAIL, i, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	ts_end.sec -= time_shift;

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		double	avg = 0;

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
				avg += values.values[i].value.dbl / (i + 1) - avg / (i + 1);
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
				avg += (double)values.values[i].value.ui64;

			avg = avg / values.values_num;
		}
		zbx_variant_set_dbl(value, avg);

		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for AVG is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_LAST                                                    *
 *                                                                            *
 * Purpose: evaluate function 'last' for the item                             *
 *                                                                            *
 * Parameters: value - dynamic buffer                                         *
 *             item - item (performance metric)                               *
 *             parameters - Nth last value and time shift (optional)          *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_LAST(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	int			ret;
	zbx_history_record_t	vc_value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED == (ret = get_last_n_value(item, parameters, ts, &vc_value, error)))
	{
		zbx_history_value2variant(&vc_value.value, item->value_type, value);
		zbx_history_record_clear(&vc_value, item->value_type);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_MIN                                                     *
 *                                                                            *
 * Purpose: evaluate function 'min' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_MIN(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	int				arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	ts_end.sec -= time_shift;

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		int	index = 0;

		if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.ui64 < values.values[index].value.ui64)
					index = i;
			}
		}
		else
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl < values.values[index].value.dbl)
					index = i;
			}
		}

		zbx_history_value2variant(&values.values[index].value, item->value_type, value);
		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for MIN is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_MAX                                                     *
 *                                                                            *
 * Purpose: evaluate function 'max' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_MAX(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	int				arg1, ret = FAIL, i, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	ts_end.sec -= time_shift;

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		int	index = 0;

		if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.ui64 > values.values[index].value.ui64)
					index = i;
			}
		}
		else
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl > values.values[index].value.dbl)
					index = i;
			}
		}

		zbx_history_value2variant(&values.values[index].value, item->value_type, value);

		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for MAX is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_PERCENTILE                                              *
 *                                                                            *
 * Purpose: evaluate function 'percentile' for the item                       *
 *                                                                            *
 * Parameters: item       - [IN] item (performance metric)                    *
 *             parameters - [IN] seconds/values, time shift (optional),       *
 *                               percentage                                   *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in        *
 *                         'value'                                            *
 *               FAIL    - failed to evaluate function                        *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_PERCENTILE(zbx_variant_t  *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	int				arg1, time_shift, ret = FAIL, seconds = 0, nvalues = 0;
	zbx_value_type_t		arg1_type;
	double				percentage;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}


	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	ts_end.sec -= time_shift;

	if (SUCCEED != get_function_parameter_float(parameters, 2, ZBX_FLAG_DOUBLE_PLAIN, &percentage) ||
			0.0 > percentage || 100.0 < percentage)
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		int	index;

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
			zbx_vector_history_record_sort(&values, (zbx_compare_func_t)history_record_float_compare);
		else
			zbx_vector_history_record_sort(&values, (zbx_compare_func_t)history_record_uint64_compare);

		if (0 == percentage)
			index = 1;
		else
			index = (int)ceil(values.values_num * (percentage / 100));

		zbx_history_value2variant(&values.values[index - 1].value, item->value_type, value);

		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for PERCENTILE is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_NODATA                                                  *
 *                                                                            *
 * Purpose: evaluate function 'nodata' for the item                           *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_NODATA(zbx_variant_t *value, DC_ITEM *item, const char *parameters, char **error)
{
	int				arg1, num, period, lazy = 1, ret = FAIL;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts;
	char				*arg2 = NULL;
	zbx_proxy_suppress_t		nodata_win;

	LOG_DBG("In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (2 < (num = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}
	
	if (SUCCEED != get_function_parameter_int(parameters, 1, ZBX_PARAM_MANDATORY, &arg1, &arg1_type) ||
			ZBX_VALUE_SECONDS != arg1_type || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}
	
	if (1 < num && (SUCCEED != get_function_parameter_str(parameters, 2, &arg2) ||
			('\0' != *arg2 && 0 != (lazy = strcmp("strict", arg2)))))
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	zbx_timespec(&ts);
	nodata_win.flags = ZBX_PROXY_SUPPRESS_DISABLE;

	if (0 != item->host.proxy_hostid && 0 != lazy)
	{
		int			lastaccess;

		if (SUCCEED != DCget_proxy_nodata_win(item->host.proxy_hostid, &nodata_win, &lastaccess))
		{
			*error = zbx_strdup(*error, "cannot retrieve proxy last access");
			goto out;
		}

		period = arg1 + (ts.sec - lastaccess);
	}
	else
		period = arg1;

	if (SUCCEED != (ret = glb_ic_get_values(item->itemid, item->value_type, &values, 0, 1, ts.sec))) {
		//there was a problem fetching the data
		*error = zbx_strdup(*error, "Couldn't fetch item, DB backend returned FAIL");
		LOG_DBG("%s: NODATA: item %ld fetching from the CACHE has failed", __func__, item->itemid);
		goto out;
	} 

	//there was no problem in fetching the data, check if the latest item is withing the period
	//if (1 == values.values_num) {
	//	zabbix_log(LOG_LEVEL_INFORMATION, "%s: NODATA: item %ld timestamp is %d period is %d time is %d", __func__, item->itemid, 
	//		values.values[0].timestamp.sec, period, time(NULL) );
	//}

	if (1 == values.values_num && 
			time(NULL) - values.values[0].timestamp.sec <=period  )
	{
		zbx_variant_set_dbl(value, 0);
	}
	else
	{
		int	seconds;

		if (SUCCEED != DCget_data_expected_from(item->itemid, &seconds))
		{
			*error = zbx_strdup(*error, "item does not exist, is disabled or belongs to a disabled host");
			goto out;
		}

		if (seconds + arg1 > ts.sec)
		{
			*error = zbx_strdup(*error,
					"item does not have enough data after server start or item creation");
			goto out;
		}

		if (0 != (nodata_win.flags & ZBX_PROXY_SUPPRESS_ACTIVE))
		{
			*error = zbx_strdup(*error, "historical data transfer from proxy is still in progress");
			goto out;
		}

		zbx_variant_set_dbl(value, 1);

		if (0 != item->host.proxy_hostid && 0 != lazy)
		{
			zabbix_log(LOG_LEVEL_TRACE, "Nodata in %s() flag:%d values_num:%d start_time:%d period:%d",
					__func__, nodata_win.flags, nodata_win.values_num, ts.sec - period, period);
		}
	}

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);
	zbx_free(arg2);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_CHANGE                                                  *
 *                                                                            *
 * Purpose: evaluate function 'change' for the item                           *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_CHANGE(zbx_variant_t *value, DC_ITEM *item, const zbx_timespec_t *ts, char **error)
{
	int				ret = FAIL;
	zbx_vector_history_record_t	values;
	double				result;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (SUCCEED != zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, 0, 2, ts) ||
			2 > values.values_num)
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	switch (item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			result = values.values[0].value.dbl - values.values[1].value.dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			/* to avoid overflow */
			if (values.values[0].value.ui64 >= values.values[1].value.ui64)
				result = values.values[0].value.ui64 - values.values[1].value.ui64;
			else
				result = -(double)(values.values[1].value.ui64 - values.values[0].value.ui64);
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (0 == strcmp(values.values[0].value.log->value, values.values[1].value.log->value))
				result = 0;
			else
				result = 1;
			break;

		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			if (0 == strcmp(values.values[0].value.str, values.values[1].value.str))
				result = 0;
			else
				result = 1;
			break;
		default:
			*error = zbx_strdup(*error, "invalid value type");
			goto out;
	}

	zbx_variant_set_dbl(value, result);
	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_FUZZYTIME                                               *
 *                                                                            *
 * Purpose: evaluate function 'fuzzytime' for the item                        *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_FUZZYTIME(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	int			arg1, ret = FAIL;
	zbx_value_type_t	arg1_type;
	zbx_history_record_t	vc_value;
	zbx_uint64_t		fuzlow, fuzhig;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_int(parameters, 1, ZBX_PARAM_MANDATORY, &arg1, &arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (ZBX_VALUE_SECONDS != arg1_type || ts->sec <= arg1)
	{
		*error = zbx_strdup(*error, "invalid argument type or value");
		goto out;
	}

	if (SUCCEED != zbx_vc_get_value(item->host.hostid, item->itemid, item->value_type, ts, &vc_value))
	{
		*error = zbx_strdup(*error, "cannot get value from value cache");
		goto out;
	}

	fuzlow = (int)(ts->sec - arg1);
	fuzhig = (int)(ts->sec + arg1);

	if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
	{
		if (vc_value.value.ui64 >= fuzlow && vc_value.value.ui64 <= fuzhig)
			zbx_variant_set_dbl(value, 1);
		else
			zbx_variant_set_dbl(value, 0);
	}
	else
	{
		if (vc_value.value.dbl >= fuzlow && vc_value.value.dbl <= fuzhig)
			zbx_variant_set_dbl(value, 1);
		else
			zbx_variant_set_dbl(value, 0);
	}

	zbx_history_record_clear(&vc_value, item->value_type);

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_BITAND                                                  *
 *                                                                            *
 * Purpose: evaluate logical bitwise function 'and' for the item              *
 *                                                                            *
 * Parameters: value - dynamic buffer                                         *
 *             item - item (performance metric)                               *
 *             parameters - to 2 comma-separated fields:                      *
 *                            (1) same as the 1st parameter for function      *
 *                                evaluate_LAST() (see documentation of       *
 *                                trigger function last()),                   *
 *                            (2) mask to bitwise AND with (mandatory),       *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_BITAND(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	char		*last_parameters = NULL;
	int		ret = FAIL;
	zbx_uint64_t	mask;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto clean;
	}

	if (2 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto clean;
	}

	if (SUCCEED != get_function_parameter_uint64(parameters, 2, &mask))
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto clean;
	}

	/* prepare the 1st and the 3rd parameter for passing to evaluate_LAST() */
	last_parameters = zbx_function_get_param_dyn(parameters, 1);

	if (SUCCEED == evaluate_LAST(value, item, last_parameters, ts, error))
	{
		/* the evaluate_LAST() should return uint64 value, but just to be sure try to convert it */
		if (SUCCEED != zbx_variant_convert(value, ZBX_VARIANT_UI64))
		{
			*error = zbx_strdup(*error, "invalid value type");
			goto clean;
		}
		zbx_variant_set_dbl(value, value->data.ui64 & (zbx_uint64_t)mask);
		ret = SUCCEED;
	}

	zbx_free(last_parameters);
clean:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_FORECAST                                                *
 *                                                                            *
 * Purpose: evaluate function 'forecast' for the item                         *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_FORECAST(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	char				*fit_str = NULL, *mode_str = NULL;
	double				*t = NULL, *x = NULL;
	int				nparams, time, arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		time_type, arg1_type;
	unsigned int			k = 0;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			zero_time;
	zbx_fit_t			fit;
	zbx_mode_t			mode;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 > (nparams = num_param(parameters)) || nparams > 4)
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (SUCCEED != get_function_parameter_int(parameters, 2, ZBX_PARAM_MANDATORY, &time, &time_type) ||
			ZBX_VALUE_SECONDS != time_type)
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	if (3 <= nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 3, &fit_str) ||
				SUCCEED != zbx_fit_code(fit_str, &fit, &k, error))
		{
			*error = zbx_strdup(*error, "invalid fourth parameter");
			goto out;
		}
	}
	else
	{
		fit = FIT_LINEAR;
	}

	if (4 == nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 4, &mode_str) ||
				SUCCEED != zbx_mode_code(mode_str, &mode, error))
		{
			*error = zbx_strdup(*error, "invalid fifth parameter");
			goto out;
		}
	}
	else
	{
		mode = MODE_VALUE;
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	ts_end.sec -= time_shift;

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		t = (double *)zbx_malloc(t, values.values_num * sizeof(double));
		x = (double *)zbx_malloc(x, values.values_num * sizeof(double));

		zero_time.sec = values.values[values.values_num - 1].timestamp.sec;
		zero_time.ns = values.values[values.values_num - 1].timestamp.ns;

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.dbl;
			}
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.ui64;
			}
		}

		zbx_variant_set_dbl(value, zbx_forecast(t, x, values.values_num,
				ts->sec - zero_time.sec - 1.0e-9 * (zero_time.ns + 1), time, fit, k, mode));
	}
	else
	{
		zbx_variant_set_dbl(value, ZBX_MATH_ERROR);
	}

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zbx_free(fit_str);
	zbx_free(mode_str);

	zbx_free(t);
	zbx_free(x);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_TIMELEFT                                                *
 *                                                                            *
 * Purpose: evaluate function 'timeleft' for the item                         *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_TIMELEFT(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	char				*fit_str = NULL;
	double				*t = NULL, *x = NULL, threshold;
	int				nparams, arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t		arg1_type;
	unsigned			k = 0;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			zero_time;
	zbx_fit_t			fit;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 > (nparams = num_param(parameters)) || nparams > 3)
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (SUCCEED != get_function_parameter_float(parameters, 2, ZBX_FLAG_DOUBLE_SUFFIX, &threshold))
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	if (3 == nparams)
	{
		if (SUCCEED != get_function_parameter_str(parameters, 3, &fit_str) ||
				SUCCEED != zbx_fit_code(fit_str, &fit, &k, error))
		{
			*error = zbx_strdup(*error, "invalid fourth parameter");
			goto out;
		}
	}
	else
	{
		fit = FIT_LINEAR;
	}

	if ((FIT_EXPONENTIAL == fit || FIT_POWER == fit) && 0.0 >= threshold)
	{
		*error = zbx_strdup(*error, "exponential and power functions are always positive");
		goto out;
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	ts_end.sec -= time_shift;

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		t = (double *)zbx_malloc(t, values.values_num * sizeof(double));
		x = (double *)zbx_malloc(x, values.values_num * sizeof(double));

		zero_time.sec = values.values[values.values_num - 1].timestamp.sec;
		zero_time.ns = values.values[values.values_num - 1].timestamp.ns;

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.dbl;
			}
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.ui64;
			}
		}

		zbx_variant_set_dbl(value, zbx_timeleft(t, x, values.values_num,
				ts->sec - zero_time.sec - 1.0e-9 * (zero_time.ns + 1), threshold, fit, k));
	}
	else
	{
		zbx_variant_set_dbl(value, ZBX_MATH_ERROR);
	}

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zbx_free(fit_str);

	zbx_free(t);
	zbx_free(x);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_TREND                                                   *
 *                                                                            *
 * Purpose: evaluate trend* functions for the item                            *
 *                                                                            *
 * Parameters: value      - [OUT] the function result                         *
 *             item       - [IN] item (performance metric)                    *
 *             func       - [IN] the trend function to evaluate               *
 *                               (avg, sum, count, delta, max, min)           *
 *             parameters - [IN] function parameters                          *
 *             ts         - [IN] the historical time when function must be    *
 *                               evaluated                                    *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_TREND(zbx_variant_t *value, DC_ITEM *item, const char *func, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	int		ret = FAIL, start, end;
	char		*period = NULL;
	const char	*table;
	double		value_dbl;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_str(parameters, 1, &period))
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	if (SUCCEED != zbx_trends_parse_range(ts->sec, period, &start, &end, error))
		goto out;

	switch (item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			table = "trends";
			break;
		case ITEM_VALUE_TYPE_UINT64:
			table = "trends_uint";
			break;
		default:
			*error = zbx_strdup(*error, "unsupported value type");
			goto out;
	}

	if (0 == strcmp(func, "avg"))
	{
		ret = zbx_trends_eval_avg(table, item->itemid, start, end, &value_dbl, error);
	}
	else if (0 == strcmp(func, "count"))
	{
		ret = zbx_trends_eval_count(table, item->itemid, start, end, &value_dbl, error);
	}
	else if (0 == strcmp(func, "max"))
	{
		ret = zbx_trends_eval_max(table, item->itemid, start, end, &value_dbl, error);
	}
	else if (0 == strcmp(func, "min"))
	{
		ret = zbx_trends_eval_min(table, item->itemid, start, end, &value_dbl, error);
	}
	else if (0 == strcmp(func, "sum"))
	{
		ret = zbx_trends_eval_sum(table, item->itemid, start, end, &value_dbl, error);
	}
	else
	{
		*error = zbx_strdup(*error, "unknown trend function");
		goto out;
	}

	if (SUCCEED == ret)
		zbx_variant_set_dbl(value, value_dbl);
out:
	zbx_free(period);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	validate_params_and_get_data(DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		zbx_vector_history_record_t *values, char **error)
{
	int			arg1, seconds = 0, nvalues = 0, time_shift;
	zbx_value_type_t	arg1_type;
	zbx_timespec_t		ts_end = *ts;

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		return FAIL;
	}

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		return FAIL;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift) ||
			ZBX_VALUE_NONE == arg1_type)
	{
		*error = zbx_strdup(*error, "invalid parameter");
		return FAIL;
	}

	ts_end.sec -= time_shift;

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			*error = zbx_strdup(*error, "invalid type of first argument");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	if (FAIL == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_FIRST                                                   *
 *                                                                            *
 * Purpose: evaluate function 'first' for the item                            *
 *                                                                            *
 * Parameters: value - dynamic buffer                                         *
 *             item - item (performance metric)                               *
 *             parameters - Nth first value and time shift (optional)         *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_FIRST(zbx_variant_t *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	int				arg1 = 1, ret = FAIL, seconds = 0, time_shift;
	zbx_value_type_t		arg1_type = ZBX_VALUE_NVALUES;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (1 != num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_hist_range(ts->sec, parameters, 1, &arg1, &arg1_type, &time_shift))
	{
		*error = zbx_strdup(*error, "invalid parameter");
		goto out;
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NONE:
			*error = zbx_strdup(*error, "the first argument is not specified");
			goto out;
		case ZBX_VALUE_NVALUES:
			*error = zbx_strdup(*error, "the first argument cannot be number of value");
			goto out;
		default:
			*error = zbx_strdup(*error, "invalid type of first argument");
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
	}

	if (0 >= arg1)
	{
		*error = zbx_strdup(*error, "the first argument must be greater than 0");
		goto out;
	}

	ts_end.sec -= time_shift;

	if (SUCCEED == zbx_vc_get_values(item->host.hostid, item->itemid, item->value_type, &values, seconds, 0, &ts_end))
	{
		if (0 < values.values_num)
		{
			zbx_history_value2variant(&values.values[values.values_num - 1].value, item->value_type, value);
			ret = SUCCEED;
		}
		else
		{
			*error = zbx_strdup(*error, "not enough data");
			goto out;
		}
	}
	else
		*error = zbx_strdup(*error, "cannot get values from value cache");
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static void	history_to_dbl_vector(const zbx_history_record_t *v, int n, unsigned char value_type,
		zbx_vector_dbl_t *values)
{
	int	i;

	zbx_vector_dbl_reserve(values, (size_t)n);

	if (ITEM_VALUE_TYPE_FLOAT == value_type)
	{
		for (i = 0; i < n; i++)
			zbx_vector_dbl_append(values, v[i].value.dbl);
	}
	else
	{
		for (i = 0; i < n; i++)
			zbx_vector_dbl_append(values, (double)v[i].value.ui64);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_statistical_func                                        *
 *                                                                            *
 * Purpose: common operations for aggregate function calculation              *
 *                                                                            *
 * Parameters: value      - [OUT] result                                      *
 *             item       - [IN] item (performance metric)                    *
 *             parameters - [IN] number of seconds/values and time shift      *
 *                               (optional)                                   *
 *             ts         - [IN] time shift                                   *
 *             stat_func  - [IN] pointer to aggregate function to be called   *
 *             min_values - [IN] minimum data values required                 *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_statistical_func(zbx_variant_t *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, zbx_statistical_func_t stat_func, int min_values, char **error)
{
	int				ret = FAIL;
	zbx_vector_history_record_t	values;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_history_record_vector_create(&values);

	if (SUCCEED != validate_params_and_get_data(item, parameters, ts, &values, error))
		goto out;

	if (min_values <= values.values_num)
	{
		zbx_vector_dbl_t	values_dbl;
		double			result;

		zbx_vector_dbl_create(&values_dbl);

		history_to_dbl_vector(values.values, values.values_num, item->value_type, &values_dbl);

		if (SUCCEED == (ret = stat_func(&values_dbl, &result, error)))
			zbx_variant_set_dbl(value, result);

		zbx_vector_dbl_destroy(&values_dbl);
	}
	else
		*error = zbx_strdup(*error, "not enough data");
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_function                                                *
 *                                                                            *
 * Purpose: evaluate function                                                 *
 *                                                                            *
 * Parameters: item      - item to calculate function for                     *
 *             function  - function (for example, 'max')                      *
 *             parameter - parameter of the function                          *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, value contains its value   *
 *               FAIL - evaluation failed                                     *
 *                                                                            *
 ******************************************************************************/
int	evaluate_function2(zbx_variant_t *value, DC_ITEM *item, const char *function, const char *parameter,
		const zbx_timespec_t *ts, char **error)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() function:'%s(/%s/%s,%s)' ts:'%s\'", __func__,
			function, item->host.host, item->key_orig, parameter, zbx_timespec_str(ts));

	if (0 == strcmp(function, "last"))
	{
		ret = evaluate_LAST(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "min"))
	{
		ret = evaluate_MIN(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "max"))
	{
		ret = evaluate_MAX(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "avg"))
	{
		ret = evaluate_AVG(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "sum"))
	{
		ret = evaluate_SUM(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "percentile"))
	{
		ret = evaluate_PERCENTILE(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "count"))
	{
		ret = evaluate_COUNT(value, item, parameter, ts, ZBX_MAX_UINT31_1, COUNT_ALL, error);
	}
	else if (0 == strcmp(function, "countunique"))
	{
		ret = evaluate_COUNT(value, item, parameter, ts, ZBX_MAX_UINT31_1, COUNT_UNIQUE, error);
	}
	else if (0 == strcmp(function, "nodata"))
	{
		ret = evaluate_NODATA(value, item, parameter, error);
	}
	else if (0 == strcmp(function, "change"))
	{
		ret = evaluate_CHANGE(value, item, ts, error);
	}
	else if (0 == strcmp(function, "find"))
	{
		ret = evaluate_COUNT(value, item, parameter, ts, 1, COUNT_ALL, error);
	}
	else if (0 == strcmp(function, "fuzzytime"))
	{
		ret = evaluate_FUZZYTIME(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "logeventid"))
	{
		ret = evaluate_LOGEVENTID(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "logseverity"))
	{
		ret = evaluate_LOGSEVERITY(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "logsource"))
	{
		ret = evaluate_LOGSOURCE(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "bitand"))
	{
		ret = evaluate_BITAND(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "forecast"))
	{
		ret = evaluate_FORECAST(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "timeleft"))
	{
		ret = evaluate_TIMELEFT(value, item, parameter, ts, error);
	}
	else if (0 == strncmp(function, "trend", 5))
	{
		ret = evaluate_TREND(value, item, function + 5, parameter, ts, error);
	}
	else if (0 == strcmp(function, "first"))
	{
		ret = evaluate_FIRST(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "kurtosis"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_kurtosis, 1, error);
	}
	else if (0 == strcmp(function, "mad"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_mad, 1, error);
	}
	else if (0 == strcmp(function, "skewness"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_skewness, 1, error);
	}
	else if (0 == strcmp(function, "stddevpop"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_stddevpop, 1, error);
	}
	else if (0 == strcmp(function, "stddevsamp"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_stddevsamp, 2, error);
	}
	else if (0 == strcmp(function, "sumofsquares"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_sumofsquares, 1, error);
	}
	else if (0 == strcmp(function, "varpop"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_varpop, 1, error);
	}
	else if (0 == strcmp(function, "varsamp"))
	{
		ret = evaluate_statistical_func(value, item, parameter, ts, zbx_eval_calc_varsamp, 2, error);
	}
	else
	{
		*error = zbx_strdup(*error, "function is not supported");
		ret = FAIL;
	}

	DEBUG_ITEM(item->itemid,"Func eval result is %d",ret);
	
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:'%s' of type:'%s'", __func__, zbx_result_string(ret),
			zbx_variant_value_desc(value), zbx_variant_type_desc(value));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_is_trigger_function                                          *
 *                                                                            *
 * Purpose: check if the specified function is a trigger function             *
 *                                                                            *
 * Parameters: name - [IN] the function name to check                         *
 *             len  - [IN] the length of function name                        *
 *                                                                            *
 * Return value: SUCCEED - the function is a trigger function                 *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
int	zbx_is_trigger_function(const char *name, size_t len)
{
	char	*functions[] = {"last", "min", "max", "avg", "sum", "percentile", "count", "countunique", "nodata",
			"change", "find", "fuzzytime", "logeventid", "logseverity", "logsource", "bitand", "forecast",
			"timeleft", "trendavg", "trendcount", "trendmax", "trendmin", "trendsum", "abs", "cbrt",
			"ceil", "exp", "floor", "log", "log10", "power", "round", "rand", "signum", "sqrt", "truncate",
			"acos", "asin", "atan", "cos", "cosh", "cot", "sin", "sinh", "tan", "degrees", "radians", "mod",
			"pi", "e", "expm1", "atan2", "first", "kurtosis", "mad", "skewness", "stddevpop", "stddevsamp",
			"sumofsquares", "varpop", "varsamp", "ascii", "bitlength", "char", "concat", "insert", "lcase",
			"left", "ltrim", "bytelength", "repeat", "replace", "right", "rtrim", "mid", "trim", "between",
			"in", "bitor", "bitxor", "bitnot", "bitlshift", "bitrshift", NULL};
	char	**ptr;

	for (ptr = functions; NULL != *ptr; ptr++)
	{
		size_t	compare_len;

		compare_len = strlen(*ptr);
		if (compare_len == len && 0 == memcmp(*ptr, name, len))
			return SUCCEED;
	}

	return FAIL;
}
