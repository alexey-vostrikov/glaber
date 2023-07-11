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

#include "discoverer.h"

#include "log.h"
#include "zbxicmpping.h"
#include "zbxdiscovery.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "zbxrtc.h"
#include "zbxnix.h"
#include "../poller/checks_agent.h"
#include "../poller/checks_snmp.h"
#include "../glb_events.h"
#include "zbxnum.h"
#include "zbxtime.h"
#include "zbxip.h"
#include "zbxsysinfo.h"
#include "zbx_rtc_constants.h"

extern unsigned char			program_type;

#define ZBX_DISCOVERER_IPRANGE_LIMIT	(1 << 16)

typedef struct
{
	zbx_uint64_t	dcheckid;
	char		*ports;
	char		*key_;
	char		*snmp_community;
	char		*snmpv3_securityname;
	char		*snmpv3_authpassphrase;
	char		*snmpv3_privpassphrase;
	char		*snmpv3_contextname;
	int		type;
	unsigned char	snmpv3_securitylevel;
	unsigned char	snmpv3_authprotocol;
	unsigned char	snmpv3_privprotocol;
}
DB_DCHECK;

/******************************************************************************
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
static void	proxy_update_service(zbx_uint64_t druleid, zbx_uint64_t dcheckid, const char *ip,
		const char *dns, int port, int status, const char *value, int now)
{
	char	*ip_esc, *dns_esc, *value_esc;

	ip_esc = zbx_db_dyn_escape_field("proxy_dhistory", "ip", ip);
	dns_esc = zbx_db_dyn_escape_field("proxy_dhistory", "dns", dns);
	value_esc = zbx_db_dyn_escape_field("proxy_dhistory", "value", value);

	zbx_db_execute("insert into proxy_dhistory (clock,druleid,dcheckid,ip,dns,port,value,status)"
			" values (%d," ZBX_FS_UI64 "," ZBX_FS_UI64 ",'%s','%s',%d,'%s',%d)",
			now, druleid, dcheckid, ip_esc, dns_esc, port, value_esc, status);

	zbx_free(value_esc);
	zbx_free(dns_esc);
	zbx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 ******************************************************************************/
static void	proxy_update_host(zbx_uint64_t druleid, const char *ip, const char *dns, int status, int now)
{
	char	*ip_esc, *dns_esc;

	ip_esc = zbx_db_dyn_escape_field("proxy_dhistory", "ip", ip);
	dns_esc = zbx_db_dyn_escape_field("proxy_dhistory", "dns", dns);

	zbx_db_execute("insert into proxy_dhistory (clock,druleid,ip,dns,status)"
			" values (%d," ZBX_FS_UI64 ",'%s','%s',%d)",
			now, druleid, ip_esc, dns_esc, status);

	zbx_free(dns_esc);
	zbx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Purpose: check if service is available                                     *
 *                                                                            *
 * Parameters: dcheck         - [IN] service type                             *
 *             ip             - [IN]                                          *
 *             port           - [IN]                                          *
 *             config_timeout - [IN]                                          *
 *             value          - [OUT]                                         *
 *             value_alloc    - [IN/OUT]                                      *
 *                                                                            *
 * Return value: SUCCEED - service is UP, FAIL - service not discovered       *
 *                                                                            *
 ******************************************************************************/
static int	discover_service(const DB_DCHECK *dcheck, char *ip, int port, int config_timeout, char **value,
		size_t *value_alloc)
{
	int		ret = SUCCEED;
	const char	*service = NULL;
	AGENT_RESULT	result;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_init_agent_result(&result);

	**value = '\0';

	switch (dcheck->type)
	{
		case SVC_SSH:
			service = "ssh";
			break;
		case SVC_LDAP:
			service = "ldap";
			break;
		case SVC_SMTP:
			service = "smtp";
			break;
		case SVC_FTP:
			service = "ftp";
			break;
		case SVC_HTTP:
			service = "http";
			break;
		case SVC_POP:
			service = "pop";
			break;
		case SVC_NNTP:
			service = "nntp";
			break;
		case SVC_IMAP:
			service = "imap";
			break;
		case SVC_TCP:
			service = "tcp";
			break;
		case SVC_HTTPS:
			service = "https";
			break;
		case SVC_TELNET:
			service = "telnet";
			break;
		case SVC_AGENT:
		case SVC_SNMPv1:
		case SVC_SNMPv2c:
		case SVC_SNMPv3:
		case SVC_ICMPPING:
			break;
		default:
			ret = FAIL;
			break;
	}

	if (SUCCEED == ret)
	{
		char		**pvalue;
		size_t		value_offset = 0;
		ZBX_FPING_HOST	host;
		DC_ITEM		item;
		char		key[MAX_STRING_LEN], error[ZBX_ITEM_ERROR_LEN_MAX];

		zbx_alarm_on(config_timeout);

		switch (dcheck->type)
		{
			/* simple checks */
			case SVC_SSH:
			case SVC_LDAP:
			case SVC_SMTP:
			case SVC_FTP:
			case SVC_HTTP:
			case SVC_POP:
			case SVC_NNTP:
			case SVC_IMAP:
			case SVC_TCP:
			case SVC_HTTPS:
			case SVC_TELNET:
				zbx_snprintf(key, sizeof(key), "net.tcp.service[%s,%s,%d]", service, ip, port);

				if (SUCCEED != zbx_execute_agent_check(key, 0, &result) || NULL ==
						ZBX_GET_UI64_RESULT(&result) || 0 == result.ui64)
				{
					ret = FAIL;
				}
				break;
			/* agent and SNMP checks */
			case SVC_AGENT:
			case SVC_SNMPv1:
			case SVC_SNMPv2c:
			case SVC_SNMPv3:
				memset(&item, 0, sizeof(DC_ITEM));

				zbx_strscpy(item.key_orig, dcheck->key_);
				item.key = item.key_orig;

				item.interface.useip = 1;
				item.interface.addr = ip;
				item.interface.port = port;

				item.value_type	= ITEM_VALUE_TYPE_STR;

				switch (dcheck->type)
				{
					case SVC_SNMPv1:
						item.snmp_version = ZBX_IF_SNMP_VERSION_1;
						item.type = ITEM_TYPE_SNMP;
						break;
					case SVC_SNMPv2c:
						item.snmp_version = ZBX_IF_SNMP_VERSION_2;
						item.type = ITEM_TYPE_SNMP;
						break;
					case SVC_SNMPv3:
						item.snmp_version = ZBX_IF_SNMP_VERSION_3;
						item.type = ITEM_TYPE_SNMP;
						break;
					default:
						item.type = ITEM_TYPE_ZABBIX;
						break;
				}

				if (SVC_AGENT == dcheck->type)
				{
					item.host.tls_connect = ZBX_TCP_SEC_UNENCRYPTED;

					if (SUCCEED == get_value_agent(&item, &result) &&
							NULL != (pvalue = ZBX_GET_TEXT_RESULT(&result)))
					{
						zbx_strcpy_alloc(value, value_alloc, &value_offset, *pvalue);
					}
					else
						ret = FAIL;
				}
				else
#ifdef HAVE_NETSNMP
				{
					item.snmp_community = strdup(dcheck->snmp_community);
					item.snmp_oid = strdup(dcheck->key_);

					zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
							NULL, NULL, NULL, NULL, NULL, &item.snmp_community,
							MACRO_TYPE_COMMON, NULL, 0);
					zbx_substitute_key_macros(&item.snmp_oid, NULL, NULL, NULL, NULL,
							MACRO_TYPE_SNMP_OID, NULL, 0);

					if (ZBX_IF_SNMP_VERSION_3 == item.snmp_version)
					{
						item.snmpv3_securityname =
								zbx_strdup(NULL, dcheck->snmpv3_securityname);
						item.snmpv3_securitylevel = dcheck->snmpv3_securitylevel;
						item.snmpv3_authpassphrase =
								zbx_strdup(NULL, dcheck->snmpv3_authpassphrase);
						item.snmpv3_privpassphrase =
								zbx_strdup(NULL, dcheck->snmpv3_privpassphrase);
						item.snmpv3_authprotocol = dcheck->snmpv3_authprotocol;
						item.snmpv3_privprotocol = dcheck->snmpv3_privprotocol;
						item.snmpv3_contextname = zbx_strdup(NULL, dcheck->snmpv3_contextname);

						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_securityname, MACRO_TYPE_COMMON, NULL, 0);
						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_authpassphrase, MACRO_TYPE_COMMON, NULL,
								0);
						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_privpassphrase, MACRO_TYPE_COMMON, NULL, 0);
						zbx_substitute_simple_macros_unmasked(NULL, NULL, NULL, NULL, NULL, NULL,
								NULL, NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_contextname, MACRO_TYPE_COMMON, NULL, 0);
					}

					if (SUCCEED == get_value_snmp(&item, &result, ZBX_NO_POLLER, config_timeout) &&
							NULL != (pvalue = ZBX_GET_TEXT_RESULT(&result)))
					{
						zbx_strcpy_alloc(value, value_alloc, &value_offset, *pvalue);
					}
					else
						ret = FAIL;

					zbx_free(item.snmp_community);
					zbx_free(item.snmp_oid);

					if (ZBX_IF_SNMP_VERSION_3 == item.snmp_version)
					{
						zbx_free(item.snmpv3_securityname);
						zbx_free(item.snmpv3_authpassphrase);
						zbx_free(item.snmpv3_privpassphrase);
						zbx_free(item.snmpv3_contextname);
					}
				}
#else
					ret = FAIL;
#endif	/* HAVE_NETSNMP */

				if (FAIL == ret && ZBX_ISSET_MSG(&result))
				{
					zabbix_log(LOG_LEVEL_DEBUG, "discovery: item [%s] error: %s",
							item.key, result.msg);
				}
				break;
			case SVC_ICMPPING:
				memset(&host, 0, sizeof(host));
				host.addr = strdup(ip);

				if (SUCCEED != zbx_ping(&host, 1, 3, 0, 0, 0, error, sizeof(error)) || 0 == host.rcv)
					ret = FAIL;

				zbx_free(host.addr);
				break;
			default:
				break;
		}

		zbx_alarm_off();
	}
	zbx_free_agent_result(&result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: check if service is available and update database                 *
 *                                                                            *
 ******************************************************************************/
static void	process_check(const DB_DCHECK *dcheck, int *host_status, char *ip, int now, zbx_vector_ptr_t *services,
		int config_timeout)
{
	const char	*start;
	char		*value = NULL;
	size_t		value_alloc = 128;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	value = (char *)zbx_malloc(value, value_alloc);

	for (start = dcheck->ports; '\0' != *start;)
	{
		char	*comma, *last_port;
		int	port, first, last;

		if (NULL != (comma = strchr(start, ',')))
			*comma = '\0';

		if (NULL != (last_port = strchr(start, '-')))
		{
			*last_port = '\0';
			first = atoi(start);
			last = atoi(last_port + 1);
			*last_port = '-';
		}
		else
			first = last = atoi(start);

		for (port = first; port <= last; port++)
		{
			zbx_dservice_t	*service;

			zabbix_log(LOG_LEVEL_DEBUG, "%s() port:%d", __func__, port);

			service = (zbx_dservice_t *)zbx_malloc(NULL, sizeof(zbx_dservice_t));
			service->status = (SUCCEED == discover_service(dcheck, ip, port, config_timeout, &value,
					&value_alloc) ? DOBJECT_STATUS_UP : DOBJECT_STATUS_DOWN);
			service->dcheckid = dcheck->dcheckid;
			service->itemtime = (time_t)now;
			service->port = port;
			zbx_strlcpy_utf8(service->value, value, ZBX_MAX_DISCOVERED_VALUE_SIZE);
			zbx_vector_ptr_append(services, service);

			/* update host status */
			if (-1 == *host_status || DOBJECT_STATUS_UP == service->status)
				*host_status = service->status;
		}

		if (NULL != comma)
		{
			*comma = ',';
			start = comma + 1;
		}
		else
			break;
	}
	zbx_free(value);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	process_checks(const zbx_db_drule *drule, int *host_status, char *ip, int unique, int now,
		zbx_vector_ptr_t *services, zbx_vector_uint64_t *dcheckids, int config_timeout)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_DCHECK	dcheck;
	char		sql[MAX_STRING_LEN];
	size_t		offset = 0;

	offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
			"select dcheckid,type,key_,snmp_community,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,snmpv3_authprotocol,snmpv3_privprotocol,"
				"ports,snmpv3_contextname"
			" from dchecks"
			" where druleid=" ZBX_FS_UI64,
			drule->druleid);

	if (0 != drule->unique_dcheckid)
	{
		offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, " and dcheckid%s" ZBX_FS_UI64,
				unique ? "=" : "<>", drule->unique_dcheckid);
	}

	zbx_snprintf(sql + offset, sizeof(sql) - offset, " order by dcheckid");

	result = zbx_db_select("%s", sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		memset(&dcheck, 0, sizeof(dcheck));

		ZBX_STR2UINT64(dcheck.dcheckid, row[0]);
		dcheck.type = atoi(row[1]);
		dcheck.key_ = row[2];
		dcheck.snmp_community = row[3];
		dcheck.snmpv3_securityname = row[4];
		dcheck.snmpv3_securitylevel = (unsigned char)atoi(row[5]);
		dcheck.snmpv3_authpassphrase = row[6];
		dcheck.snmpv3_privpassphrase = row[7];
		dcheck.snmpv3_authprotocol = (unsigned char)atoi(row[8]);
		dcheck.snmpv3_privprotocol = (unsigned char)atoi(row[9]);
		dcheck.ports = row[10];
		dcheck.snmpv3_contextname = row[11];

		zbx_vector_uint64_append(dcheckids, dcheck.dcheckid);

		process_check(&dcheck, host_status, ip, now, services, config_timeout);
	}
	zbx_db_free_result(result);
}

static int	process_services(const zbx_db_drule *drule, zbx_db_dhost *dhost, const char *ip, const char *dns,
		int now, const zbx_vector_ptr_t *services, zbx_vector_uint64_t *dcheckids)
{
	int	i, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_sort(dcheckids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	if (SUCCEED != (ret = zbx_db_lock_ids("dchecks", "dcheckid", dcheckids)))
		goto fail;

	for (i = 0; i < services->values_num; i++)
	{
		zbx_dservice_t	*service = (zbx_dservice_t *)services->values[i];

		if (FAIL == zbx_vector_uint64_bsearch(dcheckids, service->dcheckid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;

		if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		{
			zbx_discovery_update_service(drule, service->dcheckid, dhost, ip, dns, service->port,
					service->status, service->value, now);
		}
		else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
		{
			proxy_update_service(drule->druleid, service->dcheckid, ip, dns, service->port,
					service->status, service->value, now);
		}
	}
fail:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: process single discovery rule                                     *
 *                                                                            *
 ******************************************************************************/
static void	process_rule(zbx_db_drule *drule, int config_timeout)
{
	zbx_db_dhost		dhost;
	int			host_status, now;
	char			ip[ZBX_INTERFACE_IP_LEN_MAX], *start, *comma, dns[ZBX_INTERFACE_DNS_LEN_MAX];
	int			ipaddress[8];
	zbx_iprange_t		iprange;
	zbx_vector_ptr_t	services;
	zbx_vector_uint64_t	dcheckids;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() rule:'%s' range:'%s'", __func__, drule->name, drule->iprange);

	zbx_vector_ptr_create(&services);
	zbx_vector_uint64_create(&dcheckids);

	for (start = drule->iprange; '\0' != *start;)
	{
		if (NULL != (comma = strchr(start, ',')))
			*comma = '\0';

		zabbix_log(LOG_LEVEL_DEBUG, "%s() range:'%s'", __func__, start);

		if (SUCCEED != zbx_iprange_parse(&iprange, start))
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": wrong format of IP range \"%s\"",
					drule->name, start);
			goto next;
		}

		if (ZBX_DISCOVERER_IPRANGE_LIMIT < zbx_iprange_volume(&iprange))
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": IP range \"%s\" exceeds %d address limit",
					drule->name, start, ZBX_DISCOVERER_IPRANGE_LIMIT);
			goto next;
		}
#ifndef HAVE_IPV6
		if (ZBX_IPRANGE_V6 == iprange.type)
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": encountered IP range \"%s\","
					" but IPv6 support not compiled in", drule->name, start);
			goto next;
		}
#endif
		zbx_iprange_first(&iprange, ipaddress);

		do
		{
#ifdef HAVE_IPV6
			if (ZBX_IPRANGE_V6 == iprange.type)
			{
				zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x", (unsigned int)ipaddress[0],
						(unsigned int)ipaddress[1], (unsigned int)ipaddress[2],
						(unsigned int)ipaddress[3], (unsigned int)ipaddress[4],
						(unsigned int)ipaddress[5], (unsigned int)ipaddress[6],
						(unsigned int)ipaddress[7]);
			}
			else
			{
#endif
				zbx_snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned int)ipaddress[0],
						(unsigned int)ipaddress[1], (unsigned int)ipaddress[2],
						(unsigned int)ipaddress[3]);
#ifdef HAVE_IPV6
			}
#endif
			memset(&dhost, 0, sizeof(dhost));
			host_status = -1;

			now = time(NULL);

			zabbix_log(LOG_LEVEL_DEBUG, "%s() ip:'%s'", __func__, ip);

			zbx_alarm_on(config_timeout);
			zbx_gethost_by_ip(ip, dns, sizeof(dns));
			zbx_alarm_off();

			if (0 != drule->unique_dcheckid)
				process_checks(drule, &host_status, ip, 1, now, &services, &dcheckids, config_timeout);

			process_checks(drule, &host_status, ip, 0, now, &services, &dcheckids, config_timeout);

			zbx_db_begin();

			if (SUCCEED != zbx_db_lock_druleid(drule->druleid))
			{
				zbx_db_rollback();

				zabbix_log(LOG_LEVEL_DEBUG, "discovery rule '%s' was deleted during processing,"
						" stopping", drule->name);
				zbx_vector_ptr_clear_ext(&services, zbx_ptr_free);
				goto out;
			}

			if (SUCCEED != process_services(drule, &dhost, ip, dns, now, &services, &dcheckids))
			{
				zbx_db_rollback();

				zabbix_log(LOG_LEVEL_DEBUG, "all checks where deleted for discovery rule '%s'"
						" during processing, stopping", drule->name);
				zbx_vector_ptr_clear_ext(&services, zbx_ptr_free);
				goto out;
			}

			zbx_vector_uint64_clear(&dcheckids);
			zbx_vector_ptr_clear_ext(&services, zbx_ptr_free);

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
			{
				zbx_discovery_update_host(&dhost, host_status, now);
				zbx_process_events(NULL, NULL, NULL);
				zbx_clean_events();
			}
			else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
				proxy_update_host(drule->druleid, ip, dns, host_status, now);

			zbx_db_commit();
		}
		while (SUCCEED == zbx_iprange_next(&iprange, ipaddress));
next:
		if (NULL != comma)
		{
			*comma = ',';
			start = comma + 1;
		}
		else
			break;
	}
out:
	zbx_vector_ptr_destroy(&services);
	zbx_vector_uint64_destroy(&dcheckids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: clean dservices and dhosts not presenting in drule                *
 *                                                                            *
 ******************************************************************************/
static void	discovery_clean_services(zbx_uint64_t druleid)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*iprange = NULL;
	zbx_vector_uint64_t	keep_dhostids, del_dhostids, del_dserviceids;
	zbx_uint64_t		dhostid, dserviceid;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = zbx_db_select("select iprange from drules where druleid=" ZBX_FS_UI64, druleid);

	if (NULL != (row = zbx_db_fetch(result)))
		iprange = zbx_strdup(iprange, row[0]);

	zbx_db_free_result(result);

	if (NULL == iprange)
		goto out;

	zbx_vector_uint64_create(&keep_dhostids);
	zbx_vector_uint64_create(&del_dhostids);
	zbx_vector_uint64_create(&del_dserviceids);

	result = zbx_db_select(
			"select dh.dhostid,ds.dserviceid,ds.ip"
			" from dhosts dh"
				" left join dservices ds"
					" on dh.dhostid=ds.dhostid"
			" where dh.druleid=" ZBX_FS_UI64,
			druleid);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		ZBX_STR2UINT64(dhostid, row[0]);

		if (SUCCEED == zbx_db_is_null(row[1]))
		{
			zbx_vector_uint64_append(&del_dhostids, dhostid);
		}
		else if (SUCCEED != zbx_ip_in_list(iprange, row[2]))
		{
			ZBX_STR2UINT64(dserviceid, row[1]);

			zbx_vector_uint64_append(&del_dhostids, dhostid);
			zbx_vector_uint64_append(&del_dserviceids, dserviceid);
		}
		else
			zbx_vector_uint64_append(&keep_dhostids, dhostid);
	}
	zbx_db_free_result(result);

	zbx_free(iprange);

	if (0 != del_dserviceids.values_num)
	{
		int	i;

		/* remove dservices */

		zbx_vector_uint64_sort(&del_dserviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dservices where");
		zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "dserviceid",
				del_dserviceids.values, del_dserviceids.values_num);

		zbx_db_execute("%s", sql);

		/* remove dhosts */

		zbx_vector_uint64_sort(&keep_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&keep_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		zbx_vector_uint64_sort(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		for (i = 0; i < del_dhostids.values_num; i++)
		{
			dhostid = del_dhostids.values[i];

			if (FAIL != zbx_vector_uint64_bsearch(&keep_dhostids, dhostid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				zbx_vector_uint64_remove_noorder(&del_dhostids, i--);
		}
	}

	if (0 != del_dhostids.values_num)
	{
		zbx_vector_uint64_sort(&del_dhostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from dhosts where");
		zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "dhostid",
				del_dhostids.values, del_dhostids.values_num);

		zbx_db_execute("%s", sql);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&del_dserviceids);
	zbx_vector_uint64_destroy(&del_dhostids);
	zbx_vector_uint64_destroy(&keep_dhostids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int	process_discovery(time_t *nextcheck, int config_timeout)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			rule_count = 0, delay;
	char			*delay_str = NULL;
	zbx_dc_um_handle_t	*um_handle;
	zbx_uint64_t		druleid;
	time_t			now;

	now = time(NULL);

	if (SUCCEED != zbx_dc_drule_next(now, &druleid, nextcheck))
		return 0;

	um_handle = zbx_dc_open_user_macros();

	do
	{
		result = zbx_db_select(
				"select distinct r.iprange,r.name,c.dcheckid,r.delay"
				" from drules r"
					" left join dchecks c"
						" on c.druleid=r.druleid"
							" and c.uniq=1"
				" where r.druleid=" ZBX_FS_UI64, druleid);

		if (NULL != (row = zbx_db_fetch(result)))
		{
			zbx_db_drule	drule;

			rule_count++;

			delay_str = zbx_strdup(delay_str, row[3]);
			zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
					&delay_str, MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != zbx_is_time_suffix(delay_str, &delay, ZBX_LENGTH_UNLIMITED))
			{
				zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\": invalid update interval \"%s\"",
						row[1], delay_str);

				delay = ZBX_DEFAULT_INTERVAL;
			}
			else
			{
				memset(&drule, 0, sizeof(drule));

				drule.druleid = druleid;
				drule.iprange = row[0];
				drule.name = row[1];
				ZBX_DBROW2UINT64(drule.unique_dcheckid, row[2]);

				process_rule(&drule, config_timeout);
			}

			zbx_dc_drule_queue(now, druleid, delay);
			zbx_free(delay_str);

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
				discovery_clean_services(druleid);

		}
		zbx_db_free_result(result);

		now = time(NULL);
	}
	while (ZBX_IS_RUNNING() && SUCCEED == zbx_dc_drule_next(now, &druleid, nextcheck));

	zbx_dc_close_user_macros(um_handle);

	return rule_count;	/* performance metric */
}

/******************************************************************************
 *                                                                            *
 * Purpose: periodically try to find new hosts and services                   *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(discoverer_thread, args)
{
	zbx_thread_discoverer_args	*discoverer_args_in = (zbx_thread_discoverer_args *)
							(((zbx_thread_args_t *)args)->args);
	int				sleeptime = -1, rule_count = 0, old_rule_count = 0;
	double				sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t				last_stat_time, nextcheck = 0;
	zbx_ipc_async_socket_t		rtc;
	const zbx_thread_info_t		*info = &((zbx_thread_args_t *)args)->info;
	int				server_num = ((zbx_thread_args_t *)args)->info.server_num;
	int				process_num = ((zbx_thread_args_t *)args)->info.process_num;
	unsigned char			process_type = ((zbx_thread_args_t *)args)->info.process_type;
	zbx_uint32_t			rtc_msgs[] = {ZBX_RTC_SNMP_CACHE_RELOAD};

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(info->program_type),
			server_num, get_process_type_string(process_type), process_num);

	zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child(discoverer_args_in->zbx_config_tls, discoverer_args_in->zbx_get_program_type_cb_arg);
#endif
	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	zbx_db_connect(ZBX_DB_CONNECT_NORMAL);

	zbx_rtc_subscribe(process_type, process_num, rtc_msgs, ARRSIZE(rtc_msgs), discoverer_args_in->config_timeout,
			&rtc);

	while (ZBX_IS_RUNNING())
	{
		zbx_uint32_t	rtc_cmd;
		unsigned char	*rtc_data;

		sec = zbx_time();
		zbx_update_env(get_process_type_string(process_type), sec);

		if (0 != sleeptime)
		{
			zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, performing discovery]",
					get_process_type_string(process_type), process_num, old_rule_count,
					old_total_sec);
		}

		if ((int)sec >= nextcheck)
		{
			rule_count += process_discovery(&nextcheck, discoverer_args_in->config_timeout);
			total_sec += zbx_time() - sec;

			if (0 == nextcheck)
				nextcheck = time(NULL) + DISCOVERER_DELAY;
		}

		sleeptime = zbx_calculate_sleeptime(nextcheck, DISCOVERER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, performing "
						"discovery]", get_process_type_string(process_type), process_num,
						rule_count, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [processed %d rules in " ZBX_FS_DBL " sec, idle %d sec]",
						get_process_type_string(process_type), process_num, rule_count,
						total_sec, sleeptime);
				old_rule_count = rule_count;
				old_total_sec = total_sec;
			}
			rule_count = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		if (SUCCEED == zbx_rtc_wait(&rtc, info, &rtc_cmd, &rtc_data, sleeptime) && 0 != rtc_cmd)
		{
#ifdef HAVE_NETSNMP
			if (ZBX_RTC_SNMP_CACHE_RELOAD == rtc_cmd)
				zbx_clear_cache_snmp(process_type, process_num);
#endif
			if (ZBX_RTC_SHUTDOWN == rtc_cmd)
				break;
		}

	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
