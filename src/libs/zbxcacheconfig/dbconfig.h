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

#ifndef ZABBIX_DBCONFIG_H
#define ZABBIX_DBCONFIG_H

#include "zbxjson.h"
#include <stdatomic.h>

#include "zbxcacheconfig.h"
#include "user_macro.h"

#include "zbxmutexs.h"
#include "zbxalgo.h"
#include "zbxversion.h"
#include "zbx_trigger_constants.h"
#include "zbx_host_constants.h"

#define ZBX_MAINTENANCE_IDLE		0
#define ZBX_MAINTENANCE_RUNNING		1

#define ZBX_LOC_NOWHERE	0
#define ZBX_LOC_QUEUE	1
#define ZBX_LOC_POLLER	2

#define ZBX_ITEM_COLLECTED		0x01	/* force item rescheduling after new value collection */
#define ZBX_HOST_UNREACHABLE		0x02
#define ZBX_ITEM_KEY_CHANGED		0x04
#define ZBX_ITEM_TYPE_CHANGED		0x08
#define ZBX_ITEM_DELAY_CHANGED		0x10
#define ZBX_ITEM_NEW			0x20

typedef struct
{
	zbx_uint64_t	triggerid;
	const char		*description;
	const char		*expression;
	const char		*recovery_expression;
	const char		*error;
	const char		*correlation_tag;
	const char		*opdata;
	const char		*event_name;
	const unsigned char	*expression_bin;
	const unsigned char	*recovery_expression_bin;
	zbx_uint64_t		revision;
	zbx_uint64_t		timer_revision;
	unsigned char		topoindex;
	unsigned char		priority;
	unsigned char		type;
	unsigned char		locked;
	unsigned char		status; //ENABLED OR DISABLED
	unsigned char		functional;		/* see TRIGGER_FUNCTIONAL_* defines      */
	unsigned char		recovery_mode;		/* see TRIGGER_RECOVERY_MODE_* defines   */
	unsigned char		correlation_mode;	/* see ZBX_TRIGGER_CORRELATION_* defines */
	unsigned char		timer;
	unsigned char		flags;

	zbx_uint64_t		*itemids;

	zbx_vector_ptr_t	tags;
}
ZBX_DC_TRIGGER;

/* specifies if trigger expression/recovery expression has timer functions */
/* (date, time, now, dayofweek or dayofmonth)                              */
#define ZBX_TRIGGER_TIMER_DEFAULT		0x00
#define ZBX_TRIGGER_TIMER_EXPRESSION		0x01
#define ZBX_TRIGGER_TIMER_RECOVERY_EXPRESSION	0x02

typedef struct zbx_dc_trigger_deplist
{
	zbx_uint64_t		triggerid;
	int			refcount;
	ZBX_DC_TRIGGER		*trigger;
	zbx_vector_ptr_t	dependencies;
}
ZBX_DC_TRIGGER_DEPLIST;

typedef struct
{
	zbx_uint64_t	functionid;
	zbx_uint64_t	triggerid;
	zbx_uint64_t	itemid;
	const char	*function;
	const char	*parameter;
	zbx_uint64_t	revision;
	zbx_uint64_t	timer_revision;
	unsigned char	type;
}
ZBX_DC_FUNCTION;

typedef struct
{
	zbx_vector_uint64_pair_t	dep_itemids;
}
ZBX_DC_MASTERITEM;

typedef struct
{
	zbx_vector_ptr_t	preproc_ops;
}
ZBX_DC_PREPROCITEM;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		hostid;
	int					queue_next_check;
	zbx_uint64_t		interfaceid;
	//zbx_uint64_t		lastlogsize;
	zbx_uint64_t		valuemapid;
	const char 		*name;
	const char 		*description;
	const char		*key;
	const char		*port;
	const char		*delay;
	const char		*delay_ex;
	const char		*history_period;
	ZBX_DC_TRIGGER		**triggers;
	
	int			data_expected_from;
	zbx_uint64_t		revision;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		poller_type;

	unsigned char		inventory_link;
	unsigned char		location;
	unsigned char		flags;
	unsigned char		status;
	unsigned char		queue_priority;
	unsigned char		update_triggers;
	zbx_uint64_t		templateid;
	ZBX_DC_PREPROCITEM	*preproc_item;
	ZBX_DC_MASTERITEM	*master_item;

	zbx_vector_ptr_t	tags;
	u_int64_t			master_itemid;
	const char 			*params;
}
ZBX_DC_ITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	zbx_uint64_t	parent_itemid;
}
ZBX_DC_ITEM_DISCOVERY;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		hostid;
	zbx_uint64_t		templateid;
}
ZBX_DC_TEMPLATE_ITEM;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		hostid;
	zbx_uint64_t		templateid;
}
ZBX_DC_PROTOTYPE_ITEM;

typedef struct
{
	zbx_uint64_t	hostid;
	const char	*key;
	ZBX_DC_ITEM	*item_ptr;
}
ZBX_DC_ITEM_HK;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*units;
	const char	*trends_period;
}
ZBX_DC_NUMITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*snmp_oid;
	unsigned char	snmp_oid_type;
}
ZBX_DC_SNMPITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*ipmi_sensor;
}
ZBX_DC_IPMIITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*trapper_hosts;
}
ZBX_DC_TRAPITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	zbx_uint64_t	master_itemid;
	zbx_uint64_t	last_master_itemid;
	unsigned char	flags;
}
ZBX_DC_DEPENDENTITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*logtimefmt;
}
ZBX_DC_LOGITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*params;
	const char	*username;
	const char	*password;
}
ZBX_DC_DBITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*username;
	const char	*publickey;
	const char	*privatekey;
	const char	*password;
	const char	*params;
	unsigned char	authtype;
}
ZBX_DC_SSHITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*username;
	const char	*password;
	const char	*params;
}
ZBX_DC_TELNETITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*username;
	const char	*password;
}
ZBX_DC_SIMPLEITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*username;
	const char	*password;
	const char	*jmx_endpoint;
}
ZBX_DC_JMXITEM;

typedef struct
{
	zbx_uint64_t		itemid;
	const char		*params;
	const unsigned char	*formula_bin;
}
ZBX_DC_CALCITEM;

typedef struct
{
	zbx_uint64_t	itemid;
	const char	*timeout;
	const char	*url;
	const char	*query_fields;
	const char	*status_codes;
	const char	*http_proxy;
	const char	*headers;
	const char	*username;
	const char	*ssl_cert_file;
	const char	*ssl_key_file;
	const char	*ssl_key_password;
	const char	*password;
	const char	*posts;
	const char	*trapper_hosts;
	unsigned char	authtype;
	unsigned char	follow_redirects;
	unsigned char	post_type;
	unsigned char	retrieve_mode;
	unsigned char	request_method;
	unsigned char	output_format;
	unsigned char	verify_peer;
	unsigned char	verify_host;
	unsigned char	allow_traps;
}
ZBX_DC_HTTPITEM;

typedef struct
{
	zbx_uint64_t		itemid;
	const char		*script;
	const char		*timeout;
	zbx_vector_ptr_t	params;
}
ZBX_DC_SCRIPTITEM;

typedef struct
{
	const char	*tls_psk_identity;	/* pre-shared key identity           */
	const char	*tls_psk;		/* pre-shared key value (hex-string) */
	unsigned int	refcount;		/* reference count                   */
}
ZBX_DC_PSK;

ZBX_PTR_VECTOR_DECL(dc_item_ptr, ZBX_DC_ITEM *)

typedef struct
{
	zbx_uint64_t	httptestid;
	zbx_uint64_t	hostid;
	time_t		nextcheck;
	int		delay;
	unsigned char	status;
	unsigned char	location;
	zbx_uint64_t	revision;
}
zbx_dc_httptest_t;

typedef struct
{
	zbx_uint64_t	httptest_fieldid;
	zbx_uint64_t	httptestid;
}
zbx_dc_httptest_field_t;

typedef struct
{
	zbx_uint64_t	httpstepid;
	zbx_uint64_t	httptestid;
	zbx_uint64_t	revision;
}
zbx_dc_httpstep_t;

typedef struct
{
	zbx_uint64_t	httpstep_fieldid;
	zbx_uint64_t	httpstepid;
}
zbx_dc_httpstep_field_t;

ZBX_PTR_VECTOR_DECL(dc_httptest_ptr, zbx_dc_httptest_t *)

typedef struct
{
	zbx_uint64_t	hostid;
	zbx_uint64_t	proxy_hostid;
	zbx_uint64_t	items_active_normal;		/* On enabled hosts these two fields store number of enabled */
	zbx_uint64_t	items_active_notsupported;	/* and supported items and enabled and not supported items.  */
	zbx_uint64_t	items_disabled;			/* On "hosts" corresponding to proxies this and two fields   */
							/* above store cumulative statistics for all hosts monitored */
							/* by a particular proxy. */
							/* NOTE: On disabled hosts all items are counted as disabled. */
	zbx_uint64_t	maintenanceid;

	const char	*host;
	const char	*name;
	const char	*description;

	int		maintenance_from;
	int		data_expected_from;
	zbx_uint64_t	revision;

	unsigned char	maintenance_status;
	unsigned char	maintenance_type;
	unsigned char	status;

	/* 'tls_connect' and 'tls_accept' must be respected even if encryption support is not compiled in */
	unsigned char	tls_connect;
	unsigned char	tls_accept;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	const char	*tls_issuer;
	const char	*tls_subject;
	ZBX_DC_PSK	*tls_dc_psk;
#endif

	zbx_vector_ptr_t		interfaces_v;	/* for quick finding of all host interfaces in */
						/* 'config->interfaces' hashset */

	zbx_vector_dc_httptest_ptr_t	httptests;
	zbx_vector_dc_item_ptr_t	items;
}
ZBX_DC_HOST;

ZBX_PTR_VECTOR_DECL(dc_host_ptr, ZBX_DC_HOST *)

typedef struct
{
	const char	*host;
	const char	*listen_ip;
	const char	*listen_dns;
	const char	*host_metadata;
	int		flags;
	int		timestamp;
	unsigned short	listen_port;
}
ZBX_DC_AUTOREG_HOST;

typedef struct
{
	zbx_uint64_t	hostid;
	unsigned char	inventory_mode;
	const char	*values[HOST_INVENTORY_FIELD_COUNT];
}
ZBX_DC_HOST_INVENTORY;

typedef struct
{
	const char	*host;
	ZBX_DC_HOST	*host_ptr;
}
ZBX_DC_HOST_H;

typedef struct
{
	zbx_uint64_t	hostid;
	zbx_uint64_t	revision;
}
zbx_host_rev_t;

ZBX_VECTOR_DECL(host_rev, zbx_host_rev_t)

typedef struct
{
	zbx_uint64_t			hostid;
	zbx_uint64_t			hosts_monitored;	/* number of enabled hosts assigned to proxy */
	zbx_uint64_t			hosts_not_monitored;	/* number of disabled hosts assigned to proxy */
	double				required_performance;
	int				proxy_config_nextcheck;
	int				proxy_data_nextcheck;
	int				proxy_tasks_nextcheck;
	int				nextcheck;
	int				lastaccess;
	int				proxy_delay;
	zbx_proxy_suppress_t		nodata_win;
	int				last_cfg_error_time;	/* time when passive proxy misconfiguration error was seen */
								/* or 0 if no error */
	const char			*version_str;
	int				version_int;
	zbx_proxy_compatibility_t	compatibility;
	unsigned char			location;
	unsigned char			auto_compress;
	const char			*proxy_address;
	int				last_version_error_time;
	zbx_uint64_t			revision;

	zbx_vector_dc_host_ptr_t	hosts;
	zbx_vector_host_rev_t		removed_hosts;
}
ZBX_DC_PROXY;

typedef struct
{
	zbx_uint64_t	hostid;
	const char	*ipmi_username;
	const char	*ipmi_password;
	signed char	ipmi_authtype;
	unsigned char	ipmi_privilege;
}
ZBX_DC_IPMIHOST;

typedef struct
{
	const char			*key;
	const char			*value;
	zbx_vector_uint64_pair_t	macros;
	unsigned char			update;
}
zbx_dc_kv_t;

typedef struct
{
	const char	*path;
	zbx_hashset_t	kvs;
}
zbx_dc_kvs_path_t;

typedef struct
{
	zbx_uint64_t	interfaceid;
	zbx_uint64_t	hostid;
	const char	*ip;
	const char	*dns;
	const char	*port;
	unsigned char	type;
	unsigned char	main;
	unsigned char	useip;
	int		items_num;
}
ZBX_DC_INTERFACE;

typedef struct
{
	zbx_uint64_t	interfaceid;
	const char	*community;
	const char	*securityname;
	const char	*authpassphrase;
	const char	*privpassphrase;
	const char	*contextname;
	unsigned char	securitylevel;
	unsigned char	authprotocol;
	unsigned char	privprotocol;
	unsigned char	version;
	unsigned char	bulk;
	unsigned char	max_succeed;
	unsigned char	min_fail;
	int		max_repetitions;
}
ZBX_DC_SNMPINTERFACE;

typedef struct
{
	zbx_uint64_t		hostid;
	ZBX_DC_INTERFACE	*interface_ptr;
	unsigned char		type;
}
ZBX_DC_INTERFACE_HT;

typedef struct
{
	const char		*addr;
	zbx_vector_uint64_t	interfaceids;
}
ZBX_DC_INTERFACE_ADDR;

typedef struct
{
	zbx_uint64_t		interfaceid;
	zbx_vector_uint64_t	itemids;
}
ZBX_DC_INTERFACE_ITEM;

typedef struct
{
	const char		*name;
	zbx_vector_uint64_t	expressionids;
}
ZBX_DC_REGEXP;

typedef struct
{
	zbx_uint64_t	expressionid;
	const char	*expression;
	const char	*regexp;
	char		delimiter;
	unsigned char	type;
	unsigned char	case_sensitive;
}
ZBX_DC_EXPRESSION;

typedef struct
{
	const char	*severity_name[TRIGGER_SEVERITY_COUNT];
	const char	*instanceid;
	zbx_uint64_t	discovery_groupid;
	int		default_inventory_mode;
	unsigned char	snmptrap_logging;
	unsigned char	autoreg_tls_accept;
	const char	*default_timezone;
	int		auditlog_enabled;

	/* database configuration data for ZBX_CONFIG_DB_EXTENSION_* extensions */
	zbx_config_db_t	db;

	/* housekeeping related configuration data */
	zbx_config_hk_t	hk;
}
ZBX_DC_CONFIG_TABLE;

typedef struct
{
	zbx_uint64_t	hosts_monitored;		/* total number of enabled hosts */
	zbx_uint64_t	hosts_not_monitored;		/* total number of disabled hosts */
	zbx_uint64_t	items_active_normal;		/* total number of enabled and supported items */
	zbx_uint64_t	items_active_notsupported;	/* total number of enabled and not supported items */
	zbx_uint64_t	items_disabled;			/* total number of disabled items */
							/* (all items of disabled host are counted as disabled) */
	zbx_uint64_t	triggers_enabled_ok;		/* total number of enabled triggers with value OK */
	zbx_uint64_t	triggers_enabled_problem;	/* total number of enabled triggers with value PROBLEM */
	zbx_uint64_t	triggers_disabled;		/* total number of disabled triggers */
							/* (if at least one item or host involved in trigger is */
							/* disabled then trigger is counted as disabled) */
	double		required_performance;		/* required performance of server (values per second) */
	time_t		last_update;
	int		sync_ts;
}
ZBX_DC_STATUS;

typedef struct
{
	zbx_uint64_t	conditionid;
	zbx_uint64_t	actionid;
	unsigned char	conditiontype;
	unsigned char	op;
	const char	*value;
	const char	*value2;
}
zbx_dc_action_condition_t;

typedef struct
{
	zbx_uint64_t		actionid;
	const char		*formula;
	unsigned char		eventsource;
	unsigned char		evaltype;
	unsigned char		opflags;
	zbx_vector_ptr_t	conditions;
}
zbx_dc_action_t;

typedef struct
{
	zbx_uint64_t	triggertagid;
	zbx_uint64_t	triggerid;
	const char	*tag;
	const char	*value;
}
zbx_dc_trigger_tag_t;

typedef struct
{
	zbx_uint64_t	itemtagid;
	zbx_uint64_t	itemid;
	const char	*tag;
	const char	*value;
}
zbx_dc_item_tag_t;

typedef struct
{
	zbx_uint64_t	hosttagid;
	zbx_uint64_t	hostid;
	const char	*tag;
	const char	*value;
}
zbx_dc_host_tag_t;

typedef struct
{
	zbx_uint64_t		hostid;
	zbx_vector_ptr_t	tags;
		/* references to zbx_dc_host_tag_t records cached in config-> host_tags hashset */
}
zbx_dc_host_tag_index_t;

typedef struct
{
	const char	*tag;
}
zbx_dc_corr_condition_tag_t;

typedef struct
{
	const char	*tag;
	const char	*value;
	unsigned char	op;
}
zbx_dc_corr_condition_tag_value_t;

typedef struct
{
	zbx_uint64_t	groupid;
	unsigned char	op;
}
zbx_dc_corr_condition_group_t;

typedef struct
{
	const char	*oldtag;
	const char	*newtag;
}
zbx_dc_corr_condition_tag_pair_t;

typedef union
{
	zbx_dc_corr_condition_tag_t		tag;
	zbx_dc_corr_condition_tag_value_t	tag_value;
	zbx_dc_corr_condition_group_t		group;
	zbx_dc_corr_condition_tag_pair_t	tag_pair;
}
zbx_dc_corr_condition_data_t;

typedef struct
{
	zbx_uint64_t			corr_conditionid;
	zbx_uint64_t			correlationid;
	int				type;

	zbx_dc_corr_condition_data_t	data;
}
zbx_dc_corr_condition_t;

typedef struct
{
	zbx_uint64_t	corr_operationid;
	zbx_uint64_t	correlationid;
	unsigned char	type;
}
zbx_dc_corr_operation_t;

typedef struct
{
	zbx_uint64_t		correlationid;
	const char		*name;
	const char		*formula;
	unsigned char		evaltype;

	zbx_vector_ptr_t	conditions;
	zbx_vector_ptr_t	operations;
}
zbx_dc_correlation_t;

#define ZBX_DC_HOSTGROUP_FLAGS_NONE		0
#define ZBX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS	1

typedef struct
{
	zbx_uint64_t		groupid;
	const char		*name;

	zbx_vector_uint64_t	nested_groupids;
	zbx_hashset_t		hostids;
	unsigned char		flags;
}
zbx_dc_hostgroup_t;

typedef struct
{
	zbx_uint64_t	item_preprocid;
	zbx_uint64_t	itemid;
	int		step;
	int		error_handler;
	unsigned char	type;
	const char	*params;
	const char	*error_handler_params;
}
zbx_dc_preproc_op_t;

typedef struct
{
	zbx_uint64_t	item_script_paramid;
	zbx_uint64_t	itemid;
	const char	*name;
	const char	*value;
}
zbx_dc_scriptitem_param_t;

typedef struct
{
	zbx_uint64_t		maintenanceid;
	unsigned char		type;
	unsigned char		tags_evaltype;
	unsigned char		state;
	int			active_since;
	int			active_until;
	int			running_since;
	int			running_until;
	zbx_vector_uint64_t	groupids;
	zbx_vector_uint64_t	hostids;
	zbx_vector_ptr_t	tags;
	zbx_vector_ptr_t	periods;
}
zbx_dc_maintenance_t;

typedef struct
{
	zbx_uint64_t	maintenancetagid;
	zbx_uint64_t	maintenanceid;
	unsigned char	op;		/* condition operator */
	const char	*tag;
	const char	*value;
}
zbx_dc_maintenance_tag_t;

typedef struct
{
	zbx_uint64_t	timeperiodid;
	zbx_uint64_t	maintenanceid;
	unsigned char	type;
	int		every;
	int		month;
	int		dayofweek;
	int		day;
	int		start_time;
	int		period;
	int		start_date;
}
zbx_dc_maintenance_period_t;

typedef struct
{
	zbx_uint64_t	triggerid;
	int		nextcheck;
}
zbx_dc_timer_trigger_t;

typedef struct
{
	zbx_uint64_t		macroid;
	zbx_dc_kv_t		*kv;
	zbx_dc_kvs_path_t	*kv_path;
}
zbx_dc_macro_kv_t;

typedef struct
{
	zbx_uint64_t	druleid;
	zbx_uint64_t	proxy_hostid;
	time_t		nextcheck;
	int		delay;
	unsigned char	status;
	unsigned char	location;
	zbx_uint64_t	revision;
}
zbx_dc_drule_t;

typedef struct
{
	zbx_uint64_t	dcheckid;
	zbx_uint64_t	druleid;
}
zbx_dc_dcheck_t;

typedef struct
{
	zbx_uint64_t	connectortagid;
	zbx_uint64_t	connectorid;
	unsigned char	op;		/* condition operator */
	const char	*tag;
	const char	*value;
}
zbx_dc_connector_tag_t;

ZBX_PTR_VECTOR_DECL(dc_connector_tag, zbx_dc_connector_tag_t *)

typedef struct
{
	zbx_uint64_t			connectorid;
	unsigned char			protocol;
	unsigned char			data_type;
	const char			*url;
	int				max_records;
	int				max_senders;
	const char			*timeout;
	unsigned char			max_attempts;
	const char			*token;
	const char			*http_proxy;
	unsigned char			authtype;
	const char			*username;
	const char			*password;
	unsigned char			verify_peer;
	unsigned char			verify_host;
	const char			*ssl_cert_file;
	const char			*ssl_key_file;
	const char			*ssl_key_password;
	int				status;
	int				tags_evaltype;
	zbx_vector_dc_connector_tag_t	tags;
}
zbx_dc_connector_t;

typedef struct
{
	/* timestamp of the last host availability diff sent to sever, used only by proxies */
	int			availability_diff_ts;
	int			proxy_lastaccess_ts;
	int			sync_ts;

	unsigned int		internal_actions;		/* number of enabled internal actions */
	unsigned int		auto_registration_actions;	/* number of enabled auto resistration actions */

	zbx_dc_revision_t	revision;

	/* maintenance processing management */
	unsigned char		maintenance_update;		/* flag to trigger maintenance update by timers  */
	zbx_uint64_t		*maintenance_update_flags;	/* Array of flags to manage timer maintenance updates.*/
								/* Each array member contains 0/1 flag for 64 timers  */
								/* indicating if the timer must process maintenance.  */

	char			*session_token;

	zbx_hashset_t		items;
	zbx_hashset_t		items_hk;		/* hostid, key */
	zbx_hashset_t		item_discovery;
	zbx_hashset_t		template_items;		/* template items selected from items table */
	zbx_hashset_t		prototype_items;	/* item prototypes selected from items table */
	zbx_hashset_t		numitems;
	zbx_hashset_t		snmpitems;
	zbx_hashset_t		ipmiitems;
	zbx_hashset_t		trapitems;
	zbx_hashset_t		dependentitems;
	zbx_hashset_t		logitems;
	zbx_hashset_t		dbitems;
	zbx_hashset_t		sshitems;
	zbx_hashset_t		telnetitems;
	zbx_hashset_t		simpleitems;
	zbx_hashset_t		jmxitems;
	zbx_hashset_t		calcitems;
	zbx_hashset_t		httpitems;
	zbx_hashset_t		scriptitems;
	zbx_hashset_t		functions;
	zbx_hashset_t		triggers;
	zbx_hashset_t		hosts;
	zbx_hashset_t		hosts_h;		/* for searching hosts by 'host' name */
	zbx_hashset_t		hosts_p;		/* for searching proxies by 'host' name */
	zbx_hashset_t		proxies;
	zbx_hashset_t		autoreg_hosts;
	zbx_hashset_t		host_inventories;
	zbx_hashset_t		host_inventories_auto;	/* For caching of automatically populated host inventories. */
							/* Configuration syncer will read host_inventories without  */
							/* locking cache and therefore it cannot be updated by      */
							/* by history syncers when new data is received.	    */
	zbx_hashset_t		ipmihosts;
	zbx_hashset_t		gmacros;
	zbx_hashset_t		hmacros;
	zbx_hashset_t		interfaces;
	zbx_hashset_t		interfaces_snmp;
	zbx_hashset_t		interfaces_ht;		/* hostid, type */
	zbx_hashset_t		interface_snmpaddrs;	/* addr, interfaceids for SNMP interfaces */
	zbx_hashset_t		interface_snmpitems;	/* interfaceid, itemids for SNMP trap items */
	zbx_hashset_t		regexps;
	zbx_hashset_t		expressions;
	zbx_hashset_t		actions;
	zbx_hashset_t		action_conditions;
	zbx_hashset_t		trigger_tags;
	zbx_hashset_t		trigdeps;
	zbx_hashset_t		item_tags;
	zbx_hashset_t		host_tags;
	zbx_hashset_t		host_tags_index;	/* host tag index by hostid */
	zbx_hashset_t		correlations;
	zbx_hashset_t		corr_conditions;
	zbx_hashset_t		corr_operations;
	zbx_hashset_t		hostgroups;
	zbx_vector_ptr_t	hostgroups_name;	/* host groups sorted by name */
	zbx_vector_ptr_t	kvs_paths;
	zbx_hashset_t		gmacro_kv;
	zbx_hashset_t		hmacro_kv;
	zbx_hashset_t		preprocops;
	zbx_hashset_t		itemscript_params;
	zbx_hashset_t		maintenances;
	zbx_hashset_t		maintenance_periods;
	zbx_hashset_t		maintenance_tags;
	
	int last_items_change; // when changed_items has been added last time

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_hashset_t		psks;			/* for keeping PSK-identity and PSK pairs and for searching */
							/* by PSK identity */
#endif
	zbx_hashset_t		data_sessions;
	zbx_hashset_t		drules;
	zbx_hashset_t		dchecks;
	zbx_hashset_t		httptests;
	zbx_hashset_t		httptest_fields;
	zbx_hashset_t		httpsteps;
	zbx_hashset_t		httpstep_fields;
	zbx_hashset_t		connectors;
	zbx_hashset_t		connector_tags;
	zbx_hashset_t		sessions[ZBX_SESSION_TYPE_COUNT];
	zbx_binary_heap_t	queues[ZBX_POLLER_TYPE_COUNT];
	double				time_by_poller_type[ITEM_TYPE_MAX + 1];
	u_int64_t			runs_by_poller_type[ITEM_TYPE_MAX + 1];
	zbx_binary_heap_t	pqueue;
	zbx_binary_heap_t	trigger_queue;
	zbx_binary_heap_t	drule_queue;
	zbx_binary_heap_t	httptest_queue;		/* web scenario queue */
	ZBX_DC_CONFIG_TABLE	*config;
	ZBX_DC_STATUS		*status;
	zbx_hashset_t		strpool;
	zbx_um_cache_t		*um_cache;
	char			autoreg_psk_identity[HOST_TLS_PSK_IDENTITY_LEN_MAX];	/* autoregistration PSK */
	char			autoreg_psk[HOST_TLS_PSK_LEN_MAX];

	u_int64_t debug_trigger;
	u_int64_t debug_item;
	int server_time;
}
ZBX_DC_CONFIG;

typedef struct {
	obj_index_t *host_to_template_idx;
//	obj_index_t *deptrigger_to_trigger_idx;
	strpool_t strpool;
} GLB_CONFIG;

extern GLB_CONFIG *glb_config;

extern int	sync_in_progress;
extern ZBX_DC_CONFIG	*config;
extern zbx_rwlock_t	config_lock;
extern int		CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

#define	RDLOCK_CACHE	if (0 == sync_in_progress) zbx_rwlock_rdlock(config_lock); 
#define	WRLOCK_CACHE	if (0 == sync_in_progress) zbx_rwlock_wrlock(config_lock); 
#define	UNLOCK_CACHE	if (0 == sync_in_progress) zbx_rwlock_unlock(config_lock); 


//#define	RDLOCK_CACHE	if (0 == sync_in_progress) { LOG_INF("RDLOCK"); zbx_rwlock_rdlock(config_lock); LOG_INF("RDLOCKED %s:%d", __FILE__, __LINE__); }
//#define	WRLOCK_CACHE	if (0 == sync_in_progress) { LOG_INF("WRLOCK"); zbx_rwlock_wrlock(config_lock); LOG_INF("WRLOCKED %s:%d", __FILE__, __LINE__); }
//#define	UNLOCK_CACHE	if (0 == sync_in_progress) { zbx_rwlock_unlock(config_lock); LOG_INF("UNLOCKED %s:%d", __FILE__, __LINE__); }

extern zbx_rwlock_t	config_history_lock;

#define	RDLOCK_CACHE_CONFIG_HISTORY	zbx_rwlock_rdlock(config_history_lock)
#define	WRLOCK_CACHE_CONFIG_HISTORY	zbx_rwlock_wrlock(config_history_lock)
#define	UNLOCK_CACHE_CONFIG_HISTORY	zbx_rwlock_unlock(config_history_lock)

#define ZBX_IPMI_DEFAULT_AUTHTYPE	-1
#define ZBX_IPMI_DEFAULT_PRIVILEGE	2

void	dc_get_hostids_by_functionids(const zbx_uint64_t *functionids, int functionids_num,
		zbx_vector_uint64_t *hostids);

void	DCdump_configuration(void);

/* utility functions */
void	*DCfind_id(zbx_hashset_t *hashset, zbx_uint64_t id, size_t size, int *found);

/* string pool */
const char	*dc_strpool_intern(const char *str);
const char	*dc_strpool_acquire(const char *str);
void	dc_strpool_release(const char *str);
int	dc_strpool_replace(int found, const char **curr, const char *new_str);

/* host groups */
void	dc_get_nested_hostgroupids(zbx_uint64_t groupid, zbx_vector_uint64_t *nested_groupids);
void	dc_hostgroup_cache_nested_groupids(zbx_dc_hostgroup_t *parent_group);

/* synchronization */
typedef struct zbx_dbsync zbx_dbsync_t;

void	DCsync_maintenances(zbx_dbsync_t *sync);
void	DCsync_maintenance_tags(zbx_dbsync_t *sync);
void	DCsync_maintenance_periods(zbx_dbsync_t *sync);
void	DCsync_maintenance_groups(zbx_dbsync_t *sync);
void	DCsync_maintenance_hosts(zbx_dbsync_t *sync);

 
int zbx_dc_create_hello_json(struct zbx_json* j);
int zbx_dc_parce_hello_json(DC_PROXY *proxy,struct zbx_json_parse	*jp, int timediff);
zbx_uint64_t zbx_dc_recalc_topology(void);
int zbx_dc_set_topology_recalc(void);

int DC_CheckNeedsSync(); 
void DC_RequestSync();

/* maintenance support */

/* number of slots to store maintenance update flags */
#define ZBX_MAINTENANCE_UPDATE_FLAGS_NUM()	\
		((((size_t)CONFIG_FORKS[ZBX_PROCESS_TYPE_TIMER]) + sizeof(uint64_t) * 8 - 1) / (sizeof(uint64_t) * 8))

char	*dc_expand_user_macros_in_expression(const char *text, zbx_uint64_t *hostids, int hostids_num);
char	*dc_expand_user_macros_in_func_params(const char *params, zbx_uint64_t itemid);
char	*dc_expand_user_macros_in_calcitem(const char *formula, zbx_uint64_t hostid);

char	*dc_expand_user_macros(const char *text, const zbx_uint64_t *hostids, int hostids_num);

void		DCget_interface(DC_INTERFACE *dst_interface, const ZBX_DC_INTERFACE *src_interface);
ZBX_DC_HOST	*DCfind_host(const char *host);
ZBX_DC_ITEM	*DCfind_item(zbx_uint64_t hostid, const char *key);
void		DCget_function(DC_FUNCTION *dst_function, const ZBX_DC_FUNCTION *src_function);
void		DCget_trigger(DC_TRIGGER *dst_trigger, const ZBX_DC_TRIGGER *src_trigger, unsigned int flags);
int		DCitem_nextcheck_update(ZBX_DC_ITEM *item, const ZBX_DC_INTERFACE *interface, int flags, int now,
			char **error);



#define ZBX_TRIGGER_TIMER_NONE			0x0000
#define ZBX_TRIGGER_TIMER_TRIGGER		0x0001
#define ZBX_TRIGGER_TIMER_FUNCTION_TIME		0x0002
#define ZBX_TRIGGER_TIMER_FUNCTION_TREND	0x0004
#define ZBX_TRIGGER_TIMER_FUNCTION		(ZBX_TRIGGER_TIMER_FUNCTION_TIME | ZBX_TRIGGER_TIMER_FUNCTION_TREND)

zbx_um_cache_t	*um_cache_sync(zbx_um_cache_t *cache, zbx_uint64_t revision, zbx_dbsync_t *gmacros,
		zbx_dbsync_t *hmacros, zbx_dbsync_t *htmpls, const zbx_config_vault_t *config_vault);

#endif
