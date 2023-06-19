/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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

#include "operations.h"
#include "zbxcommon.h"


static void	execute_commands(u_int64_t problemid, const DB_ACKNOWLEDGE *ack,
		const zbx_service_alarm_t *service_alarm, const ZBX_DB_SERVICE *service, zbx_uint64_t actionid,
		zbx_uint64_t operationid, int esc_step, int macro_type, const char *default_timezone,
		int config_timeout)
{
	DB_RESULT		result;
	DB_ROW			row;
	zbx_db_insert_t		db_insert;
	int			alerts_num = 0;
	char			*buffer = NULL;
	size_t			buffer_alloc = 2 * ZBX_KIBIBYTE, buffer_offset = 0;
	zbx_vector_uint64_t	executed_on_hosts, groupids;
	zbx_dc_um_handle_t	*um_handle;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	buffer = (char *)zbx_malloc(buffer, buffer_alloc);

	/* get hosts operation's hosts */

	zbx_vector_uint64_create(&groupids);
	get_operation_groupids(operationid, &groupids);

	if (0 != groupids.values_num)
	{
		zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				/* the 1st 'select' works if remote command target is "Host group" */
				"select h.hostid,h.proxy_hostid,h.host,s.type,s.scriptid,s.execute_on,s.port"
					",s.authtype,s.username,s.password,s.publickey,s.privatekey,s.command,s.groupid"
					",s.scope,s.timeout,s.name,h.tls_connect"
#ifdef HAVE_OPENIPMI
				/* do not forget to update ZBX_IPMI_FIELDS_NUM if number of selected IPMI fields changes */
				",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
				);

		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,hosts_groups hg,hosts h,scripts s"
				" where o.operationid=" ZBX_FS_UI64
					" and o.scriptid=s.scriptid"
					" and hg.hostid=h.hostid"
					" and h.status=%d"
					" and",
				operationid, HOST_STATUS_MONITORED);

		DBadd_condition_alloc(&buffer, &buffer_alloc, &buffer_offset, "hg.groupid", groupids.values,
				groupids.values_num);

		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, " union all ");
	}

	zbx_vector_uint64_destroy(&groupids);

	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
			/* the 2nd 'select' works if remote command target is "Host" */
			"select h.hostid,h.proxy_hostid,h.host,s.type,s.scriptid,s.execute_on,s.port"
				",s.authtype,s.username,s.password,s.publickey,s.privatekey,s.command,s.groupid"
				",s.scope,s.timeout,s.name,h.tls_connect"
#ifdef HAVE_OPENIPMI
			",h.ipmi_authtype,h.ipmi_privilege,h.ipmi_username,h.ipmi_password"
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			",h.tls_issuer,h.tls_subject,h.tls_psk_identity,h.tls_psk"
#endif
			);
	zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
			" from opcommand o,opcommand_hst oh,hosts h,scripts s"
			" where o.operationid=oh.operationid"
				" and o.scriptid=s.scriptid"
				" and oh.hostid=h.hostid"
				" and o.operationid=" ZBX_FS_UI64
				" and h.status=%d"
			" union all "
			/* the 3rd 'select' works if remote command target is "Current host" */
			"select 0,0,null,s.type,s.scriptid,s.execute_on,s.port"
				",s.authtype,s.username,s.password,s.publickey,s.privatekey,s.command,s.groupid"
				",s.scope,s.timeout,s.name,%d",
			operationid, HOST_STATUS_MONITORED, ZBX_TCP_SEC_UNENCRYPTED);
#ifdef HAVE_OPENIPMI
	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",0,2,null,null");
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset,
				",null,null,null,null");
#endif
	if (EVENT_SOURCE_SERVICE == event->source)
	{
		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,scripts s"
				" where o.scriptid=s.scriptid"
					" and o.operationid=" ZBX_FS_UI64,
				operationid);
	}
	else
	{
		zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset,
				" from opcommand o,opcommand_hst oh,scripts s"
				" where o.operationid=oh.operationid"
					" and o.scriptid=s.scriptid"
					" and o.operationid=" ZBX_FS_UI64
					" and oh.hostid is null",
				operationid);
	}

	result = DBselect("%s", buffer);

	zbx_free(buffer);
	zbx_vector_uint64_create(&executed_on_hosts);

	um_handle = zbx_dc_open_user_macros();

//this first part should be entirely replaced by fetched from the mem configuration



//fetched something to the buffer (most likely, it's the host info and operations data
//which is already in the problem/cache in our case

	while (NULL != (row = DBfetch(result)))
	{
		int			scope, i, rc = SUCCEED;
		DC_HOST			host;
		zbx_script_t		script;
		zbx_alert_status_t	status = ALERT_STATUS_NOT_SENT;
		zbx_uint64_t		alertid, groupid;
		char			*webhook_params_json = NULL, *script_name = NULL;
		zbx_vector_ptr_pair_t	webhook_params;
		char			error[ALERT_ERROR_LEN_MAX];

		*error = '\0';
		memset(&host, 0, sizeof(host));
		zbx_script_init(&script);
		zbx_vector_ptr_pair_create(&webhook_params);

		/* fill 'script' elements */
        //based on db fetched data, this should be done form operations ?
		ZBX_STR2UCHAR(script.type, row[3]);

		if (ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT == script.type)
			ZBX_STR2UCHAR(script.execute_on, row[5]);

		if (ZBX_SCRIPT_TYPE_SSH == script.type)
		{
			ZBX_STR2UCHAR(script.authtype, row[7]);
			script.publickey = zbx_strdup(script.publickey, row[10]);
			script.privatekey = zbx_strdup(script.privatekey, row[11]);
		}

		if (ZBX_SCRIPT_TYPE_SSH == script.type || ZBX_SCRIPT_TYPE_TELNET == script.type)
		{
			script.port = zbx_strdup(script.port, row[6]);
			script.username = zbx_strdup(script.username, row[8]);
			script.password = zbx_strdup(script.password, row[9]);
		}

		script.command = zbx_strdup(script.command, row[12]);
		script.command_orig = zbx_strdup(script.command_orig, row[12]);

		ZBX_DBROW2UINT64(script.scriptid, row[4]);

		if (SUCCEED != zbx_is_time_suffix(row[15], &script.timeout, ZBX_LENGTH_UNLIMITED))
		{
			zbx_strlcpy(error, "Invalid timeout value in script configuration.", sizeof(error));
			rc = FAIL;
			goto fail;
		}

		script_name = row[16];

		/* validate script permissions */

		scope = atoi(row[14]);
		ZBX_DBROW2UINT64(groupid, row[13]);

		ZBX_STR2UINT64(host.hostid, row[0]);
		ZBX_DBROW2UINT64(host.proxy_hostid, row[1]);

		if (ZBX_SCRIPT_SCOPE_ACTION != scope)
		{
			zbx_snprintf(error, sizeof(error), "Script is not allowed in action operations: scope:%d",
					scope);
			rc = FAIL;
			goto fail;
		}

		if (EVENT_SOURCE_SERVICE == event->source)
		{
			/* service event cannot have target, force execution on Zabbix server */
			script.execute_on = ZBX_SCRIPT_EXECUTE_ON_SERVER;
			zbx_strscpy(host.host, "Zabbix server");
		}
		else
		{
			/* get host details */

			if (0 == host.hostid)
			{
				/* target is "Current host" */
				if (SUCCEED != (rc = get_host_from_event((NULL != r_event ? r_event : event), &host, error,
						sizeof(error))))
				{
					goto fail;
				}
			}

			if (FAIL != zbx_vector_uint64_search(&executed_on_hosts, host.hostid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				goto skip;

			zbx_vector_uint64_append(&executed_on_hosts, host.hostid);

			if (0 < groupid && SUCCEED != zbx_check_script_permissions(groupid, host.hostid))
			{
				zbx_strlcpy(error, "Script does not have permission to be executed on the host.",
						sizeof(error));
				rc = FAIL;
				goto fail;
			}


			if ('\0' == *host.host)
			{
				/* target is from "Host" list or "Host group" list */

				zbx_strscpy(host.host, row[2]);
				host.tls_connect = (unsigned char)atoi(row[17]);
#ifdef HAVE_OPENIPMI
				host.ipmi_authtype = (signed char)atoi(row[18]);
				host.ipmi_privilege = (unsigned char)atoi(row[19]);
				zbx_strscpy(host.ipmi_username, row[20]);
				zbx_strscpy(host.ipmi_password, row[21]);
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				zbx_strscpy(host.tls_issuer, row[18 + ZBX_IPMI_FIELDS_NUM]);
				zbx_strscpy(host.tls_subject, row[19 + ZBX_IPMI_FIELDS_NUM]);
				zbx_strscpy(host.tls_psk_identity, row[20 + ZBX_IPMI_FIELDS_NUM]);
				zbx_strscpy(host.tls_psk, row[21 + ZBX_IPMI_FIELDS_NUM]);
#endif
			}
		}

		/* substitute macros in script body and webhook parameters */

		if (ZBX_SCRIPT_TYPE_WEBHOOK != script.type)
		{
			if (SUCCEED != zbx_substitute_simple_macros_unmasked(&actionid, event, r_event, NULL, NULL, &host,
					NULL, NULL, ack, service_alarm, service, default_timezone, &script.command,
					macro_type, error,
					sizeof(error)))
			{
				rc = FAIL;
				goto fail;
			}

			/* expand macros in command_orig used for non-secure logging */
			if (SUCCEED != zbx_substitute_simple_macros(&actionid, event, r_event, NULL, NULL, &host,
					NULL, NULL, ack, service_alarm, service, default_timezone, &script.command_orig,
					macro_type, error, sizeof(error)))
			{
				/* script command_orig is a copy of script command - if the script command  */
				/* macro substitution succeeded, then it will succeed also for command_orig */
				THIS_SHOULD_NEVER_HAPPEN;
				rc = FAIL;
				goto fail;
			}
		}
		else
		{
			if (SUCCEED != DBfetch_webhook_params(script.scriptid, &webhook_params, error, sizeof(error)))
			{
				rc = FAIL;
				goto fail;
			}

			for (i = 0; i < webhook_params.values_num; i++)
			{
				if (SUCCEED != zbx_substitute_simple_macros_unmasked(&actionid, event, r_event, NULL,
						NULL, &host, NULL, NULL, ack, service_alarm, service, default_timezone,
						(char **)&webhook_params.values[i].second, macro_type, error,
						sizeof(error)))
				{
					rc = FAIL;
					goto fail;
				}
			}

			zbx_webhook_params_pack_json(&webhook_params, &webhook_params_json);
		}
fail:
		alertid = DBget_maxid("alerts");

		if (SUCCEED == rc)
		{
			if (SUCCEED == (rc = zbx_script_prepare(&script, &host.hostid, error, sizeof(error))))
			{
				if (0 == host.proxy_hostid || ZBX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on ||
						ZBX_SCRIPT_TYPE_WEBHOOK == script.type)
				{
					rc = zbx_script_execute(&script, &host, webhook_params_json, config_timeout,
							NULL, error, sizeof(error), NULL);
					status = ALERT_STATUS_SENT;
				}
				else
				{
					if (0 == zbx_script_create_task(&script, &host, alertid, time(NULL)))
						rc = FAIL;
				}
			}
		}

		if (SUCCEED != rc)
			status = ALERT_STATUS_FAILED;

		add_command_alert(&db_insert, alerts_num++, alertid, host.host, event, r_event, actionid,
				esc_step, (ZBX_SCRIPT_TYPE_WEBHOOK == script.type) ? script_name : script.command_orig,
				status, error);
skip:
		zbx_free(webhook_params_json);

		for (i = 0; i < webhook_params.values_num; i++)
		{
			zbx_free(webhook_params.values[i].first);
			zbx_free(webhook_params.values[i].second);
		}

		zbx_vector_ptr_pair_destroy(&webhook_params);
		zbx_script_clean(&script);
	}
	zbx_db_free_result(result);
	zbx_vector_uint64_destroy(&executed_on_hosts);

	zbx_dc_close_user_macros(um_handle);

	if (0 < alerts_num)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

