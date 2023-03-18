
#include "zbxcommon.h"
#include "zbxeval.h"
#include "../../zbxserver/evalfunc.h"
#include "zbxalgo.h"
#include "conf_calc_trigger.h"
#include "zbxcacheconfig.h"
#include "zbx_trigger_constants.h"
#include "../glb_macro/glb_macro.h"
#include "../glb_state/glb_state_problems.h"
#include "../glb_events_log/glb_events_log.h"
#include "../glb_state/glb_state_triggers.h"
#include "zbxdbwrap.h"

typedef enum
{
	TRIGGER_CACHE_EVAL_CTX,
	TRIGGER_CACHE_EVAL_CTX_R,
	TRIGGER_CACHE_EVAL_CTX_MACROS,
	TRIGGER_CACHE_EVAL_CTX_R_MACROS,
	TRIGGER_CACHE_HOSTIDS,
}
zbx_trigger_cache_state_t;

static trigger_cache_t	*calc_trigger_get_cache(calc_trigger_t *trigger, zbx_trigger_cache_state_t state);

/******************************************************************************
 * Purpose: expand macros in trigger expression/recovery expression           *
 ******************************************************************************/
static int	calc_trigger_expand_expression_macros(calc_trigger_t *trigger, zbx_eval_context_t *ctx)
{
 	int 			i;

 	zbx_dc_um_handle_t	*um_handle;
 	trigger_cache_t	*cache;

 	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_HOSTIDS)))
 		return FAIL;

 	um_handle = zbx_dc_open_user_macros();

 	(void)zbx_eval_expand_user_macros(ctx, cache->hostids.values, cache->hostids.values_num,
 			(zbx_macro_expand_func_t)zbx_dc_expand_user_macros, um_handle, NULL);

 	zbx_dc_close_user_macros(um_handle);

 	for (i = 0; i < ctx->stack.values_num; i++)
 	{
 		char			*value;
 		zbx_eval_token_t	*token = &ctx->stack.values[i];

 		switch (token->type)
 		{
 			case ZBX_EVAL_TOKEN_VAR_STR:
 				if (ZBX_VARIANT_NONE != token->value.type)
 				{
 					zbx_variant_convert(&token->value, ZBX_VARIANT_STR);
 					value = token->value.data.str;
 					zbx_variant_set_none(&token->value);
 					break;
 				}
 				value = zbx_substr_unquote(ctx->expression, token->loc.l, token->loc.r);
 				break;
 			case ZBX_EVAL_TOKEN_VAR_MACRO:
 				value = zbx_substr_unquote(ctx->expression, token->loc.l, token->loc.r);
 				break;
 			default:
 				continue;
 		}

 		if (SUCCEED == glb_macro_expand_by_trigger(trigger, &value,  NULL, 0))
 		{
 			zbx_variant_clear(&token->value);
 			zbx_variant_set_str(&token->value, value);
 		}
 		else
 			zbx_free(value);
 	}

 	return SUCCEED;
}


// static void	calc_trigger_get_functionids(calc_trigger_t *trigger, zbx_vector_uint64_t *functionids)
// {
// 	trigger_cache_t	*cache;

//  	if (NULL != (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX)))
//  		zbx_eval_get_functionids(&cache->eval_ctx, functionids);

//  	if (NULL != (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX_R)))
//  		zbx_eval_get_functionids(&cache->eval_ctx_r, functionids);

//  	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
//  	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
// }

static void	calc_trigger_get_all_functionids(calc_trigger_t *trigger, zbx_vector_uint64_t *functionids)
{
 	trigger_cache_t	*cache;

	if (NULL != (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX)))
		zbx_eval_get_functionids(&cache->eval_ctx, functionids);

	if (NULL != (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX_R)))
		zbx_eval_get_functionids(&cache->eval_ctx_r, functionids);

	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

static trigger_cache_t	*calc_trigger_get_cache(calc_trigger_t *trigger, zbx_trigger_cache_state_t state)
{
 	//trigger_cache_t	*cache;
 	char			*error = NULL;
 	zbx_uint32_t		flag = 1 << state;
 	zbx_vector_uint64_t	functionids;

 	if (0 != (trigger->eval_cache.init & flag))
 		return 0 != (trigger->eval_cache.done & flag) ? &trigger->eval_cache : NULL;

 	trigger->eval_cache.init |= flag;

 	switch (state)
 	{
 		case TRIGGER_CACHE_EVAL_CTX:
 			if ('\0' == *trigger->expression)
 				return NULL;

			zbx_eval_deserialize(&trigger->eval_cache.eval_ctx, trigger->expression, ZBX_EVAL_TRIGGER_EXPRESSION, trigger->expression_bin);
			zbx_eval_extract_tokens(&trigger->eval_cache.eval_ctx, trigger->expression,ZBX_EVAL_TRIGGER_EXPRESSION);
 			// if (FAIL == zbx_eval_parse_expression(&trigger->eval_cache.eval_ctx, trigger->expression,
 			// 		ZBX_EVAL_TRIGGER_EXPRESSION, &error))
 			// {
 			// 	zbx_free(error);
 			// 	return NULL;
 			// }

 			break;
 		case TRIGGER_CACHE_EVAL_CTX_R:
 			if ('\0' == *trigger->recovery_expression)
 				return NULL;

 			// if (FAIL == zbx_eval_parse_expression(&trigger->eval_cache.eval_ctx_r, trigger->recovery_expression,
 			// 		ZBX_EVAL_TRIGGER_EXPRESSION, &error))
 			// {
 			// 	zbx_free(error);
 			// 	return NULL;
 			// }
			zbx_eval_deserialize(&trigger->eval_cache.eval_ctx_r, trigger->recovery_expression, ZBX_EVAL_TRIGGER_EXPRESSION, trigger->recovery_expression_bin);
			zbx_eval_extract_tokens(&trigger->eval_cache.eval_ctx_r, trigger->recovery_expression, ZBX_EVAL_TRIGGER_EXPRESSION);
 			break;
 		case TRIGGER_CACHE_EVAL_CTX_MACROS:
 			if (FAIL ==  calc_trigger_expand_expression_macros(trigger, &trigger->eval_cache.eval_ctx))
 				return NULL;

 			break;
 		case TRIGGER_CACHE_EVAL_CTX_R_MACROS:
 			if (FAIL == calc_trigger_expand_expression_macros(trigger, &trigger->eval_cache.eval_ctx_r))
 				return NULL;

 			break;
 		case TRIGGER_CACHE_HOSTIDS:
 			zbx_vector_uint64_create(&trigger->eval_cache.hostids);
 			zbx_vector_uint64_create(&functionids);
 			calc_trigger_get_all_functionids(trigger, &functionids);
 			DCget_hostids_by_functionids(&functionids, &trigger->eval_cache.hostids);
 			zbx_vector_uint64_destroy(&functionids);
 			break;
 		default:
 			return NULL;
 	}

 	trigger->eval_cache.done |= flag;
 	return &trigger->eval_cache;
}

/******************************************************************************
 * Purpose: get original trigger expression/recovery expression with expanded *
 *          functions                                                         *
 ******************************************************************************/
static void	calc_trigger_get_expression(const zbx_eval_context_t *ctx, char **expression)
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

void	conf_calc_trigger_get_expression(calc_trigger_t *trigger, char **expression) {
 	trigger_cache_t	*cache;

 	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX)))
 		*expression = zbx_strdup(NULL, trigger->expression);
 	else
 		calc_trigger_get_expression(&cache->eval_ctx, expression);
}

/******************************************************************************
 * Purpose: get original trigger recovery expression with expanded functions  *
 ******************************************************************************/
void	conf_calc_trigger_get_recovery_expression(calc_trigger_t *trigger, char **expression)
 {
	trigger_cache_t	*cache;

 	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX_R)))
 		*expression = zbx_strdup(NULL, trigger->recovery_expression);
 	else
 	 	calc_trigger_get_expression(&cache->eval_ctx_r, expression);
}

//this func _probably_ belongs to functions rather then to triggers
static void	calc_trigger_evaluate_function_by_id(zbx_uint64_t functionid,  zbx_variant_t *value, zbx_trigger_func_t eval_func_cb)
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
		//	zbx_variant_t		var;
			zbx_timespec_t		ts;
			DC_EVALUATE_ITEM	evaluate_item;

			parameter = zbx_dc_expand_user_macros_in_func_params(function.parameter, item.host.hostid);
			zbx_timespec(&ts);

			evaluate_item.itemid = item.itemid;
			evaluate_item.value_type = item.value_type;
			evaluate_item.proxy_hostid = item.host.proxy_hostid;
			evaluate_item.host = item.host.host;
			evaluate_item.key_orig = item.key_orig;

			zbx_variant_clear(value);

			if (SUCCEED != eval_func_cb(value, &evaluate_item, function.function, parameter, &ts, &error))
				zbx_variant_set_error(value, error);
			else
				zbx_free(error);

			zbx_free(parameter);
			DCconfig_clean_items(&item, &err_item, 1);
		}

		DCconfig_clean_functions(&function, &err_func, 1);
	}

	if (VARIANT_VALUE_NONE == value->type )
	 	zbx_variant_set_str(value,zbx_strdup(NULL, "*UNKNOWN*"));

}

static void	conf_trigger_explain_expression(const zbx_eval_context_t *ctx, char **expression,
		zbx_trigger_func_t eval_func_cb)
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
		calc_trigger_evaluate_function_by_id(functionid, &token->value, eval_func_cb);
	}

	zbx_eval_compose_expression(&local_ctx, expression);

	zbx_eval_clear(&local_ctx);
}

void	conf_cacl_trigger_explain_expression(calc_trigger_t *trigger, char **expression,
		zbx_trigger_func_t eval_func_cb, int recovery)
{
	trigger_cache_t		*cache;
	zbx_trigger_cache_state_t	state;
	const zbx_eval_context_t	*ctx;

	state = (1 == recovery) ? TRIGGER_CACHE_EVAL_CTX_R_MACROS : TRIGGER_CACHE_EVAL_CTX_MACROS;

	if (NULL == (cache = calc_trigger_get_cache(trigger, state)))
	{
		*expression = zbx_strdup(NULL, "*UNKNOWN*");
		return;
	}

	ctx = (1 == recovery) ? &cache->eval_ctx_r : &cache->eval_ctx;

	conf_trigger_explain_expression(ctx, expression, eval_func_cb);
}


static void	calc_trigger_explain_expression(calc_trigger_t *trigger, char **expression, 
		zbx_trigger_func_t eval_func_cb, int is_recovery)
{
	trigger_cache_t		*cache;
	zbx_trigger_cache_state_t	state;
	const zbx_eval_context_t	*ctx;

	state = (1 == is_recovery) ? TRIGGER_CACHE_EVAL_CTX_R_MACROS : TRIGGER_CACHE_EVAL_CTX_MACROS;

	if (NULL == (cache = calc_trigger_get_cache(trigger, state)))
	{
		*expression = zbx_strdup(NULL, "*UNKNOWN*");
		return;
	}

	ctx = (1 == is_recovery) ? &cache->eval_ctx_r : &cache->eval_ctx;

	conf_trigger_explain_expression(ctx, expression, eval_func_cb);
}

void	conf_calc_trigger_explain_recovery_expression(calc_trigger_t *trigger, char **expression, 
		zbx_trigger_func_t eval_func_cb) {
	
	calc_trigger_explain_expression(trigger, expression, eval_func_cb, 1);
}

void	conf_calc_trigger_explain_expression(calc_trigger_t *trigger, char **expression,
		zbx_trigger_func_t eval_func_cb) {
		calc_trigger_explain_expression(trigger, expression, eval_func_cb, 0);
}

static void	calc_trigger_free_eval_cache(trigger_cache_t *cache)
{
 	if (0 != (cache->done & (1 << TRIGGER_CACHE_EVAL_CTX)))
 		zbx_eval_clear(&cache->eval_ctx);

 	if (0 != (cache->done & (1 << TRIGGER_CACHE_EVAL_CTX_R)))
 		zbx_eval_clear(&cache->eval_ctx_r);

 	if (0 != (cache->done & (1 << TRIGGER_CACHE_HOSTIDS)))
 		zbx_vector_uint64_destroy(&cache->hostids);
}


void calc_trigger_clean(calc_trigger_t *trigger)
{
	zbx_free(trigger->new_error);
	zbx_free(trigger->error);
	zbx_free(trigger->expression);
	zbx_free(trigger->recovery_expression);
	zbx_free(trigger->description);
	zbx_free(trigger->correlation_tag);
	zbx_free(trigger->opdata);
	zbx_free(trigger->event_name);
	zbx_free(trigger->expression_bin);
	zbx_free(trigger->recovery_expression_bin);

	zbx_vector_ptr_clear_ext(&trigger->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&trigger->tags);

	calc_trigger_free_eval_cache(&trigger->eval_cache);
}

/* Purpose: get the Nth function item from trigger expression  */
int	conf_calc_trigger_get_N_itemid(calc_trigger_t *trigger, int index, zbx_uint64_t *itemid)
{
	int			i, ret = FAIL;
	trigger_cache_t	*cache;

	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_EVAL_CTX)))
		return FAIL;

	for (i = 0; i < cache->eval_ctx.stack.values_num; i++)
	{
		zbx_eval_token_t	*token = &cache->eval_ctx.stack.values[i];
		zbx_uint64_t		functionid;
		DC_FUNCTION		function;
		int			errcode;

		if (ZBX_EVAL_TOKEN_FUNCTIONID != token->type || (int)token->opt + 1 != index)
			continue;

		switch (token->value.type)
		{
			case ZBX_VARIANT_UI64:
				functionid = token->value.data.ui64;
				break;
			case ZBX_VARIANT_NONE:
				if (SUCCEED != zbx_is_uint64_n(cache->eval_ctx.expression + token->loc.l + 1,
						token->loc.r - token->loc.l - 1, &functionid))
				{
					return FAIL;
				}
				zbx_variant_set_ui64(&token->value, functionid);
				break;
			default:
				return FAIL;
		}

		DCconfig_get_functions_by_functionids(&function, &functionid, &errcode, 1);

		if (SUCCEED == errcode)
		{
			*itemid = function.itemid;
			ret = SUCCEED;
		}

		DCconfig_clean_functions(&function, &errcode, 1);
		break;
	}

	return ret;
}

int	conf_calc_trigger_get_all_hostids(calc_trigger_t *trigger, const zbx_vector_uint64_t **hostids) {
  	trigger_cache_t	*cache;

  	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_HOSTIDS)))
  		return FAIL;

  	*hostids = &cache->hostids;
  	return SUCCEED;
}

int	conf_calc_trigger_get_N_hostid(calc_trigger_t *trigger, int index, u_int64_t *hostid) {
  	trigger_cache_t	*cache;

  	if (NULL == (cache = calc_trigger_get_cache(trigger, TRIGGER_CACHE_HOSTIDS)))
  		return FAIL;

  	if (index > trigger->eval_cache.hostids.values_num || index < 1)
		return FAIL;
	
	*hostid = trigger->eval_cache.hostids.values[index - 1];
	return SUCCEED;
}

static int	substitute_expression_functions_results(zbx_eval_context_t *ctx, char **error)
{
	zbx_uint64_t		functionid;
	zbx_variant_t func_value ={0};
	int			i;

	zbx_variant_set_none(&func_value);
	LOG_INF("Total %d functions in the stack",  ctx->stack.values_num );
	for (i = 0; i < ctx->stack.values_num; i++)
	{
		zbx_eval_token_t	*token = &ctx->stack.values[i];

		if (ZBX_EVAL_TOKEN_FUNCTIONID != token->type)
			continue;

		LOG_INF("Got token type %d, value type is %d, token value as ui is %ld", token->type, token->value.type, token->value.data.ui64);
		if (ZBX_VARIANT_UI64 != token->value.type)
		{
			/* functionids should be already extracted into uint64 vars */
			THIS_SHOULD_NEVER_HAPPEN;
			LOG_INF("Got function i %d, value type %d", i, token->value.type);
			*error = zbx_dsprintf(*error, "Cannot parse function at: \"%s\"",
					ctx->expression + token->loc.l);
			LOG_INF("Cannot parse function at: \"%s\"",	ctx->expression + token->loc.l);
			return FAIL;
		}

		functionid = token->value.data.ui64;
		
		calc_trigger_evaluate_function_by_id(functionid, &func_value, evaluate_function);

		if (VARIANT_VALUE_ERROR == func_value.type) {
			*error = func_value.data.err;
			return FAIL;
		}

		if (ZBX_VARIANT_NONE == func_value.type)
		{
			*error = zbx_strdup(*error, "Unexpected error while processing a trigger expression");
			return FAIL;
		}

		zbx_variant_copy(&token->value, &func_value);
	}

	return SUCCEED;
}

static int calc_trigger_evaluate_expression_result(calc_trigger_t *tr, zbx_eval_context_t *ctx, double *result)
{
	zbx_variant_t value;

	if (SUCCEED != zbx_eval_execute(ctx, &tr->timespec, &value, &tr->new_error))
		return FAIL;

	if (DC_get_debug_trigger() == tr->triggerid)
	{
		char *expression = NULL;

		zbx_eval_compose_expression(ctx, &expression);
		DEBUG_TRIGGER(tr->triggerid, "%s(): %s => %s", __func__, expression, zbx_variant_value_desc(&value));
		zbx_free(expression);
	}

	if (SUCCEED != zbx_variant_convert(&value, ZBX_VARIANT_DBL))
	{
		tr->new_error = zbx_dsprintf(tr->new_error, "Cannot convert expression result of type \"%s\" to"
									  " floating point value",
							  zbx_variant_type_desc(&value));
		zbx_variant_clear(&value);
		DEBUG_TRIGGER(tr->triggerid, "Cannot convert function result: %s", tr->new_error);
		return FAIL;
	}

	*result = value.data.dbl;
	DEBUG_TRIGGER(tr->triggerid, "Result of the function calculation is %f", value.data.dbl);

	return SUCCEED;
}

static int	calc_trigger_evaluate(calc_trigger_t *tr) {
	double expr_result;
	
	//calc all functions and replace them in the expression {245423} -> {0.25}
	substitute_expression_functions_results(&tr->eval_cache.eval_ctx, &tr->new_error);

	//macros in the expressions
	calc_trigger_expand_expression_macros(tr, &tr->eval_cache.eval_ctx);
	calc_trigger_expand_expression_macros(tr, &tr->eval_cache.eval_ctx_r);

	if (SUCCEED != calc_trigger_evaluate_expression_result(tr, &tr->eval_cache.eval_ctx, &expr_result)) {
 		tr->new_value = TRIGGER_VALUE_UNKNOWN;
 		DEBUG_TRIGGER(tr->triggerid, "Couldn't expand trigger macro, set to UNKNOWN value :%s", tr->new_error);
		return FAIL;
	}
 	
	DEBUG_TRIGGER(tr->triggerid,"Calculating trigger problem expression");

 	tr->new_value = tr->value;
		
	/* trigger expression evaluates to true, set PROBLEM value */
	if (SUCCEED != zbx_double_compare(expr_result, 0.0))
	{
		DEBUG_TRIGGER(tr->triggerid, "Trigger expression calculated to non-zero (PROBLEM)");
		tr->new_value = TRIGGER_VALUE_PROBLEM;
		return SUCCEED;
	}

 	/* OK value */

 	if (TRIGGER_RECOVERY_MODE_NONE == tr->recovery_mode)
 		return SUCCEED;

 	if (TRIGGER_RECOVERY_MODE_EXPRESSION == tr->recovery_mode)
 	{
 		tr->new_value = TRIGGER_VALUE_OK;
 		return SUCCEED;
 	}

 	DEBUG_TRIGGER(tr->triggerid,"Calculating trigger recovery expression");
 	/* processing recovery expression mode */
 	if (SUCCEED !=calc_trigger_evaluate_expression_result(tr, &tr->eval_cache.eval_ctx_r, &expr_result)) {
 		tr->new_value = TRIGGER_VALUE_UNKNOWN;
 		return FAIL;
 	}

	if (SUCCEED != zbx_double_compare(expr_result, 0.0))
	{
		tr->new_value = TRIGGER_VALUE_OK;
		return SUCCEED;
	}

	return SUCCEED;
 }


void calc_trigger_prepare_expressions(calc_trigger_t *tr)
{
		//ZBX_EVAL_TRIGGER_EXPRESSION
		calc_trigger_get_cache(tr, TRIGGER_CACHE_EVAL_CTX);
		//zbx_eval_deserialize(&tr->eval_cache.eval_ctx, tr->expression, ZBX_EVAL_EXTRACT_ALL, tr->expression_bin);
		DEBUG_TRIGGER(tr->triggerid, "Extracted trigger expression to binary");

		if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
		{
			calc_trigger_get_cache(tr, TRIGGER_CACHE_EVAL_CTX_R);
		//	zbx_eval_deserialize(&tr->eval_cache.eval_ctx_r, tr->recovery_expression, ZBX_EVAL_EXTRACT_ALL, tr->recovery_expression_bin);
			DEBUG_TRIGGER(tr->triggerid, "Extracted trigger recovery expression to binary");
		}
}

static void calc_trigger_check_dependency(calc_trigger_t *trigger) {
	if (FAIL == glb_check_trigger_has_value_ok_masters(trigger->triggerid)) {
		zbx_free(trigger->new_error);
		trigger->new_error = zbx_strdup(NULL,"There are master trigger(s) in PROBLEM or UNKNOWN state");
		trigger->value = TRIGGER_VALUE_UNKNOWN;
		DEBUG_TRIGGER(trigger->triggerid, "Dependency check found master triggers in PROBLEM or UNKNOWN state, trigger is set to UNKNWON value");
		return;
	}
	DEBUG_TRIGGER(trigger->triggerid, "Dependency check not found master triggers in PROBLEM or UNKNOWN state, trigger value is not changed %d", trigger->value);
}

static int calc_trigger_error_changed(calc_trigger_t *trigger) {
	if (NULL == trigger->error && NULL != trigger->new_error)
		return SUCCEED;
	if (NULL == trigger->new_error && NULL != trigger->error)
		return SUCCEED;
	
	return strcmp(trigger->error, trigger->new_error);
}


static void calc_trigger_update_state(calc_trigger_t *trigger) {

	if (trigger->value != trigger->new_value) {
		DEBUG_TRIGGER(trigger->triggerid, "Will write value change %d->%d to events log", trigger->value, trigger->new_value  );
		glb_state_trigger_set_value(trigger->triggerid, trigger->new_value, 0);
		write_event_log_attr_int_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "value", trigger->value, trigger->new_value );
	}

	if (calc_trigger_error_changed(trigger)) {
		DEBUG_TRIGGER(trigger->triggerid, "Will write error change '%s'->'%s' to events log", trigger->error, trigger->new_error  );
		write_event_log_attr_str_change(trigger->triggerid, 0, GLB_EVENT_LOG_SOURCE_TRIGGER, "error", trigger->error, trigger->new_error);
		
		if (NULL != trigger->new_error && *trigger->new_error != '\0') 
			glb_state_trigger_set_error(trigger->triggerid, trigger->new_error);
	}
}

static int calc_trigger_process(calc_trigger_t *tr) {
	int max_severity = TRIGGER_SEVERITY_NOT_CLASSIFIED;

	DEBUG_TRIGGER(tr->triggerid, "Processing trigger value %d", tr->new_value);
	calc_trigger_check_dependency(tr);
	calc_trigger_update_state(tr);
	glb_state_problems_process_trigger_value(tr, &max_severity);
}

int  DCget_calc_trigger_by_id(calc_trigger_t *tr, u_int64_t triggerid);

int  calc_trigger_get(calc_trigger_t *tr, u_int64_t triggerid){
	return DCget_calc_trigger_by_id(tr, triggerid);
}

int conf_calc_trigger_calculate(u_int64_t triggerid) {
	calc_trigger_t tr = {0};
	int max_severity = TRIGGER_SEVERITY_NOT_CLASSIFIED;
	
	/*config and state retrieval*/
	if (FAIL == calc_trigger_get(&tr, triggerid))
		return FAIL;
	
	/*prepare trigger expressions*/
	calc_trigger_prepare_expressions(&tr);
	
	/*calculate new trigger value*/
	calc_trigger_evaluate(&tr);

	/*processing the new trigger's value*/
	max_severity = calc_trigger_process(&tr);

	calc_trigger_clean(&tr);

	return max_severity;
}

char* conf_calc_trigger_get_severity_name(unsigned char priority)
{
	static zbx_config_t	cfg = {0};

	if (TRIGGER_SEVERITY_COUNT <= priority)
		return NULL;

	//this will init once and never free cfg, but we most likely reuse it often
	if ( NULL == cfg.severity_name) { 
		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_SEVERITY_NAME);
	}
	//zbx_config_clean(&cfg);
	return cfg.severity_name[priority];
}

char      *conf_calc_trigger_get_admin_state_name(unsigned char admin_state)
 {
 	switch (admin_state)
 	{
 		case TRIGGER_STATUS_ENABLED:
			return "Enabled";
		case TRIGGER_STATUS_DISABLED:
 			return "Disabled";
 	}
	HALT_HERE("Unknown admin state passed %d", admin_state)
}
