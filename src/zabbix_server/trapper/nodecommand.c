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

#include "nodecommand.h"

#include "zbxserver.h"
#include "log.h"
#include "trapper_auth.h"

#include "../scripts/scripts.h"
#include "audit/zbxaudit.h"
//#include "../../libs/zbxserver/get_host_from_event.h"
#include "../../libs/zbxserver/zabbix_users.h"
#include "../../libs/glb_state/glb_state_problems.h"
#include "zbxdbwrap.h"
#include "zbx_trigger_constants.h"

/******************************************************************************
 *                                                                            *
 * Purpose: execute remote command and wait for the result                    *
 *                                                                            *
 * Return value:  SUCCEED - the remote command was executed successfully      *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 ******************************************************************************/
static int	execute_remote_script(const zbx_script_t *script, const DC_HOST *host, char **info, char *error,
		size_t max_error_len)
{
	int		time_start;
	zbx_uint64_t	taskid;
	DB_RESULT	result = NULL;
	DB_ROW		row;

	if (0 == (taskid = zbx_script_create_task(script, host, 0, time(NULL))))
	{
		zbx_snprintf(error, max_error_len, "Cannot create remote command task.");
		return FAIL;
	}

	for (time_start = time(NULL); SEC_PER_MIN > time(NULL) - time_start; sleep(1))
	{
		result = DBselect(
				"select tr.status,tr.info"
				" from task t"
				" left join task_remote_command_result tr"
					" on tr.taskid=t.taskid"
				" where tr.parent_taskid=" ZBX_FS_UI64,
				taskid);

		if (NULL != (row = DBfetch(result)))
		{
			int	ret;

			if (SUCCEED == (ret = atoi(row[0])))
				*info = zbx_strdup(*info, row[1]);
			else
				zbx_strlcpy(error, row[1], max_error_len);

			zbx_db_free_result(result);
			return ret;
		}

		zbx_db_free_result(result);
	}

	zbx_snprintf(error, max_error_len, "Timeout while waiting for remote command result.");

	return FAIL;
}

static int	zbx_get_script_details(zbx_uint64_t scriptid, zbx_script_t *script, int *scope, zbx_uint64_t *usrgrpid,
		zbx_uint64_t *groupid, char *error, size_t error_len)
{
	int		ret = FAIL;
	DB_RESULT	db_result;
	DB_ROW		row;
	zbx_uint64_t	usrgrpid_l, groupid_l;

	db_result = DBselect("select command,host_access,usrgrpid,groupid,type,execute_on,timeout,scope,port,authtype"
			",username,password,publickey,privatekey"
			" from scripts"
			" where scriptid=" ZBX_FS_UI64, scriptid);

	if (NULL == db_result)
	{
		zbx_strlcpy(error, "Cannot select from table 'scripts'.", error_len);
		return FAIL;
	}

	if (NULL == (row = DBfetch(db_result)))
	{
		zbx_strlcpy(error, "Script not found.", error_len);
		goto fail;
	}

	ZBX_DBROW2UINT64(usrgrpid_l, row[2]);
	*usrgrpid = usrgrpid_l;

	ZBX_DBROW2UINT64(groupid_l, row[3]);
	*groupid = groupid_l;

	ZBX_STR2UCHAR(script->type, row[4]);

	if (ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT == script->type)
		ZBX_STR2UCHAR(script->execute_on, row[5]);

	if (ZBX_SCRIPT_TYPE_SSH == script->type)
	{
		ZBX_STR2UCHAR(script->authtype, row[9]);
		script->publickey = zbx_strdup(script->publickey, row[12]);
		script->privatekey = zbx_strdup(script->privatekey, row[13]);
	}

	if (ZBX_SCRIPT_TYPE_SSH == script->type || ZBX_SCRIPT_TYPE_TELNET == script->type)
	{
		script->port = zbx_strdup(script->port, row[8]);
		script->username = zbx_strdup(script->username, row[10]);
		script->password = zbx_strdup(script->password, row[11]);
	}

	script->command = zbx_strdup(script->command, row[0]);
	script->command_orig = zbx_strdup(script->command_orig, row[0]);

	script->scriptid = scriptid;

	ZBX_STR2UCHAR(script->host_access, row[1]);

	if (SUCCEED != zbx_is_time_suffix(row[6], &script->timeout, ZBX_LENGTH_UNLIMITED))
	{
		zbx_strlcpy(error, "Invalid timeout value in script configuration.", error_len);
		goto fail;
	}

	*scope = atoi(row[7]);

	ret = SUCCEED;
fail:
	zbx_db_free_result(db_result);

	return ret;
}

static int	is_user_in_allowed_group(zbx_uint64_t userid, zbx_uint64_t usrgrpid, char *error, size_t error_len)
{
	DB_RESULT	result;
	int		ret = FAIL;

	result = DBselect("select null"
			" from users_groups"
			" where usrgrpid=" ZBX_FS_UI64
			" and userid=" ZBX_FS_UI64,
			usrgrpid, userid);

	if (NULL == result)
	{
		zbx_strlcpy(error, "Database error, cannot get user rights.", error_len);
		goto fail;
	}

	if (NULL == DBfetch(result))
		zbx_strlcpy(error, "User has no rights to execute this script.", error_len);
	else
		ret = SUCCEED;

	zbx_db_free_result(result);
fail:
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Purpose: executing command                                                 *
 *                                                                            *
 * Parameters:  scriptid       - [IN] the id of a script to be executed       *
 *              hostid         - [IN] the host the script will be executed on *
 *              eventid        - [IN] the id of an event                      *
 *              user           - [IN] the user who executes the command       *
 *              clientip       - [IN] the IP of client                        *
 *              config_timeout - [IN]                                         *
 *              result         - [OUT] the result of a script execution       *
 *              debug          - [OUT] the debug data (optional)              *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Comments: either 'hostid' or 'eventid' must be > 0, but not both           *
 *                                                                            *
 ******************************************************************************/
static int	execute_script(zbx_uint64_t scriptid, zbx_uint64_t hostid, zbx_user_t *user,
		const char *clientip, int config_timeout, char **result, char **debug)
{
	int			ret = FAIL, scope = 0, i;
	DC_HOST			host = {0};
	zbx_script_t		script;
	zbx_uint64_t		usrgrpid, groupid;
	zbx_vector_ptr_pair_t	webhook_params;
	char			*user_timezone = NULL, *webhook_params_json = NULL, error[MAX_STRING_LEN];
	zbx_dc_um_handle_t	*um_handle = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() scriptid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64 
			" userid:" ZBX_FS_UI64 " clientip:%s",
			__func__, scriptid, hostid, user->userid, clientip);

	*error = '\0';

	zbx_vector_ptr_pair_create(&webhook_params);
	zbx_script_init(&script);

	if (SUCCEED != zbx_get_script_details(scriptid, &script, &scope, &usrgrpid, &groupid, error, sizeof(error)))
		goto fail;

	/* validate script permissions */
	if (0 < usrgrpid &&
			USER_TYPE_SUPER_ADMIN != user->type &&
			SUCCEED != is_user_in_allowed_group(user->userid, usrgrpid, error, sizeof(error)))
	{
		goto fail;
	}

	if ( 0 == hostid || SUCCEED != DCget_host_by_hostid(&host, hostid))
	{
		zbx_snprintf(error, sizeof(error), "Unknown host identifier hostid:%lld",hostid);
		goto fail;
	}
	
	if (SUCCEED != zbx_check_script_permissions(groupid, host.hostid))
	{
		zbx_strlcpy(error, "Script does not have permission to be executed on the host.", sizeof(error));
		goto fail;
	}

	if (USER_TYPE_SUPER_ADMIN != user->type &&
			SUCCEED != zbx_check_script_user_permissions(user->userid, host.hostid, &script))
	{
		zbx_strlcpy(error, "User does not have permission to execute this script on the host.", sizeof(error));
		goto fail;
	}

	user_timezone = get_user_timezone(user->userid);

	/* substitute macros in script body and webhook parameters */

//	if (0 != hostid)	/* script on host */
//		macro_type = MACRO_TYPE_SCRIPT;
	
	um_handle = zbx_dc_open_user_macros();

	 if ( ZBX_SCRIPT_TYPE_WEBHOOK == script.type) {
	 	if (SUCCEED != DBfetch_webhook_params(script.scriptid, &webhook_params, error, sizeof(error)))
			goto fail;

		zbx_webhook_params_pack_json(&webhook_params, &webhook_params_json);
	}

	if (SUCCEED == (ret = zbx_script_prepare(&script, &host.hostid, error, sizeof(error))))
	{
		const char	*poutput = NULL, *perror = NULL;
		int		audit_res;

		if (0 == host.proxy_hostid || ZBX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on ||
				ZBX_SCRIPT_TYPE_WEBHOOK == script.type)
		{
			ret = zbx_script_execute(&script, &host, webhook_params_json, config_timeout, result, error,
					sizeof(error), debug);
		}
		else
			ret = execute_remote_script(&script, &host, result, error, sizeof(error));

		if (SUCCEED == ret)
			poutput = *result;
		else
			perror = error;

		audit_res = zbx_auditlog_global_script(script.type, script.execute_on, script.command_orig, host.hostid,
				host.name, 0, host.proxy_hostid, user->userid, user->username, clientip, poutput,
				perror);

		/* At the moment, there is no special processing of audit failures. */
		/* It can fail only due to the DB errors and those are visible in   */
		/* the log anyway */
		ZBX_UNUSED(audit_res);
	}
fail:
	if (NULL != um_handle)
		zbx_dc_close_user_macros(um_handle);

	if (SUCCEED != ret)
		*result = zbx_strdup(*result, error);

	zbx_script_clean(&script);
	zbx_free(webhook_params_json);
	zbx_free(user_timezone);

	for (i = 0; i < webhook_params.values_num; i++)
	{
		zbx_free(webhook_params.values[i].first);
		zbx_free(webhook_params.values[i].second);
	}

	zbx_vector_ptr_pair_destroy(&webhook_params);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: process command received from the frontend                        *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	node_process_command(zbx_socket_t *sock, const char *data, const struct zbx_json_parse *jp, int config_timeout)
{
	char			*result = NULL, *send = NULL, *debug = NULL, tmp[64], tmp_hostid[64],
				clientip[MAX_STRING_LEN];
	int			ret = FAIL, got_hostid = 0;
	zbx_uint64_t		scriptid, hostid = 0, eventid = 0;
	struct zbx_json		j;
	zbx_user_t		user;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s(): data:%s ", __func__, data);

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
	zbx_user_init(&user);

	/* check who is connecting, get user details, check access rights */

	if (FAIL == zbx_get_user_from_json(jp, &user, &result))
		goto finish;

	if (SUCCEED != check_perm2system(user.userid))
	{
		result = zbx_strdup(result, "Permission denied. User is a member of group with disabled access.");
		goto finish;
	}
#define ZBX_USER_ROLE_PERMISSION_ACTIONS_DEFAULT_ACCESS		"actions.default_access"
#define ZBX_USER_ROLE_PERMISSION_ACTIONS_EXECUTE_SCRIPTS	"actions.execute_scripts"
	if (SUCCEED != zbx_check_user_administration_actions_permissions(&user,
			ZBX_USER_ROLE_PERMISSION_ACTIONS_DEFAULT_ACCESS,
			ZBX_USER_ROLE_PERMISSION_ACTIONS_EXECUTE_SCRIPTS))
	{
		result = zbx_strdup(result, "Permission denied. No role access.");
		goto finish;
	}
#undef ZBX_USER_ROLE_PERMISSION_ACTIONS_DEFAULT_ACCESS
#undef ZBX_USER_ROLE_PERMISSION_ACTIONS_EXECUTE_SCRIPTS
	/* extract and validate other JSON elements */

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_SCRIPTID, tmp, sizeof(tmp), NULL) ||
			FAIL == zbx_is_uint64(tmp, &scriptid))
	{
		result = zbx_dsprintf(result, "Failed to parse command request tag: %s.", ZBX_PROTO_TAG_SCRIPTID);
		goto finish;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOSTID, tmp_hostid, sizeof(tmp_hostid), NULL)) {
		got_hostid = 1;

		result = zbx_dsprintf(result, "Failed to parse command request tag %s or %s.",
				ZBX_PROTO_TAG_HOSTID, ZBX_PROTO_TAG_EVENTID);
		goto finish;
	}

	if (SUCCEED != zbx_is_uint64(tmp_hostid, &hostid))
	{
		result = zbx_dsprintf(result, "Failed to parse value of command request tag %s.",
				ZBX_PROTO_TAG_HOSTID);
			goto finish;
	}

	if (0 == hostid)
	{
		result = zbx_dsprintf(result, "%s value cannot be 0.", ZBX_PROTO_TAG_HOSTID);
		goto finish;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_CLIENTIP, clientip, sizeof(clientip), NULL))
		*clientip = '\0';

	if (SUCCEED == (ret = execute_script(scriptid, hostid,  &user, clientip, config_timeout, &result,
			&debug)))
	{
		zbx_json_addstring(&j, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_SUCCESS, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_PROTO_TAG_DATA, result, ZBX_JSON_TYPE_STRING);

		if (NULL != debug)
			zbx_json_addraw(&j, "debug", debug);

		send = j.buffer;
	}
finish:
	if (SUCCEED != ret)
	{
		zbx_json_addstring(&j, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_FAILED, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_PROTO_TAG_INFO, (NULL != result ? result : "Unknown error."),
				ZBX_JSON_TYPE_STRING);
		send = j.buffer;
	}

	zbx_alarm_on(config_timeout);
	if (SUCCEED != zbx_tcp_send(sock, send))
		zabbix_log(LOG_LEVEL_WARNING, "Error sending result of command");
	else
		zabbix_log(LOG_LEVEL_DEBUG, "Sending back command '%s' result '%s'", data, send);
	zbx_alarm_off();

	zbx_json_free(&j);
	zbx_free(result);
	zbx_free(debug);
	zbx_user_free(&user);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}
