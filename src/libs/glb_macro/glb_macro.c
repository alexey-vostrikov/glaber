/*
** Copyright Glaber
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

/* reasoning: make macro calc optimal, avoid db usage, reuse objects already
having data needed */

#include "zbxcommon.h"
#include "glb_macro_defs.h"
#include "zbxexpr.h"
#include "log.h"
#include "zbxdbhigh.h"
#include "zbxcacheconfig.h"
#include "zbxserver.h"
#include "../zbxserver/macrofunc.h"


typedef struct {
    int pos;
    const char *macro;
    int indexed_macro;
    int Nfuncid;
    char c;
    int raw_value; 
    zbx_token_t token;
    zbx_token_t inner_token;
    zbx_token_search_t token_search;
    zbx_dc_um_handle_t	*um_handle;
    int require_addr;
    char * replace_to;
    int ret;
    size_t	data_alloc;
    size_t  data_len;
    int res;
} macro_proc_data_t;

typedef void *(*macro_proc_func_t)( macro_proc_data_t *macro_proc, void *obj_data);

static int  expand_macros_impl(void *obj_data, char **replace_data, 
            int macro_proc_type, macro_proc_func_t macro_proc_func, char *error, int maxerrlen);

void *macro_proc_common( macro_proc_data_t *macro_proc, void *obj_data) {
    u_int64_t hostid = (u_int64_t)obj_data;
    
    if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type)
	{
	    if (0 != hostid)
			zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, &hostid, 1, &macro_proc->replace_to);
		else
			zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, NULL, 0, &macro_proc->replace_to);

		macro_proc->pos = macro_proc->token.loc.r;
    }
}

void *macro_proc_host(macro_proc_data_t *macro_proc, void *obj_data) {
    
    DC_HOST *dc_host = obj_data;
    DC_INTERFACE interface;
    LOG_INF("Looking for host data from dc_host object");
//	LOG_INF("Token is %s", macro_proc->token.loc.l);
  //  LOG_INF("Macro is %s", macro_proc->macro);
    if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type)
	{
    	zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, &dc_host->hostid, 1, &macro_proc->replace_to);
		macro_proc->pos = macro_proc->token.loc.r;
	}
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_HOST) || 0 == strcmp(macro_proc->macro, MVAR_HOSTNAME)) {
        LOG_INF("Running strdup");
    	macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, dc_host->host);
        LOG_INF("Duplicated");
    }
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_NAME))
		macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, dc_host->name);
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_IP) || 0 == strcmp(macro_proc->macro, MVAR_IPADDRESS))
	{
		if (SUCCEED == (macro_proc->ret = DCconfig_get_interface(&interface, dc_host->hostid, 0)))
			macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, interface.ip_orig);
	}
	else if	(0 == strcmp(macro_proc->macro, MVAR_HOST_DNS))
	{
		if (SUCCEED == (macro_proc->ret = DCconfig_get_interface(&interface, dc_host->hostid, 0)))
			macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, interface.dns_orig);
	}
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_CONN))
	{
		if (SUCCEED == (macro_proc->ret = DCconfig_get_interface(&interface, dc_host->hostid, 0)))
			macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, interface.addr);
	}
}

/* parse event name, trigger message and comments */
void *macro_proc_event_name(macro_proc_data_t *macro_proc, void *obj_data)
{
	CALC_TRIGGER *tr = obj_data;

	if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type)
	{
		zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, tr->hostids.values,
							  tr->hostids.values_num, &macro_proc->replace_to);

		macro_proc->pos = macro_proc->token.loc.r;
	}
	else if (ZBX_TOKEN_REFERENCE == macro_proc->token.type)
	{

		if (SUCCEED != zbx_db_trigger_get_constant(&event->trigger,
												   token.data.reference.index, &replace_to))
		{
			/* expansion failed, reference substitution is impossible */
			token_search &= ~ZBX_TOKEN_SEARCH_REFERENCES;
			continue;
		}
	}
	else if (ZBX_TOKEN_EXPRESSION_MACRO == macro_proc->inner_token.type)
	{
		if (0 != (macro_proc->macro_type & MACRO_TYPE_EVENT_NAME))
		{

			zbx_timespec_t ts;
			char *errmsg = NULL;

			ts.sec = event->clock;
			ts.ns = event->ns;

			if (SUCCEED != (ret = get_expression_macro_result(event, *data,
															  &inner_token.data.expression_macro.expression, &ts,
															  &replace_to, &errmsg)))
			{
				*errmsg = tolower(*errmsg);
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot evaluate"
											" expression macro: %s",
						   __func__, errmsg);
				zbx_strlcpy(error, errmsg, maxerrlen);
				zbx_free(errmsg);
			}
		}
	}
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_HOST) || 0 == strcmp(macro_proc->macro, MVAR_HOSTNAME))
	{
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_HOST);
	}
	else if (0 == strcmp(macro_proc->macro, MVAR_HOST_NAME))
	{
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_NAME);
	}
	else if (0 == strcmp(m, MVAR_HOST_IP) || 0 == strcmp(m, MVAR_IPADDRESS))
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_IP);
	else if (0 == strcmp(m, MVAR_HOST_DNS))
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_DNS);
	else if (0 == strcmp(m, MVAR_HOST_CONN))
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_CONN);
	else if (0 == strcmp(m, MVAR_HOST_PORT))
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_PORT);
	else if (0 == strcmp(m, MVAR_HOST_DESCRIPTION))
		ret = DBget_trigger_value(&event->trigger, &replace_to, N_functionid, ZBX_REQUEST_HOST_DESCRIPTION);
	else if (0 == strcmp(m, MVAR_ITEM_VALUE))
		ret = DBitem_value(&event->trigger, &replace_to, N_functionid, event->clock, event->ns, raw_value);
	else if (0 == strncmp(m, MVAR_ITEM_LOG, ZBX_CONST_STRLEN(MVAR_ITEM_LOG)))
		ret = get_history_log_value(m, &event->trigger, &replace_to, N_functionid, event->clock, event->ns, tz);
	else if (0 == strcmp(m, MVAR_ITEM_LASTVALUE))
		ret = DBitem_lastvalue(&event->trigger, &replace_to, N_functionid, raw_value);
	else if (0 != (macro_type & MACRO_TYPE_EVENT_NAME))
	{
		if (0 == strcmp(m, MVAR_TIME))
		{
			replace_to = zbx_strdup(replace_to, zbx_time2str(time(NULL), tz));
		}
		else if (0 == strcmp(m, MVAR_TRIGGER_EXPRESSION_EXPLAIN))
		{
			zbx_db_trigger_explain_expression(&event->trigger, &replace_to, evaluate_function, 0);
		}
		else if (1 == indexed_macro && 0 == strcmp(m, MVAR_FUNCTION_VALUE))
		{
			zbx_db_trigger_get_function_value(&event->trigger, N_functionid,
											  &replace_to, evaluate_function, 0);
		}
	}
}

zbx_token_search_t set_token_search_flags(int macro_type) {
	int flag = 0;

	if (0 != (macro_type & (MACRO_TYPE_TRIGGER_DESCRIPTION | MACRO_TYPE_EVENT_NAME)))
		flag |= ZBX_TOKEN_SEARCH_REFERENCES;

	if (0 != (macro_type & (MACRO_TYPE_MESSAGE_NORMAL | MACRO_TYPE_MESSAGE_RECOVERY | MACRO_TYPE_MESSAGE_UPDATE |
			MACRO_TYPE_EVENT_NAME)))
	{
		flag |= ZBX_TOKEN_SEARCH_EXPRESSION_MACRO;
	}
	
	return flag;
}

int glb_macro_expand_trigger_ctx_expression(CALC_TRIGGER *trigger, char **data, int token_type, char *error, size_t errlen) {
	HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_trigger_expressions(CALC_TRIGGER *trigger) {
	  HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_translate_event_name(CALC_TRIGGER *trigger, char **event_name, char *error, size_t errlen) {


    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_translate_string(const char *expression, int token_type, char *result, int result_size) {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_common_unmasked(char **data, char *error, size_t errlen) {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_item_key(char **data, int key_type, char *error, size_t errlen) {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_common_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen) {

    LOG_INF("unExpanded macro: %s", *data);
    expand_macros_impl(&hostid, data, MACRO_TYPE_COMMON, macro_proc_common, error, errlen);
    LOG_INF("Expanded macro: %s", *data);
}

int glb_macro_expand_item_key_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen) {
	glb_macro_expand_common_by_hostid(data, hostid, error, errlen);
}

int glb_macro_expand_common_by_hostid_unmasked(char **data, u_int64_t hostid, char *error, size_t errlen) {
    zbx_dc_um_handle_t	*um_handle;
    um_handle = zbx_dc_open_user_macros_secure();
    //LOG_INF("unExpanded macro: %s", *data);
    expand_macros_impl(&hostid, data, MACRO_TYPE_COMMON, macro_proc_common, error, errlen);
    //LOG_INF("Expanded macro: %s", *data);
    zbx_dc_close_user_macros(um_handle);
}

int glb_macro_expand_by_host(char **data, const DC_HOST *host, int field_type, char *error, size_t errlen) {
    LOG_INF("In host");
    LOG_INF("unExpanded macro: %s", *data);
    expand_macros_impl((void *)host, data, MACRO_TYPE_HTTPTEST_FIELD, macro_proc_host, error, errlen);
    LOG_INF("Expanded macro: %s", *data);

}

int glb_macro_expand_by_host_unmasked(char **data, const DC_HOST *host, int macro_type, char *error, size_t errlen) {
    zbx_dc_um_handle_t	*um_handle;
    um_handle = zbx_dc_open_user_macros_secure();
     LOG_INF("In host");
    LOG_INF("unExpanded macro: %s", *data);
    expand_macros_impl((void *)host, data, macro_type, macro_proc_host, error, errlen);
    LOG_INF("Expanded macro: %s", *data);
    zbx_dc_close_user_macros(um_handle);
}

int glb_macro_expand_by_item(char **data, const DC_ITEM *item, int type, char *error, size_t errlen) {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_by_item_unmasked(char **data, const DC_ITEM *item, int type, char *error, size_t errlen) {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_lld() {
    HALT_HERE("Not implemented: %s", __func__);
}

int glb_macro_expand_alert_data(DB_ALERT *db_alert, char *param) {
    HALT_HERE("Not implemented: %s", __func__);
}


/******************************************************************************
 *                                                                            *
 * Purpose: check if a macro in string is one of the list and extract index   *
 *                                                                            *
 * Parameters: str          - [IN] string containing potential macro          *
 *             strloc       - [IN] part of the string to check                *
 *             macros       - [IN] list of allowed macros (without indices)   *
 *             N_functionid - [OUT] index of the macro in string (if valid)   *
 *                                                                            *
 * Return value: unindexed macro from the allowed list or NULL                *
 *                                                                            *
 * Comments: example: N_functionid is untouched if function returns NULL, for *
 *           a valid unindexed macro N_function is 1.                         *
 *                                                                            *
 ******************************************************************************/
static const char	*macro_in_list(const char *str, zbx_strloc_t strloc, const char **macros, int *N_functionid)
{
	const char	**macro, *m;
	size_t		i;

	for (macro = macros; NULL != *macro; macro++)
	{
		for (m = *macro, i = strloc.l; '\0' != *m && i <= strloc.r && str[i] == *m; m++, i++)
			;

		/* check whether macro has ended while strloc hasn't or vice-versa */
		if (('\0' == *m && i <= strloc.r) || ('\0' != *m && i > strloc.r))
			continue;

		/* strloc either fully matches macro... */
		if ('\0' == *m)
		{
			if (NULL != N_functionid)
				*N_functionid = 1;

			break;
		}

		/* ...or there is a mismatch, check if it's in a pre-last character and it's an index */
		if (i == strloc.r - 1 && '1' <= str[i] && str[i] <= '9' && str[i + 1] == *m && '\0' == *(m + 1))
		{
			if (NULL != N_functionid)
				*N_functionid = str[i] - '0';

			break;
		}
	}

	return *macro;
}


/******************************************************************************
 * Purpose: check if a token contains indexed macro                           *
 ******************************************************************************/
static int	is_indexed_macro(const char *str, const zbx_token_t *token)
{
	const char	*p;

	switch (token->type)
	{
		case ZBX_TOKEN_MACRO:
			p = str + token->loc.r - 1;
			break;
		case ZBX_TOKEN_FUNC_MACRO:
			p = str + token->data.func_macro.macro.r - 1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	return '1' <= *p && *p <= '9' ? 1 : 0;
}

/* macros that are supported in expression macro */
static const char	*expr_macros[] = {MVAR_HOST_HOST, MVAR_HOSTNAME, MVAR_ITEM_KEY, NULL};

typedef struct
{
	char	*macro;
	char	*functions;
}
zbx_macro_functions_t;

/* macros that can be modified using macro functions */
static zbx_macro_functions_t	mod_macros[] =
{
	{MVAR_ITEM_VALUE, "regsub,iregsub,fmtnum"},
	{MVAR_ITEM_LASTVALUE, "regsub,iregsub,fmtnum"},
	{MVAR_TIME, "fmttime"},
	{"{?}", "fmtnum"},
	{NULL, NULL}
};

/******************************************************************************
 *                                                                            *
 * Purpose: check if a macro function one in the list for the macro           *
 *                                                                            *
 * Parameters: str          - [IN] string containing potential macro          *
 *             fm           - [IN] function macro to check                    *
 *             N_functionid - [OUT] index of the macro in string (if valid)   *
 *                                                                            *
 * Return value: unindexed macro from the allowed list or NULL                *
 *                                                                            *
 ******************************************************************************/
static const char	*func_macro_in_list(const char *str, zbx_token_func_macro_t *fm, int *N_functionid)
{
	int	i;

	for (i = 0; NULL != mod_macros[i].macro; i++)
	{
		size_t	len, fm_len;

		len = strlen(mod_macros[i].macro);
		fm_len = fm->macro.r - fm->macro.l + 1;

		if (len > fm_len || 0 != strncmp(mod_macros[i].macro, str + fm->macro.l, len - 1))
			continue;

		if ('?' != mod_macros[i].macro[1] && len != fm_len)
		{
			if (SUCCEED != zbx_is_uint_n_range(str + fm->macro.l + len - 1, fm_len - len, N_functionid,
					sizeof(*N_functionid), 1, 9))
			{
				continue;
			}
		}
		else if (mod_macros[i].macro[len - 1] != str[fm->macro.l + fm_len - 1])
			continue;

		if (SUCCEED == zbx_str_n_in_list(mod_macros[i].functions, str + fm->func.l, fm->func_param.l - fm->func.l,
				','))
		{
			return mod_macros[i].macro;
		}
	}

	return NULL;
}


static int is_empty_replace_data(char **replace_data) {
	if (NULL == replace_data || NULL == *replace_data || '\0' == **replace_data)
        return SUCCEED;
    
    return FAIL;
}


static int check_token_is_processable_type(macro_proc_data_t *macro_proc, char *data) {
    switch (macro_proc->token.type)
	{
		case ZBX_TOKEN_OBJECTID:
		case ZBX_TOKEN_LLD_MACRO:
		case ZBX_TOKEN_LLD_FUNC_MACRO:
			/* neither lld nor {123123} macros are processed by this function, skip them */
			macro_proc->pos = macro_proc->token.loc.r + 1;
			return FAIL;
		case ZBX_TOKEN_MACRO:
				if (0 != is_indexed_macro(data, &macro_proc->token) &&
					NULL != (macro_proc->macro = macro_in_list(data, macro_proc->token.loc, ex_macros, &macro_proc->Nfuncid)))
			{
				macro_proc->indexed_macro = 1;
			}
			else
			{
				LOG_INF("setting macro addr and sybol");
				macro_proc->macro = data + macro_proc->token.loc.l;
				LOG_INF("setting macro addr and symbol: %s", data);
				LOG_INF("setting macro addr and symbol: %s", macro_proc->macro);
				macro_proc->c = (data)[ (macro_proc->token.loc.r) + 1];
				(data)[macro_proc->token.loc.r + 1] = '\0';
			}
			return SUCCEED;
		case ZBX_TOKEN_FUNC_MACRO:
			macro_proc->raw_value = 1;
			macro_proc->indexed_macro = is_indexed_macro(data, &macro_proc->token);
			if (NULL == (macro_proc->macro = func_macro_in_list(data, &macro_proc->token.data.func_macro, &macro_proc->Nfuncid)) ||
					SUCCEED != zbx_token_find(data, macro_proc->token.data.func_macro.macro.l,
							&macro_proc->inner_token, macro_proc->token_search))
			{
				/* Ignore functions with macros not supporting them, but do not skip the */
				/* whole token, nested macro should be resolved in this case. */
				macro_proc->pos ++;
				return FAIL;
			}
			break;
		case ZBX_TOKEN_USER_MACRO:
			/* To avoid *data modification user macro resolver should be replaced with a function */
			/* that takes initial *data string and token.data.user_macro instead of m as params.  */
			macro_proc->macro = data + macro_proc->token.loc.l;
			macro_proc->c = (data)[macro_proc->token.loc.r + 1];
			(data)[macro_proc->token.loc.r + 1] = '\0';
			return SUCCEED;
			case ZBX_TOKEN_REFERENCE:
		case ZBX_TOKEN_EXPRESSION_MACRO:
			/* These macros (and probably all other in the future) must be resolved using only */
			/* information stored in token.data union. For now, force crash if they rely on m. */
			macro_proc->macro = NULL;
			return FAIL;
			default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
}



static void init_iter_macro_proc(macro_proc_data_t *macro_proc) {
		macro_proc->indexed_macro = 0;
		macro_proc->Nfuncid =1;
        macro_proc->raw_value = 0;
        macro_proc->pos = macro_proc->token.loc.l;
        macro_proc->inner_token = macro_proc->token;
        macro_proc->require_addr = 0;
        macro_proc->replace_to = NULL;
}

static void  macro_calc_token_function(macro_proc_data_t *macro_proc, char *data) {
    if (ZBX_TOKEN_FUNC_MACRO == macro_proc->token.type && NULL != macro_proc->replace_to) 
		if (SUCCEED != (zbx_calculate_macro_function(data, &macro_proc->token.data.func_macro, &macro_proc->replace_to))){
			zbx_free(macro_proc->replace_to);
            macro_proc->replace_to = NULL;
            macro_proc->ret = FAIL;
		}
}

static void macro_escape_replace_to(int macro_type,  char **replace_to) {
    if (0 != (macro_type & (MACRO_TYPE_HTTP_JSON | MACRO_TYPE_SCRIPT_PARAMS_FIELD)) && NULL != *replace_to)
			zbx_json_escape(replace_to);

	if (0 != (macro_type & MACRO_TYPE_QUERY_FILTER) && NULL != *replace_to)
		{
			char	*esc;
			esc = zbx_dyn_escape_string(*replace_to, "\\");
			zbx_free(*replace_to);
			*replace_to = esc;
		}
}

static void macro_check_has_address(macro_proc_data_t *macro_proc, char *data, char *error, int maxerrlen) {
    if (NULL != macro_proc->replace_to)
		{
			if (1 == macro_proc->require_addr && NULL != strstr(macro_proc->replace_to, "{$"))
			{
				/* Macros should be already expanded. An unexpanded user macro means either unknown */
				/* macro or macro value validation failure.                                         */
				zbx_snprintf(error, maxerrlen, "Invalid macro '%.*s' value",
						(int)(macro_proc->token.loc.r - macro_proc->token.loc.l + 1), *data + macro_proc->token.loc.l);
				macro_proc->res = FAIL;
			}
		}
}

static void macro_replace_calc_result(macro_proc_data_t *macro_proc, char **data) {
	//LOG_INF("Wvfwef");
    if (FAIL == macro_proc->ret)
	{
		LOG_INF("cannot resolve macro '%.*s'",
				(int)(macro_proc->token.loc.r - macro_proc->token.loc.l + 1), *data + macro_proc->token.loc.l);
		macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, STR_UNKNOWN_VARIABLE);
	}
	//LOG_INF("Wvfwef1");
	
	if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type || (ZBX_TOKEN_MACRO == macro_proc->token.type && 0 == macro_proc->indexed_macro)){
	//	LOG_INF("Updating data %s, len is %d, token.loc.r is %d", *data, strlen(*data), macro_proc->token.loc.r);
		(*data)[macro_proc->token.loc.r + 1] = macro_proc->c;
	}
	
	//LOG_INF("Wvfwef2");
	if (NULL != macro_proc->replace_to)
	{
	//	LOG_INF("Wvfwef21");
		macro_proc->pos = macro_proc->token.loc.r;

		macro_proc->pos += zbx_replace_mem_dyn(data, &macro_proc->data_alloc, &macro_proc->data_len, macro_proc->token.loc.l,
				macro_proc->token.loc.r - macro_proc->token.loc.l + 1, macro_proc->replace_to, strlen(macro_proc->replace_to));
		zbx_free(macro_proc->replace_to);
	}
	//LOG_INF("Wvfwef3");
	macro_proc->pos++;
}

static int  expand_macros_impl(void *obj_data, char **replace_data, 
            int macro_proc_type, macro_proc_func_t macro_proc_func, char *error, int maxerrlen)
{
	char				*replace_to = NULL;// sql[64];
		
	// 				     user_names_found = 0, raw_value;
	int pos = 0, found, res = SUCCEED;
    // char				*expression = NULL, *user_username = NULL, *user_name = NULL,
	// 				*user_surname = NULL;
	macro_proc_data_t  macro_proc;
	macro_proc.token_search =  ZBX_TOKEN_SEARCH_BASIC | set_token_search_flags(macro_proc_type);

    if (SUCCEED == is_empty_replace_data(replace_data) || 
        SUCCEED != zbx_token_find(*replace_data, pos, &macro_proc.token, macro_proc.token_search))
        return SUCCEED;
	
//	LOG_INF("Replacing macro in %s", *replace_data);

	macro_proc.um_handle = zbx_dc_open_user_macros();
	macro_proc.data_alloc = macro_proc.data_len = strlen(*replace_data) + 1;

	for (found = SUCCEED; SUCCEED == res && SUCCEED == found;
			found = zbx_token_find(*replace_data, pos, &macro_proc.token, macro_proc.token_search))
	{
        init_iter_macro_proc(&macro_proc);

	    if (FAIL == check_token_is_processable_type(&macro_proc, *replace_data))
            continue;
        LOG_INF("Calling callback func, macro is %s", macro_proc.macro);
        macro_proc_func(&macro_proc, obj_data);
        LOG_INF("Escaping");
        macro_escape_replace_to(macro_proc_type, &macro_proc.replace_to);
        LOG_INF("Func calc");
        macro_calc_token_function(&macro_proc, *replace_data);
        macro_check_has_address(&macro_proc, *replace_data, error, maxerrlen);
        LOG_INF("Replacing result to %s", *replace_data);
		macro_replace_calc_result(&macro_proc, replace_data);
    }

    //zbx_free(user_username);
	//zbx_free(user_name);
	//zbx_free(user_surname);
	//zbx_free(expression);
	//zbx_vector_uint64_destroy(&hostids);
	zbx_dc_close_user_macros(macro_proc.um_handle);
    return macro_proc.res;
}

// static int macro_expand_common_by_hostid(char **replace_to, const char * macro, u_int64_t hostid, const zbx_dc_um_handle_t *um_handle) {
	
//     if (0 != hostid)
// 		zbx_dc_get_user_macro(um_handle, macro, hostid, 1, &replace_to);
// 	else
// 		zbx_dc_get_user_macro(um_handle, macro, NULL, 0, &replace_to);

// }




// int process_macro(char *macro) {
// 	return SUCCEED;
// }

// static add_non_macro_data(char *res, char *data, size_t start, size_t bytes) {
// 	if ( bytes > 0 ) 
// 		zbx_strlcpy(res, data+start, bytes + 1);
// }

