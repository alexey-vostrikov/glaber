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

#include "preproc_worker.h"

#include "../db_lengths.h"
#include "zbxself.h"
#include "log.h"
#include "item_preproc.h"
#include "preproc_history.h"
#include "preproc_snmp.h"
#include "zbxtime.h"

#define ZBX_PREPROC_VALUE_PREVIEW_LEN		100
/******************************************************************************
 *                                                                            *
 * Purpose: formats value in text format                                      *
 *                                                                            *
 * Parameters: value     - [IN] the value to format                           *
 *             value_str - [OUT] the formatted value                          *
 *                                                                            *
 * Comments: Control characters are replaced with '.' and truncated if it's   *
 *           larger than ZBX_PREPROC_VALUE_PREVIEW_LEN characters.            *
 *                                                                            *
 ******************************************************************************/
static void	worker_format_value(const zbx_variant_t *value, char **value_str)
{
	const char	*value_desc;
	size_t		i, len;

	value_desc = zbx_variant_value_desc(value);

	if (ZBX_PREPROC_VALUE_PREVIEW_LEN < zbx_strlen_utf8(value_desc))
	{
		/* truncate value and append '...' */
		len = zbx_strlen_utf8_nchars(value_desc, ZBX_PREPROC_VALUE_PREVIEW_LEN - ZBX_CONST_STRLEN("..."));
		*value_str = zbx_malloc(NULL, len + ZBX_CONST_STRLEN("...") + 1);
		memcpy(*value_str, value_desc, len);
		memcpy(*value_str + len, "...", ZBX_CONST_STRLEN("...") + 1);
	}
	else
	{
		*value_str = zbx_malloc(NULL, (len = strlen(value_desc)) + 1);
		memcpy(*value_str, value_desc, len + 1);
	}

	/* replace control characters */
	for (i = 0; i < len; i++)
	{
		if (0 != iscntrl((*value_str)[i]))
			(*value_str)[i] = '.';
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: formats one preprocessing step result                             *
 *                                                                            *
 * Parameters: step   - [IN] the preprocessing step number                    *
 *             result - [IN] the preprocessing step result                    *
 *             error  - [IN] the preprocessing step error (can be NULL)       *
 *             out    - [OUT] the formatted string                            *
 *                                                                            *
 ******************************************************************************/
static void	worker_format_result(int step, const zbx_preproc_result_t *result, const char *error, char **out)
{
	char	*actions[] = {"", " (discard value)", " (set value)", " (set error)"};

	if (NULL == error)
	{
		char	*value_str;

		worker_format_value(&result->value, &value_str);
		*out = zbx_dsprintf(NULL, "%d. Result%s: %s\n", step, actions[result->action], value_str);
		zbx_free(value_str);
	}
	else
	{
		*out = zbx_dsprintf(NULL, "%d. Failed%s: %s\n", step, actions[result->action], error);
		zbx_rtrim(*out, ZBX_WHITESPACE);
	}
}

/* mock field to estimate how much data can be stored in characters, bytes or both, */
/* depending on database backend                                                    */

typedef struct
{
	int	bytes_num;
	int	chars_num;
}
zbx_db_mock_field_t;

/******************************************************************************
 *                                                                            *
 * Purpose: initializes mock field                                            *
 *                                                                            *
 * Parameters: field      - [OUT] the field data                              *
 *             field_type - [IN] the field type in database schema            *
 *             field_len  - [IN] the field size in database schema            *
 *                                                                            *
 ******************************************************************************/
static void	zbx_db_mock_field_init(zbx_db_mock_field_t *field, int field_type, int field_len)
{
	switch (field_type)
	{
		case ZBX_TYPE_CHAR:
#if defined(HAVE_ORACLE)
			field->chars_num = field_len;
			field->bytes_num = 4000;
#else
			field->chars_num = field_len;
			field->bytes_num = -1;
#endif
			return;
	}

	THIS_SHOULD_NEVER_HAPPEN;

	field->chars_num = 0;
	field->bytes_num = 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: 'appends' text to the field, if successful the character/byte     *
 *           limits are updated                                               *
 *                                                                            *
 * Parameters: field - [IN/OUT] the mock field                                *
 *             text  - [IN] the text to append                                *
 *                                                                            *
 * Return value: SUCCEED - the field had enough space to append the text      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	zbx_db_mock_field_append(zbx_db_mock_field_t *field, const char *text)
{
	int	bytes_num, chars_num;

	if (-1 != field->bytes_num)
	{
		bytes_num = strlen(text);
		if (bytes_num > field->bytes_num)
			return FAIL;
	}
	else
		bytes_num = 0;

	if (-1 != field->chars_num)
	{
		chars_num = zbx_strlen_utf8(text);
		if (chars_num > field->chars_num)
			return FAIL;
	}
	else
		chars_num = 0;

	field->bytes_num -= bytes_num;
	field->chars_num -= chars_num;

	return SUCCEED;
}



/******************************************************************************
 *                                                                            *
 * Purpose: formats preprocessing error message                               *
 *                                                                            *
 * Parameters: value        - [IN] the input value                            *
 *             results      - [IN] the preprocessing step results             *
 *             results_num  - [IN] the number of executed steps               *
 *             errmsg       - [IN] the error message of last executed step    *
 *             error        - [OUT] the formatted error message               *
 *                                                                            *
 ******************************************************************************/
void	worker_format_error(const zbx_variant_t *value, zbx_preproc_result_t *results, int results_num,
		const char *errmsg, char **error)
{
	char			*value_str, *err_step;
	int			i;
	size_t			error_alloc = 512, error_offset = 0;
	zbx_vector_str_t	results_str;
	zbx_db_mock_field_t	field;

	zbx_vector_str_create(&results_str);

	/* add header to error message */
	*error = zbx_malloc(NULL, error_alloc);
	worker_format_value(value, &value_str);
	zbx_snprintf_alloc(error, &error_alloc, &error_offset, "Preprocessing failed for: %s\n", value_str);
	zbx_free(value_str);

	zbx_db_mock_field_init(&field, ZBX_TYPE_CHAR, ZBX_ITEM_ERROR_LEN);

	zbx_db_mock_field_append(&field, *error);
	zbx_db_mock_field_append(&field, "...\n");

	/* format the last (failed) step */
	worker_format_result(results_num, &results[results_num - 1], errmsg, &err_step);
	zbx_vector_str_append(&results_str, err_step);

	if (SUCCEED == zbx_db_mock_field_append(&field, err_step))
	{
		/* format the first steps */
		for (i = results_num - 2; i >= 0; i--)
		{
			worker_format_result(i + 1, &results[i], NULL, &err_step);

			if (SUCCEED != zbx_db_mock_field_append(&field, err_step))
			{
				zbx_free(err_step);
				break;
			}

			zbx_vector_str_append(&results_str, err_step);
		}
	}

	/* add steps to error message */

	if (results_str.values_num < results_num)
		zbx_strcpy_alloc(error, &error_alloc, &error_offset, "...\n");

	for (i = results_str.values_num - 1; i >= 0; i--)
		zbx_strcpy_alloc(error, &error_alloc, &error_offset, results_str.values[i]);

	/* truncate formatted error if necessary */
	if (ZBX_ITEM_ERROR_LEN < zbx_strlen_utf8(*error))
	{
		char	*ptr;

		ptr = (*error) + zbx_db_strlen_n(*error, ZBX_ITEM_ERROR_LEN - 3);
		for (i = 0; i < 3; i++)
			*ptr++ = '.';
		*ptr = '\0';
	}

	zbx_vector_str_clear_ext(&results_str, zbx_str_free);
	zbx_vector_str_destroy(&results_str);
}

/******************************************************************************
 *                                                                            *
 * Purpose: execute preprocessing steps                                       *
 *                                                                            *
 * Parameters: cache         - [IN/OUT] the preprocessing cache               *
 *             value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             steps         - [IN] the preprocessing steps to execute        *
 *             steps_num     - [IN] the number of preprocessing steps         *
 *             history_in    - [IN] the preprocessing history                 *
 *             history_out   - [OUT] the new preprocessing history            *
 *             results       - [OUT] the preprocessing step results           *
 *             results_num   - [OUT] the number of step results               *
 *             error         - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the preprocessing steps finished successfully      *
 *               FAIL - otherwise, error contains the error message           *
 *                                                                            *
 ******************************************************************************/
int	worker_item_preproc_execute(u_int64_t itemid, zbx_preproc_cache_t *cache, unsigned char value_type,
		zbx_variant_t *value_in, zbx_variant_t *value_out, const zbx_timespec_t *ts,
		zbx_preproc_op_t *steps, int steps_num, zbx_vector_ptr_t *history_in, zbx_vector_ptr_t *history_out,
		zbx_preproc_result_t *results, int *results_num, u_int64_t flags, char **error)
{
	int		i, ret = SUCCEED;

	if (value_in != value_out)
	{
		if (0 == steps_num || NULL == cache || NULL == zbx_preproc_cache_get(cache, steps[0].type))
			zbx_variant_copy(value_out, value_in);
	}

	for (i = 0; i < steps_num; i++)
	{
		zbx_preproc_op_t	*op = &steps[i];
		zbx_variant_t		history_value;
		zbx_timespec_t		history_ts;
		zbx_preproc_cache_t	*pcache = (0 == i ? cache : NULL);

		zbx_preproc_history_pop_value(history_in, i, &history_value, &history_ts);

		if (FAIL == (ret = zbx_item_preproc(itemid, pcache, value_type, value_out, ts, op, &history_value, 
					&history_ts, flags,	error)))
		{
			results[i].action = op->error_handler;
			ret = zbx_item_preproc_handle_error(value_out, op, error);
			zbx_variant_clear(&history_value);
		}
		else
			results[i].action = ZBX_PREPROC_FAIL_DEFAULT;

		if (SUCCEED == ret)
		{
			if (NULL == *error)
			{
				/* result history is kept to report results of steps before failing step, */
				/* which means it can be omitted for the last step.                       */
				if (i != steps_num - 1)
					zbx_variant_copy(&results[i].value, value_out);
				else
					zbx_variant_set_none(&results[i].value);
			}
			else
			{
				/* preprocessing step successfully extracted error, set it */
				results[i].action = ZBX_PREPROC_FAIL_FORCE_ERROR;
				ret = FAIL;
			}
		}
	
		if (SUCCEED != ret)
		{
			break;
		}

		if (ZBX_VARIANT_NONE != history_value.type)
		{
			/* the value is byte copied to history_out vector and doesn't have to be cleared */
			zbx_preproc_history_add_value(history_out, i, &history_value, &history_ts);
		}

		if (ZBX_VARIANT_NONE == value_out->type)
			break;
	}
	
	*results_num = (i == steps_num ? i : i + 1);

	return ret;
}
/******************************************************************************
 *                                                                            *
 * Purpose: handle item value test preprocessing task                         *
 *                                                                            *
 * Parameters: socket  - [IN] IPC socket                                      *
 *             message - [IN] packed preprocessing task                       *
 *                                                                            *
 ******************************************************************************/

int	zbx_preprocessor_test( unsigned char value_type, char *value_str,  const zbx_timespec_t *ts,
						  zbx_vector_ptr_t *steps,  zbx_vector_ptr_t *results,  zbx_vector_ptr_t *history, char **error ) {

	zbx_variant_t		 value;
	int			i;
	zbx_vector_ptr_t	history_out;
	
	zbx_vector_ptr_create(&history_out);
	zbx_variant_set_str(&value, value_str);

	zbx_item_preproc_test(value_type, &value, ts, steps, history, &history_out,
				results, error);

	zbx_variant_clear(&value);
	zbx_vector_ptr_clear_ext(history, (zbx_clean_func_t)zbx_preproc_op_history_free);
	zbx_vector_ptr_destroy(history);
	
	*history = history_out; //byte-copy new history to the free structure of existing history
}