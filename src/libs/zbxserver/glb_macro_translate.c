

glb_translate_event_name_macros() {

	if (0 != (macro_type & (MACRO_TYPE_TRIGGER_DESCRIPTION | MACRO_TYPE_EVENT_NAME)))
		token_search |= ZBX_TOKEN_SEARCH_REFERENCES;

	if (0 != (macro_type & (MACRO_TYPE_MESSAGE_NORMAL | MACRO_TYPE_MESSAGE_RECOVERY | MACRO_TYPE_MESSAGE_UPDATE |
			MACRO_TYPE_EVENT_NAME)))
	{
		token_search |= ZBX_TOKEN_SEARCH_EXPRESSION_MACRO;
	}

    if (SUCCEED != zbx_token_find(*data, pos, &token, token_search))
		goto out;

	um_handle = zbx_dc_open_user_macros();
	zbx_vector_uint64_create(&hostids);

	data_alloc = data_len = strlen(*data) + 1;

	for (found = SUCCEED; SUCCEED == res && SUCCEED == found;
			found = zbx_token_find(*data, pos, &token, token_search))
	{
       	indexed_macro = 0;
		require_address = 0;
		N_functionid = 1;
		raw_value = 0;
		pos = token.loc.l;
		inner_token = token;

    }


		switch (token.type)
		{
			case ZBX_TOKEN_OBJECTID:
			case ZBX_TOKEN_LLD_MACRO:
			case ZBX_TOKEN_LLD_FUNC_MACRO:
				/* neither lld nor {123123} macros are processed by this function, skip them */
				pos = token.loc.r + 1;
				continue;
			case ZBX_TOKEN_MACRO:
				if (0 != is_indexed_macro(*data, &token) &&
						NULL != (m = macro_in_list(*data, token.loc, ex_macros, &N_functionid)))
				{
					indexed_macro = 1;
				}
				else
				{
					m = *data + token.loc.l;
					c = (*data)[token.loc.r + 1];
					(*data)[token.loc.r + 1] = '\0';
				}
				break;
			case ZBX_TOKEN_FUNC_MACRO:
				raw_value = 1;
				indexed_macro = is_indexed_macro(*data, &token);
				if (NULL == (m = func_macro_in_list(*data, &token.data.func_macro, &N_functionid)) ||
						SUCCEED != zbx_token_find(*data, token.data.func_macro.macro.l,
								&inner_token, token_search))
				{
					/* Ignore functions with macros not supporting them, but do not skip the */
					/* whole token, nested macro should be resolved in this case. */
					pos++;
					continue;
				}
				break;
			case ZBX_TOKEN_USER_MACRO:
				/* To avoid *data modification user macro resolver should be replaced with a function */
				/* that takes initial *data string and token.data.user_macro instead of m as params.  */
				m = *data + token.loc.l;
				c = (*data)[token.loc.r + 1];
				(*data)[token.loc.r + 1] = '\0';
				break;
			case ZBX_TOKEN_REFERENCE:
			case ZBX_TOKEN_EXPRESSION_MACRO:
				/* These macros (and probably all other in the future) must be resolved using only */
				/* information stored in token.data union. For now, force crash if they rely on m. */
				m = NULL;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				res = FAIL;
				continue;
		}



        
}