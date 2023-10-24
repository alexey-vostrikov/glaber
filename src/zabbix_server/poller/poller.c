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

#include "poller.h"
#include "zbxserver.h"

#include "checks_agent.h"
#include "checks_external.h"
#include "checks_internal.h"
#include "checks_script.h"
#include "checks_simple.h"
#include "checks_snmp.h"
#include "checks_db.h"
#include "checks_ssh.h"
#include "checks_telnet.h"
#include "checks_java.h"
#include "checks_calculated.h"
#include "checks_http.h"

#include "zbxnix.h"
#include "zbxself.h"
#include "preproc.h"
#include "zbxrtc.h"
#include "zbxcrypto.h"
#include "zbxjson.h"
#include "zbxhttp.h"
#include "log.h"
#include "zbxcomms.h"
#include "zbxnum.h"
#include "zbxtime.h"
#include "zbxsysinfo.h"
#include "zbx_rtc_constants.h"
#include "zbx_item_constants.h"
#include "../../libs/glb_state/glb_state_hosts.h"
#include "../../libs/glb_state/glb_state_items.h"
#include "glb_preproc.h"

static const char	*item_type_agent_string(zbx_item_type_t item_type)
{
	switch (item_type)
	{
		case ITEM_TYPE_ZABBIX:
			return "Zabbix agent";
		case ITEM_TYPE_SNMP:
			return "SNMP agent";
		case ITEM_TYPE_IPMI:
			return "IPMI agent";
		case ITEM_TYPE_JMX:
			return "JMX agent";
		default:
			return "generic";
	}
}

void	zbx_free_agent_result_ptr(AGENT_RESULT *result)
{
	zbx_free_agent_result(result);
	zbx_free(result);
}

static int	get_value(DC_ITEM *item, AGENT_RESULT *result, zbx_vector_ptr_t *add_results,
		const zbx_config_comms_args_t *config_comms, int config_startup_time)
{
	int	res = FAIL;
	
	DEBUG_ITEM(item->itemid,"Fetching item in sync way");

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key_orig);

	switch (item->type)
	{
		case ITEM_TYPE_ZABBIX:
			zbx_alarm_on(config_comms->config_timeout);
			res = get_value_agent(item, result);
			zbx_alarm_off();
			break;
		case ITEM_TYPE_SIMPLE:
			/* simple checks use their own timeouts */
			res = get_value_simple(item, result, add_results, glb_state_items_get_lastlogsize(item->itemid));
			break;
		case ITEM_TYPE_INTERNAL:
			res = get_value_internal(item, result, config_comms, config_startup_time);
			DEBUG_ITEM(item->itemid, "Got item result '%s' code is %d", result->str, res);
			break;
		case ITEM_TYPE_DB_MONITOR:
#ifdef HAVE_UNIXODBC
			res = get_value_db(item, config_comms->config_timeout, result);
#else
			SET_MSG_RESULT(result,
					zbx_strdup(NULL, "Support for Database monitor checks was not compiled in."));
			DEBUG_ITEM(item->itemid,"Setting NO DB SUPPORT COMPILED IN");
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_EXTERNAL:
			/* external checks use their own timeouts */
			res = get_value_external(item, config_comms->config_timeout, result);
			break;
		case ITEM_TYPE_SSH:
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
			zbx_alarm_on(config_comms->config_timeout);
			res = get_value_ssh(item, result);
			zbx_alarm_off();
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for SSH checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_TELNET:
			zbx_alarm_on(config_comms->config_timeout);
			res = get_value_telnet(item, result);
			zbx_alarm_off();
			break;
		case ITEM_TYPE_CALCULATED:
			res = get_value_calculated(item, result);
			break;
		case ITEM_TYPE_HTTPAGENT:
#ifdef HAVE_LIBCURL
			res = get_value_http(item, result);
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for HTTP agent checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_SCRIPT:
			res = get_value_script(item, result);
			break;
		default:
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported item type:%d", item->type));
			res = CONFIG_ERROR;
	}

	if (SUCCEED != res)
	{
		if (!ZBX_ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, ZBX_NOTSUPPORTED_MSG));

		zabbix_log(LOG_LEVEL_DEBUG, "Item [%s:%s] error: %s", item->host.host, item->key_orig, result->msg);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(res));
	DEBUG_ITEM(item->itemid,"Fetching item finished");
	
	return res;
}

static int	parse_query_fields(const DC_ITEM *item, char **query_fields, unsigned char expand_macros)
{
	struct zbx_json_parse	jp_array, jp_object;
	char			name[MAX_STRING_LEN], value[MAX_STRING_LEN], *str = NULL;
	const char		*member, *element = NULL;
	size_t			alloc_len, offset;

	if ('\0' == **query_fields)
		return SUCCEED;

	if (SUCCEED != zbx_json_open(*query_fields, &jp_array))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", zbx_json_strerror());
		return FAIL;
	}

	if (NULL == (element = zbx_json_next(&jp_array, element)))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: array is empty");
		return FAIL;
	}

	do
	{
		char	*data = NULL;

		if (SUCCEED != zbx_json_brackets_open(element, &jp_object) ||
				NULL == (member = zbx_json_pair_next(&jp_object, NULL, name, sizeof(name))) ||
				NULL == zbx_json_decodevalue(member, value, sizeof(value), NULL))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot parse query fields: %s", zbx_json_strerror());
			zbx_free(str);
			return FAIL;
		}

		if (NULL == str && NULL == strchr(item->url, '?'))
			zbx_chrcpy_alloc(&str, &alloc_len, &offset, '?');
		else
			zbx_chrcpy_alloc(&str, &alloc_len, &offset, '&');

		data = zbx_strdup(data, name);
		if (MACRO_EXPAND_YES == expand_macros)
		{
			zbx_substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, NULL, NULL,
					NULL, &data, MACRO_TYPE_HTTP_RAW, NULL, 0);
		}
		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(&str, &alloc_len, &offset, data);
		zbx_chrcpy_alloc(&str, &alloc_len, &offset, '=');

		data = zbx_strdup(data, value);
		if (MACRO_EXPAND_YES == expand_macros)
		{
			zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL,
					NULL, NULL, NULL, &data, MACRO_TYPE_HTTP_RAW, NULL, 0);
		}

		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(&str, &alloc_len, &offset, data);

		free(data);
	}
	while (NULL != (element = zbx_json_next(&jp_array, element)));

	zbx_free(*query_fields);
	*query_fields = str;

	return SUCCEED;
}

void	zbx_prepare_items(DC_ITEM *items, int *errcodes, int num, AGENT_RESULT *results, unsigned char expand_macros)
{
	int			i;
	char			*port = NULL, error[ZBX_ITEM_ERROR_LEN_MAX];
	zbx_dc_um_handle_t	*um_handle;
	
	if (MACRO_EXPAND_YES == expand_macros)
		um_handle = zbx_dc_open_user_macros();
	
	for (i = 0; i < num; i++)
	{
		zbx_init_agent_result(&results[i]);
		errcodes[i] = SUCCEED;

		if (MACRO_EXPAND_YES == expand_macros)
		{
			ZBX_STRDUP(items[i].key, items[i].key_orig);
			if (SUCCEED != zbx_substitute_key_macros_unmasked(&items[i].key, NULL, &items[i], NULL, NULL,
					MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
			{
				SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
				errcodes[i] = CONFIG_ERROR;
				continue;
			}
		}

		switch (items[i].type)
		{
			case ITEM_TYPE_ZABBIX:
			case ITEM_TYPE_SNMP:
			case ITEM_TYPE_JMX:
				ZBX_STRDUP(port, items[i].interface.port_orig);
				if (MACRO_EXPAND_YES == expand_macros)
				{
					zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
							NULL, NULL, NULL, NULL, NULL, NULL, &port, MACRO_TYPE_COMMON,
							NULL, 0);
				}

				if (FAIL == zbx_is_ushort(port, &items[i].interface.port))
				{
					SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "Invalid port number [%s]",
								items[i].interface.port_orig));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
		}

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMP:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				if (ZBX_IF_SNMP_VERSION_3 == items[i].snmp_version)
				{
					ZBX_STRDUP(items[i].snmpv3_securityname, items[i].snmpv3_securityname_orig);
					ZBX_STRDUP(items[i].snmpv3_authpassphrase, items[i].snmpv3_authpassphrase_orig);
					ZBX_STRDUP(items[i].snmpv3_privpassphrase, items[i].snmpv3_privpassphrase_orig);
					ZBX_STRDUP(items[i].snmpv3_contextname, items[i].snmpv3_contextname_orig);

					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid,
							NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							&items[i].snmpv3_securityname, MACRO_TYPE_COMMON, NULL,
							0);
					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid,
							NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							&items[i].snmpv3_authpassphrase, MACRO_TYPE_COMMON,
							NULL, 0);
					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid,
							NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							&items[i].snmpv3_privpassphrase, MACRO_TYPE_COMMON,
							NULL, 0);
					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid,
							NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							&items[i].snmpv3_contextname, MACRO_TYPE_COMMON, NULL,
							0);
				}

				ZBX_STRDUP(items[i].snmp_community, items[i].snmp_community_orig);
				ZBX_STRDUP(items[i].snmp_oid, items[i].snmp_oid_orig);

				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].snmp_community,
						MACRO_TYPE_COMMON, NULL, 0);
				if (SUCCEED != zbx_substitute_key_macros(&items[i].snmp_oid, &items[i].host.hostid,
						NULL, NULL, NULL, MACRO_TYPE_SNMP_OID, error, sizeof(error)))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
			case ITEM_TYPE_SCRIPT:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				ZBX_STRDUP(items[i].timeout, items[i].timeout_orig);

				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, NULL, NULL, NULL, &items[i].timeout, MACRO_TYPE_COMMON,
						NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL, &items[i], NULL,
						NULL, NULL, NULL, NULL, &items[i].script_params,
						MACRO_TYPE_SCRIPT_PARAMS_FIELD, NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].params, MACRO_TYPE_COMMON,
						NULL, 0);
				break;
			case ITEM_TYPE_SSH:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				ZBX_STRDUP(items[i].publickey, items[i].publickey_orig);
				ZBX_STRDUP(items[i].privatekey, items[i].privatekey_orig);

				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, NULL, NULL, NULL, &items[i].publickey, MACRO_TYPE_COMMON,
						NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, NULL, NULL, NULL, &items[i].privatekey, MACRO_TYPE_COMMON, NULL, 0);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL, &items[i], NULL,
						NULL, NULL, NULL, NULL, &items[i].params, MACRO_TYPE_PARAMS_FIELD,
						NULL, 0);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_SIMPLE:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
				items[i].password = zbx_strdup(items[i].password, items[i].password_orig);

				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].username,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].password,
						MACRO_TYPE_COMMON, NULL, 0);
				break;
			case ITEM_TYPE_JMX:
				if (MACRO_EXPAND_NO == expand_macros)
					break;

				items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
				items[i].password = zbx_strdup(items[i].password, items[i].password_orig);
				items[i].jmx_endpoint = zbx_strdup(items[i].jmx_endpoint, items[i].jmx_endpoint_orig);

				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].username,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].password,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, NULL, NULL, NULL, &items[i].jmx_endpoint, MACRO_TYPE_JMX_ENDPOINT,
						NULL, 0);
				break;
			case ITEM_TYPE_HTTPAGENT:
				if (MACRO_EXPAND_YES == expand_macros)
				{
					ZBX_STRDUP(items[i].timeout, items[i].timeout_orig);
					ZBX_STRDUP(items[i].url, items[i].url_orig);
					ZBX_STRDUP(items[i].status_codes, items[i].status_codes_orig);
					ZBX_STRDUP(items[i].http_proxy, items[i].http_proxy_orig);
					ZBX_STRDUP(items[i].ssl_cert_file, items[i].ssl_cert_file_orig);
					ZBX_STRDUP(items[i].ssl_key_file, items[i].ssl_key_file_orig);
					ZBX_STRDUP(items[i].ssl_key_password, items[i].ssl_key_password_orig);
					ZBX_STRDUP(items[i].username, items[i].username_orig);
					ZBX_STRDUP(items[i].password, items[i].password_orig);
					ZBX_STRDUP(items[i].query_fields, items[i].query_fields_orig);

					zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
							NULL, NULL, NULL, NULL, NULL, NULL, &items[i].timeout,
							MACRO_TYPE_COMMON, NULL, 0);
					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, &items[i].host,
							&items[i], NULL, NULL, NULL, NULL, NULL, &items[i].url,
							MACRO_TYPE_HTTP_RAW, NULL, 0);
				}

				if (SUCCEED != zbx_http_punycode_encode_url(&items[i].url))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Cannot encode URL into punycode"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				if (FAIL == parse_query_fields(&items[i], &items[i].query_fields, expand_macros))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Invalid query fields"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				if (MACRO_EXPAND_NO == expand_macros)
					break;

				switch (items[i].post_type)
				{
					case ZBX_POSTTYPE_XML:
						if (SUCCEED != zbx_substitute_macros_xml_unmasked(&items[i].posts, &items[i],
								NULL, NULL, error, sizeof(error)))
						{
							SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "%s.", error));
							errcodes[i] = CONFIG_ERROR;
							continue;
						}
						break;
					case ZBX_POSTTYPE_JSON:
						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL,NULL, NULL,
								&items[i].host, &items[i], NULL, NULL, NULL, NULL, NULL,
								&items[i].posts, MACRO_TYPE_HTTP_JSON, NULL, 0);
						break;
					default:
						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL,NULL, NULL,
								&items[i].host, &items[i], NULL, NULL, NULL, NULL, NULL,
								&items[i].posts, MACRO_TYPE_HTTP_RAW, NULL, 0);
						break;
				}

				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL,NULL, NULL, &items[i].host,
						&items[i], NULL, NULL, NULL, NULL, NULL, &items[i].headers,
						MACRO_TYPE_HTTP_RAW, NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].status_codes,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].http_proxy,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, NULL, NULL, NULL, &items[i].ssl_cert_file, MACRO_TYPE_HTTP_RAW,
						NULL, 0);
				zbx_substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, NULL, NULL, NULL, &items[i].ssl_key_file, MACRO_TYPE_HTTP_RAW,
						NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].ssl_key_password,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].username,
						MACRO_TYPE_COMMON, NULL, 0);
				zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, NULL, NULL, NULL, &items[i].password,
						MACRO_TYPE_COMMON, NULL, 0);
				break;
		}
	}

	zbx_free(port);

	if (MACRO_EXPAND_YES == expand_macros)
		zbx_dc_close_user_macros(um_handle);

}

void	zbx_check_items(DC_ITEM *items, int *errcodes, int num, AGENT_RESULT *results, zbx_vector_ptr_t *add_results,
		unsigned char poller_type, const zbx_config_comms_args_t *config_comms, int config_startup_time)
{
	if (ITEM_TYPE_SNMP == items[0].type)
	{
#ifndef HAVE_NETSNMP
		int	i;

		ZBX_UNUSED(poller_type);

		for (i = 0; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Support for SNMP checks was not compiled in."));
			errcodes[i] = CONFIG_ERROR;
		}
#else
		/* SNMP checks use their own timeouts */
		get_values_snmp(items, results, errcodes, num, poller_type, config_comms->config_timeout);
#endif
	}
	else if (ITEM_TYPE_JMX == items[0].type)
	{
		zbx_alarm_on(config_comms->config_timeout);
		get_values_java(ZBX_JAVA_GATEWAY_REQUEST_JMX, items, results, errcodes, num,
				config_comms->config_timeout);
		zbx_alarm_off();
	}
	else if (1 == num)
	{
		if (SUCCEED == errcodes[0])
			errcodes[0] = get_value(&items[0], &results[0], add_results, config_comms,
				config_startup_time);
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;
}

void	zbx_clean_items(DC_ITEM *items, int num, AGENT_RESULT *results)
{
	int	i;
	for (i = 0; i < num; i++)
	{
		zbx_free(items[i].key);

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMP:
				if (ZBX_IF_SNMP_VERSION_3 == items[i].snmp_version)
				{
					zbx_free(items[i].snmpv3_securityname);
					zbx_free(items[i].snmpv3_authpassphrase);
					zbx_free(items[i].snmpv3_privpassphrase);
					zbx_free(items[i].snmpv3_contextname);
				}

				zbx_free(items[i].snmp_community);
				zbx_free(items[i].snmp_oid);
				break;
			case ITEM_TYPE_HTTPAGENT:
				zbx_free(items[i].timeout);
				zbx_free(items[i].url);
				zbx_free(items[i].query_fields);
				zbx_free(items[i].status_codes);
				zbx_free(items[i].http_proxy);
				zbx_free(items[i].ssl_cert_file);
				zbx_free(items[i].ssl_key_file);
				zbx_free(items[i].ssl_key_password);
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				break;
			case ITEM_TYPE_SCRIPT:
				zbx_free(items[i].timeout);
				break;
			case ITEM_TYPE_SSH:
				zbx_free(items[i].publickey);
				zbx_free(items[i].privatekey);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
			case ITEM_TYPE_SIMPLE:
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				break;
			case ITEM_TYPE_JMX:
				zbx_free(items[i].username);
				zbx_free(items[i].password);
				zbx_free(items[i].jmx_endpoint);
				break;
		}

		zbx_free_agent_result(&results[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieve values of metrics from monitored hosts                   *
 *                                                                            *
 * Parameters: poller_type         - [IN] poller type (ZBX_POLLER_TYPE_...)   *
 *             nextcheck           - [OUT] item nextcheck                     *
 *             config_comms        - [IN] server/proxy configuration for      *
 *                                      communication                         *
 *             config_startup_time - [IN] program startup time                *
 *                                                                            *
 * Return value: number of items processed                                    *
 *                                                                            *
 * Comments: processes single item at a time except for Java, SNMP items,     *
 *           see DCconfig_get_poller_items()                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_values(unsigned char poller_type, int *nextcheck, const zbx_config_comms_args_t *config_comms,
		int config_startup_time)
{
	DC_ITEM			item, *items;
	AGENT_RESULT		results[MAX_POLLER_ITEMS];
	int			errcodes[MAX_POLLER_ITEMS];
	zbx_timespec_t		timespec;
	int			i, num; 
	zbx_vector_ptr_t	add_results;
	unsigned char		*data = NULL;
	size_t			data_alloc = 0, data_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
	items = &item;
	num = DCconfig_get_poller_items(poller_type, config_comms->config_timeout, &items);

	if (0 == num)
	{
		*nextcheck = DCconfig_get_poller_nextcheck(poller_type);
		goto exit;
	}
	
	DEBUG_ITEM(items[0].itemid, "Fetched from the queue in zbx poller, total %d items", num);
	zbx_vector_ptr_create(&add_results);

	zbx_prepare_items(items, errcodes, num, results, MACRO_EXPAND_YES);
	zbx_check_items(items, errcodes, num, results, &add_results, poller_type, config_comms, config_startup_time);

	zbx_timespec(&timespec);

	for (i = 0; i < num; i++)
	{
		switch (errcodes[i])
		{
			case SUCCEED:
			case NOTSUPPORTED:
			case AGENT_ERROR:
				if (ITEM_TYPE_AGENT == items[i].type ||	ITEM_TYPE_SNMP == items[i].type ||
					ITEM_TYPE_JMX == items[i].type || ITEM_TYPE_IPMI == items[i].type)
					glb_state_host_iface_register_response_arrive(items[i].host.hostid,
									 				items[i].interface.interfaceid, NULL);
				break;
			case NETWORK_ERROR:
			case GATEWAY_ERROR:
			case TIMEOUT_ERROR:
				if (ITEM_TYPE_AGENT == items[i].type ||	ITEM_TYPE_SNMP == items[i].type ||
					ITEM_TYPE_JMX == items[i].type || ITEM_TYPE_IPMI == items[i].type) 
					glb_state_host_iface_register_timeout(items[i].host.hostid, items[i].interface.interfaceid, 
									NULL, "There was a timeout/configuration/network error");
				break;		
			case FAIL:
			case CONFIG_ERROR:
				/* nothing to do */
				break;
			case SIG_ERROR:
				/* nothing to do, execution was forcibly interrupted by signal */
				break;
			default:
				zbx_error("unknown response code returned for item %ld of type %d keys %s - code %d",items[i].itemid, items[i].type, items[i].key, errcodes[i]);
				THIS_SHOULD_NEVER_HAPPEN;
		}
		DEBUG_ITEM(items[i].itemid, "Processing item result, errcode is %d, add results %d", errcodes[i], add_results.values_num);

		if (SUCCEED == errcodes[i])
		{
			if (0 == add_results.values_num)
			{
				DEBUG_ITEM(items[i].itemid, "Processing item as agent result");
				preprocess_agent_result(items[i].host.hostid, items[i].itemid, items[i].flags, &timespec, &results[i], items[i].value_type );
			}
			else
			{
				/* vmware.eventlog item returns vector of AGENT_RESULT representing events */

				int		j;
				zbx_timespec_t	ts_tmp = timespec;

				for (j = 0; j < add_results.values_num; j++)
				{
					AGENT_RESULT	*add_result = (AGENT_RESULT *)add_results.values[j];

					if (ZBX_ISSET_MSG(add_result))
					{
						preprocess_error(items[i].host.hostid, items[i].itemid, items[i].flags, &ts_tmp, add_result->msg);
					}
					else
					{
						preprocess_agent_result(items[i].host.hostid, items[i].itemid, items[i].flags, &ts_tmp, add_result, items[i].value_type);
					}

					/* ensure that every log item value timestamp is unique */
					if (++ts_tmp.ns == 1000000000)
					{
						ts_tmp.sec++;
						ts_tmp.ns = 0;
					}
				}
			}
		}
		else if (NOTSUPPORTED == errcodes[i] || AGENT_ERROR == errcodes[i] || CONFIG_ERROR == errcodes[i])
		{
			preprocess_error(items[i].host.hostid, items[i].itemid, items[i].flags, &timespec, results[i].msg);
		}
		DEBUG_ITEM(items[i].itemid,"Returning item to the queue");

		DCrequeue_items(&items[i].itemid, &timespec.sec, &errcodes[i], 1);
		DEBUG_ITEM(items[i].itemid,"Poller %d: Returned item to the zbx queue", poller_type);
	
		*nextcheck = time(NULL);

	}

	preprocessing_flush();
	zbx_clean_items(items, num, results);
	DCconfig_clean_items(items, NULL, num);
	zbx_vector_ptr_clear_ext(&add_results, (zbx_mem_free_func_t)zbx_free_agent_result_ptr);
	zbx_vector_ptr_destroy(&add_results);

	if (items != &item)
		zbx_free(items);
exit:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);
	return num;
}

ZBX_THREAD_ENTRY(poller_thread, args)
{
	zbx_thread_poller_args	*poller_args_in = (zbx_thread_poller_args *)(((zbx_thread_args_t *)args)->args);

	int			nextcheck, sleeptime = -1, processed = 0, old_processed = 0, total_sec = 0, old_total_sec = 0;
	double			sec;
	unsigned char		poller_type;
	zbx_ipc_async_socket_t	rtc;
	const zbx_thread_info_t	*info = &((zbx_thread_args_t *)args)->info;
	int			server_num = ((zbx_thread_args_t *)args)->info.server_num;
	int			process_num = ((zbx_thread_args_t *)args)->info.process_num;
	unsigned char		process_type = ((zbx_thread_args_t *)args)->info.process_type;
	zbx_uint32_t		rtc_msgs[] = {ZBX_RTC_SNMP_CACHE_RELOAD};
	int last_stat_time = time(NULL);

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	poller_type = (poller_args_in->poller_type);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(info->program_type),
			server_num, get_process_type_string(process_type), process_num);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

	scriptitem_es_engine_init();

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child(poller_args_in->config_comms->config_tls,
			poller_args_in->zbx_get_program_type_cb_arg);
#endif
	if (ZBX_POLLER_TYPE_HISTORY == poller_type)
	{
		zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type),
				process_num);

		zbx_db_connect(ZBX_DB_CONNECT_NORMAL);
	}
	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	zbx_rtc_subscribe(process_type, process_num, rtc_msgs, ARRSIZE(rtc_msgs),
			poller_args_in->config_comms->config_timeout, &rtc);

	while (ZBX_IS_RUNNING())
	{
		zbx_uint32_t	rtc_cmd;
		unsigned char	*rtc_data;

		sec = zbx_time();
		zbx_update_env(get_process_type_string(process_type), sec);

		if (0 != sleeptime)
		{
			zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, old_processed,
					old_total_sec);
		}

		processed += get_values(poller_type, &nextcheck, poller_args_in->config_comms,
				poller_args_in->config_startup_time);
		total_sec += zbx_time() - sec;

		sleeptime = zbx_calculate_sleeptime(nextcheck, POLLER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, processed, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, idle %d sec]",
					get_process_type_string(process_type), process_num, processed, total_sec,
					sleeptime);
				old_processed = processed;
				old_total_sec = total_sec;
			}
			processed = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}
		
		if (SUCCEED == zbx_rtc_wait(&rtc, info, &rtc_cmd, &rtc_data, sleeptime) && 0 != rtc_cmd)
		{
#ifdef HAVE_NETSNMP
			if (ZBX_RTC_SNMP_CACHE_RELOAD == rtc_cmd)
			{
				if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_UNREACHABLE == poller_type)
					zbx_clear_cache_snmp(process_type, process_num);
			}
#endif
			if (ZBX_RTC_SHUTDOWN == rtc_cmd)
				break;
		}

	}

	scriptitem_es_engine_destroy();

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
