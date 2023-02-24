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

// zbx_token_search_t set_token_search_flags(int macro_type) {
// 	int flag = 0;

// 	if (0 != (macro_type & (MACRO_TYPE_TRIGGER_DESCRIPTION | MACRO_TYPE_EVENT_NAME)))
// 		flag |= ZBX_TOKEN_SEARCH_REFERENCES;

// 	if (0 != (macro_type & (MACRO_TYPE_MESSAGE_NORMAL | MACRO_TYPE_MESSAGE_RECOVERY | MACRO_TYPE_MESSAGE_UPDATE |
// 			MACRO_TYPE_EVENT_NAME)))
// 	{
// 		flag |= ZBX_TOKEN_SEARCH_EXPRESSION_MACRO;
// 	}
	
// 	return flag;
// }

// int process_macro(char *macro) {
// 	return SUCCEED;
// }

// static add_non_macro_data(char *res, char *data, size_t start, size_t bytes) {
// 	if ( bytes > 0 ) 
// 		zbx_strlcpy(res, data+start, bytes + 1);
// }

/*translates the string given the set of processing procs and data for the macro*/
int glb_macro_translate_string(const char *expression, int token_type, char *result, int result_size) {
// 	zbx_token_t token = {0};                                        
// 	int prev_token_end = 0;
//    	static char res[MAX_STRING_LEN];
// 	LOG_INF("Parsing macroses in: '%s'", expression);

//    	while (SUCCEED == zbx_token_find(expression, token.loc.r + 1, &token, ZBX_TOKEN_SEARCH_EXPRESSION_MACRO| ZBX_TOKEN_SEARCH_BASIC )) {
// 		//zbx_snprintf(res, MAX_STRING_LEN, "%s")	
// 		add_non_macro_data(res, expression,  expression + prev_token_end, token.loc.l - prev_token_end );

// 		if ( token.loc.l-prev_token_end > 0 ) { //copy non-macro characters to result
// 			zbx_strlcpy(res, expression + prev_token_end, token.loc.l - prev_token_end + 1);
// 			LOG_INF("Got space chars: '%s', %d chars", res, token.loc.l - prev_token_end);
// 		}
		
// 		LOG_INF("Found token %s (%d, %d)", expression + token.loc.l, token.loc.l, token.loc.r);
// 		prev_token_end = token.loc.r + 1;
// 	}
// 	add_non_macro_data(res, )
//   //    zbx_snprintf(result, )
//   //      process_token(expression, &token);           
    HALT_HERE("Not complete yet");
}