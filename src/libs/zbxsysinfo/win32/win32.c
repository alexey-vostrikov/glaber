/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#include "zbxsysinfo.h"
#include "../sysinfo.h"

#include "zbxwinservice.h"

ZBX_METRIC	parameters_specific[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"vfs.fs.size",		CF_HAVEPARAMS,	vfs_fs_size,		"c:,free"},
	{"vfs.fs.discovery",	0,		vfs_fs_discovery,	NULL},
	{"vfs.fs.get",		0,		vfs_fs_get,		NULL},

	{"net.tcp.listen",	CF_HAVEPARAMS,	net_tcp_listen,		"80"},

	{"net.if.in",		CF_HAVEPARAMS,	net_if_in,		"MS TCP Loopback interface,bytes"},
	{"net.if.out",		CF_HAVEPARAMS,	net_if_out,		"MS TCP Loopback interface,bytes"},
	{"net.if.total",	CF_HAVEPARAMS,	net_if_total,		"MS TCP Loopback interface,bytes"},
	{"net.if.discovery",	0,		net_if_discovery,	NULL},
	{"net.if.list",		0,		net_if_list,		NULL},

	{"vm.memory.size",	CF_HAVEPARAMS,	vm_memory_size,		"free"},

	{"proc.get",		CF_HAVEPARAMS,	proc_get,		"svchost.exe"},
	{"proc.num",		CF_HAVEPARAMS,	proc_num,		"svchost.exe"},

	{"system.cpu.util",	CF_HAVEPARAMS,	system_cpu_util,	"all,system,avg1"},
	{"system.cpu.load",	CF_HAVEPARAMS,	system_cpu_load,	"all,avg1"},
	{"system.cpu.num",	CF_HAVEPARAMS,	system_cpu_num,		"online"},
	{"system.cpu.discovery",0,		system_cpu_discovery,	NULL},

	{"system.sw.arch",	0,		system_sw_arch,		NULL},

	{"system.swap.size",	CF_HAVEPARAMS,	system_swap_size,	"all,free"},
	{"vm.vmemory.size",	CF_HAVEPARAMS,	vm_vmemory_size,	"total"},

	{"system.uptime",	0,		system_uptime,		NULL},

	{"system.uname",	0,		system_uname,		NULL},

	{"service.discovery",	0,		discover_services,	NULL},
	{"service.info",	CF_HAVEPARAMS,	get_service_info,	ZABBIX_SERVICE_NAME},
	{"service_state",	CF_HAVEPARAMS,	get_service_state,	ZABBIX_SERVICE_NAME},
	{"services",		CF_HAVEPARAMS,	get_list_of_services,	NULL},
	{"perf_counter",	CF_HAVEPARAMS,	perf_counter,		"\\System\\Processes"},
	{"perf_counter_en",	CF_HAVEPARAMS,	perf_counter_en,	"\\System\\Processes"},
	{"perf_instance.discovery",	CF_HAVEPARAMS,	perf_instance_discovery,	"Processor"},
	{"perf_instance_en.discovery",	CF_HAVEPARAMS,	perf_instance_discovery_en,	"Processor"},
	{"proc_info",		CF_HAVEPARAMS,	proc_info,		"svchost.exe"},

	{"__UserPerfCounter",	CF_HAVEPARAMS,	user_perf_counter,	""},

	{"wmi.get",		CF_HAVEPARAMS,	wmi_get,
							"root\\cimv2,select Caption from Win32_OperatingSystem"},
	{"wmi.getall",		CF_HAVEPARAMS,	wmi_getall,
							"root\\cimv2,select * from Win32_OperatingSystem"},

	{"registry.data",		CF_HAVEPARAMS,	registry_data,		NULL},
	{"registry.get",		CF_HAVEPARAMS,	registry_get,		NULL},

	{NULL}
};
