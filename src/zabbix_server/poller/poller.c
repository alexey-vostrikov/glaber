/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
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

#include "db.h"
#include "dbcache.h"
#include "daemon.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "preproc.h"
#include "../events.h"

#include "poller.h"

#include "checks_agent.h"
#include "checks_aggregate.h"
#include "checks_external.h"
#include "checks_internal.h"
#include "checks_simple.h"
#include "checks_snmp.h"
#include "checks_db.h"
#include "checks_ssh.h"
#include "checks_telnet.h"
#include "checks_java.h"
#include "checks_calculated.h"
#include "checks_http.h"
#include "../../libs/zbxcrypto/tls.h"
#include "zbxjson.h"
#include "zbxhttp.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: db_host_update_availability                                      *
 *                                                                            *
 * Purpose: write host availability changes into database                     *
 *                                                                            *
 * Parameters: ha    - [IN] the host availability data                        *
 *                                                                            *
 * Return value: SUCCEED - the availability changes were written into db      *
 *               FAIL    - no changes in availability data were detected      *
 *                                                                            *
 ******************************************************************************/
static int	db_host_update_availability(const zbx_host_availability_t *ha)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;

	if (SUCCEED == zbx_sql_add_host_availability(&sql, &sql_alloc, &sql_offset, ha))
	{
		DBbegin();
		DBexecute("%s", sql);
		DBcommit();

		zbx_free(sql);

		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: host_get_availability                                            *
 *                                                                            *
 * Purpose: get host availability data based on the specified agent type      *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             type         - [IN] the agent type                             *
 *             availability - [OUT] the host availability data                *
 *                                                                            *
 * Return value: SUCCEED - the host availability data was retrieved           *
 *                         successfully                                       *
 *               FAIL    - failed to retrieve host availability data,         *
 *                         invalid agent type was specified                   *
 *                                                                            *
 ******************************************************************************/
static int	host_get_availability(const DC_HOST *dc_host, unsigned char agent, zbx_host_availability_t *ha)
{
	zbx_agent_availability_t	*availability = &ha->agents[agent];

	availability->flags = ZBX_FLAGS_AGENT_STATUS;

	switch (agent)
	{
		case ZBX_AGENT_ZABBIX:
			availability->available = dc_host->available;
			availability->error = zbx_strdup(NULL, dc_host->error);
			availability->errors_from = dc_host->errors_from;
			availability->disable_until = dc_host->disable_until;
			break;
		case ZBX_AGENT_SNMP:
			availability->available = dc_host->snmp_available;
			availability->error = zbx_strdup(NULL, dc_host->snmp_error);
			availability->errors_from = dc_host->snmp_errors_from;
			availability->disable_until = dc_host->snmp_disable_until;
			break;
		case ZBX_AGENT_IPMI:
			availability->available = dc_host->ipmi_available;
			availability->error = zbx_strdup(NULL, dc_host->ipmi_error);
			availability->errors_from = dc_host->ipmi_errors_from;
			availability->disable_until = dc_host->ipmi_disable_until;
			break;
		case ZBX_AGENT_JMX:
			availability->available = dc_host->jmx_available;
			availability->error = zbx_strdup(NULL, dc_host->jmx_error);
			availability->disable_until = dc_host->jmx_disable_until;
			availability->errors_from = dc_host->jmx_errors_from;
			break;
		default:
			return FAIL;
	}

	ha->hostid = dc_host->hostid;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: host_set_availability                                            *
 *                                                                            *
 * Purpose: sets host availability data based on the specified agent type     *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             type         - [IN] the agent type                             *
 *             availability - [IN] the host availability data                 *
 *                                                                            *
 * Return value: SUCCEED - the host availability data was set successfully    *
 *               FAIL    - failed to set host availability data,              *
 *                         invalid agent type was specified                   *
 *                                                                            *
 ******************************************************************************/
static int	host_set_availability(DC_HOST *dc_host, unsigned char agent, const zbx_host_availability_t *ha)
{
	const zbx_agent_availability_t	*availability = &ha->agents[agent];
	unsigned char			*pavailable;
	int				*perrors_from, *pdisable_until;
	char				*perror;

	switch (agent)
	{
		case ZBX_AGENT_ZABBIX:
			pavailable = &dc_host->available;
			perror = dc_host->error;
			perrors_from = &dc_host->errors_from;
			pdisable_until = &dc_host->disable_until;
			break;
		case ZBX_AGENT_SNMP:
			pavailable = &dc_host->snmp_available;
			perror = dc_host->snmp_error;
			perrors_from = &dc_host->snmp_errors_from;
			pdisable_until = &dc_host->snmp_disable_until;
			break;
		case ZBX_AGENT_IPMI:
			pavailable = &dc_host->ipmi_available;
			perror = dc_host->ipmi_error;
			perrors_from = &dc_host->ipmi_errors_from;
			pdisable_until = &dc_host->ipmi_disable_until;
			break;
		case ZBX_AGENT_JMX:
			pavailable = &dc_host->jmx_available;
			perror = dc_host->jmx_error;
			pdisable_until = &dc_host->jmx_disable_until;
			perrors_from = &dc_host->jmx_errors_from;
			break;
		default:
			return FAIL;
	}

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_AVAILABLE))
		*pavailable = availability->available;

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_ERROR))
		zbx_strlcpy(perror, availability->error, HOST_ERROR_LEN_MAX);

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_ERRORS_FROM))
		*perrors_from = availability->errors_from;

	if (0 != (availability->flags & ZBX_FLAGS_AGENT_STATUS_DISABLE_UNTIL))
		*pdisable_until = availability->disable_until;

	return SUCCEED;
}

static unsigned char	host_availability_agent_by_item_type(unsigned char type)
{
	switch (type)
	{
		case ITEM_TYPE_ZABBIX:
			return ZBX_AGENT_ZABBIX;
			break;
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			return ZBX_AGENT_SNMP;
			break;
		case ITEM_TYPE_IPMI:
			return ZBX_AGENT_IPMI;
			break;
		case ITEM_TYPE_JMX:
			return ZBX_AGENT_JMX;
			break;
		default:
			return ZBX_AGENT_UNKNOWN;
	}
}

void	zbx_activate_item_host(DC_ITEM *item, zbx_timespec_t *ts)
{
	zbx_host_availability_t	in, out;
	unsigned char		agent_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" ZBX_FS_UI64 " itemid:" ZBX_FS_UI64 " type:%d",
			__func__, item->host.hostid, item->itemid, (int)item->type);

	zbx_host_availability_init(&in, item->host.hostid);
	zbx_host_availability_init(&out, item->host.hostid);

	if (ZBX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	if (FAIL == DChost_activate(item->host.hostid, agent_type, ts, &in.agents[agent_type], &out.agents[agent_type]))
		goto out;

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	if (HOST_AVAILABLE_TRUE == in.agents[agent_type].available)
	{
		zabbix_log(LOG_LEVEL_WARNING, "resuming %s checks on host \"%s\": connection restored",
				zbx_agent_type_string(item->type), item->host.host);
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "enabling %s checks on host \"%s\": host became available",
				zbx_agent_type_string(item->type), item->host.host);
	}
out:
	zbx_host_availability_clean(&out);
	zbx_host_availability_clean(&in);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void	zbx_deactivate_item_host(DC_ITEM *item, zbx_timespec_t *ts, const char *error)
{
	zbx_host_availability_t	in, out;
	unsigned char		agent_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostid:" ZBX_FS_UI64 " itemid:" ZBX_FS_UI64 " type:%d",
			__func__, item->host.hostid, item->itemid, (int)item->type);

	zbx_host_availability_init(&in, item->host.hostid);
	zbx_host_availability_init(&out,item->host.hostid);

	if (ZBX_AGENT_UNKNOWN == (agent_type = host_availability_agent_by_item_type(item->type)))
		goto out;

	if (FAIL == host_get_availability(&item->host, agent_type, &in))
		goto out;

	if (FAIL == DChost_deactivate(item->host.hostid, agent_type, ts, &in.agents[agent_type],
			&out.agents[agent_type], error))
	{
		goto out;
	}

	if (FAIL == db_host_update_availability(&out))
		goto out;

	host_set_availability(&item->host, agent_type, &out);

	if (0 == in.agents[agent_type].errors_from)
	{
		zabbix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
				" first network error, wait for %d seconds",
				zbx_agent_type_string(item->type), item->key_orig, item->host.host,
				out.agents[agent_type].disable_until - ts->sec);
	}
	else
	{
		if (HOST_AVAILABLE_FALSE != in.agents[agent_type].available)
		{
			if (HOST_AVAILABLE_FALSE != out.agents[agent_type].available)
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s item \"%s\" on host \"%s\" failed:"
						" another network error, wait for %d seconds",
						zbx_agent_type_string(item->type), item->key_orig, item->host.host,
						out.agents[agent_type].disable_until - ts->sec);
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "temporarily disabling %s checks on host \"%s\":"
						" host unavailable",
						zbx_agent_type_string(item->type), item->host.host);
			}
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() errors_from:%d available:%d", __func__,
			out.agents[agent_type].errors_from, out.agents[agent_type].available);
out:
	zbx_host_availability_clean(&out);
	zbx_host_availability_clean(&in);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	free_result_ptr(AGENT_RESULT *result)
{
	free_result(result);
	zbx_free(result);
}

static int	get_value(DC_ITEM *item, AGENT_RESULT *result, zbx_vector_ptr_t *add_results)
{
	int	res = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key_orig);

	switch (item->type)
	{
		case ITEM_TYPE_ZABBIX:
			zbx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_agent(item, result);
			zbx_alarm_off();
			break;
		case ITEM_TYPE_SIMPLE:
			/* simple checks use their own timeouts */
			res = get_value_simple(item, result, add_results);
			break;
		case ITEM_TYPE_INTERNAL:
			res = get_value_internal(item, result);
			break;
		case ITEM_TYPE_DB_MONITOR:
#ifdef HAVE_UNIXODBC
			res = get_value_db(item, result);
#else
			SET_MSG_RESULT(result,
					zbx_strdup(NULL, "Support for Database monitor checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_AGGREGATE:
			res = get_value_aggregate(item, result);
			break;
		case ITEM_TYPE_EXTERNAL:
			/* external checks use their own timeouts */
			res = get_value_external(item, result);
			break;
		case ITEM_TYPE_SSH:
#ifdef HAVE_SSH2
			zbx_alarm_on(CONFIG_TIMEOUT);
			res = get_value_ssh(item, result);
			zbx_alarm_off();
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for SSH checks was not compiled in."));
			res = CONFIG_ERROR;
#endif
			break;
		case ITEM_TYPE_TELNET:
			zbx_alarm_on(CONFIG_TIMEOUT);
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
		default:
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Not supported item type:%d", item->type));
			res = CONFIG_ERROR;
	}

	if (SUCCEED != res)
	{
		if (!ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, ZBX_NOTSUPPORTED_MSG));

		zabbix_log(LOG_LEVEL_DEBUG, "Item [%s:%s] error: %s", item->host.host, item->key_orig, result->msg);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(res));

	return res;
}

static int	parse_query_fields(const DC_ITEM *item, char **query_fields)
{
	struct zbx_json_parse	jp_array, jp_object;
	char			name[MAX_STRING_LEN], value[MAX_STRING_LEN];
	const char		*member, *element = NULL;
	size_t			alloc_len, offset;

	if ('\0' == *item->query_fields_orig)
	{
		ZBX_STRDUP(*query_fields, item->query_fields_orig);
		return SUCCEED;
	}

	if (SUCCEED != zbx_json_open(item->query_fields_orig, &jp_array))
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
			return FAIL;
		}

		if (NULL == *query_fields && NULL == strchr(item->url, '?'))
			zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '?');
		else
			zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '&');

		data = zbx_strdup(data, name);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(query_fields, &alloc_len, &offset, data);
		zbx_chrcpy_alloc(query_fields, &alloc_len, &offset, '=');

		data = zbx_strdup(data, value);
		substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &item->host, item, NULL, NULL, &data,
				MACRO_TYPE_HTTP_RAW, NULL, 0);
		zbx_http_url_encode(data, &data);
		zbx_strcpy_alloc(query_fields, &alloc_len, &offset, data);

		free(data);
	}
	while (NULL != (element = zbx_json_next(&jp_array, element)));

	return SUCCEED;
}
static int prepare_items(DC_ITEM *items, int *errcodes, AGENT_RESULT *results,  int max_items) {
	char 	*port = NULL, error[ITEM_ERROR_LEN_MAX];
	int num=0;
	int i=0;

	for (i = 0; i < max_items; i++)
	{
		if (errcodes[i] != POLL_CC_FETCHED) continue;

		init_result(&results[i]);
		num++;
		ZBX_STRDUP(items[i].key, items[i].key_orig);
		if (SUCCEED != substitute_key_macros(&items[i].key, NULL, &items[i], NULL, NULL,
				MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
		{
			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}
		
		switch (items[i].type)
		{
			case ITEM_TYPE_ZABBIX:
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
			case ITEM_TYPE_SNMPv3:
			case ITEM_TYPE_JMX:
				ZBX_STRDUP(port, items[i].interface.port_orig);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &port, MACRO_TYPE_COMMON, NULL, 0);
				if (FAIL == is_ushort(port, &items[i].interface.port))
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
			case ITEM_TYPE_SNMPv3:
				ZBX_STRDUP(items[i].snmpv3_securityname, items[i].snmpv3_securityname_orig);
				ZBX_STRDUP(items[i].snmpv3_authpassphrase, items[i].snmpv3_authpassphrase_orig);
				ZBX_STRDUP(items[i].snmpv3_privpassphrase, items[i].snmpv3_privpassphrase_orig);
				ZBX_STRDUP(items[i].snmpv3_contextname, items[i].snmpv3_contextname_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_securityname,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_authpassphrase,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_privpassphrase,
						MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmpv3_contextname,
						MACRO_TYPE_COMMON, NULL, 0);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
				ZBX_STRDUP(items[i].snmp_community, items[i].snmp_community_orig);
				//zabbix_log(LOG_LEVEL_INFORMATION,"#42 starting oid cpy for item %ld %s -> %s",items[i].itemid, items[i].snmp_oid_orig, items[i].snmp_oid);
				ZBX_STRDUP(items[i].snmp_oid, items[i].snmp_oid_orig);
				//zabbix_log(LOG_LEVEL_INFORMATION,"#42 starting oid cpy for item %ld %s -> %s",items[i].itemid, items[i].snmp_oid_orig, items[i].snmp_oid);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].snmp_community, MACRO_TYPE_COMMON, NULL, 0);
				if (SUCCEED != substitute_key_macros(&items[i].snmp_oid, &items[i].host.hostid, NULL,
						NULL, NULL, MACRO_TYPE_SNMP_OID, error, sizeof(error)))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}
				break;
			case ITEM_TYPE_SSH:
				ZBX_STRDUP(items[i].publickey, items[i].publickey_orig);
				ZBX_STRDUP(items[i].privatekey, items[i].privatekey_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].publickey, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].privatekey, MACRO_TYPE_COMMON, NULL, 0);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_TELNET:
			case ITEM_TYPE_DB_MONITOR:
				substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, &items[i].params, MACRO_TYPE_PARAMS_FIELD, NULL, 0);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_SIMPLE:
				items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
				items[i].password = zbx_strdup(items[i].password, items[i].password_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				break;
			case ITEM_TYPE_JMX:
				items[i].username = zbx_strdup(items[i].username, items[i].username_orig);
				items[i].password = zbx_strdup(items[i].password, items[i].password_orig);
				items[i].jmx_endpoint = zbx_strdup(items[i].jmx_endpoint, items[i].jmx_endpoint_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &items[i],
						NULL, NULL, &items[i].jmx_endpoint, MACRO_TYPE_JMX_ENDPOINT, NULL, 0);
				break;
			case ITEM_TYPE_HTTPAGENT:
				ZBX_STRDUP(items[i].timeout, items[i].timeout_orig);
				ZBX_STRDUP(items[i].url, items[i].url_orig);
				ZBX_STRDUP(items[i].status_codes, items[i].status_codes_orig);
				ZBX_STRDUP(items[i].http_proxy, items[i].http_proxy_orig);
				ZBX_STRDUP(items[i].ssl_cert_file, items[i].ssl_cert_file_orig);
				ZBX_STRDUP(items[i].ssl_key_file, items[i].ssl_key_file_orig);
				ZBX_STRDUP(items[i].ssl_key_password, items[i].ssl_key_password_orig);
				ZBX_STRDUP(items[i].username, items[i].username_orig);
				ZBX_STRDUP(items[i].password, items[i].password_orig);

				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].timeout, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].url, MACRO_TYPE_HTTP_RAW, NULL, 0);

				if (SUCCEED != zbx_http_punycode_encode_url(&items[i].url))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Cannot encode URL into punycode"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				if (FAIL == parse_query_fields(&items[i], &items[i].query_fields))
				{
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Invalid query fields"));
					errcodes[i] = CONFIG_ERROR;
					continue;
				}

				switch (items[i].post_type)
				{
					case ZBX_POSTTYPE_XML:
						if (SUCCEED != substitute_macros_xml(&items[i].posts, &items[i], NULL,
								NULL, error, sizeof(error)))
						{
							SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "%s.", error));
							errcodes[i] = CONFIG_ERROR;
							continue;
						}
						break;
					case ZBX_POSTTYPE_JSON:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_JSON, NULL, 0);
						break;
					default:
						substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host,
								&items[i], NULL, NULL, &items[i].posts,
								MACRO_TYPE_HTTP_RAW, NULL, 0);
						break;
				}

				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].headers, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].status_codes, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].http_proxy, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_cert_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL,NULL, NULL, &items[i].host, &items[i], NULL,
						NULL, &items[i].ssl_key_file, MACRO_TYPE_HTTP_RAW, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL, NULL,
						NULL, NULL, &items[i].ssl_key_password, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].username, MACRO_TYPE_COMMON, NULL, 0);
				substitute_simple_macros(NULL, NULL, NULL, NULL, &items[i].host.hostid, NULL,
						NULL, NULL, NULL, &items[i].password, MACRO_TYPE_COMMON, NULL, 0);
				break;
		}
		errcodes[i]=POLL_PREPARED;
	}
	
	zbx_free(port);
	return num;
}
static int preprocess_values(DC_ITEM *items, int *errcodes, AGENT_RESULT *results,  int max_items, int poller_type, zbx_vector_ptr_t* add_results) {
	int i, num=0;
	zbx_timespec_t		timespec;
	
	zbx_timespec(&timespec);


	for (i = 0; i < max_items; i++ ) {
		//zabbix_log(LOG_LEVEL_INFORMATION, "Prproc checkinmg item%d  %ld in state %d",i, items[i].itemid, errcodes[i]);
		switch (errcodes[i]) {
			case SUCCEED:
			case FAIL:
			case NOTSUPPORTED:
			case NETWORK_ERROR:
			case TIMEOUT_ERROR:
			case AGENT_ERROR:
			case GATEWAY_ERROR:
			case CONFIG_ERROR:
				break;
		default:
			continue;
		}
		//zabbix_log(LOG_LEVEL_INFORMATION, "Preprocessing item %ld in state %d",items[i].itemid, errcodes[i]);

		if (SUCCEED == errcodes[i])
			{
				if (0 == add_results->values_num)
				{
					items[i].state = ITEM_STATE_NORMAL;
					num++;
							
					zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
							&results[i], &timespec, items[i].state, NULL);
				}
				else {
				/* vmware.eventlog item returns vector of AGENT_RESULT representing events */
					int		j;
					zbx_timespec_t	ts_tmp = timespec;

					for (j = 0; j < add_results->values_num; j++)
					{
						AGENT_RESULT	*add_result = (AGENT_RESULT *)add_results->values[j];

						if (ISSET_MSG(add_result))
						{
							items[i].state = ITEM_STATE_NOTSUPPORTED;
							num++;
							zbx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, NULL, &ts_tmp, items[i].state,
								add_result->msg);
						}
						else {
							zabbix_log(LOG_LEVEL_INFORMATION, "Prc4");
							items[i].state = ITEM_STATE_NORMAL;
							num++;
							zbx_preprocess_item_value(items[i].itemid, items[i].value_type,
								items[i].flags, add_result, &ts_tmp, items[i].state,
								NULL);
						}

						/* ensure that every log item value timestamp is unique */
						if (++ts_tmp.ns == 1000000000) {
							ts_tmp.sec++;
							ts_tmp.ns = 0;
						}
					}
				}
			}
			else if (NOTSUPPORTED == errcodes[i] || AGENT_ERROR == errcodes[i] || CONFIG_ERROR == errcodes[i])
			{	
			
				items[i].state = ITEM_STATE_NOTSUPPORTED;
				num++;			
			
				zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL, &timespec,
					items[i].state, results[i].msg);
			}
			//zabbix_log(LOG_LEVEL_INFORMATION,"Requeueing item %ld",items[i].itemid);		
			DCpoller_requeue_items(&items[i].itemid, &items[i].state, &timespec.sec, &errcodes[i], 1, poller_type);
			errcodes[i]=POLL_PREPROCESSED;	
		}

		zbx_preprocessor_flush();
		return num;
}

static int clean_values(DC_ITEM *items, int *errcodes, AGENT_RESULT *results,  int max_items , int poller_type, int force_clean) {
	int i;
	int num=0;
	zbx_timespec_t		timespec;
	
	zbx_timespec(&timespec);
	for (i = 0; i < max_items; i++)
	{
	
		if ( POLL_FREE == errcodes[i]) continue; //not doing double free, we need no segv panics
		//if ( !force_clean && PROCESSED != errcodes[i] ) continue; //only clean processed items unless force is set
	
		//unless there is a force clean, we only clean items being preprocessed
		if ( !force_clean && POLL_PREPROCESSED !=errcodes[i]) continue;

		//zabbix_log(LOG_LEVEL_INFORMATION, "Cleaning item %ld in state %d",items[i].itemid, errcodes[i]);
		num++;
		zabbix_log(LOG_LEVEL_DEBUG,"%s, cleaning item %d state is %d",__func__, i,errcodes[i]);
		zbx_free(items[i].key);

		switch (items[i].type)
		{
			case ITEM_TYPE_SNMPv3:
				zbx_free(items[i].snmpv3_securityname);
				zbx_free(items[i].snmpv3_authpassphrase);
				zbx_free(items[i].snmpv3_privpassphrase);
				zbx_free(items[i].snmpv3_contextname);
				ZBX_FALLTHROUGH;
			case ITEM_TYPE_SNMPv1:
			case ITEM_TYPE_SNMPv2c:
				zbx_free(items[i].snmp_community);
				//zabbix_log(LOG_LEVEL_INFORMATION,"cleaning item %ld errcode is %d, oid is %s",items[i].itemid,errcodes[i],items[i].snmp_oid);
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

		free_result(&results[i]);
		DCconfig_clean_items(&items[i], NULL, 1);
		
		errcodes[i]=POLL_FREE;
	}
	
	return num;

}
extern int CONFIG_ASYNC_SNMP_POLLER_FORKS;
extern int CONFIG_ASYNC_SNMP_AGENT_CONNS;
/******************************************************************************
 *                                                                            *
 * Function: get_values                                                       *
 *                                                                            *
 * Purpose: retrieve values of metrics from monitored hosts                   *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: number of items processed                                    *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: processes single item at a time except for Java, SNMP items,     *
 *           see DCconfig_get_poller_items()                                  *
 *                                                                            *
 ******************************************************************************/
static int	get_values(unsigned char poller_type, int *processed_num,
			DC_ITEM	 *items,	AGENT_RESULT *results, int *errcodes, int max_items)
{
	int			i, num_collected=0, num, start_time=time(NULL);
	int last_stat_time=time(NULL);
	int queue_idx=poller_type;

//	#define MAX_ROLL_TIME 3000
//	#define MAX_ROLL_ITERATIONS 2000
	#define STAT_INTERVAL	5
	static int roll = 0;
	zbx_vector_ptr_t	add_results;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
	
	
	for (i = 0; i < max_items; i++) 
		errcodes[i]=POLL_FREE;
	if (ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type )	
		queue_idx = ZBX_POLLER_TYPE_COUNT + process_num -1;	
	if (ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type )	
		queue_idx = ZBX_POLLER_TYPE_COUNT + CONFIG_ASYNC_SNMP_POLLER_FORKS + process_num - 1;	


//	for (roll=0; (roll < MAX_ROLL_ITERATIONS && time(NULL)-start_time < MAX_ROLL_TIME) ; roll++ ) {
	do {  //async loop supposed to never end 

		//if (ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type) 
		//	zabbix_log(LOG_LEVEL_INFORMATION, "Doing async snmp roll %d",roll);
		zbx_vector_ptr_create(&add_results);	
		//zabbix_log(LOG_LEVEL_INFORMATION,"#42 - before get poller items ");
		//zabbix_log(LOG_LEVEL_INFORMATION,"Getting items for queue %d",queue_idx);

		num = DCconfig_get_poller_items(queue_idx, items, errcodes, max_items);
		
		if ( 0 == num ) usleep(20000);
		//todo: make all delays through zabbix sleep routines
		//zabbix_log(LOG_LEVEL_INFORMATION,"#42 - before get prepare items ");
		
		num=prepare_items(items, errcodes,results, max_items);
		//zabbix_log(LOG_LEVEL_INFORMATION,"#42 - after prepare items poller type is %d ",poller_type);
		//zabbix_log(LOG_LEVEL_INFORMATION,"%s: starting poll iteration %d",__func__,roll++);
#ifdef HAVE_NETSNMP
		if (ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type ) {
			//zbx_alarm_on(CONFIG_TIMEOUT*2);
			//zabbix_log(LOG_LEVEL_INFORMATION,"#42 - before get async values ");
			num = get_values_snmp_async();
			//zabbix_log(LOG_LEVEL_INFORMATION, "Got %d asyn values", num);
			//zbx_alarm_off();
		}
#endif
		if (ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type ) 
		{
		//	zbx_alarm_on(CONFIG_TIMEOUT*2);
			num = get_values_agent_async();
		//	zbx_alarm_off();
		}
			//zabbix_log(LOG_LEVEL_INFORMATION,"#42 - before old style fallback ");
		//fallback-old sync processing, still need this for LLD items a
		//zabbix_log(LOG_LEVEL_INFORMATION, "Poller type %d, errcode is %d",poller_type,errcodes[0]);

		for (i = 0; i < max_items; i++) {
			/* only process items skipped by async methods */		
			if ( (POLL_SKIPPED == errcodes[i] && 
				 (	poller_type == ZBX_POLLER_TYPE_ASYNC_AGENT ||  
				  	poller_type == ZBX_POLLER_TYPE_ASYNC_SNMP || 
					poller_type == ZBX_POLLER_TYPE_UNREACHABLE
					)
				) || 
				 ( POLL_PREPARED ==	errcodes[i] && 
				 	( ZBX_POLLER_TYPE_NORMAL == poller_type|| 
					  ZBX_POLLER_TYPE_JAVA == poller_type)
				 )	
			   ) {
				
				errcodes[i]=SUCCEED;
				/* retrieve item values */
				//zabbix_log(LOG_LEVEL_INFORMATION, "Processing item in old sync way");
				if (SUCCEED == is_snmp_type(items[i].type))
				{	
#ifdef HAVE_NETSNMP
					/* SNMP checks use their own timeouts */
					get_values_snmp(&items[i], &results[i], &errcodes[i], 1);
					
#else
				SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Support for SNMP checks was not compiled in."));
				errcodes[i] = CONFIG_ERROR;
#endif
				} else if (ITEM_TYPE_JMX == items[i].type)
				{
					zbx_alarm_on(CONFIG_TIMEOUT);
					get_values_java(ZBX_JAVA_GATEWAY_REQUEST_JMX, items+i, results+i, errcodes+i, 1);
					zbx_alarm_off();
				} else 
				{	
					errcodes[i] = get_value(&items[i], &results[i], &add_results);
				}
			}	
		}
	
		num=preprocess_values(items, errcodes, results, max_items, poller_type, &add_results);
		*processed_num+=num;
		//zabbix_log(LOG_LEVEL_INFORMATION, "QPoller_stat: preprocessed %d items", num);
		
		num=clean_values(items, errcodes, results, max_items, poller_type, 0);
		//zabbix_log(LOG_LEVEL_INFORMATION, "QPoller_stat: cleaned %d items", num);
	
		zbx_vector_ptr_clear_ext(&add_results, (zbx_mem_free_func_t)free_result_ptr);
		zbx_vector_ptr_destroy(&add_results);

		//only one iteration for non-snmp async things
		//and much more for async pollers
	
		if (ZBX_POLLER_TYPE_ASYNC_SNMP !=poller_type && ZBX_POLLER_TYPE_ASYNC_AGENT != poller_type ) break;
		
		if (STAT_INTERVAL <= time(NULL) - last_stat_time) 
		{
			zbx_setproctitle("%s #%d [got %d items/sec, getting values]",
					get_process_type_string(process_type), process_num, (*processed_num/STAT_INTERVAL), time(NULL) - last_stat_time);
			last_stat_time=time(NULL);
			*processed_num=0;
		}
		
	} while (( ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type ) ||
			(ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type) );
	
//	} while (( ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type && roll++ < GLB_ASYNC_POLLING_MAX_ITERATIONS ) ||
//			(ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type) );
	

/*
	if ( ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type  ) {
		
		int waitend = time(NULL)+20*CONFIG_TIMEOUT; 
		int items_polling;
		int cnt=0;

		zabbix_log(LOG_LEVEL_INFORMATION,"Satrting async polling megaloop");
		
		do {
			get_values_snmp_async();
			items_polling =0;
			for (i=0; i<max_items; i++) 
				if (POLL_PREPARED == errcodes[i] || 
					POLL_POLLING == errcodes[i] || 
					POLL_QUEUED == errcodes[i]) items_polling++;
			usleep(10000);
		
			if (cnt %10 == 0) zabbix_log(LOG_LEVEL_INFORMATION,"Still waiting for %d items to finish", items_polling);

		} while (waitend > time(NULL) && 0 != items_polling);
		
		zabbix_log(LOG_LEVEL_INFORMATION,"Funished async polling megaloop with %d active itmes",items_polling);
		//we'll keep calling async polling in shutdown mode, to finish polling of all items before 
	}
*/

	clean_values(items, errcodes, results, max_items, poller_type, 1); //forced clean
	roll = 0;

exit:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num_collected;
 
}

extern int	CONFIG_ASYNC_SNMP_POLLER_CONNS;
extern int 	CONFIG_ASYNC_AGENT_POLLER_CONNS;
extern int CONFIG_ASYNC_SNMP_POLLER_CONNS;

ZBX_THREAD_ENTRY(poller_thread, args)
{
	int		nextcheck, sleeptime = -1, processed = 0, old_processed = 0;
	double		sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t		last_stat_time;
	unsigned char	poller_type;
	int max_items = 1;
	
	DC_ITEM			*items;
	AGENT_RESULT		*results;
	int			*errcodes;


//#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	poller_type = *(unsigned char *)((zbx_thread_args_t *)args)->args;
	process_type = ((zbx_thread_args_t *)args)->process_type;

	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#ifdef HAVE_NETSNMP
		if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_UNREACHABLE == poller_type || ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type ) {
			zbx_init_snmp();
		}
#endif


#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child();
#endif
	switch (poller_type)
	{
		case ZBX_POLLER_TYPE_ASYNC_AGENT:
			max_items=MAX_ASYNC_AGENT_ITEMS;
			break;
		case ZBX_POLLER_TYPE_ASYNC_SNMP:
			max_items=MAX_ASYNC_SNMP_ITEMS;
			break;
		case ZBX_POLLER_TYPE_UNREACHABLE:
			max_items=MAX_UNREACH_ITEMS;
			break;
		default: 
			max_items=MAX_POLLER_ITEMS;
	}
	//zabbix_log(LOG_LEVEL_INFORMATION, "poller type is %d, max items is %d",poller_type, max_items);

	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	if (NULL == (items=zbx_malloc(NULL,sizeof(DC_ITEM)*max_items))) 
	{
		zabbix_log(LOG_LEVEL_WARNING,"Cannot allocate memory for polling");
		return FAIL;
	};
	if (NULL == (results=zbx_malloc(NULL,sizeof(AGENT_RESULT)*max_items))) 
	{
		zabbix_log(LOG_LEVEL_WARNING,"Cannot allocate memory for polling");
		return FAIL;
	};
	if (NULL == (errcodes=zbx_malloc(NULL,sizeof(int)*max_items))) 
	{
		zabbix_log(LOG_LEVEL_WARNING,"Cannot allocate memory for polling");
		return FAIL;
	};
 
	
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	for (;;)
	{
		sec = zbx_time();
		zbx_update_env(sec);

		if (0 != sleeptime)
		{
			zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
					get_process_type_string(process_type), process_num, old_processed,
					old_total_sec);
		}

		if (ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type )
			  init_async_snnmp(items, results,errcodes, max_items, CONFIG_ASYNC_SNMP_POLLER_CONNS);
		if (ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type )
			  init_async_agent(items, results,errcodes, max_items, CONFIG_ASYNC_AGENT_POLLER_CONNS);
			   

		processed += get_values(poller_type,&processed,items,results,errcodes,max_items);
		total_sec += zbx_time() - sec;
		
		if (ZBX_POLLER_TYPE_ASYNC_SNMP == poller_type )  destroy_aync_snmp();
		if (ZBX_POLLER_TYPE_ASYNC_AGENT == poller_type )  destroy_aync_agent();

		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

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

		
		zbx_sleep_loop(sleeptime);
	}
	zbx_free(items);
	zbx_free(results);
	zbx_free(errcodes);

#undef STAT_INTERVAL
}

