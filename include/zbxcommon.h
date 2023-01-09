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

#ifndef ZABBIX_COMMON_H
#define ZABBIX_COMMON_H

//#define HAVE_GLB_TESTS 1

#include "zbxsysinc.h"
#include "module.h"
#include "version.h"

#if defined(__MINGW32__)
#	define __try
#	define __except(x) if (0)
#endif

#ifndef va_copy
#	if defined(__va_copy)
#		define va_copy(d, s) __va_copy(d, s)
#	else
#		define va_copy(d, s) memcpy(&d, &s, sizeof(va_list))
#	endif
#endif

#ifdef snprintf
#	undef snprintf
#endif
#define snprintf	ERROR_DO_NOT_USE_SNPRINTF_FUNCTION_TRY_TO_USE_ZBX_SNPRINTF

#ifdef sprintf
#	undef sprintf
#endif
#define sprintf		ERROR_DO_NOT_USE_SPRINTF_FUNCTION_TRY_TO_USE_ZBX_SNPRINTF

#ifdef strncpy
#	undef strncpy
#endif
#define strncpy		ERROR_DO_NOT_USE_STRNCPY_FUNCTION_TRY_TO_USE_ZBX_STRLCPY

#ifdef strcpy
#	undef strcpy
#endif
#define strcpy		ERROR_DO_NOT_USE_STRCPY_FUNCTION_TRY_TO_USE_ZBX_STRLCPY

#ifdef vsprintf
#	undef vsprintf
#endif
#define vsprintf	ERROR_DO_NOT_USE_VSPRINTF_FUNCTION_TRY_TO_USE_ZBX_VSNPRINTF

#ifdef strncat
#	undef strncat
#endif
#define strncat		ERROR_DO_NOT_USE_STRNCAT_FUNCTION_TRY_TO_USE_ZBX_STRLCAT

#ifdef strncasecmp
#	undef strncasecmp
#endif
#define strncasecmp	ERROR_DO_NOT_USE_STRNCASECMP_FUNCTION_TRY_TO_USE_ZBX_STRNCASECMP

#define ON	1
#define OFF	0

#if defined(_WINDOWS)
#	define	ZBX_SERVICE_NAME_LEN	64
extern char ZABBIX_SERVICE_NAME[ZBX_SERVICE_NAME_LEN];
extern char ZABBIX_EVENT_SOURCE[ZBX_SERVICE_NAME_LEN];

#	pragma warning (disable: 4996)	/* warning C4996: <function> was declared deprecated */
#endif

#if defined(__GNUC__) && __GNUC__ >= 7
#	define ZBX_FALLTHROUGH	__attribute__ ((fallthrough))
#else
#	define ZBX_FALLTHROUGH
#endif

//preprocessing types - used for glaber optimized preprocessing
#define GLB_PREPROC_MANAGER	0
#define GLB_PREPROC_LOCAL	1
#define GLB_NO_PREPROC	2

//for efficient polling in async mode
#define	POLL_FREE			108
#define POLL_CC_FETCHED		109
#define POLL_PREPARED		110
#define	POLL_POLLING		111
#define POLL_SKIPPED		112
#define POLL_PREPROCESSED	113
#define POLL_FINISHED		114
#define	POLL_QUEUED			115
#define	POLL_CONNECT_SENT	116	
#define	POLL_REQ_SENT		117
#define POLL_REQUEUED		118
#define POLL_NODATA			119

#define SUCCEED_OR_FAIL(result) (FAIL != (result) ? SUCCEED : FAIL)
const char	*zbx_sysinfo_ret_string(int ret);
const char	*zbx_result_string(int result);

#define MAX_ID_LEN			21
#define MAX_STRING_LEN			2048
#define MAX_BUFFER_LEN			65536
#define ZBX_MAX_HOSTNAME_LEN		128
#define ZBX_HOSTNAME_BUF_LEN	(ZBX_MAX_HOSTNAME_LEN + 1)
#define ZBX_MAX_DNSNAME_LEN		255	/* maximum host DNS name length from RFC 1035 */
						/*(without terminating '\0') */
#define MAX_EXECUTE_OUTPUT_LEN		(512 * ZBX_KIBIBYTE)

#define ZBX_MAX_UINT64		(~__UINT64_C(0))
#define ZBX_MAX_UINT64_LEN	21
#define ZBX_MAX_DOUBLE_LEN	24

#define ZBX_SIZE_T_MAX	(~(size_t)0)

#define is_pid(str, value) \
	is_uint_n_range(str, ZBX_SIZE_T_MAX, value, 4, 0x0, 4194304)

/******************************************************************************
 *                                                                            *
 * Macro: ZBX_UNUSED                                                          *
 *                                                                            *
 * Purpose: silences compiler warning about unused function parameter         *
 *                                                                            *
 * Parameters:                                                                *
 *      var       - [IN] the unused parameter                                 *
 *                                                                            *
 * Comments: Use only on unused, non-volatile function parameters!            *
 *                                                                            *
 ******************************************************************************/
#define ZBX_UNUSED(var) (void)(var)

/* item types */
typedef enum
{
	ITEM_TYPE_ZABBIX = 0,
/*	ITEM_TYPE_SNMPv1,*/
	ITEM_TYPE_TRAPPER = 2,
	ITEM_TYPE_SIMPLE,
/*	ITEM_TYPE_SNMPv2c,*/
	ITEM_TYPE_INTERNAL = 5,
/*	ITEM_TYPE_SNMPv3,*/
	ITEM_TYPE_ZABBIX_ACTIVE = 7,
/*	ITEM_TYPE_AGGREGATE, */
	ITEM_TYPE_HTTPTEST = 9,
	ITEM_TYPE_EXTERNAL,
	ITEM_TYPE_DB_MONITOR,
	ITEM_TYPE_IPMI,
	ITEM_TYPE_SSH,
	ITEM_TYPE_TELNET,
	ITEM_TYPE_CALCULATED,
	ITEM_TYPE_JMX,
	ITEM_TYPE_SNMPTRAP,
	ITEM_TYPE_DEPENDENT,
	ITEM_TYPE_HTTPAGENT,
	ITEM_TYPE_SNMP,
	ITEM_TYPE_SCRIPT,	/* 21 */
	ITEM_TYPE_WORKER_SERVER = 47,
	ITEM_TYPE_MAX
}
zbx_item_type_t;

#define ITEM_TYPE_AGENT ITEM_TYPE_ZABBIX

typedef enum
{
	INTERFACE_TYPE_UNKNOWN = 0,
	INTERFACE_TYPE_AGENT,
	INTERFACE_TYPE_SNMP,
	INTERFACE_TYPE_IPMI,
	INTERFACE_TYPE_JMX,
	INTERFACE_TYPE_OPT = 254,
	INTERFACE_TYPE_ANY = 255
}
zbx_interface_type_t;
const char	*zbx_interface_type_string(zbx_interface_type_t type);

#define INTERFACE_TYPE_COUNT	4	/* number of interface types */
extern const int	INTERFACE_TYPE_PRIORITY[INTERFACE_TYPE_COUNT];

#define SNMP_BULK_DISABLED	0
#define SNMP_BULK_ENABLED	1

#define ZBX_IF_SNMP_VERSION_1	1
#define ZBX_IF_SNMP_VERSION_2	2
#define ZBX_IF_SNMP_VERSION_3	3

#define ZBX_FLAG_DISCOVERY_NORMAL	0x00
#define ZBX_FLAG_DISCOVERY_RULE		0x01
#define ZBX_FLAG_DISCOVERY_PROTOTYPE	0x02
#define ZBX_FLAG_DISCOVERY_CREATED	0x04

#define ZBX_HOST_PROT_INTERFACES_INHERIT	0
#define ZBX_HOST_PROT_INTERFACES_CUSTOM		1

typedef enum
{
	ITEM_AUTHTYPE_PASSWORD = 0,
	ITEM_AUTHTYPE_PUBLICKEY
}
zbx_item_authtype_t;

/* event status */
#define EVENT_STATUS_RESOLVED		0
#define EVENT_STATUS_PROBLEM		1

/* event sources */
#define EVENT_SOURCE_TRIGGERS		0
#define EVENT_SOURCE_DISCOVERY		1
#define EVENT_SOURCE_AUTOREGISTRATION	2
#define EVENT_SOURCE_INTERNAL		3
#define EVENT_SOURCE_SERVICE		4
#define EVENT_SOURCE_COUNT		5

/* event objects */
#define EVENT_OBJECT_TRIGGER		0
#define EVENT_OBJECT_DHOST		1
#define EVENT_OBJECT_DSERVICE		2
#define EVENT_OBJECT_ZABBIX_ACTIVE	3
#define EVENT_OBJECT_ITEM		4
#define EVENT_OBJECT_LLDRULE		5
#define EVENT_OBJECT_SERVICE		6

/* acknowledged flags */
#define EVENT_NOT_ACKNOWLEDGED		0
#define EVENT_ACKNOWLEDGED		1

typedef enum
{
	DOBJECT_STATUS_UP = 0,
	DOBJECT_STATUS_DOWN,
	DOBJECT_STATUS_DISCOVER,
	DOBJECT_STATUS_LOST
}
zbx_dstatus_t;

/* item value types */
typedef enum
{
	ITEM_VALUE_TYPE_FLOAT = 0,
	ITEM_VALUE_TYPE_STR,
	ITEM_VALUE_TYPE_LOG,
	ITEM_VALUE_TYPE_UINT64,
	ITEM_VALUE_TYPE_TEXT,
	/* the number of defined value types */
	ITEM_VALUE_TYPE_MAX,
	ITEM_VALUE_TYPE_NONE,
}
zbx_item_value_type_t;
const char	*zbx_item_value_type_string(zbx_item_value_type_t value_type);

typedef struct
{
	int	timestamp;
	u_int64_t	logeventid; // both are
//u_int64_t 	objectid; //used to pass reference to the log triggering data for links/colorisation
	int	severity;
	char	*source;
	char	*value;
}
zbx_log_value_t;

typedef union
{
	double		dbl;
	zbx_uint64_t	ui64;
	char		*str;
	char		*err;
	zbx_log_value_t	*log;
}
history_value_t;

/* item data types */
typedef enum
{
	ITEM_DATA_TYPE_DECIMAL = 0,
	ITEM_DATA_TYPE_OCTAL,
	ITEM_DATA_TYPE_HEXADECIMAL,
	ITEM_DATA_TYPE_BOOLEAN
}
zbx_item_data_type_t;

/* service supported by discoverer */
typedef enum
{
	SVC_SSH = 0,
	SVC_LDAP,
	SVC_SMTP,
	SVC_FTP,
	SVC_HTTP,
	SVC_POP,
	SVC_NNTP,
	SVC_IMAP,
	SVC_TCP,
	SVC_AGENT,
	SVC_SNMPv1,
	SVC_SNMPv2c,
	SVC_ICMPPING,
	SVC_SNMPv3,
	SVC_HTTPS,
	SVC_TELNET
}
zbx_dservice_type_t;

typedef enum
{
	SYSMAP_ELEMENT_TYPE_HOST = 0,
	SYSMAP_ELEMENT_TYPE_MAP,
	SYSMAP_ELEMENT_TYPE_TRIGGER,
	SYSMAP_ELEMENT_TYPE_HOST_GROUP,
	SYSMAP_ELEMENT_TYPE_IMAGE
}
zbx_sysmap_element_types_t;

typedef enum
{
	GRAPH_YAXIS_TYPE_CALCULATED = 0,
	GRAPH_YAXIS_TYPE_FIXED,
	GRAPH_YAXIS_TYPE_ITEM_VALUE
}
zbx_graph_yaxis_types_t;

/* item key to distinguish extrnal calls */
#define SERVER_EXTERNAL_KEY	"script"

/* runtime control options */
#define ZBX_CONFIG_CACHE_RELOAD		"config_cache_reload"
#define ZBX_SERVICE_CACHE_RELOAD	"service_cache_reload"
#define ZBX_SECRETS_RELOAD		"secrets_reload"
#define ZBX_HOUSEKEEPER_EXECUTE		"housekeeper_execute"
#define ZBX_LOG_LEVEL_INCREASE		"log_level_increase"
#define ZBX_LOG_LEVEL_DECREASE		"log_level_decrease"
#define ZBX_SNMP_CACHE_RELOAD		"snmp_cache_reload"
#define ZBX_DIAGINFO			"diaginfo"
#define ZBX_TRIGGER_HOUSEKEEPER_EXECUTE "trigger_housekeeper_execute"
#define ZBX_HA_STATUS			"ha_status"
#define ZBX_HA_REMOVE_NODE		"ha_remove_node"
#define ZBX_HA_SET_FAILOVER_DELAY	"ha_set_failover_delay"
#define ZBX_USER_PARAMETERS_RELOAD	"userparameter_reload"
#define ZBX_PROXY_CONFIG_CACHE_RELOAD	"proxy_config_cache_reload"

/* value for not supported items */
#define ZBX_NOTSUPPORTED	"ZBX_NOTSUPPORTED"
/* the error message for not supported items when reason is unknown */
#define ZBX_NOTSUPPORTED_MSG	"Unknown error."

/* Zabbix Agent non-critical error (agents older than 2.0) */
#define ZBX_ERROR		"ZBX_ERROR"

/* media types */
typedef enum
{
	MEDIA_TYPE_EMAIL = 0,
	MEDIA_TYPE_EXEC,
	MEDIA_TYPE_SMS,
	MEDIA_TYPE_WEBHOOK = 4
}
zbx_media_type_t;

/* alert statuses */
typedef enum
{
	ALERT_STATUS_NOT_SENT = 0,
	ALERT_STATUS_SENT,
	ALERT_STATUS_FAILED,
	ALERT_STATUS_NEW
}
zbx_alert_status_t;

/* escalation statuses */
typedef enum
{
	ESCALATION_STATUS_ACTIVE = 0,
	ESCALATION_STATUS_RECOVERY,	/* only in server code, never in DB, deprecated */
	ESCALATION_STATUS_SLEEP,
	ESCALATION_STATUS_COMPLETED	/* only in server code, never in DB */
}
zbx_escalation_status_t;

/* alert types */
typedef enum
{
	ALERT_TYPE_MESSAGE = 0,
	ALERT_TYPE_COMMAND
}
zbx_alert_type_t;

/* item statuses */
#define ITEM_STATUS_ACTIVE		0
#define ITEM_STATUS_DISABLED		1


/* item states */
#define ITEM_STATE_NORMAL		0
#define ITEM_STATE_NOTSUPPORTED	1
#define ITEM_STATE_UNKNOWN	2

/* group statuses */
typedef enum
{
	GROUP_STATUS_ACTIVE = 0,
	GROUP_STATUS_DISABLED
}
zbx_group_status_type_t;

/* program type */
#define ZBX_PROGRAM_TYPE_SERVER		0x01
#define ZBX_PROGRAM_TYPE_PROXY_ACTIVE	0x02
#define ZBX_PROGRAM_TYPE_PROXY_PASSIVE	0x04
#define ZBX_PROGRAM_TYPE_PROXY		0x06	/* ZBX_PROGRAM_TYPE_PROXY_ACTIVE | ZBX_PROGRAM_TYPE_PROXY_PASSIVE */
#define ZBX_PROGRAM_TYPE_AGENTD		0x08
#define ZBX_PROGRAM_TYPE_SENDER		0x10
#define ZBX_PROGRAM_TYPE_GET		0x20
const char	*get_program_type_string(unsigned char program_type);

/* process type */
#define ZBX_PROCESS_TYPE_POLLER			0
#define ZBX_PROCESS_TYPE_UNREACHABLE		1
#define ZBX_PROCESS_TYPE_IPMIPOLLER		2
#define ZBX_PROCESS_TYPE_PINGER			3
#define ZBX_PROCESS_TYPE_JAVAPOLLER		4
#define ZBX_PROCESS_TYPE_HTTPPOLLER		5
#define ZBX_PROCESS_TYPE_TRAPPER		6
#define ZBX_PROCESS_TYPE_SNMPTRAPPER		7
#define ZBX_PROCESS_TYPE_PROXYPOLLER		8
#define ZBX_PROCESS_TYPE_ESCALATOR		9
#define ZBX_PROCESS_TYPE_HISTSYNCER		10
#define ZBX_PROCESS_TYPE_DISCOVERER		11
#define ZBX_PROCESS_TYPE_ALERTER		12
#define ZBX_PROCESS_TYPE_TIMER			13
#define ZBX_PROCESS_TYPE_HOUSEKEEPER		14
#define ZBX_PROCESS_TYPE_DATASENDER		15
#define ZBX_PROCESS_TYPE_CONFSYNCER		16
#define ZBX_PROCESS_TYPE_SELFMON		17
#define ZBX_PROCESS_TYPE_VMWARE			18
#define ZBX_PROCESS_TYPE_COLLECTOR		19
#define ZBX_PROCESS_TYPE_LISTENER		20
#define ZBX_PROCESS_TYPE_ACTIVE_CHECKS		21
#define ZBX_PROCESS_TYPE_TASKMANAGER		22
#define ZBX_PROCESS_TYPE_IPMIMANAGER		23
#define ZBX_PROCESS_TYPE_ALERTMANAGER		24
#define ZBX_PROCESS_TYPE_PREPROCMAN		25
#define ZBX_PROCESS_TYPE_PREPROCESSOR		26
#define ZBX_PROCESS_TYPE_LLDMANAGER		27
#define ZBX_PROCESS_TYPE_LLDWORKER		28
#define ZBX_PROCESS_TYPE_ALERTSYNCER		29
#define ZBX_PROCESS_TYPE_HISTORYPOLLER		30
#define ZBX_PROCESS_TYPE_AVAILMAN		31
#define ZBX_PROCESS_TYPE_REPORTMANAGER		32
#define ZBX_PROCESS_TYPE_REPORTWRITER		33
#define ZBX_PROCESS_TYPE_SERVICEMAN		34
#define ZBX_PROCESS_TYPE_TRIGGERHOUSEKEEPER	35
#define ZBX_PROCESS_TYPE_ODBCPOLLER		36
#define GLB_PROCESS_TYPE_SNMP	37
#define GLB_PROCESS_TYPE_PINGER	38
#define GLB_PROCESS_TYPE_WORKER	39
#define GLB_PROCESS_TYPE_SERVER	40
#define GLB_PROCESS_TYPE_AGENT	41
#define GLB_PROCESS_TYPE_API_TRAPPER	42
#define GLB_PROCESS_TYPE_PREPROCESSOR	43
#define ZBX_PROCESS_TYPE_COUNT		44	/* number of process types */


/* special processes that are not present worker list */
#define ZBX_PROCESS_TYPE_EXT_FIRST		126
#define ZBX_PROCESS_TYPE_HA_MANAGER		126
#define ZBX_PROCESS_TYPE_MAIN			127
#define ZBX_PROCESS_TYPE_EXT_LAST		127

#define ZBX_PROCESS_TYPE_UNKNOWN		255

const char	*get_process_type_string(unsigned char proc_type);
int		get_process_type_by_name(const char *proc_type_str);

/* maintenance */
typedef enum
{
	TIMEPERIOD_TYPE_ONETIME = 0,
/*	TIMEPERIOD_TYPE_HOURLY,*/
	TIMEPERIOD_TYPE_DAILY = 2,
	TIMEPERIOD_TYPE_WEEKLY,
	TIMEPERIOD_TYPE_MONTHLY
}
zbx_timeperiod_type_t;

typedef enum
{
	MAINTENANCE_TYPE_NORMAL = 0,
	MAINTENANCE_TYPE_NODATA
}
zbx_maintenance_type_t;

typedef enum
{
	ZBX_PROTOTYPE_STATUS_ENABLED,
	ZBX_PROTOTYPE_STATUS_DISABLED,
	ZBX_PROTOTYPE_STATUS_COUNT
}
zbx_prototype_status_t;

typedef enum
{
	ZBX_PROTOTYPE_DISCOVER,
	ZBX_PROTOTYPE_NO_DISCOVER,
	ZBX_PROTOTYPE_DISCOVER_COUNT
}
zbx_prototype_discover_t;

/* regular expressions */
#define EXPRESSION_TYPE_INCLUDED	0
#define EXPRESSION_TYPE_ANY_INCLUDED	1
#define EXPRESSION_TYPE_NOT_INCLUDED	2
#define EXPRESSION_TYPE_TRUE		3
#define EXPRESSION_TYPE_FALSE		4

#define ZBX_IGNORE_CASE			0
#define ZBX_CASE_SENSITIVE		1

/* HTTP tests statuses */
#define HTTPTEST_STATUS_MONITORED	0
#define HTTPTEST_STATUS_NOT_MONITORED	1

/* discovery rule */
#define DRULE_STATUS_MONITORED		0
#define DRULE_STATUS_NOT_MONITORED	1

/* host statuses */
#define HOST_STATUS_MONITORED		0
#define HOST_STATUS_NOT_MONITORED	1
/*#define HOST_STATUS_UNREACHABLE	2*/
#define HOST_STATUS_TEMPLATE		3
/*#define HOST_STATUS_DELETED		4*/
#define HOST_STATUS_PROXY_ACTIVE	5
#define HOST_STATUS_PROXY_PASSIVE	6
#define HOST_STATUS_DOMAIN			7
#define	HOST_STATUS_SERVER			8

/* host group types */
#define HOSTGROUP_TYPE_HOST		0
#define HOSTGROUP_TYPE_TEMPLATE		1

/* host maintenance status */
#define HOST_MAINTENANCE_STATUS_OFF	0
#define HOST_MAINTENANCE_STATUS_ON	1

/* host inventory mode */
#define HOST_INVENTORY_DISABLED		-1	/* the host has no record in host_inventory */
						/* only in server code, never in DB */
#define HOST_INVENTORY_MANUAL		0
#define HOST_INVENTORY_AUTOMATIC	1
#define HOST_INVENTORY_COUNT		2

#define HOST_INVENTORY_FIELD_COUNT	70

/* interface availability */
#define INTERFACE_AVAILABLE_UNKNOWN		0
#define INTERFACE_AVAILABLE_TRUE		1
#define INTERFACE_AVAILABLE_FALSE		2

/* trigger statuses */
#define TRIGGER_STATUS_ENABLED		0
#define TRIGGER_STATUS_DISABLED		1

/* trigger types */
#define TRIGGER_TYPE_NORMAL		0
#define TRIGGER_TYPE_MULTIPLE_TRUE	1

/* trigger values */
#define TRIGGER_VALUE_OK		0
#define TRIGGER_VALUE_PROBLEM		1
#define TRIGGER_VALUE_UNKNOWN		2	/* only in server code, never in DB */
#define TRIGGER_VALUE_NONE		3	/* only in server code, never in DB */

/* trigger states */
//#define TRIGGER_STATE_NORMAL		0
//#define TRIGGER_STATE_UNKNOWN		1

/* trigger severity */
#define TRIGGER_SEVERITY_NOT_CLASSIFIED	0
#define TRIGGER_SEVERITY_INFORMATION	1
#define TRIGGER_SEVERITY_WARNING	2
#define TRIGGER_SEVERITY_AVERAGE	3
#define TRIGGER_SEVERITY_HIGH		4
#define TRIGGER_SEVERITY_DISASTER	5
#define TRIGGER_SEVERITY_COUNT		6	/* number of trigger severities */
#define TRIGGER_SEVERITY_UNDEFINED	255
		
/* trigger recovery mode */
#define TRIGGER_RECOVERY_MODE_EXPRESSION		0
#define TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION	1
#define TRIGGER_RECOVERY_MODE_NONE			2

/* business service values */
#define SERVICE_VALUE_OK		0
#define SERVICE_VALUE_PROBLEM		1

#define ITEM_LOGTYPE_INFORMATION	1
#define ITEM_LOGTYPE_WARNING		2
#define ITEM_LOGTYPE_ERROR		4
#define ITEM_LOGTYPE_FAILURE_AUDIT	7
#define ITEM_LOGTYPE_SUCCESS_AUDIT	8
#define ITEM_LOGTYPE_CRITICAL		9
#define ITEM_LOGTYPE_VERBOSE		10

/* media statuses */
#define MEDIA_STATUS_ACTIVE	0
#define MEDIA_STATUS_DISABLED	1

/* action statuses */
#define ACTION_STATUS_ACTIVE	0
#define ACTION_STATUS_DISABLED	1

/* action escalation processing mode */
#define ACTION_PAUSE_SUPPRESSED_FALSE	0	/* process escalation for suppressed events */
#define ACTION_PAUSE_SUPPRESSED_TRUE	1	/* pause escalation for suppressed events */

/* action escalation canceled notification mode */
#define ACTION_NOTIFY_IF_CANCELED_TRUE	1	/* notify about canceled escalations for action (default) */
#define ACTION_NOTIFY_IF_CANCELED_FALSE	0	/* do not notify about canceled escalations for action */

/* max number of retries for alerts */
#define ALERT_MAX_RETRIES	3

/* media type statuses */
#define MEDIA_TYPE_STATUS_ACTIVE	0
#define MEDIA_TYPE_STATUS_DISABLED	1

/* SMTP security options */
#define SMTP_SECURITY_NONE	0
#define SMTP_SECURITY_STARTTLS	1
#define SMTP_SECURITY_SSL	2

/* SMTP authentication options */
#define SMTP_AUTHENTICATION_NONE		0
#define SMTP_AUTHENTICATION_NORMAL_PASSWORD	1

/* operation types */
#define OPERATION_TYPE_MESSAGE		0
#define OPERATION_TYPE_COMMAND		1
#define OPERATION_TYPE_HOST_ADD		2
#define OPERATION_TYPE_HOST_REMOVE	3
#define OPERATION_TYPE_GROUP_ADD	4
#define OPERATION_TYPE_GROUP_REMOVE	5
#define OPERATION_TYPE_TEMPLATE_ADD	6
#define OPERATION_TYPE_TEMPLATE_REMOVE	7
#define OPERATION_TYPE_HOST_ENABLE	8
#define OPERATION_TYPE_HOST_DISABLE	9
#define OPERATION_TYPE_HOST_INVENTORY	10
#define OPERATION_TYPE_RECOVERY_MESSAGE	11
#define OPERATION_TYPE_UPDATE_MESSAGE	12

/* normal and recovery operations */
#define ZBX_OPERATION_MODE_NORMAL	0
#define ZBX_OPERATION_MODE_RECOVERY	1
#define ZBX_OPERATION_MODE_UPDATE	2

/* algorithms for service status calculation */
#define ZBX_SERVICE_STATUS_CALC_SET_OK			0
#define ZBX_SERVICE_STATUS_CALC_MOST_CRITICAL_ALL	1
#define ZBX_SERVICE_STATUS_CALC_MOST_CRITICAL_ONE	2

/* HTTP item types */
#define ZBX_HTTPITEM_TYPE_RSPCODE	0
#define ZBX_HTTPITEM_TYPE_TIME		1
#define ZBX_HTTPITEM_TYPE_SPEED		2
#define ZBX_HTTPITEM_TYPE_LASTSTEP	3
#define ZBX_HTTPITEM_TYPE_LASTERROR	4

/* proxy_history flags */
#define PROXY_HISTORY_FLAG_META		0x01
#define PROXY_HISTORY_FLAG_NOVALUE	0x02

#define PROXY_HISTORY_MASK_NOVALUE	(PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE)

/* global correlation constants */
#define ZBX_CORRELATION_ENABLED				0
#define ZBX_CORRELATION_DISABLED			1

#define ZBX_CORR_CONDITION_OLD_EVENT_TAG		0
#define ZBX_CORR_CONDITION_NEW_EVENT_TAG		1
#define ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP		2
#define ZBX_CORR_CONDITION_EVENT_TAG_PAIR		3
#define ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE		4
#define ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE		5

#define ZBX_CORR_OPERATION_CLOSE_OLD			0
#define ZBX_CORR_OPERATION_CLOSE_NEW			1

/* trigger correlation modes */
#define ZBX_TRIGGER_CORRELATION_NONE	0
#define ZBX_TRIGGER_CORRELATION_TAG	1

/* acknowledgment actions (flags) */
#define ZBX_PROBLEM_UPDATE_CLOSE		0x0001
#define ZBX_PROBLEM_UPDATE_ACKNOWLEDGE		0x0002
#define ZBX_PROBLEM_UPDATE_MESSAGE		0x0004
#define ZBX_PROBLEM_UPDATE_SEVERITY		0x0008
#define ZBX_PROBLEM_UPDATE_UNACKNOWLEDGE	0x0010
#define ZBX_PROBLEM_UPDATE_SUPPRESS		0x0020
#define ZBX_PROBLEM_UPDATE_UNSUPPRESS		0x0040

#define ZBX_PROBLEM_UPDATE_ACTION_COUNT	7

/* database double precision upgrade states */
#define ZBX_DB_DBL_PRECISION_DISABLED	0
#define ZBX_DB_DBL_PRECISION_ENABLED	1

#define ZBX_USER_ONLINE_TIME	600

/* user role permissions */
typedef enum
{
	ROLE_PERM_DENY = 0,
	ROLE_PERM_ALLOW = 1,
}
zbx_user_role_permission_t;

#define ZBX_USER_ROLE_PERMISSION_ACTIONS_DEFAULT_ACCESS		"actions.default_access"
#define ZBX_USER_ROLE_PERMISSION_ACTIONS_EXECUTE_SCRIPTS	"actions.execute_scripts"

#define ZBX_USER_ROLE_PERMISSION_UI_DEFAULT_ACCESS		"ui.default_access"
#define ZBX_USER_ROLE_PERMISSION_UI_MONITORING_SERVICES		"ui.monitoring.services"

/* user permissions */
typedef enum
{
	USER_TYPE_ZABBIX_USER = 1,
	USER_TYPE_ZABBIX_ADMIN,
	USER_TYPE_SUPER_ADMIN
}
zbx_user_type_t;

typedef struct
{
	zbx_uint64_t	userid;
	zbx_user_type_t	type;
	zbx_uint64_t	roleid;
	char		*username;
}
zbx_user_t;

typedef enum
{
	PERM_DENY = 0,
	PERM_READ = 2,
	PERM_READ_WRITE
}
zbx_user_permission_t;

typedef struct
{
	unsigned char	type;
	unsigned char	execute_on;
	char		*port;
	unsigned char	authtype;
	char		*username;
	char		*password;
	char		*publickey;
	char		*privatekey;
	char		*command;
	char		*command_orig;
	zbx_uint64_t	scriptid;
	unsigned char	host_access;
	int		timeout;
}
zbx_script_t;

#define ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT	0
#define ZBX_SCRIPT_TYPE_IPMI		1
#define ZBX_SCRIPT_TYPE_SSH		2
#define ZBX_SCRIPT_TYPE_TELNET		3
#define ZBX_SCRIPT_TYPE_WEBHOOK		5

#define ZBX_SCRIPT_SCOPE_ACTION	1
#define ZBX_SCRIPT_SCOPE_HOST	2
#define ZBX_SCRIPT_SCOPE_EVENT	4

#define ZBX_SCRIPT_EXECUTE_ON_AGENT	0
#define ZBX_SCRIPT_EXECUTE_ON_SERVER	1
#define ZBX_SCRIPT_EXECUTE_ON_PROXY	2	/* fall back to execution on server if target not monitored by proxy */

#define POLLER_DELAY		5
#define DISCOVERER_DELAY	60

#define HOUSEKEEPER_STARTUP_DELAY	30	/* in minutes */

#define ZBX_DEFAULT_INTERVAL	SEC_PER_MIN

#define	GET_SENDER_TIMEOUT	60

#ifndef MAX
#	define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#	define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define zbx_calloc(old, nmemb, size)	zbx_calloc2(__FILE__, __LINE__, old, nmemb, size)
#define zbx_malloc(old, size)		zbx_malloc2(__FILE__, __LINE__, old, size)
#define zbx_realloc(src, size)		zbx_realloc2(__FILE__, __LINE__, src, size)
#define zbx_strdup(old, str)		zbx_strdup2(__FILE__, __LINE__, old, str)
//null-tolerant version of strdup
#define strdup_null_safe(str)		strdup_null_safe2(__FILE__, __LINE__, str)
#define ZBX_STRDUP(var, str)	(var = zbx_strdup(var, str))

void	*zbx_calloc2(const char *filename, int line, void *old, size_t nmemb, size_t size);
void	*zbx_malloc2(const char *filename, int line, void *old, size_t size);
void	*zbx_realloc2(const char *filename, int line, void *old, size_t size);
char	*zbx_strdup2(const char *filename, int line, char *old, const char *str);
char	*strdup_null_safe2(const char *filename, int line, const char *str);

void	*zbx_guaranteed_memset(void *v, int c, size_t n);

#define zbx_free(ptr)		\
				\
do				\
{				\
	if (ptr)		\
	{			\
		free(ptr);	\
		ptr = NULL;	\
	}			\
}				\
while (0)

#define zbx_fclose(file)	\
				\
do				\
{				\
	if (file)		\
	{			\
		fclose(file);	\
		file = NULL;	\
	}			\
}				\
while (0)

#define THIS_SHOULD_NEVER_HAPPEN										\
														\
do														\
{														\
	zbx_error("ERROR [file and function: <%s,%s>, revision:%s, line:%d] Something impossible has just"	\
			" happened.", __FILE__, __func__, ZABBIX_REVISION, __LINE__);				\
	zbx_backtrace();											\
}														\
while (0)

/* to avoid dependency on libzbxnix.a */
#define	THIS_SHOULD_NEVER_HAPPEN_NO_BACKTRACE									\
	zbx_error("ERROR [file and function: <%s,%s>, revision:%s, line:%d] Something impossible has just"	\
			" happened.", __FILE__, __func__, ZABBIX_REVISION, __LINE__);				\

extern const char	*progname;
extern const char	title_message[];
extern const char	syslog_app_name[];
extern const char	*usage_message[];
extern const char	*help_message[];

#define ARRSIZE(a)	(sizeof(a) / sizeof(*a))

void	zbx_help(void);
void	zbx_usage(void);
void	zbx_version(void);

const char	*get_program_name(const char *path);
typedef unsigned char	(*zbx_get_program_type_f)(void);
typedef const char	*(*zbx_get_progname_f)(void);

typedef enum
{
	ZBX_TASK_START = 0,
	ZBX_TASK_PRINT_SUPPORTED,
	ZBX_TASK_TEST_METRIC,
	ZBX_TASK_SHOW_USAGE,
	ZBX_TASK_SHOW_VERSION,
	ZBX_TASK_SHOW_HELP,
#ifdef _WINDOWS
	ZBX_TASK_INSTALL_SERVICE,
	ZBX_TASK_UNINSTALL_SERVICE,
	ZBX_TASK_START_SERVICE,
	ZBX_TASK_STOP_SERVICE
#else
	ZBX_TASK_RUNTIME_CONTROL
#endif
}
zbx_task_t;

typedef enum
{
	HTTPTEST_AUTH_NONE = 0,
	HTTPTEST_AUTH_BASIC,
	HTTPTEST_AUTH_NTLM,
	HTTPTEST_AUTH_NEGOTIATE,
	HTTPTEST_AUTH_DIGEST
}
zbx_httptest_auth_t;

#define ZBX_TASK_FLAG_MULTIPLE_AGENTS	0x01
#define ZBX_TASK_FLAG_FOREGROUND	0x02

typedef struct
{
	zbx_task_t	task;
	unsigned int	flags;
	int		data;
	char		*opts;
}
ZBX_TASK_EX;

#define NET_DELAY_MAX	(SEC_PER_MIN / 4)

typedef struct
{
	int	values_num;
	int	period_end;
#define ZBX_PROXY_SUPPRESS_DISABLE	0x00
#define ZBX_PROXY_SUPPRESS_ACTIVE	0x01
#define ZBX_PROXY_SUPPRESS_MORE		0x02
#define ZBX_PROXY_SUPPRESS_EMPTY	0x04
#define ZBX_PROXY_SUPPRESS_ENABLE	(	\
		ZBX_PROXY_SUPPRESS_ACTIVE |	\
		ZBX_PROXY_SUPPRESS_MORE |	\
		ZBX_PROXY_SUPPRESS_EMPTY)
	int	flags;
}
zbx_proxy_suppress_t;

#define ZBX_RTC_MSG_SHIFT	0
#define ZBX_RTC_SCOPE_SHIFT	8
#define ZBX_RTC_DATA_SHIFT	16

#define ZBX_RTC_MSG_MASK	0x000000ff
#define ZBX_RTC_SCOPE_MASK	0x0000ff00
#define ZBX_RTC_DATA_MASK	0xffff0000

#define ZBX_RTC_GET_MSG(task)	(int)(((unsigned int)task & ZBX_RTC_MSG_MASK) >> ZBX_RTC_MSG_SHIFT)
#define ZBX_RTC_GET_SCOPE(task)	(int)(((unsigned int)task & ZBX_RTC_SCOPE_MASK) >> ZBX_RTC_SCOPE_SHIFT)
#define ZBX_RTC_GET_DATA(task)	(int)(((unsigned int)task & ZBX_RTC_DATA_MASK) >> ZBX_RTC_DATA_SHIFT)

#define ZBX_RTC_MAKE_MESSAGE(msg, scope, data)	((msg << ZBX_RTC_MSG_SHIFT) | (scope << ZBX_RTC_SCOPE_SHIFT) | \
	(data << ZBX_RTC_DATA_SHIFT))

#define ZBX_KIBIBYTE		1024
#define ZBX_MEBIBYTE		1048576
#define ZBX_GIBIBYTE		1073741824
#define ZBX_TEBIBYTE		__UINT64_C(1099511627776)

#define SEC_PER_MIN		60
#define SEC_PER_HOUR		3600
#define SEC_PER_DAY		86400
#define SEC_PER_WEEK		(7 * SEC_PER_DAY)
#define SEC_PER_MONTH		(30 * SEC_PER_DAY)
#define SEC_PER_YEAR		(365 * SEC_PER_DAY)
#define ZBX_JAN_2038		2145916800
#define ZBX_JAN_1970_IN_SEC	2208988800.0	/* 1970 - 1900 in seconds */

#define ZBX_MAX_RECV_DATA_SIZE		(1 * ZBX_GIBIBYTE)
#if defined(_WINDOWS)
#define ZBX_MAX_RECV_LARGE_DATA_SIZE	(1 * ZBX_GIBIBYTE)
#else
#define ZBX_MAX_RECV_LARGE_DATA_SIZE	(__UINT64_C(16) * ZBX_GIBIBYTE)
#endif

/* max length of base64 data */
#define ZBX_MAX_B64_LEN		(16 * ZBX_KIBIBYTE)

/* string functions that could not be moved into libzbxstr.a because they */
/* are used by libzbxcommon.a */

/* used by log which will be part of common*/
#if defined(__GNUC__) || defined(__clang__)
#	define __zbx_attr_format_printf(idx1, idx2) __attribute__((__format__(__printf__, (idx1), (idx2))))
#else
#	define __zbx_attr_format_printf(idx1, idx2)
#endif

/* used by cuid and also by log */
size_t	zbx_snprintf(char *str, size_t count, const char *fmt, ...) __zbx_attr_format_printf(3, 4);

/* could be moved into libzbxstr.a but it seems to be logically grouped with surrounding functions */
void	zbx_snprintf_alloc(char **str, size_t *alloc_len, size_t *offset, const char *fmt, ...)
		__zbx_attr_format_printf(4, 5);

/* used by log */
size_t	zbx_vsnprintf(char *str, size_t count, const char *fmt, va_list args);

/* used by log */
char	*zbx_dsprintf(char *dest, const char *f, ...) __zbx_attr_format_printf(2, 3);

/* used by zbxcommon, setproctitle */
size_t	zbx_strlcpy(char *dst, const char *src, size_t siz);

/* used by dsprintf, which is used by log */
char	*zbx_dvsprintf(char *dest, const char *f, va_list args);

#define VALUE_ERRMSG_MAX	128
#define ZBX_LENGTH_UNLIMITED	0x7fffffff

#if defined(_WINDOWS) || defined(__MINGW32__)
wchar_t	*zbx_acp_to_unicode(const char *acp_string);
wchar_t	*zbx_utf8_to_unicode(const char *utf8_string);
wchar_t	*zbx_oemcp_to_unicode(const char *oemcp_string);
#endif
/* string functions that could not be moved into libzbxstr.a because they */
/* are used by libzbxcommon.a END */

/* future proctitle library */
void	zbx_setproctitle(const char *fmt, ...) __zbx_attr_format_printf(1, 2);
/* future proctitle library END */

void	zbx_error(const char *fmt, ...) __zbx_attr_format_printf(1, 2);

/* misc functions */
int	zbx_validate_hostname(const char *hostname);

void	zbx_backtrace(void);

int	get_nearestindex(const void *p, size_t sz, int num, zbx_uint64_t id);
int	uint64_array_add(zbx_uint64_t **values, int *alloc, int *num, zbx_uint64_t value, int alloc_step);
void	uint64_array_remove(zbx_uint64_t *values, int *num, const zbx_uint64_t *rm_values, int rm_num);

#if defined(_WINDOWS) || defined(__MINGW32__)
const OSVERSIONINFOEX	*zbx_win_getversion(void);
void	zbx_wmi_get(const char *wmi_namespace, const char *wmi_query, double timeout, char **utf8_value);
#endif

#if defined(_WINDOWS)
typedef struct __stat64	zbx_stat_t;
int	__zbx_stat(const char *path, zbx_stat_t *buf);
int	__zbx_open(const char *pathname, int flags);
#elif defined(__MINGW32__)
typedef struct _stat64	zbx_stat_t;
#else
typedef struct stat	zbx_stat_t;
#endif	/* _WINDOWS */

typedef struct
{
	zbx_fs_time_t	modification_time;	/* time of last modification */
	zbx_fs_time_t	access_time;		/* time of last access */
	zbx_fs_time_t	change_time;		/* time of last status change */
}
zbx_file_time_t;

int	zbx_get_file_time(const char *path, int sym, zbx_file_time_t *time);
void	find_cr_lf_szbyte(const char *encoding, const char **cr, const char **lf, size_t *szbyte);
int	zbx_read(int fd, char *buf, size_t count, const char *encoding);
int	zbx_is_regular_file(const char *path);
char	*zbx_fgets(char *buffer, int size, FILE *fp);
int	zbx_write_all(int fd, const char *buf, size_t n);

int	MAIN_ZABBIX_ENTRY(int flags);

zbx_uint64_t	zbx_letoh_uint64(zbx_uint64_t data);
zbx_uint64_t	zbx_htole_uint64(zbx_uint64_t data);

zbx_uint32_t	zbx_letoh_uint32(zbx_uint32_t data);
zbx_uint32_t	zbx_htole_uint32(zbx_uint32_t data);

unsigned char	get_interface_type_by_item_type(unsigned char type);

#define ZBX_SESSION_ACTIVE		0
#define ZBX_SESSION_PASSIVE		1
#define ZBX_AUTH_TOKEN_ENABLED		0
#define ZBX_AUTH_TOKEN_DISABLED		1
#define ZBX_AUTH_TOKEN_NEVER_EXPIRES	0

#define ZBX_DO_NOT_SEND_RESPONSE	0
#define ZBX_SEND_RESPONSE		1

/* Do not forget to synchronize HOST_TLS_* definitions with DB schema ! */
#define HOST_TLS_ISSUER_LEN		4096				/* for up to 1024 UTF-8 characters */
#define HOST_TLS_ISSUER_LEN_MAX		(HOST_TLS_ISSUER_LEN + 1)
#define HOST_TLS_SUBJECT_LEN		4096				/* for up to 1024 UTF-8 characters */
#define HOST_TLS_SUBJECT_LEN_MAX	(HOST_TLS_SUBJECT_LEN + 1)
#define HOST_TLS_PSK_IDENTITY_LEN	512				/* for up to 128 UTF-8 characters */
#define HOST_TLS_PSK_IDENTITY_LEN_MAX	(HOST_TLS_PSK_IDENTITY_LEN + 1)
#define HOST_TLS_PSK_LEN		512				/* for up to 256 hex-encoded bytes (ASCII) */
#define HOST_TLS_PSK_LEN_MAX		(HOST_TLS_PSK_LEN + 1)
#define HOST_TLS_PSK_LEN_MIN		32				/* for 16 hex-encoded bytes (128-bit PSK) */

#define ZBX_PSK_FOR_HOST		0x01				/* PSK can be used for a known host */
#define ZBX_PSK_FOR_AUTOREG		0x02				/* PSK can be used for host autoregistration */
#define ZBX_PSK_FOR_PROXY		0x04				/* PSK is configured on proxy */

void	zbx_alarm_flag_set(void);
void	zbx_alarm_flag_clear(void);

#ifndef _WINDOWS
unsigned int	zbx_alarm_on(unsigned int seconds);
unsigned int	zbx_alarm_off(void);
#endif

int	zbx_alarm_timed_out(void);

#define zbx_bsearch(key, base, nmemb, size, compar)	(0 == (nmemb) ? NULL : bsearch(key, base, nmemb, size, compar))

#define ZBX_PREPROC_MULTIPLIER			1
#define ZBX_PREPROC_RTRIM			2
#define ZBX_PREPROC_LTRIM			3
#define ZBX_PREPROC_TRIM			4
#define ZBX_PREPROC_REGSUB			5
#define ZBX_PREPROC_BOOL2DEC			6
#define ZBX_PREPROC_OCT2DEC			7
#define ZBX_PREPROC_HEX2DEC			8
#define ZBX_PREPROC_DELTA_VALUE			9
#define ZBX_PREPROC_DELTA_SPEED			10
#define ZBX_PREPROC_XPATH			11
#define ZBX_PREPROC_JSONPATH			12
#define ZBX_PREPROC_VALIDATE_RANGE		13
#define ZBX_PREPROC_VALIDATE_REGEX		14
#define ZBX_PREPROC_VALIDATE_NOT_REGEX		15
#define ZBX_PREPROC_ERROR_FIELD_JSON		16
#define ZBX_PREPROC_ERROR_FIELD_XML		17
#define ZBX_PREPROC_ERROR_FIELD_REGEX		18
#define ZBX_PREPROC_THROTTLE_VALUE		19
#define ZBX_PREPROC_THROTTLE_TIMED_VALUE	20
#define ZBX_PREPROC_SCRIPT			21
#define ZBX_PREPROC_PROMETHEUS_PATTERN		22
#define ZBX_PREPROC_PROMETHEUS_TO_JSON		23
#define ZBX_PREPROC_CSV_TO_JSON			24
#define ZBX_PREPROC_STR_REPLACE			25
#define ZBX_PREPROC_VALIDATE_NOT_SUPPORTED	26
#define ZBX_PREPROC_XML_TO_JSON			27
#define GLB_PREPROC_THROTTLE_TIMED_VALUE_AGG	126

#define GLB_PREPROC_DISPATCH_ITEM_BY_IP	125
#define GLB_PREPROC_DISPATCH_ITEM	127
#define GLB_PREPROC_JSON_FILTER 	128
#define GLB_PREPROC_DISCOVERY_PREPARE  129

/* custom on fail actions */
#define ZBX_PREPROC_FAIL_DEFAULT	0
#define ZBX_PREPROC_FAIL_DISCARD_VALUE	1
#define ZBX_PREPROC_FAIL_SET_VALUE	2
#define ZBX_PREPROC_FAIL_SET_ERROR	3

/* internal on fail actions */
#define ZBX_PREPROC_FAIL_FORCE_ERROR	4

#define ZBX_HTTPFIELD_HEADER		0
#define ZBX_HTTPFIELD_VARIABLE		1
#define ZBX_HTTPFIELD_POST_FIELD	2
#define ZBX_HTTPFIELD_QUERY_FIELD	3

#define ZBX_POSTTYPE_RAW		0
#define ZBX_POSTTYPE_FORM		1
#define ZBX_POSTTYPE_JSON		2
#define ZBX_POSTTYPE_XML		3

#define ZBX_RETRIEVE_MODE_CONTENT	0
#define ZBX_RETRIEVE_MODE_HEADERS	1
#define ZBX_RETRIEVE_MODE_BOTH		2

void	zbx_update_env(double time_now);

#define ZBX_PROBLEM_SUPPRESSED_FALSE	0
#define ZBX_PROBLEM_SUPPRESSED_TRUE	1

/* includes terminating '\0' */
#define CUID_LEN	26
void	zbx_new_cuid(char *cuid);

typedef struct
{
	char	*tag;
	char	*value;
}
zbx_tag_t;

void	zbx_free_tag(zbx_tag_t *tag);

#define ZBX_STR2UCHAR(var, string) var = (unsigned char)atoi(string)

#define ZBX_CONST_STRING(str) "" str
#define ZBX_CONST_STRLEN(str) (sizeof(ZBX_CONST_STRING(str)) - 1)

/* time and memory size suffixes */
zbx_uint64_t	suffix2factor(char c);

#define DEBUG_MESSAGE_HOST 

#ifndef DEBUG_ITEM
u_int64_t DC_get_debug_item();
#define DEBUG_ITEM(id, message,...) {if ( DC_get_debug_item() == id && id > 0 )\
		zabbix_log(LOG_LEVEL_INFORMATION,  "In %s:%d, debug_item:%ld, " message, __FILE__, __LINE__, id, ##__VA_ARGS__);}
#endif

#ifndef DEBUG_TRIGGER
u_int64_t DC_get_debug_trigger();
#define DEBUG_TRIGGER(id, message,...) if ( DC_get_debug_trigger() == id && id > 0 )\
		zabbix_log(LOG_LEVEL_INFORMATION,  "In %s:%d, debug_trigger:%ld, " message, __FILE__, __LINE__, id, ##__VA_ARGS__);
#endif

#ifndef HALT_HERE
#define HALT_HERE(message,...) { zabbix_log(LOG_LEVEL_WARNING, "In %s:%d, intentional halt: " message, __FILE__, __LINE__, ##__VA_ARGS__); zbx_backtrace(); exit(-1); }
#endif

#ifndef RUN_ONCE_IN

#define RUN_ONCE_IN(freq) { \
        static int __lastcall= 0; \
        int __now = time(NULL); \
        if (__lastcall + freq > __now) \
            return; \
        __lastcall = __now; \
        }

#define RUN_ONCE_IN_WITH_RET(freq, ret) { \
        static int __lastcall= 0; \
        int __now = time(NULL); \
        if (__lastcall + freq > __now) \
            return ret; \
        __lastcall = __now; \
        }
#endif

#endif
