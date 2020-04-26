/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

#include "common.h"
#include "log.h"
#include "zbxexec.h"

#include "checks_external.h"
#include "../../libs/zbxexec/worker.h"

extern char	*CONFIG_EXTERNALSCRIPTS;

/******************************************************************************
 *                                                                            *
 * Function: get_value_external                                               *
 *                                                                            *
 * Purpose: retrieve data from script executed on Zabbix server               *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Mike Nestor, rewritten by Alexander Vladishev                      *
 *                                                                            *
 ******************************************************************************/
int	get_value_external(DC_ITEM *item, AGENT_RESULT *result)
{
	char		error[ITEM_ERROR_LEN_MAX], *cmd = NULL, *buf = NULL;
	size_t		cmd_alloc = ZBX_KIBIBYTE, cmd_offset = 0;
	int		i, ret = NOTSUPPORTED;
	AGENT_REQUEST	request;
	DC_EXT_WORKER *worker=NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key);

	init_request(&request);

	if (SUCCEED != parse_item_key(item->key, &request))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		goto out;
	}

	cmd = (char *)zbx_malloc(cmd, cmd_alloc);
	zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, "%s/%s", CONFIG_EXTERNALSCRIPTS, get_rkey(&request));
	if (-1 == access(cmd, X_OK))
	{
		zabbix_log(LOG_LEVEL_INFORMATION,"EXEC: script %s not found",cmd);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "%s: %s", cmd, zbx_strerror(errno)));
		goto out;
	}
	
	//lets try to find a worker for the cmd
	//worker=glb_get_worker_script(cmd);

	for (i = 0; i < get_rparams_num(&request); i++)
	{
		const char	*param;
		char		*param_esc;

		param = get_rparam(&request, i);

		param_esc = zbx_dyn_escape_shell_single_quote(param);
		zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, " '%s'", param_esc);
		zbx_free(param_esc);
	}
	
	zabbix_log(LOG_LEVEL_DEBUG, "EXEC: runnig external cmd %s",cmd);
	
	//if we have a worker, let's run it!
	if ( NULL != worker )  {
			zabbix_log(LOG_LEVEL_INFORMATION, "EXEC: will use WORKER for that");
			//adding trailing newline for the worker
			zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, "\n");
			
			if (SUCCEED == glb_process_worker_request(worker, cmd, &buf)) {
				zbx_rtrim(buf, ZBX_WHITESPACE);
				set_result_type(result, ITEM_VALUE_TYPE_TEXT, buf);
				ret=SUCCEED;
			} else {
				SET_MSG_RESULT(result, zbx_strdup(NULL, error));		
			}
	} 
	else 
	if (SUCCEED == zbx_execute(cmd, &buf, error, sizeof(error), CONFIG_TIMEOUT, ZBX_EXIT_CODE_CHECKS_DISABLED))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "EXEC: will use fork+execv for that");
		zbx_rtrim(buf, ZBX_WHITESPACE);
		set_result_type(result, ITEM_VALUE_TYPE_TEXT, buf);
		ret = SUCCEED;
	}
	else
		SET_MSG_RESULT(result, zbx_strdup(NULL, error));
out:
//	if (worker.workerid > 0) zbx_dc_return_ext_worker(&worker);
	
	zbx_free(buf);
	zbx_free(cmd);

	free_request(&request);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}
