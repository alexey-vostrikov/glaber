/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "trapper_preproc.h"
#include "zbxpreproc.h"
#include "preproc.h"
#include "trapper_auth.h"
#include "zbxcommshigh.h"
#include "../../libs/zbxpreproc/pp_execute.h"
#include "../../libs/zbxpreproc/pp_error.h"

#define ZBX_STATE_NOT_SUPPORTED	1
extern int CONFIG_DOUBLE_PRECISION;

/******************************************************************************
 *                                                                            *
 * Purpose: parses preprocessing test request                                 *
 *                                                                            *
 * Parameters: jp           - [IN] the request                                *
 *             values       - [OUT] the values to test optional               *
 *                                  (history + current)                       *
 *             ts           - [OUT] value timestamps                          *
 *             values_num   - [OUT] the number of values                      *
 *             value_type   - [OUT] the value type                            *
 *             steps        - [OUT] the preprocessing steps                   *
 *             single     - [OUT] is preproc step single                      *
 *             state        - [OUT] the item state                            *
 *             bypass_first - [OUT] the flag to bypass first step             *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: SUCCEED - the request was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	trapper_parse_preproc_test(const struct zbx_json_parse *jp, char **values, zbx_timespec_t *ts,
		int *values_num, unsigned char *value_type, zbx_pp_step_t **steps, int *steps_num, int *single,
		int *state,
		int *bypass_first, char **error)
{
	char			buffer[MAX_STRING_LEN], *step_params = NULL, *error_handler_params = NULL;
	const char		*ptr;
	zbx_user_t		user;
	int			ret = FAIL;
	struct zbx_json_parse	jp_data, jp_history, jp_steps, jp_step;
	size_t			size;
	zbx_timespec_t		ts_now;
	
	zbx_user_init(&user);

	if (FAIL == zbx_get_user_from_json(jp, &user, NULL) || USER_TYPE_ZABBIX_ADMIN > user.type)
	{
		*error = zbx_strdup(NULL, "Permission denied.");
		goto out;
	}

	if (FAIL == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DATA, &jp_data))
	{
		*error = zbx_strdup(NULL, "Missing data field.");
		goto out;
	}

	if (FAIL == zbx_json_value_by_name(&jp_data, ZBX_PROTO_TAG_VALUE_TYPE, buffer, sizeof(buffer), NULL))
	{
		*error = zbx_strdup(NULL, "Missing value type field.");
		goto out;
	}
	*value_type = (unsigned char)atoi(buffer);

	if (FAIL == zbx_json_value_by_name(&jp_data, ZBX_PROTO_TAG_SINGLE, buffer, sizeof(buffer), NULL))
		*single = 0;
	else
		*single = (0 == strcmp(buffer, "true") ? 1 : 0);

	*state = 0;
	if (SUCCEED == zbx_json_value_by_name(&jp_data, ZBX_PROTO_TAG_STATE, buffer, sizeof(buffer), NULL))
		*state = atoi(buffer);

	zbx_timespec(&ts_now);
	if (SUCCEED == zbx_json_brackets_by_name(&jp_data, ZBX_PROTO_TAG_HISTORY, &jp_history))
	{
		size = 0;
		if (FAIL == zbx_json_value_by_name_dyn(&jp_history, ZBX_PROTO_TAG_VALUE, values, &size, NULL))
		{
			*error = zbx_strdup(NULL, "Missing history value field.");
			goto out;
		}
		(*values_num)++;

		if (FAIL == zbx_json_value_by_name(&jp_history, ZBX_PROTO_TAG_TIMESTAMP, buffer, sizeof(buffer), NULL))
		{
			*error = zbx_strdup(NULL, "Missing history timestamp field.");
			goto out;
		}

		if (0 != strncmp(buffer, "now", ZBX_CONST_STRLEN("now")))
		{
			*error = zbx_dsprintf(NULL, "invalid history value timestamp: %s", buffer);
			goto out;
		}

		ts[0] = ts_now;
		ptr = buffer + ZBX_CONST_STRLEN("now");

		if ('\0' != *ptr)
		{
			int	delay;

			if ('-' != *ptr || FAIL == zbx_is_time_suffix(ptr + 1, &delay, (int)strlen(ptr + 1)))
			{
				*error = zbx_dsprintf(NULL, "invalid history value timestamp: %s", buffer);
				goto out;
			}

			ts[0].sec -= delay;
		}
	}

	size = 0;
	if (FAIL == zbx_json_value_by_name_dyn(&jp_data, ZBX_PROTO_TAG_VALUE, &values[*values_num], &size, NULL))
	{
		*error = zbx_strdup(NULL, "Missing value field.");
		goto out;
	}
	ts[(*values_num)++] = ts_now;

	if (FAIL == zbx_json_brackets_by_name(&jp_data, ZBX_PROTO_TAG_STEPS, &jp_steps))
	{
		*error = zbx_strdup(NULL, "Missing preprocessing steps field.");
		goto out;
	}

	*bypass_first = 0;
	*steps_num = zbx_json_count(&jp_steps);
	*steps = zbx_malloc(NULL, sizeof(zbx_pp_step_t) * (*steps_num));

	int i = 0;

	for (ptr = NULL; NULL != (ptr = zbx_json_next(&jp_steps, ptr));)
	{
		zbx_pp_step_t	*step = (void *)*steps + i *sizeof(zbx_pp_step_t);

		int		step_type, error_handler;

		if (FAIL == zbx_json_brackets_open(ptr, &jp_step))
		{
			*error = zbx_strdup(NULL, "Cannot parse preprocessing step.");
			goto out;
		}

		if (FAIL == zbx_json_value_by_name(&jp_step, ZBX_PROTO_TAG_TYPE, buffer, sizeof(buffer), NULL))
		{
			*error = zbx_strdup(NULL, "Missing preprocessing step type field.");
			goto out;
		}
		step_type = atoi(buffer);

		if (FAIL == zbx_json_value_by_name(&jp_step, ZBX_PROTO_TAG_ERROR_HANDLER, buffer, sizeof(buffer), NULL))
		{
			*error = zbx_strdup(NULL, "Missing preprocessing step type error handler field.");
			goto out;
		}
		error_handler = atoi(buffer);

		size = 0;
		if (FAIL == zbx_json_value_by_name_dyn(&jp_step, ZBX_PROTO_TAG_PARAMS, &step_params, &size, NULL))
		{
			*error = zbx_strdup(NULL, "Missing preprocessing step type params field.");
			goto out;
		}

		size = 0;
		if (FAIL == zbx_json_value_by_name_dyn(&jp_step, ZBX_PROTO_TAG_ERROR_HANDLER_PARAMS,
				&error_handler_params, &size, NULL))
		{
			*error = zbx_strdup(NULL, "Missing preprocessing step type error handler params field.");
			goto out;
		}
		
		if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED != step_type || ZBX_STATE_NOT_SUPPORTED == *state)
		{
			step->type = step_type;
			step->params = step_params;
			step->error_handler = error_handler;
			step->error_handler_params = error_handler_params;
		}
		else
		{
			zbx_free(step_params);
			zbx_free(error_handler_params);
			*bypass_first = 1;
		}

		step_params = NULL;
		error_handler_params = NULL;
	
		i++;
	}

	ret = SUCCEED;
out:
	if (FAIL == ret)
	{
		for ( i =0 ; i< *steps_num; i++) {
			zbx_free(steps[i]->params);
			zbx_free(steps[i]->error_handler_params);
		}
		zbx_free(values[0]);
		zbx_free(values[1]);
	}

	zbx_free(step_params);
	zbx_free(error_handler_params);

	zbx_user_free(&user);

	return ret;
}

int	zbx_preprocessor_test(unsigned char value_type, const char *value, const zbx_timespec_t *ts,
		unsigned char state, zbx_pp_step_t *steps, int steps_num, zbx_pp_result_t **results, int *results_num)
{
	int		ret = FAIL;

	zbx_pp_item_preproc_t preproc_conf = {
				.history = NULL,
				.history_num = 0,
				.steps = steps,
				.steps_num = steps_num };

	zbx_pp_context_t ctx = {0};
	metric_t test_metric = {0};

	test_metric.ts = *ts;
	test_metric.itemid = 111;
	
	zbx_variant_set_str(&test_metric.value, (char *)value);
	
	zbx_variant_t value_out = {0};
	int i;

	pp_execute(&ctx, &preproc_conf, NULL, &test_metric, &value_out, results, results_num);

	zbx_variant_clear(&value_out);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: executes preprocessing test request                               *
 *                                                                            *
 * Parameters: jp    - [IN] the request                                       *
 *             json  - [OUT] the output json                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - the request was executed successfully              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function will fail if the request format is not valid or    *
 *           there was connection (to preprocessing manager) error.           *
 *           Any errors in the preprocessing itself are reported in output    *
 *           json and success is returned.                                    *
 *                                                                            *
 ******************************************************************************/
int	trapper_preproc_test_run(const struct zbx_json_parse *jp, struct zbx_json *json, char **error)
{
	char				*values[2] = {NULL, NULL}, *preproc_error = NULL;
	int				i, single, state, bypass_first, ret = FAIL, values_num = 0, first_step_type;
	unsigned char			value_type;
	zbx_pp_step_t	*steps = NULL;
	int steps_num = 0 ;
	zbx_timespec_t			ts[2];
	zbx_pp_result_t			*results = NULL;
	int 					results_num;

	if (FAIL == trapper_parse_preproc_test(jp, values, ts, &values_num, &value_type, &steps, &steps_num, 
				&single, &state, &bypass_first, error))
	{
		goto out;
	}

	first_step_type = 0;
	if (0 != steps_num)
		first_step_type  = steps[0].type;

	if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED != first_step_type && ZBX_STATE_NOT_SUPPORTED == state)
	{
		preproc_error = zbx_strdup(NULL, "This item is not supported. Please, add a preprocessing step"
				" \"Check for not supported value\" to process it.");
		zbx_json_addstring(json, ZBX_PROTO_TAG_RESPONSE, "success", ZBX_JSON_TYPE_STRING);
		zbx_json_addobject(json, ZBX_PROTO_TAG_DATA);
		zbx_json_addarray(json, ZBX_PROTO_TAG_STEPS);
		goto err;
	}

	for (i = 0; i < values_num; i++)
	{

		if (0 == steps_num)
		{
			results = (zbx_pp_result_t *)zbx_malloc(NULL, sizeof(zbx_pp_result_t));
			results->action = ZBX_PREPROC_FAIL_DEFAULT;
			zbx_variant_set_str(&results->value, zbx_strdup(NULL, values[i]));
			zbx_variant_set_none(&results->value_raw);
			results_num = 1;
		}

		zbx_preprocessor_test(value_type, values[i], &ts[i], state, steps, steps_num, &results, &results_num);

		if ( 0 < results_num && ZBX_VARIANT_ERR == results[results_num - 1].value.type)
		{
			preproc_error = zbx_strdup(NULL, results[results_num - 1].value.data.err);
			break;
		}
		
		if (0 == single && 0 < results_num)
		{
			zbx_pp_result_t * result = &results[results_num - 1];
			
			if (ZBX_VARIANT_NONE != result->value.type && FAIL == zbx_variant_to_value_type(&result->value,
					value_type, CONFIG_DOUBLE_PRECISION, &preproc_error))
			{
				break;
			}
		}
	}

	zbx_json_addstring(json, ZBX_PROTO_TAG_RESPONSE, "success", ZBX_JSON_TYPE_STRING);
	zbx_json_addobject(json, ZBX_PROTO_TAG_DATA);

	if (i + 1 < values_num)
		zbx_json_addstring(json, ZBX_PROTO_TAG_PREVIOUS, "true", ZBX_JSON_TYPE_INT);

	zbx_json_addarray(json, ZBX_PROTO_TAG_STEPS);

	if (1 == bypass_first)
	{
		zbx_json_addobject(json, NULL);
		zbx_json_addstring(json, ZBX_PROTO_TAG_RESULT, ZBX_PROTO_TAG_VALUE, ZBX_JSON_TYPE_STRING);
		zbx_json_close(json);
	}
	
	if (0 != steps_num)
	{
		for (i = 0; i < results_num; i++)
		{
			zbx_pp_result_t * result = &results[i];

			zbx_json_addobject(json, NULL);

			if (ZBX_PREPROC_FAIL_DEFAULT != result->action)
			{
				zbx_json_addint64(json, ZBX_PROTO_TAG_ACTION, result->action);
				if (ZBX_VARIANT_ERR == result->value_raw.type)
					zbx_json_addstring(json, ZBX_PROTO_TAG_ERROR, result->value_raw.data.err,
							ZBX_JSON_TYPE_STRING);

				if (ZBX_PREPROC_FAIL_SET_ERROR == result->action)
				{
					zbx_json_addstring(json, ZBX_PROTO_TAG_FAILED, preproc_error,
							ZBX_JSON_TYPE_STRING);
				}
			}

			if (ZBX_VARIANT_ERR == result->value.type)
			{
				if (ZBX_PREPROC_FAIL_DEFAULT == result->action)
				{
					zbx_json_addstring(json, ZBX_PROTO_TAG_ERROR, result->value.data.err,
						ZBX_JSON_TYPE_STRING);
				}
			}
			else if (ZBX_VARIANT_NONE != result->value.type)
			{
				zbx_json_addstring(json, ZBX_PROTO_TAG_RESULT,
						zbx_variant_value_desc(&result->value), ZBX_JSON_TYPE_STRING);
			}
			else {
				zbx_json_addstring(json, ZBX_PROTO_TAG_RESULT, NULL, ZBX_JSON_TYPE_NULL);
			}

			zbx_json_close(json);
		}
	}
err:
	zbx_json_close(json);
	
	if (NULL == preproc_error)
	{
		zbx_pp_result_t * result = &results[results_num - 1];

		if (ZBX_VARIANT_NONE != result->value.type)
		{
			zbx_json_addstring(json, ZBX_PROTO_TAG_RESULT, zbx_variant_value_desc(&result->value),
					ZBX_JSON_TYPE_STRING);
		}
		else {
			zbx_json_addstring(json, ZBX_PROTO_TAG_RESULT, NULL, ZBX_JSON_TYPE_NULL);
		}
	}
	else {
		zbx_json_addstring(json, ZBX_PROTO_TAG_ERROR, preproc_error, ZBX_JSON_TYPE_STRING);
	}

	ret = SUCCEED;
out:
	zbx_free(preproc_error);

	for (i = 0; i < values_num; i++) 
		zbx_free(values[i]);

	pp_free_results(results, results_num);
	
	for(i=0; i< steps_num; i++) {
		zbx_free(steps[i].params);
		zbx_free(steps[i].error_handler_params);
	}
	
	zbx_free(steps);
	
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: processes preprocessing test request                              *
 *                                                                            *
 * Parameters: sock           - [IN] the request source socket (frontend)     *
 *             jp             - [IN] the request                              *
 *             config_timeout - [IN]                                          *
 *                                                                            *
 * Return value: SUCCEED - the request was processed successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function will send proper (success/fail) response to the    *
 *           request socket.                                                  *
 *           Preprocessing failure (error returned by a preprocessing step)   *
 *           is counted as successful test and will return success response.  *
 *                                                                            *
 ******************************************************************************/
int	zbx_trapper_preproc_test(zbx_socket_t *sock, const struct zbx_json_parse *jp, int config_timeout)
{
	char		*error = NULL;
	int		ret;
	struct zbx_json	json;

	zbx_json_init(&json, 1024);

	if (SUCCEED == (ret = trapper_preproc_test_run(jp, &json, &error)))
	{
		zbx_tcp_send_bytes_to(sock, json.buffer, json.buffer_size, config_timeout);
	}
	else
	{
		zbx_send_response(sock, ret, error, config_timeout);
		zbx_free(error);
	}

	zbx_json_free(&json);

	return ret;
}
