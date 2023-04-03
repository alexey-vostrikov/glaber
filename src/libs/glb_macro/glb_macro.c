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
#include "glb_macro_builtin.h"

typedef void (*macro_proc_func_t)( macro_proc_data_t *macro_proc, void *obj_data);

static int  expand_macros_impl(void *obj_data, char **replace_data, 
            int macro_proc_type, macro_proc_func_t macro_proc_func, char *error, int maxerrlen);

static void macro_proc_hostid( macro_proc_data_t *macro_proc, void *obj_data) {
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

static void macro_proc_hostids( macro_proc_data_t *macro_proc, void *obj_data) {
    zbx_vector_uint64_t *hostids = obj_data;

    for (int i = 0; i < hostids->values_num; i++) 
		macro_proc_hostid(macro_proc, &hostids->values[i]);
}


static void macro_proc_dchost(macro_proc_data_t *macro_proc, void *obj_data) {
 //   LOG_INF("In %s", __func__);
	DC_HOST *host =  obj_data;
//	LOG_INF("Looking for host data from dc_host object, macro is %s", macro_proc->macro);

    if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type) 
		zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, &host->hostid, 1, &macro_proc->replace_to);
	else 
		glb_macro_builtin_expand_by_host(macro_proc, host);
//	LOG_INF("Replaced to %s", macro_proc->replace_to);
}

static void macro_proc_item(macro_proc_data_t *macro_proc, void *obj_data) {
    DC_ITEM *item =  obj_data;
	
	//LOG_INF("In %s", __func__);
    
	if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type) 
		zbx_dc_get_user_macro(macro_proc->um_handle, macro_proc->macro, 
			&item->host.hostid, 1, &macro_proc->replace_to);
	else 
		glb_macro_builtin_expand_by_item(macro_proc, item);
}



/* parse event name, trigger message or comments */
static void macro_proc_trigger(macro_proc_data_t *macro_proc, void *obj_data)
{
	calc_trigger_t *tr = obj_data;
	
	//LOG_INF("In %s", __func__);
	//LOG_INF("Trigger has %d hosts in the context", hostids->values_num);
	//LOG_INF("Trigger has expression: %s", tr->expression);

	if (ZBX_TOKEN_USER_MACRO == macro_proc->token.type) //{$MACRO_DEFINED_BY_A_USER}
	{
		zbx_vector_uint64_t *hostids;
		
		if (SUCCEED == conf_calc_trigger_get_all_hostids(tr, &hostids) && hostids->values_num > 0)
			macro_proc_hostids(macro_proc, obj_data);

		macro_proc->pos = macro_proc->token.loc.r;
		return;
	}

	if (ZBX_TOKEN_EXPRESSION_MACRO == macro_proc->token.type) //{?max(/{HOST.HOST}/{ITEM.KEY},#2)/100}
	{
		HALT_HERE("Write and finish expression parsing and it's tests");
		// char *expanded_expression = zbx_strdup(NULL, macro_proc->macro);
		// //expanding expression based first
		// if (SUCCEED == expand_macros_impl(tr, expanded_expression, 0, macro_proc_trigger, error, len)) {
		// 	macro_proc->replace_to = glb_expression_calc(expanded_expression);
		// 	zbx_free(expanded_expression);
		// 	return;
		// }

		// macro_proc->ret = glb_expression_calc_by_trigger(tr, macro_proc->macro, &macro_proc->replace_to);
		// macro_proc->pos = macro_proc->token.loc.r;
		return;
	}

	if (ZBX_TOKEN_MACRO == macro_proc->token.type) {
		glb_macro_builtin_expand_by_trigger(macro_proc, tr);
		return;
	}

	HALT_HERE("Not implemented: unexpended macro type %d",  macro_proc->token.type);
}

zbx_token_search_t set_token_search_flags(int macro_type) {
	int flag = 0;

//	if (0 != (macro_type & (MACRO_TYPE_TRIGGER_DESCRIPTION | MACRO_TYPE_EVENT_NAME)))
//		flag |= ZBX_TOKEN_SEARCH_REFERENCES;

	if (0 != (macro_type & (MACRO_TYPE_MESSAGE_NORMAL | MACRO_TYPE_MESSAGE_RECOVERY | MACRO_TYPE_MESSAGE_UPDATE |
			MACRO_TYPE_EVENT_NAME)))
	{
		flag |= ZBX_TOKEN_SEARCH_EXPRESSION_MACRO;
	}
	
	return flag;
}

//for trigger processing need to pass context - normal or recovery for fetching indexed data
int glb_macro_expand_by_trigger(calc_trigger_t *trigger, char **data, char *error, size_t errlen) {
	return expand_macros_impl(trigger, data, 0, macro_proc_trigger, error, errlen);
}

int glb_macro_expand_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen) {
    return expand_macros_impl(&hostid, data, MACRO_TYPE_COMMON, macro_proc_hostid, error, errlen);
}

int glb_macro_expand_common(char **data, char *error, size_t errlen) {
	u_int64_t id=0;
    return expand_macros_impl(&id, data, MACRO_TYPE_COMMON, macro_proc_hostid, error, errlen);
}


int glb_macro_expand_by_hostids(char **data, zbx_vector_uint64_t *hostids, char *error, size_t errlen) {
    int ret = SUCCEED;

    for (int i = 0; i < hostids->values_num; i++)
        if (SUCCEED != glb_macro_expand_by_hostid(data, hostids->values[i], error, errlen))
            ret = FAIL;

    return ret;
}

int glb_macro_expand_item_key_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen) {
	glb_macro_expand_by_hostid(data, hostid, error, errlen);
}

int glb_macro_expand_by_hostid_unmasked(char **data, u_int64_t hostid, char *error, size_t errlen) {
    zbx_dc_um_handle_t	*um_handle;
    um_handle = zbx_dc_open_user_macros_secure();
	expand_macros_impl(&hostid, data, MACRO_TYPE_COMMON, macro_proc_hostid, error, errlen);
    zbx_dc_close_user_macros(um_handle);
}

int glb_macro_expand_common_unmasked(char **data, char *error, size_t errlen) {
	u_int64_t id=0;
	
    return glb_macro_expand_by_hostid_unmasked(data, id, error, errlen);
}


int glb_macro_expand_by_host(char **data, const DC_HOST *host, char *error, size_t errlen) {
//    LOG_INF("In host");
//    LOG_INF("unExpanded macro: %s", *data);
    expand_macros_impl((void *)host, data, MACRO_TYPE_COMMON, macro_proc_dchost , error, errlen);
//    LOG_INF("Expanded macro: %s", *data);

}

int glb_macro_expand_by_host_unmasked(char **data, const DC_HOST *host, char *error, size_t errlen) {
    zbx_dc_um_handle_t	*um_handle;
    um_handle = zbx_dc_open_user_macros_secure();

    expand_macros_impl((void *)host, data, MACRO_TYPE_COMMON, macro_proc_dchost, error, errlen);

    zbx_dc_close_user_macros(um_handle);
}

int glb_macro_expand_by_item(char **data, const DC_ITEM *item, char *error, size_t errlen) {
    expand_macros_impl((void *)item, data, MACRO_TYPE_COMMON, macro_proc_item, error, errlen);
}

int glb_macro_expand_by_item_unmasked(char **data, const DC_ITEM *item, int type, char *error, size_t errlen) {
	zbx_dc_um_handle_t	*um_handle;
   
    um_handle = zbx_dc_open_user_macros_secure();

	expand_macros_impl((void *)item, data, 0,  macro_proc_item, error, errlen);
	
	zbx_dc_close_user_macros(um_handle);
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
//static const char	*expr_macros[] = {MVAR_HOST_HOST, MVAR_HOSTNAME, MVAR_ITEM_KEY, NULL};

typedef struct
{
	char	*macro;
	char	*functions;
}
zbx_macro_functions_t;

/* macros that can be modified using macro functions */
static zbx_macro_functions_t	mod_macros[] =
{
	{"{ITEM.LASTVALUE}", "regsub,iregsub,fmtnum"},
	{"{ITEM.VALUE}", "regsub,iregsub,fmtnum"},
	{"{TIME}", "fmttime"},
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
static const char *func_macro_in_list(const char *str, zbx_token_func_macro_t *fm, int *N_functionid)
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
			//	LOG_INF("setting macro addr and sybol");
				macro_proc->macro = data + macro_proc->token.loc.l;
			//	LOG_INF("setting macro addr and symbol: %s", data);
			//	LOG_INF("setting macro addr and symbol: %s", macro_proc->macro);
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
			macro_proc->c = data[macro_proc->token.loc.r + 1];
			data[macro_proc->token.loc.r + 1] = '\0';
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
	//	LOG_INF("cannot resolve macro '%.*s'",
	//			(int)(macro_proc->token.loc.r - macro_proc->token.loc.l + 1), *data + macro_proc->token.loc.l);
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
//	LOG_INF("Pos is %d",macro_proc->pos);
}
//overall general processing logic: whenever parsing is required there should be 
//a way to pass a context of macro calculation. The context (defined by a function name) also
//determines what kind of object(s) should be passed to the macro processing
//to make the data parsable. 

//only user macroses {$MACRO} and predefined context macroses {HOST.NAME} (also functions) also indexes {HOST.NAME1} are suppoted here
//so depending on the object type macro translation proc should be able to fetch the required object data

//to help functions to get the data required predefined macroses are preprocessed so that macro id and required object id is calculated
//and this data is passed to the macro's processing functions.

static int  expand_macros_impl(void *obj_data, char **replace_data, 
            int macro_proc_type, macro_proc_func_t macro_proc_func, char *error, int maxerrlen)
{
	char				*replace_to = NULL;// sql[64];
		
	// 				     user_names_found = 0, raw_value;
	int found;
    // char				*expression = NULL, *user_username = NULL, *user_name = NULL,
	// 				*user_surname = NULL;
	macro_proc_data_t  macro_proc = {.pos = 0, .res = SUCCEED};
	macro_proc.token_search =  ZBX_TOKEN_SEARCH_BASIC | set_token_search_flags(macro_proc_type);

    if (SUCCEED == is_empty_replace_data(replace_data) || 
        SUCCEED != zbx_token_find(*replace_data, macro_proc.pos, &macro_proc.token, macro_proc.token_search))
        return SUCCEED;
	
//	LOG_INF("Replacing macro in %s", *replace_data);

	macro_proc.um_handle = zbx_dc_open_user_macros();
	macro_proc.data_alloc = macro_proc.data_len = strlen(*replace_data) + 1;
	
	for (found = SUCCEED; SUCCEED == macro_proc.res && SUCCEED == found;
			found = zbx_token_find(*replace_data, macro_proc.pos, &macro_proc.token, macro_proc.token_search))
	{
		init_iter_macro_proc(&macro_proc);
		//after token processing we've got the token type, pointers to macro boundaries
		//so depending on recognised token type either skip it or process
		
	    if (FAIL == check_token_is_processable_type(&macro_proc, *replace_data))
            continue;
	
	//	LOG_INF("Processing data %s, macro %s, token type %d, start %d, end %d, found is %d", 
	//			*replace_data,  macro_proc.macro, macro_proc.token.type, macro_proc.token.loc.l, macro_proc.token.loc.r, found);

		recognize_macro_type_and_object_type(&macro_proc);

    //    LOG_INF("Calling callback func, data is %s macro is %s", *replace_data, macro_proc.macro);
        macro_proc_func(&macro_proc, obj_data);
    //    LOG_INF("Escaping");
        macro_escape_replace_to(macro_proc_type, &macro_proc.replace_to);
    //    LOG_INF("Func calc");
        macro_calc_token_function(&macro_proc, *replace_data);
        macro_check_has_address(&macro_proc, *replace_data, error, maxerrlen);
    //    LOG_INF("Replacing result to %s", *replace_data);
		macro_replace_calc_result(&macro_proc, replace_data);
    }
	//LOG_INF("After processing: %s", *replace_data);
    //zbx_free(user_username);
	//zbx_free(user_name);
	//zbx_free(user_surname);
	//zbx_free(expression);
	//zbx_vector_uint64_destroy(&hostids);
	zbx_dc_close_user_macros(macro_proc.um_handle);
    return macro_proc.res;
}

