/*
** Copyright Glaber 2018-2023
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
#include "zbxeval.h"
#include "zbxcacheconfig.h"
#include "../zbxserver/evalfunc.h"
#include "zbxdbwrap.h"

static void	evaluate_function_by_id(zbx_uint64_t functionid, char **value, zbx_trigger_func_t eval_func_cb)
{
	DC_ITEM		item;
	DC_FUNCTION	function;
	int		err_func, err_item;

	DCconfig_get_functions_by_functionids(&function, &functionid, &err_func, 1);

	if (SUCCEED == err_func)
	{
		DCconfig_get_items_by_itemids(&item, &function.itemid, &err_item, 1);

		if (SUCCEED == err_item)
		{
			char			*error = NULL, *parameter = NULL;
			zbx_variant_t		var;
			zbx_timespec_t		ts;
			DC_EVALUATE_ITEM	evaluate_item;

			parameter = zbx_dc_expand_user_macros_in_func_params(function.parameter, item.host.hostid);
			zbx_timespec(&ts);

			evaluate_item.itemid = item.itemid;
			evaluate_item.value_type = item.value_type;
			evaluate_item.proxy_hostid = item.host.proxy_hostid;
			evaluate_item.host = item.host.host;
			evaluate_item.key_orig = item.key_orig;

			if (SUCCEED == eval_func_cb(&var, &evaluate_item, function.function, parameter, &ts, &error) &&
					ZBX_VARIANT_NONE != var.type)
			{
				*value = zbx_strdup(NULL, zbx_variant_value_desc(&var));
				zbx_variant_clear(&var);
			}
			else
				zbx_free(error);

			zbx_free(parameter);
			DCconfig_clean_items(&item, &err_item, 1);
		}

		DCconfig_clean_functions(&function, &err_func, 1);
	}

	if (NULL == *value)
		*value = zbx_strdup(NULL, "*UNKNOWN*");
}


//db_trigger_get_expression
void	glb_expression_get_by_trigger(const zbx_eval_context_t *ctx, char **expression)
{
	int			i;
	zbx_eval_context_t	local_ctx;

	zbx_eval_copy(&local_ctx, ctx, ctx->expression);
	local_ctx.rules |= ZBX_EVAL_COMPOSE_MASK_ERROR;

	for (i = 0; i < local_ctx.stack.values_num; i++)
	{
		zbx_eval_token_t	*token = &local_ctx.stack.values[i];
		zbx_uint64_t		functionid;
		DC_FUNCTION		function;
		DC_ITEM			item;
		int			err_func, err_item;

		if (ZBX_EVAL_TOKEN_FUNCTIONID != token->type)
		{
			/* reset cached token values to get the original expression */
			zbx_variant_clear(&token->value);
			continue;
		}

		switch (token->value.type)
		{
			case ZBX_VARIANT_UI64:
				functionid = token->value.data.ui64;
				break;
			case ZBX_VARIANT_NONE:
				if (SUCCEED != zbx_is_uint64_n(local_ctx.expression + token->loc.l + 1,
						token->loc.r - token->loc.l - 1, &functionid))
				{
					continue;
				}
				break;
			default:
				continue;
		}

		DCconfig_get_functions_by_functionids(&function, &functionid, &err_func, 1);

		if (SUCCEED == err_func)
		{
			DCconfig_get_items_by_itemids(&item, &function.itemid, &err_item, 1);

			if (SUCCEED == err_item)
			{
				char	*func = NULL;
				size_t	func_alloc = 0, func_offset = 0;

				zbx_snprintf_alloc(&func, &func_alloc, &func_offset, "%s(/%s/%s",
						function.function, item.host.host, item.key_orig);

				if ('\0' != *function.parameter)
					zbx_snprintf_alloc(&func, &func_alloc, &func_offset, ",%s", function.parameter);

				zbx_chrcpy_alloc(&func, &func_alloc, &func_offset,')');

				zbx_variant_clear(&token->value);
				zbx_variant_set_str(&token->value, func);
				DCconfig_clean_items(&item, &err_item, 1);
			}
			else
			{
				zbx_variant_clear(&token->value);
				zbx_variant_set_error(&token->value, zbx_dsprintf(NULL, "item id:" ZBX_FS_UI64
						" deleted", function.itemid));
			}

			DCconfig_clean_functions(&function, &err_func, 1);
		}
		else
		{
			zbx_variant_clear(&token->value);
			zbx_variant_set_error(&token->value, zbx_dsprintf(NULL, "function id:" ZBX_FS_UI64 " deleted",
					functionid));
		}
	}

	zbx_eval_compose_expression(&local_ctx, expression);

	zbx_eval_clear(&local_ctx);
}

int glb_expression_explain_by_trigger(const zbx_eval_context_t *ctx, char **expression)
{
	int			i;
	zbx_eval_context_t	local_ctx;

	zbx_eval_copy(&local_ctx, ctx, ctx->expression);
	local_ctx.rules |= ZBX_EVAL_COMPOSE_MASK_ERROR;

	for (i = 0; i < local_ctx.stack.values_num; i++)
	{
		zbx_eval_token_t	*token = &local_ctx.stack.values[i];
		char			*value = NULL;
		zbx_uint64_t		functionid;

		if (ZBX_EVAL_TOKEN_FUNCTIONID != token->type)
			continue;

		switch (token->value.type)
		{
			case ZBX_VARIANT_UI64:
				functionid = token->value.data.ui64;
				break;
			case ZBX_VARIANT_NONE:
				if (SUCCEED != zbx_is_uint64_n(local_ctx.expression + token->loc.l + 1,
						token->loc.r - token->loc.l - 1, &functionid))
				{
					continue;
				}
				break;
			default:
				continue;
		}

		zbx_variant_clear(&token->value);
		evaluate_function_by_id(functionid, &value, evaluate_function);
		zbx_variant_set_str(&token->value, value);
	}

	zbx_eval_compose_expression(&local_ctx, expression);

	zbx_eval_clear(&local_ctx);
}
