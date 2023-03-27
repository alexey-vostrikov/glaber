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

#include "zbxcommon.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Purpose: Returns Zabbix process name                                       *
 *                                                                            *
 * Parameters: proc_type - [IN] process type; ZBX_PROCESS_TYPE_*              *
 *                                                                            *
 * Comments: used in internals checks zabbix["process",...], process titles   *
 *           and log files                                                    *
 *                                                                            *
 ******************************************************************************/
const char	*get_process_type_string(unsigned char proc_type)
{
	switch (proc_type)
	{
		case GLB_PROCESS_TYPE_EVENTS_PROCESSOR:
			return "glb_events_processor";
		case GLB_PROCESS_TYPE_SNMP:
			return "glb_snmp_poller";
		case GLB_PROCESS_TYPE_PINGER:
			return "glb_pinger";
		case GLB_PROCESS_TYPE_WORKER:
			return "glb_worker";
		case GLB_PROCESS_TYPE_SERVER:
			return "glb_server";
		case GLB_PROCESS_TYPE_AGENT:
			return "glb_agent_poller";
		case ZBX_PROCESS_TYPE_POLLER:
			return "poller";
		case GLB_PROCESS_TYPE_API_TRAPPER:
			return "api_trapper";	
		case GLB_PROCESS_TYPE_PREPROCESSOR:
			return "glb_preproc_worker";
		case ZBX_PROCESS_TYPE_UNREACHABLE:
			return "unreachable poller";
		case ZBX_PROCESS_TYPE_IPMIPOLLER:
			return "ipmi poller";
		case ZBX_PROCESS_TYPE_PINGER:
			return "icmp pinger";
		case ZBX_PROCESS_TYPE_JAVAPOLLER:
			return "java poller";
		case ZBX_PROCESS_TYPE_HTTPPOLLER:
			return "http poller";
		case ZBX_PROCESS_TYPE_TRAPPER:
			return "trapper";
		case ZBX_PROCESS_TYPE_SNMPTRAPPER:
			return "snmp trapper";
		case ZBX_PROCESS_TYPE_PROXYPOLLER:
			return "proxy poller";
		case 9:
			return "deprecated_escalator";
		case ZBX_PROCESS_TYPE_HISTSYNCER:
			return "history syncer";
		case ZBX_PROCESS_TYPE_DISCOVERER:
			return "discoverer";
		case ZBX_PROCESS_TYPE_ALERTER:
			return "alerter";
		case ZBX_PROCESS_TYPE_TIMER:
			return "timer";
		case ZBX_PROCESS_TYPE_HOUSEKEEPER:
			return "housekeeper";
		case ZBX_PROCESS_TYPE_DATASENDER:
			return "data sender";
		case ZBX_PROCESS_TYPE_CONFSYNCER:
			return "configuration syncer";
		case ZBX_PROCESS_TYPE_SELFMON:
			return "self-monitoring";
		case ZBX_PROCESS_TYPE_VMWARE:
			return "vmware collector";
		case ZBX_PROCESS_TYPE_COLLECTOR:
			return "collector";
		case ZBX_PROCESS_TYPE_LISTENER:
			return "listener";
		case ZBX_PROCESS_TYPE_ACTIVE_CHECKS:
			return "active checks";
		case ZBX_PROCESS_TYPE_TASKMANAGER:
			return "task manager";
		case ZBX_PROCESS_TYPE_IPMIMANAGER:
			return "ipmi manager";
		case ZBX_PROCESS_TYPE_ALERTMANAGER:
			return "alert manager";
		case ZBX_PROCESS_TYPE_PREPROCMAN:
			return "preprocessing manager";
		case ZBX_PROCESS_TYPE_PREPROCESSOR:
			return "preprocessing worker";
		case ZBX_PROCESS_TYPE_LLDMANAGER:
			return "lld manager";
		case ZBX_PROCESS_TYPE_LLDWORKER:
			return "lld worker";
		case ZBX_PROCESS_TYPE_ALERTSYNCER:
			return "alert syncer";
		case ZBX_PROCESS_TYPE_HISTORYPOLLER:
			return "glb_calc_poller";
		case ZBX_PROCESS_TYPE_AVAILMAN:
			return "availability manager";
		case ZBX_PROCESS_TYPE_REPORTMANAGER:
			return "report manager";
		case ZBX_PROCESS_TYPE_REPORTWRITER:
			return "report writer";
		case ZBX_PROCESS_TYPE_SERVICEMAN:
			return "service manager";
		case ZBX_PROCESS_TYPE_TRIGGERHOUSEKEEPER:
			return "trigger housekeeper";
		case ZBX_PROCESS_TYPE_HA_MANAGER:
			return "ha manager";
		case ZBX_PROCESS_TYPE_ODBCPOLLER:
			return "odbc poller";
		case ZBX_PROCESS_TYPE_MAIN:
			return "main";
	}
	LOG_WRN("Unknown process type %d", proc_type);
	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

int	get_process_type_by_name(const char *proc_type_str)
{
	int	i;

	for (i = 0; i < ZBX_PROCESS_TYPE_COUNT; i++)
	{
		if (0 == strcmp(proc_type_str, get_process_type_string((unsigned char)i)))
			return i;
	}

	for (i = ZBX_PROCESS_TYPE_EXT_FIRST; i <= ZBX_PROCESS_TYPE_EXT_LAST; i++)
	{
		if (0 == strcmp(proc_type_str, get_process_type_string((unsigned char)i)))
			return i;
	}

	return ZBX_PROCESS_TYPE_UNKNOWN;
}

const char	*get_program_type_string(unsigned char program_type)
{
	switch (program_type)
	{
		case ZBX_PROGRAM_TYPE_SERVER:
			return "server";
		case ZBX_PROGRAM_TYPE_PROXY_ACTIVE:
		case ZBX_PROGRAM_TYPE_PROXY_PASSIVE:
			return "proxy";
		case ZBX_PROGRAM_TYPE_AGENTD:
			return "agent";
		case ZBX_PROGRAM_TYPE_SENDER:
			return "sender";
		case ZBX_PROGRAM_TYPE_GET:
			return "get";
		default:
			return "unknown";
	}
}

const char	*zbx_item_value_type_string(zbx_item_value_type_t value_type)
{
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			return "Numeric (float)";
		case ITEM_VALUE_TYPE_STR:
			return "Character";
		case ITEM_VALUE_TYPE_LOG:
			return "Log";
		case ITEM_VALUE_TYPE_UINT64:
			return "Numeric (unsigned)";
		case ITEM_VALUE_TYPE_TEXT:
			return "Text";
		default:
			return "unknown";
	}
}

const char	*zbx_interface_type_string(zbx_interface_type_t type)
{
	switch (type)
	{
		case INTERFACE_TYPE_AGENT:
			return "AGENT";
		case INTERFACE_TYPE_SNMP:
			return "SNMP";
		case INTERFACE_TYPE_IPMI:
			return "IPMI";
		case INTERFACE_TYPE_JMX:
			return "JMX";
		case INTERFACE_TYPE_OPT:
			return "optional";
		case INTERFACE_TYPE_ANY:
			return "any";
		case INTERFACE_TYPE_UNKNOWN:
		default:
			return "unknown";
	}
}

const char	*zbx_sysinfo_ret_string(int ret)
{
	switch (ret)
	{
		case SYSINFO_RET_OK:
			return "SYSINFO_SUCCEED";
		case SYSINFO_RET_FAIL:
			return "SYSINFO_FAIL";
		default:
			return "SYSINFO_UNKNOWN";
	}
}

const char	*zbx_result_string(int result)
{
	switch (result)
	{
		case SUCCEED:
			return "SUCCEED";
		case FAIL:
			return "FAIL";
		case CONFIG_ERROR:
			return "CONFIG_ERROR";
		case NOTSUPPORTED:
			return "NOTSUPPORTED";
		case NETWORK_ERROR:
			return "NETWORK_ERROR";
		case TIMEOUT_ERROR:
			return "TIMEOUT_ERROR";
		case AGENT_ERROR:
			return "AGENT_ERROR";
		case GATEWAY_ERROR:
			return "GATEWAY_ERROR";
		case SIG_ERROR:
			return "SIG_ERROR";
		case SYSINFO_RET_FAIL:
			return "SYSINFO_RET_FAIL";
		default:
			return "unknown";
	}
}
