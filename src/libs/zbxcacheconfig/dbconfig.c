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

#include "dbconfig.h"
#include "log.h"
#include "zbxtasks.h"
#include "zbxserver.h"
#include "zbxshmem.h"
#include "zbxregexp.h"
#include "cfg.h"
#include "zbxcrypto.h"
#include "zbxvault.h"
#include "base64.h"
#include "zbxdbhigh.h"
#include "dbsync.h"
#include "actions.h"
#include "zbxtrends.h"
#include "zbxserialize.h"
#include "user_macro.h"
#include "zbxexpr.h"
#include "zbxnum.h"
#include "zbxtime.h"
#include "zbxip.h"
#include "zbxsysinfo.h"
#include "zbx_host_constants.h"
#include "zbx_trigger_constants.h"
#include "zbx_item_constants.h"
#include "../glb_state/glb_state_items.h"
#include "../glb_state/glb_state_hosts.h"
#include "../glb_state/glb_state_triggers.h"
#include "../glb_conf/conf_hosts.h"
#include "../../zabbix_server/glb_poller/poller_ipc.h"
#include "../../zabbix_server/glb_poller/glb_poller.h"
#include "../../zabbix_server/poller/poller.h"
#include "../../zabbix_server/dbsyncer/trends.h"
#include "zbxconnector.h"
#include "zbxpreproc.h"
#include "log.h"

int sync_in_progress = 0;

#define START_SYNC               \
	WRLOCK_CACHE_CONFIG_HISTORY; \
	WRLOCK_CACHE;                \
	sync_in_progress = 1
#define FINISH_SYNC       \
	sync_in_progress = 0; \
	UNLOCK_CACHE;         \
	UNLOCK_CACHE_CONFIG_HISTORY;

#define ZBX_SNMP_OID_TYPE_NORMAL 0
#define ZBX_SNMP_OID_TYPE_DYNAMIC 1
#define ZBX_SNMP_OID_TYPE_MACRO 2

/* trigger is functional unless its expression contains disabled or not monitored items */
#define TRIGGER_FUNCTIONAL_TRUE 0
#define TRIGGER_FUNCTIONAL_FALSE 1

/* trigger contains time functions and is also scheduled by timer queue */
#define ZBX_TRIGGER_TIMER_UNKNOWN 0
#define ZBX_TRIGGER_TIMER_QUEUE 1

/* item priority in poller queue */
#define ZBX_QUEUE_PRIORITY_HIGH 0
#define ZBX_QUEUE_PRIORITY_NORMAL 1
#define ZBX_QUEUE_PRIORITY_LOW 2

#define ZBX_DEFAULT_ITEM_UPDATE_INTERVAL 60

#define ZBX_TRIGGER_POLL_INTERVAL (SEC_PER_MIN * 10)

/* shorthand macro for calling in_maintenance_without_data_collection() */
#define DCin_maintenance_without_data_collection(dc_host, dc_item)      \
	in_maintenance_without_data_collection(dc_host->maintenance_status, \
										   dc_host->maintenance_type, dc_item->type)

ZBX_PTR_VECTOR_IMPL(cached_proxy_ptr, zbx_cached_proxy_t *)
ZBX_PTR_VECTOR_IMPL(dc_httptest_ptr, zbx_dc_httptest_t *)
ZBX_PTR_VECTOR_IMPL(dc_host_ptr, ZBX_DC_HOST *)
ZBX_PTR_VECTOR_IMPL(dc_item_ptr, ZBX_DC_ITEM *)
ZBX_VECTOR_IMPL(host_rev, zbx_host_rev_t)
ZBX_PTR_VECTOR_IMPL(dc_connector_tag, zbx_dc_connector_tag_t *)



static int cmp_key_id(const char *key_1, const char *key_2)
{
	const char *p, *q;

	for (p = key_1, q = key_2; *p == *q && '\0' != *q && '[' != *q; p++, q++)
		;

	return ('\0' == *p || '[' == *p) && ('\0' == *q || '[' == *q) ? SUCCEED : FAIL;
}

/*************************************************************
 * returns SUCCEED if the item can be async polled and 
 * doesn't have to be put in any of zabbix standard queues
 * **********************************************************/
extern int CONFIG_DISABLE_SNMPV1_ASYNC;
extern int CONFIG_ICMP_METHOD;
extern char *CONFIG_WORKERS_DIR;

static int glb_might_be_async_polled( const ZBX_DC_ITEM *zbx_dc_item,const ZBX_DC_HOST *zbx_dc_host ) {
	extern unsigned char program_type;

	DEBUG_ITEM(zbx_dc_item->itemid, "Item being checked if can be  async polled");
	
	if ( NULL == zbx_dc_host || NULL == zbx_dc_item ) {
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	if (zbx_dc_host->proxy_hostid > 0 &&  //assigned to proxy
		0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) &&   //running as server
		ITEM_TYPE_CALCULATED != zbx_dc_item->type)  //all but calculated should be polled by proxy
	 		return FAIL;
		

	switch (zbx_dc_item->type) {
		case ITEM_TYPE_CALCULATED:
			DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled");
			return SUCCEED;

		case ITEM_TYPE_WORKER_SERVER:
			DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled");
			return SUCCEED;

		case ITEM_TYPE_AGENT: 
			if ( CONFIG_FORKS[GLB_PROCESS_TYPE_AGENT] == 0 ) {
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled");
				return FAIL;
			}
			
			//tls handshake isn't implemented in async mode yet
			if ( ZBX_TCP_SEC_UNENCRYPTED != zbx_dc_host->tls_connect) {
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled");
				return FAIL;
			}

			DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled");

			return SUCCEED;

		case ITEM_TYPE_SNMP: {
			ZBX_DC_SNMPINTERFACE *snmp_iface;
#ifdef HAVE_NETSNMP				
			if ( CONFIG_FORKS[GLB_PROCESS_TYPE_SNMP]== 0 ) {
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled");
				return FAIL;
			}
	
			snmp_iface = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &zbx_dc_item->interfaceid);
							//avoiding dynamic and discovery items from being processed by async glb pollers
			
			if ( NULL == snmp_iface || (
					snmp_iface->version == ZBX_IF_SNMP_VERSION_3) ||
			   		(snmp_iface->version == ZBX_IF_SNMP_VERSION_1 && CONFIG_DISABLE_SNMPV1_ASYNC) ) {
				
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be SNMP async polled, unsupported by async snmp version");
				return FAIL;
			}
			
			DEBUG_ITEM(zbx_dc_item->itemid, "Item can be SNMP async polled");
			
			return SUCCEED;
#endif
		}
		return FAIL;
		break;
		//typical script will look key will look like this: wisi.py["{HOST.CONN}", "1.3.6.1.4.1.7465.20.2.9.4.4.5.1.2.1.7.1.2"] 
		case ITEM_TYPE_SIMPLE: {
			if (0 < CONFIG_FORKS[GLB_PROCESS_TYPE_AGENT] && 0 == strncmp(zbx_dc_item->key, "net.tcp.service[http",20) ) 
				return SUCCEED;

			if (0 == CONFIG_FORKS[GLB_PROCESS_TYPE_PINGER])  {
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled, no glb pinger forks");
 				return FAIL;
			}

			if (NULL == zbx_dc_item->key) {
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled, no key is set");
				return FAIL;
			}

			if (SUCCEED == cmp_key_id(zbx_dc_item->key, ZBX_SERVER_ICMPPING_KEY) ||
				SUCCEED == cmp_key_id(zbx_dc_item->key, ZBX_SERVER_ICMPPINGSEC_KEY) ||
				SUCCEED == cmp_key_id(zbx_dc_item->key, ZBX_SERVER_ICMPPINGLOSS_KEY)) 	{  
				
				if (NULL != strstr(zbx_dc_item->key,"glbmap]")) {
					DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled, set to glbmap");
					return SUCCEED;
				}
    			
				if (NULL != strstr(zbx_dc_item->key,"fping]")) {
					DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled, set to fping");
					return FAIL;
				}

    			//method isn't set per item, looking at default
				if (CONFIG_ICMP_METHOD == GLB_ICMP) {
					DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled, default is glb_icmp");
					return SUCCEED;
				}

    			//default method is ZBX, so we will only process if there are no zbx pingers are started
    			if ( 1 > CONFIG_FORKS[GLB_PROCESS_TYPE_PINGER]) {
					DEBUG_ITEM(zbx_dc_item->itemid, "Item can be async polled, no zbx pingers available");
					return SUCCEED;
 				}
				
				DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled, doesn't meet async pinger conditions");
    			return FAIL;
			}
			
			DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled due to key mismatch");
			return FAIL;
		}
		break;
	
		case ITEM_TYPE_EXTERNAL: {
			char		*cmd = NULL;
			size_t		cmd_alloc = ZBX_KIBIBYTE, cmd_offset = 0;
			int		ret = FAIL;
			
			if (0 == CONFIG_FORKS[GLB_PROCESS_TYPE_WORKER])  
				return FAIL;
			if (NULL == zbx_dc_item->key)
				return FAIL;
			
			AGENT_REQUEST	request;
			zbx_init_agent_request(&request);

			if (NULL == CONFIG_WORKERS_DIR) {
				zabbix_log(LOG_LEVEL_DEBUG,"Workers dir is not set, not using glb_worker for item %ld, key %s",
						zbx_dc_item->itemid, zbx_dc_item->key);
				return FAIL;
			}

			if (SUCCEED !=zbx_parse_item_key(zbx_dc_item->key, &request)) 
				return FAIL;

			cmd = (char *)zbx_malloc(cmd, cmd_alloc);
			zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, "%s/%s", CONFIG_WORKERS_DIR, get_rkey(&request));

			if (-1 != access(cmd, X_OK)) {
				ret = SUCCEED; 
				zabbix_log(LOG_LEVEL_DEBUG ,"Found command '%s' - ok for adding to glb_worker",cmd);
			} else {
				zabbix_log(LOG_LEVEL_DEBUG ,"Couldn't find command '%s' - not adding to workers",cmd);
			}
			zbx_free(cmd);
			zbx_free_agent_request(&request);
			zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

			return ret;
		}

		break; 
	}
	DEBUG_ITEM(zbx_dc_item->itemid, "Item can not be async polled");
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Purpose: validate macro value when expanding user macros                   *
 *                                                                            *
 * Parameters: macro   - [IN] the user macro                                  *
 *             value   - [IN] the macro value                                 *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - the value is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
typedef int (*zbx_value_validator_func_t)(const char *macro, const char *value, char **error);

ZBX_DC_CONFIG *config = NULL;
zbx_rwlock_t config_lock = ZBX_RWLOCK_NULL;
zbx_rwlock_t config_history_lock = ZBX_RWLOCK_NULL;
zbx_shmem_info_t *config_mem;

extern unsigned char program_type;

ZBX_SHMEM_FUNC_IMPL(__config, config_mem)

mem_funcs_t memf ={	.free_func = __config_shmem_free_func, 
					.malloc_func = __config_shmem_malloc_func, 
					.realloc_func = __config_shmem_realloc_func};



static void dc_maintenance_precache_nested_groups(void);
static void dc_item_reset_triggers(ZBX_DC_ITEM *item, ZBX_DC_TRIGGER *trigger_exclude);

static void dc_reschedule_items(const zbx_hashset_t *activated_hosts);
static void dc_reschedule_httptests(zbx_hashset_t *activated_hosts);

static int dc_host_update_revision(ZBX_DC_HOST *host, zbx_uint64_t revision);
static int dc_item_update_revision(ZBX_DC_ITEM *item, zbx_uint64_t revision);

/******************************************************************************
 *                                                                            *
 * Purpose: copies string into configuration cache shared memory              *
 *                                                                            *
 ******************************************************************************/
static char *dc_strdup(const char *source)
{
	char *dst;
	size_t len;

	len = strlen(source) + 1;
	dst = (char *)__config_shmem_malloc_func(NULL, len);
	memcpy(dst, source, len);
	return dst;
}

/* user macro cache */

struct zbx_dc_um_handle_t
{
	zbx_dc_um_handle_t *prev;
	zbx_um_cache_t **cache;
	unsigned char macro_env;
};

static zbx_dc_um_handle_t *dc_um_handle = NULL;

/******************************************************************************
 *                                                                            *
 * Parameters: type - [IN] item type [ITEM_TYPE_* flag]                       *
 *             key  - [IN] item key                                           *
 *                                                                            *
 * Return value: SUCCEED when an item should be processed by server           *
 *               FAIL otherwise                                               *
 *                                                                            *
 * Comments: list of the items, always processed by server                    *
 *           ,------------------+--------------------------------------,      *
 *           | type             | key                                  |      *
 *           +------------------+--------------------------------------+      *
 *           | Zabbix internal  | zabbix[host,,items]                  |      *
 *           | Zabbix internal  | zabbix[host,,items_unsupported]      |      *
 *           | Zabbix internal  | zabbix[host,discovery,interfaces]    |      *
 *           | Zabbix internal  | zabbix[host,,maintenance]            |      *
 *           | Zabbix internal  | zabbix[proxy,discovery]              |      *
 *           | Zabbix internal  | zabbix[proxy,<proxyname>,lastaccess] |      *
 *           | Zabbix internal  | zabbix[proxy,<proxyname>,delay]      |      *
 *           | Zabbix aggregate | *                                    |      *
 *           | Calculated       | *                                    |      *
 *           '------------------+--------------------------------------'      *
 *                                                                            *
 ******************************************************************************/
int is_item_processed_by_server(unsigned char type, const char *key)
{
	int ret = FAIL;

	switch (type)
	{
	case ITEM_TYPE_CALCULATED:
		ret = SUCCEED;
		break;

	case ITEM_TYPE_INTERNAL:
		if (0 == strncmp(key, "zabbix[", 7))
		{
			AGENT_REQUEST request;
			char *arg1, *arg2, *arg3;

			zbx_init_agent_request(&request);

			if (SUCCEED != zbx_parse_item_key(key, &request) || 2 > request.nparam ||
				3 < request.nparam)
			{
				goto clean;
			}

			arg1 = get_rparam(&request, 0);
			arg2 = get_rparam(&request, 1);

			if (2 == request.nparam)
			{
				if (0 == strcmp(arg1, "proxy") && 0 == strcmp(arg2, "discovery"))
					ret = SUCCEED;

				goto clean;
			}

			arg3 = get_rparam(&request, 2);

			if (0 == strcmp(arg1, "host"))
			{
				if ('\0' == *arg2)
				{
					if (0 == strcmp(arg3, "maintenance") || 0 == strcmp(arg3, "items") ||
						0 == strcmp(arg3, "items_unsupported"))
					{
						ret = SUCCEED;
					}
				}
				else if (0 == strcmp(arg2, "discovery") && 0 == strcmp(arg3, "interfaces"))
					ret = SUCCEED;
			}
			else if (0 == strcmp(arg1, "proxy") && (0 == strcmp(arg3, "lastaccess") ||
													0 == strcmp(arg3, "delay")))
			{
				ret = SUCCEED;
			}
		clean:
			zbx_free_agent_request(&request);
		}
		break;
	}

	return ret;
}


static unsigned char poller_by_item(unsigned char type, const char *key)
{
	switch (type)
	{
	case ITEM_TYPE_SIMPLE:
		if (SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPING_KEY) ||
			SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPINGSEC_KEY) ||
			SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPINGLOSS_KEY))
		{
			if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_PINGER])
				break;

			return ZBX_POLLER_TYPE_PINGER;
		}
		ZBX_FALLTHROUGH;
	case ITEM_TYPE_ZABBIX:
	case ITEM_TYPE_SNMP:
	case ITEM_TYPE_EXTERNAL:
	case ITEM_TYPE_SSH:
	case ITEM_TYPE_TELNET:
	case ITEM_TYPE_HTTPAGENT:
	case ITEM_TYPE_SCRIPT:
	case ITEM_TYPE_INTERNAL:
		if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_POLLER])
			break;

		return ZBX_POLLER_TYPE_NORMAL;
	case ITEM_TYPE_DB_MONITOR:
		if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_ODBCPOLLER])
			break;

		return ZBX_POLLER_TYPE_ODBC;
	case ITEM_TYPE_CALCULATED:
		if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_HISTORYPOLLER])
			break;

		return ZBX_POLLER_TYPE_HISTORY;
	case ITEM_TYPE_IPMI:
		if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_IPMIPOLLER])
			break;

		return ZBX_POLLER_TYPE_IPMI;
	case ITEM_TYPE_JMX:
		if (0 == CONFIG_FORKS[ZBX_PROCESS_TYPE_JAVAPOLLER])
			break;

		return ZBX_POLLER_TYPE_JAVA;
	}

	return ZBX_NO_POLLER;
}

/******************************************************************************
 *                                                                            *
 * Purpose: determine whether the given item type is counted in item queue    *
 *                                                                            *
 * Return value: SUCCEED if item is counted in the queue, FAIL otherwise      *
 *                                                                            *
 ******************************************************************************/
int zbx_is_counted_in_item_queue(unsigned char type, const char *key)
{
	switch (type)
	{
	case ITEM_TYPE_ZABBIX_ACTIVE:
		if (0 == strncmp(key, "log[", 4) ||
			0 == strncmp(key, "logrt[", 6) ||
			0 == strncmp(key, "eventlog[", 9) ||
			0 == strncmp(key, "mqtt.get[", ZBX_CONST_STRLEN("mqtt.get[")))
		{
			return FAIL;
		}
		break;
	case ITEM_TYPE_TRAPPER:
	case ITEM_TYPE_DEPENDENT:
	case ITEM_TYPE_HTTPTEST:
	case ITEM_TYPE_SNMPTRAP:
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get the seed value to be used for item nextcheck calculations     *
 *                                                                            *
 * Return value: the seed for nextcheck calculations                          *
 *                                                                            *
 * Comments: The seed value is used to spread multiple item nextchecks over   *
 *           the item delay period to even the system load.                   *
 *           Items with the same delay period and seed value will have the    *
 *           same nextcheck values.                                           *
 *                                                                            *
 ******************************************************************************/
static zbx_uint64_t get_item_nextcheck_seed(zbx_uint64_t itemid, zbx_uint64_t interfaceid, unsigned char type,
											const char *key)
{
	if (ITEM_TYPE_JMX == type)
		return interfaceid;

	if (ITEM_TYPE_SNMP == type)
	{
		ZBX_DC_SNMPINTERFACE *snmp;
		ZBX_DC_SNMPITEM *snmpitem;

		if (NULL != (snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &itemid)))
		{
			if (0 == strncmp(snmpitem->snmp_oid, "walk[", 5))
			{
				return itemid;
			}
		}

		if (NULL == (snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid)) || SNMP_BULK_ENABLED != snmp->bulk)
		{
			return itemid;
		}

		return interfaceid;
	}

	if (ITEM_TYPE_SIMPLE == type)
	{
		if (SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPING_KEY) ||
			SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPINGSEC_KEY) ||
			SUCCEED == cmp_key_id(key, ZBX_SERVER_ICMPPINGLOSS_KEY))
		{
			return interfaceid;
		}
	}

	return itemid;
}


int DCitem_nextcheck_update(ZBX_DC_ITEM *item, const ZBX_DC_INTERFACE *interface, int flags, int now,
							char **error)
{
	zbx_uint64_t seed;
	int simple_interval, disable_until, ret, nextcheck, iface_avail;
	zbx_custom_interval_t *custom_intervals;
	char *delay_s;

	nextcheck = glb_state_item_get_nextcheck(item->itemid);

	if (0 == (flags & ZBX_ITEM_COLLECTED) && 0 != nextcheck &&
		0 == (flags & ZBX_ITEM_KEY_CHANGED) && 0 == (flags & ZBX_ITEM_TYPE_CHANGED) &&
		0 == (flags & ZBX_ITEM_DELAY_CHANGED))
	{
		return SUCCEED; /* avoid unnecessary nextcheck updates when syncing items in cache */
	}

	seed = get_item_nextcheck_seed(item->itemid, item->interfaceid, item->type, item->key);

	delay_s = dc_expand_user_macros_dyn(item->delay, &item->hostid, 1, ZBX_MACRO_ENV_NONSECURE);
	ret = zbx_interval_preproc(delay_s, &simple_interval, &custom_intervals, error);
	zbx_free(delay_s);

	if (SUCCEED != ret)
	{
		/* Polling items with invalid update intervals repeatedly does not make sense because they */
		/* can only be healed by editing configuration (either update interval or macros involved) */
		/* and such changes will be detected during configuration synchronization. DCsync_items()  */
		/* detects item configuration changes affecting check scheduling and passes them in flags. */

		glb_state_item_update_nextcheck(item->itemid, ZBX_JAN_2038);
		item->queue_next_check = ZBX_JAN_2038;
		return FAIL;
	}

	iface_avail = glb_state_host_get_id_interface_avail(item->hostid, item->interfaceid, &disable_until);

	if ( FAIL == iface_avail )
	{
		nextcheck = zbx_calculate_item_nextcheck_unreachable(simple_interval,
															 custom_intervals, disable_until);
	}
	else
	{
		if (0 != (flags & ZBX_ITEM_NEW) &&
			FAIL == zbx_custom_interval_is_scheduling(custom_intervals) &&
			ITEM_TYPE_ZABBIX_ACTIVE != item->type &&
			ZBX_DEFAULT_ITEM_UPDATE_INTERVAL < simple_interval)
		{
			nextcheck = zbx_calculate_item_nextcheck(seed, item->type,
													 ZBX_DEFAULT_ITEM_UPDATE_INTERVAL, NULL, now);
		}
		else
		{
			/* supported items and items that could not have been scheduled previously, but had */
			/* their update interval fixed, should be scheduled using their update intervals */
			nextcheck = zbx_calculate_item_nextcheck(seed, item->type, simple_interval,
													 custom_intervals, now);
		}
	}
	glb_state_item_update_nextcheck(item->itemid, nextcheck);
	item->queue_next_check = nextcheck;
	zbx_custom_interval_free(custom_intervals);

	return SUCCEED;
}

void DCitem_poller_type_update(ZBX_DC_ITEM *dc_item, const ZBX_DC_HOST *dc_host, int flags)
{
	unsigned char poller_type;

	if (0 != dc_host->proxy_hostid && SUCCEED != is_item_processed_by_server(dc_item->type, dc_item->key))
	{
		dc_item->poller_type = ZBX_NO_POLLER;
		DEBUG_ITEM(dc_item->itemid, "%ld at %s: set poller type to %d", dc_item->itemid, __func__, ZBX_NO_POLLER);
		return;
	}

	poller_type = poller_by_item(dc_item->type, dc_item->key);

	if (0 != (flags & ZBX_HOST_UNREACHABLE))
	{
		if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_JAVA == poller_type)
			poller_type = ZBX_POLLER_TYPE_UNREACHABLE;

		DEBUG_ITEM(dc_item->itemid, "%ld at %s: set poller type to %d", dc_item->itemid, __func__, poller_type);
		dc_item->poller_type = poller_type;
		return;
	}

	if (0 != (flags & ZBX_ITEM_COLLECTED))
	{
		dc_item->poller_type = poller_type;
		return;
	}

	if (ZBX_POLLER_TYPE_UNREACHABLE != dc_item->poller_type ||
		(ZBX_POLLER_TYPE_NORMAL != poller_type && ZBX_POLLER_TYPE_JAVA != poller_type))
	{
		dc_item->poller_type = poller_type;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: Find an element in a hashset by its 'id' or create the element if *
 *          it does not exist                                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     hashset - [IN] hashset to search                                       *
 *     id      - [IN] id of element to search for                             *
 *     size    - [IN] size of element to search for                           *
 *     found   - [OUT flag. 0 - element did not exist, it was created.        *
 *                          1 - existing element was found.                   *
 *                                                                            *
 * Return value: pointer to the found or created element                      *
 *                                                                            *
 ******************************************************************************/
void *DCfind_id(zbx_hashset_t *hashset, zbx_uint64_t id, size_t size, int *found)
{
	void *ptr;
	zbx_uint64_t buffer[1024]; /* adjust buffer size to accommodate any type DCfind_id() can be called for */
	bzero(buffer, 1024);

	if (NULL == (ptr = zbx_hashset_search(hashset, &id)))
	{
		*found = 0;

		buffer[0] = id;
		ptr = zbx_hashset_insert(hashset, &buffer[0], size);
	}
	else
	{
		*found = 1;
	}

	return ptr;
}

ZBX_DC_ITEM *DCfind_item(zbx_uint64_t hostid, const char *key)
{
	ZBX_DC_ITEM_HK *item_hk, item_hk_local;

	item_hk_local.hostid = hostid;
	item_hk_local.key = key;

	if (NULL == (item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local)))
		return NULL;
	else
		return item_hk->item_ptr;
}

ZBX_DC_HOST *DCfind_host(const char *host)
{
	ZBX_DC_HOST_H *host_h, host_h_local;

	host_h_local.host = host;

	if (NULL == (host_h = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_h, &host_h_local)))
		return NULL;
	else
		return host_h->host_ptr;
}

static ZBX_DC_AUTOREG_HOST *DCfind_autoreg_host(const char *host)
{
	ZBX_DC_AUTOREG_HOST autoreg_host_local;

	autoreg_host_local.host = host;

	return (ZBX_DC_AUTOREG_HOST *)zbx_hashset_search(&config->autoreg_hosts, &autoreg_host_local);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Find a record with proxy details in configuration cache using the *
 *          proxy name                                                        *
 *                                                                            *
 * Parameters: host - [IN] proxy name                                         *
 *                                                                            *
 * Return value: pointer to record if found or NULL otherwise                 *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_HOST *DCfind_proxy(const char *host)
{
	ZBX_DC_HOST_H *host_p, host_p_local;

	host_p_local.host = host;

	if (NULL == (host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local)))
		return NULL;
	else
		return host_p->host_ptr;
}

/* private strpool functions */

#define REFCOUNT_FIELD_SIZE sizeof(zbx_uint32_t)

static zbx_hash_t __config_strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((const char *)data + REFCOUNT_FIELD_SIZE);
}

static int __config_strpool_compare(const void *d1, const void *d2)
{
	return strcmp((const char *)d1 + REFCOUNT_FIELD_SIZE, (const char *)d2 + REFCOUNT_FIELD_SIZE);
}

const char *dc_strpool_intern(const char *str)
{
	void *record;
	zbx_uint32_t *refcount;

	if (NULL == str)
		return NULL;

	if (NULL == str)
		return NULL;

	record = zbx_hashset_search(&config->strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == record)
	{
		record = zbx_hashset_insert_ext(&config->strpool, str - REFCOUNT_FIELD_SIZE,
										REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);
		*(zbx_uint32_t *)record = 0;
	}

	refcount = (zbx_uint32_t *)record;
	(*refcount)++;

	return (char *)record + REFCOUNT_FIELD_SIZE;
}

void dc_strpool_release(const char *str)
{
	zbx_uint32_t *refcount;

	if (NULL == str)
		return;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	if (0 == --(*refcount))
		zbx_hashset_remove(&config->strpool, str - REFCOUNT_FIELD_SIZE);
}

const char *dc_strpool_acquire(const char *str)
{
	zbx_uint32_t *refcount;

	if (NULL == str)
		return NULL;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

int dc_strpool_replace(int found, const char **curr, const char *new_str)
{
	if (1 == found && NULL != new_str)
	{
		if (0 == strcmp(*curr, new_str))
			return FAIL;

		dc_strpool_release(*curr);
	}

	*curr = dc_strpool_intern(new_str);

	return SUCCEED; /* indicate that the string has been replaced */
}

void DCupdate_item_queue(ZBX_DC_ITEM *item, unsigned char old_poller_type)
{
	zbx_binary_heap_elem_t elem;
	ZBX_DC_HOST *zbx_dc_host;

	if (ZBX_LOC_POLLER == item->location)
		return;

	if (ZBX_LOC_QUEUE == item->location && old_poller_type != item->poller_type)
	{
		item->location = ZBX_LOC_NOWHERE;
		zbx_binary_heap_remove_direct(&config->queues[old_poller_type], item->itemid);
		DEBUG_ITEM(item->itemid, "Removing item from poller %d ", old_poller_type);
	}

	if (item->poller_type == ZBX_NO_POLLER)
		return;

	if (ZBX_LOC_QUEUE == item->location) //&& old_nextcheck == item->nextcheck)
		return;

	// do not put to queue items that might be processed in a glaber specifig pollers
	zbx_dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid);

	if (NULL != zbx_dc_host && SUCCEED == glb_might_be_async_polled(item, zbx_dc_host))
	{
		DEBUG_ITEM(item->itemid, "Not putting item to the queue, it might be async pooled");
		item->poller_type = ZBX_NO_POLLER;
		return;
	}

	elem.key = item->itemid;
	elem.data = (const void *)item;

	if (ZBX_LOC_QUEUE != item->location)
	{
		item->location = ZBX_LOC_QUEUE;
		DEBUG_ITEM(item->itemid, "Putting item to normal poller queue %d", item->poller_type);
		DEBUG_ITEM(item->itemid, "Item's next check is in %d", glb_state_item_get_nextcheck(item->itemid) - time(NULL));
		zbx_binary_heap_insert(&config->queues[item->poller_type], &elem);
	}
	else
		zbx_binary_heap_update_direct(&config->queues[item->poller_type], &elem);
}

extern int CONFIG_PROXYCONFIG_FREQUENCY;

void DCupdate_proxy_queue(ZBX_DC_PROXY *proxy)
{
	zbx_binary_heap_elem_t elem;

	if (ZBX_LOC_POLLER == proxy->location)
		return;

	proxy->nextcheck = proxy->proxy_tasks_nextcheck;

	if (proxy->proxy_data_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_data_nextcheck;
	if (proxy->proxy_config_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_config_nextcheck;

	elem.key = proxy->hostid;
	elem.data = (const void *)proxy;
	int now = time(NULL);

	if (proxy->nextcheck - now < CONFIG_PROXYCONFIG_FREQUENCY / 2)
		proxy->nextcheck = now + CONFIG_PROXYCONFIG_FREQUENCY;

	if (ZBX_LOC_QUEUE != proxy->location)
	{
		proxy->location = ZBX_LOC_QUEUE;
		zbx_binary_heap_insert(&config->pqueue, &elem);
	}
	else
		zbx_binary_heap_update_direct(&config->pqueue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Purpose: sets and validates global housekeeping option                     *
 *                                                                            *
 * Parameters: value     - [OUT] housekeeping setting                         *
 *             non_zero  - [IN] 0 if value is allowed to be zero, 1 otherwise *
 *             value_min - [IN] minimal acceptable setting value              *
 *             value_raw - [IN] setting value to validate                     *
 *                                                                            *
 ******************************************************************************/
static int set_hk_opt(int *value, int non_zero, int value_min, const char *value_raw, zbx_uint64_t revision)
{
	int value_int;

	if (SUCCEED != zbx_is_time_suffix(value_raw, &value_int, ZBX_LENGTH_UNLIMITED))
		return FAIL;

	if (0 != non_zero && 0 == value_int)
		return FAIL;

	if (0 != *value && (value_min > value_int || ZBX_HK_PERIOD_MAX < value_int))
		return FAIL;

	if (*value != value_int)
	{
		*value = value_int;
		config->revision.config_table = revision;
	}

	return SUCCEED;
}

static int DCsync_config(zbx_dbsync_t *sync, zbx_uint64_t revision, int *flags)
{
	const ZBX_TABLE *config_table;

	/* sync with zbx_dbsync_compare_config() */
	const char *selected_fields[] = {"discovery_groupid", "snmptrap_logging",
									 "severity_name_0", "severity_name_1", "severity_name_2", "severity_name_3",
									 "severity_name_4", "severity_name_5", "hk_events_mode", "hk_events_trigger",
									 "hk_events_internal", "hk_events_discovery", "hk_events_autoreg",
									 "hk_services_mode", "hk_services", "hk_audit_mode", "hk_audit",
									 "hk_sessions_mode", "hk_sessions", "hk_history_mode", "hk_history_global",
									 "hk_history", "hk_trends_mode", "hk_trends_global", "hk_trends",
									 "default_inventory_mode", "db_extension", "autoreg_tls_accept",
									 "compression_status", "compress_older", "instanceid",
									 "default_timezone", "hk_events_service", "auditlog_enabled"};

	const char *row[ARRSIZE(selected_fields)];
	size_t i;
	int j, found = 1, ret, value_int;
	unsigned char value_uchar;
	char **db_row;
	zbx_uint64_t rowid, value_uint64;
	unsigned char tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*flags = 0;

	if (NULL == config->config)
	{
		found = 0;
		config->config = (ZBX_DC_CONFIG_TABLE *)__config_shmem_malloc_func(NULL, sizeof(ZBX_DC_CONFIG_TABLE));
		memset(config->config, 0, sizeof(ZBX_DC_CONFIG_TABLE));
	}

	if (SUCCEED != (ret = zbx_dbsync_next(sync, &rowid, &db_row, &tag)))
	{
		/* load default config data */

		if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
			zabbix_log(LOG_LEVEL_ERR, "no records in table 'config'");

		config_table = zbx_db_get_table("config");

		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = zbx_db_get_field(config_table, selected_fields[i])->default_value;
	}
	else
	{
		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = db_row[i];
	}

	/* store the config data */

	if (NULL != row[0])
		ZBX_STR2UINT64(value_uint64, row[0]);
	else
		value_uint64 = ZBX_DISCOVERY_GROUPID_UNDEFINED;

	if (config->config->discovery_groupid != value_uint64)
	{
		config->config->discovery_groupid = value_uint64;
		config->revision.config_table = revision;
	}

	ZBX_STR2UCHAR(value_uchar, row[1]);
	if (config->config->snmptrap_logging != value_uchar)
	{
		config->config->snmptrap_logging = value_uchar;
		config->revision.config_table = revision;
	}

	if (config->config->default_inventory_mode != (value_int = atoi(row[25])))
	{
		config->config->default_inventory_mode = value_int;
		config->revision.config_table = revision;
	}

	if (NULL == config->config->db.extension || 0 != strcmp(config->config->db.extension, row[26]))
	{
		dc_strpool_replace(found, (const char **)&config->config->db.extension, row[26]);
		config->revision.config_table = revision;
	}

	ZBX_STR2UCHAR(value_uchar, row[27]);
	if (config->config->autoreg_tls_accept != value_uchar)
	{
		config->config->autoreg_tls_accept = value_uchar;
		config->revision.config_table = revision;
	}

	if (SUCCEED != zbx_is_time_suffix(row[29], &value_int, ZBX_LENGTH_UNLIMITED))
	{
		zabbix_log(LOG_LEVEL_WARNING, "invalid history compression age: %s", row[29]);
		value_int = 0;
	}

	for (j = 0; TRIGGER_SEVERITY_COUNT > j; j++)
	{
		if (NULL == config->config->severity_name[j] || 0 != strcmp(config->config->severity_name[j], row[2 + j]))
		{
			dc_strpool_replace(found, (const char **)&config->config->severity_name[j], row[2 + j]);
			config->revision.config_table = revision;
		}
	}

	/* instance id cannot be changed - update it only at first sync to avoid read locks later */
	if (0 == found)
		dc_strpool_replace(found, &config->config->instanceid, row[30]);

#if TRIGGER_SEVERITY_COUNT != 6
#error "row indexes below are based on assumption of six trigger severity levels"
#endif

	/* read housekeeper configuration */
	if (ZBX_HK_OPTION_ENABLED == (value_int = atoi(row[8])) &&
		(SUCCEED != set_hk_opt(&config->config->hk.events_trigger, 1, SEC_PER_DAY, row[9], revision) ||
		 SUCCEED != set_hk_opt(&config->config->hk.events_internal, 1, SEC_PER_DAY, row[10], revision) ||
		 SUCCEED != set_hk_opt(&config->config->hk.events_discovery, 1, SEC_PER_DAY, row[11], revision) ||
		 SUCCEED != set_hk_opt(&config->config->hk.events_autoreg, 1, SEC_PER_DAY, row[12], revision) ||
		 SUCCEED != set_hk_opt(&config->config->hk.events_service, 1, SEC_PER_DAY, row[32], revision)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "trigger, internal, network discovery and auto-registration data"
									  " housekeeping will be disabled due to invalid settings");
		value_int = ZBX_HK_OPTION_DISABLED;
	}
	if (config->config->hk.events_mode != value_int)
	{
		config->config->hk.events_mode = value_int;
		config->revision.config_table = revision;
	}

	if (ZBX_HK_OPTION_ENABLED == (value_int = atoi(row[13])) &&
		SUCCEED != set_hk_opt(&config->config->hk.services, 1, SEC_PER_DAY, row[14], revision))
	{
		zabbix_log(LOG_LEVEL_WARNING, "IT services data housekeeping will be disabled due to invalid"
									  " settings");
		value_int = ZBX_HK_OPTION_DISABLED;
	}
	if (config->config->hk.services_mode != value_int)
	{
		config->config->hk.services_mode = value_int;
		config->revision.config_table = revision;
	}

	if (ZBX_HK_OPTION_ENABLED == (value_int = atoi(row[15])) &&
		SUCCEED != set_hk_opt(&config->config->hk.audit, 1, SEC_PER_DAY, row[16], revision))
	{
		zabbix_log(LOG_LEVEL_WARNING, "audit data housekeeping will be disabled due to invalid"
									  " settings");
		value_int = ZBX_HK_OPTION_DISABLED;
	}
	if (config->config->hk.audit_mode != value_int)
	{
		config->config->hk.audit_mode = value_int;
		config->revision.config_table = revision;
	}

	if (ZBX_HK_OPTION_ENABLED == (value_int = atoi(row[17])) &&
		SUCCEED != set_hk_opt(&config->config->hk.sessions, 1, SEC_PER_DAY, row[18], revision))
	{
		zabbix_log(LOG_LEVEL_WARNING, "user sessions data housekeeping will be disabled due to invalid"
									  " settings");
		value_int = ZBX_HK_OPTION_DISABLED;
	}

	if (config->config->hk.sessions_mode != value_int)
	{
		config->config->hk.sessions_mode = value_int;
		config->revision.config_table = revision;
	}

	if (NULL == config->config->default_timezone || 0 != strcmp(config->config->default_timezone, row[31]))
	{
		dc_strpool_replace(found, (const char **)&config->config->default_timezone, row[31]);
		config->revision.config_table = revision;
	}

	if (config->config->auditlog_enabled != (value_int = atoi(row[33])))
	{
		config->config->auditlog_enabled = value_int;
		config->revision.config_table = revision;
	}

	if (SUCCEED == ret && SUCCEED == zbx_dbsync_next(sync, &rowid, &db_row, &tag)) /* table must have */
		zabbix_log(LOG_LEVEL_ERR, "table 'config' has multiple records");		   /* only one record */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: calculate nextcheck timestamp for passive proxy                   *
 *                                                                            *
 * Parameters: hostid - [IN] host identifier from database                    *
 *             delay  - [IN] default delay value, can be overridden           *
 *             now    - [IN] current timestamp                                *
 *                                                                            *
 * Return value: nextcheck value                                              *
 *                                                                            *
 ******************************************************************************/
static time_t calculate_proxy_nextcheck(zbx_uint64_t hostid, unsigned int delay, time_t now)
{
	time_t nextcheck;

	nextcheck = delay * (now / delay) + (unsigned int)(hostid % delay);

	while (nextcheck <= now)
		nextcheck += delay;

	return nextcheck;
}

void DCsync_autoreg_config(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	/* sync this function with zbx_dbsync_compare_autoreg_psk() */
	char **db_row;
	zbx_uint64_t rowid;
	unsigned char tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED == zbx_dbsync_next(sync, &rowid, &db_row, &tag))
	{
		switch (tag)
		{
		case ZBX_DBSYNC_ROW_ADD:
		case ZBX_DBSYNC_ROW_UPDATE:
			zbx_strlcpy(config->autoreg_psk_identity, db_row[0],
						sizeof(config->autoreg_psk_identity));
			zbx_strlcpy(config->autoreg_psk, db_row[1], sizeof(config->autoreg_psk));
			break;
		case ZBX_DBSYNC_ROW_REMOVE:
			config->autoreg_psk_identity[0] = '\0';
			zbx_guaranteed_memset(config->autoreg_psk, 0, sizeof(config->autoreg_psk));
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
		}

		config->revision.autoreg_tls = revision;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_autoreg_host(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_DC_AUTOREG_HOST *autoreg_host, autoreg_host_local = {.host = row[0]};
		int found;

		autoreg_host = (ZBX_DC_AUTOREG_HOST *)zbx_hashset_search(&config->autoreg_hosts, &autoreg_host_local);
		if (NULL == autoreg_host)
		{
			found = 0;
			autoreg_host = zbx_hashset_insert(&config->autoreg_hosts, &autoreg_host_local,
											  sizeof(ZBX_DC_AUTOREG_HOST));
		}
		else
		{
			zabbix_log(LOG_LEVEL_DEBUG, "cannot process duplicate host '%s' in autoreg_host table",
					   row[0]);
			found = 1;
		}

		dc_strpool_replace(found, &autoreg_host->host, row[0]);
		dc_strpool_replace(found, &autoreg_host->listen_ip, row[1]);
		dc_strpool_replace(found, &autoreg_host->listen_dns, row[2]);
		dc_strpool_replace(found, &autoreg_host->host_metadata, row[3]);
		autoreg_host->flags = atoi(row[4]);
		autoreg_host->listen_port = atoi(row[5]);
		autoreg_host->timestamp = 0;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_proxy_remove(ZBX_DC_PROXY *proxy)
{
	if (ZBX_LOC_QUEUE == proxy->location)
	{
		zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
		proxy->location = ZBX_LOC_NOWHERE;
	}

	dc_strpool_release(proxy->proxy_address);
	dc_strpool_release(proxy->version_str);

	zbx_vector_dc_host_ptr_destroy(&proxy->hosts);
	zbx_vector_host_rev_destroy(&proxy->removed_hosts);

	zbx_hashset_remove_direct(&config->proxies, proxy);
}

static void dc_host_deregister_proxy(ZBX_DC_HOST *host, zbx_uint64_t proxy_hostid, zbx_uint64_t revision)
{
	ZBX_DC_PROXY *proxy;
	int i;
	zbx_host_rev_t rev;

	if (NULL == (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &proxy_hostid)))
		return;

	rev.hostid = host->hostid;
	rev.revision = revision;
	zbx_vector_host_rev_append(&proxy->removed_hosts, rev);

	if (FAIL == (i = zbx_vector_dc_host_ptr_search(&proxy->hosts, host, ZBX_DEFAULT_PTR_COMPARE_FUNC)))
		return;

	zbx_vector_dc_host_ptr_remove_noorder(&proxy->hosts, i);
	proxy->revision = revision;
}

static void dc_host_register_proxy(ZBX_DC_HOST *host, zbx_uint64_t proxy_hostid, zbx_uint64_t revision)
{
	ZBX_DC_PROXY *proxy;

	if (NULL == (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &proxy_hostid)))
		return;

	zbx_vector_dc_host_ptr_append(&proxy->hosts, host);
	proxy->revision = revision;
}

static void DCsync_hosts(zbx_dbsync_t *sync, zbx_uint64_t revision, zbx_vector_uint64_t *active_avail_diff,
						 zbx_hashset_t *activated_hosts, const zbx_config_vault_t *config_vault)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_HOST *host;
	ZBX_DC_IPMIHOST *ipmihost;
	ZBX_DC_PROXY *proxy;
	ZBX_DC_HOST_H *host_h, host_h_local, *host_p, host_p_local;

	int i, found;
	int update_index_h, update_index_p, ret;
	zbx_uint64_t hostid, proxy_hostid;
	unsigned char status;
	time_t now;
	signed char ipmi_authtype;
	unsigned char ipmi_privilege;
	zbx_vector_dc_host_ptr_t proxy_hosts;
	zbx_vector_uint64_t changed_hosts_ids;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	ZBX_DC_PSK *psk_i, psk_i_local;
	zbx_ptr_pair_t *psk_owner, psk_owner_local;
	zbx_hashset_t psk_owners;
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_hashset_create(&psk_owners, 0, ZBX_DEFAULT_PTR_HASH_FUNC, ZBX_DEFAULT_PTR_COMPARE_FUNC);
#endif
	zbx_vector_dc_host_ptr_create(&proxy_hosts);

	now = time(NULL);

	zbx_vector_uint64_create(&changed_hosts_ids);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_DBROW2UINT64(proxy_hostid, row[1]);
		ZBX_STR2UCHAR(status, row[10]);

		zbx_vector_uint64_append(&changed_hosts_ids, hostid);

		host = (ZBX_DC_HOST *)DCfind_id(&config->hosts, hostid, sizeof(ZBX_DC_HOST), &found);
		host->revision = revision;

		/* see whether we should and can update 'hosts_h' and 'hosts_p' indexes at this point */

		update_index_h = 0;
		update_index_p = 0;

		if ((HOST_STATUS_MONITORED == status || HOST_STATUS_NOT_MONITORED == status) &&
			(0 == found || 0 != strcmp(host->host, row[2])))
		{
			if (1 == found)
			{
				host_h_local.host = host->host;
				host_h = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_h, &host_h_local);

				if (NULL != host_h && host == host_h->host_ptr) /* see ZBX-4045 for NULL check */
				{
					dc_strpool_release(host_h->host);
					zbx_hashset_remove_direct(&config->hosts_h, host_h);
				}
			}

			host_h_local.host = row[2];
			host_h = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_h, &host_h_local);

			if (NULL != host_h)
				host_h->host_ptr = host;
			else
				update_index_h = 1;
		}
		else if ((HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status) &&
				 (0 == found || 0 != strcmp(host->host, row[2])))
		{
			if (1 == found)
			{
				host_p_local.host = host->host;
				host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local);

				if (NULL != host_p && host == host_p->host_ptr)
				{
					dc_strpool_release(host_p->host);
					zbx_hashset_remove_direct(&config->hosts_p, host_p);
				}
			}

			host_p_local.host = row[2];
			host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local);

			if (NULL != host_p)
				host_p->host_ptr = host;
			else
				update_index_p = 1;
		}

		/* store new information in host structure */

		dc_strpool_replace(found, &host->host, row[2]);
		dc_strpool_replace(found, &host->name, row[11]);
		dc_strpool_replace(found, &host->description, row[18 + ZBX_HOST_TLS_OFFSET]);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		dc_strpool_replace(found, &host->tls_issuer, row[15]);
		dc_strpool_replace(found, &host->tls_subject, row[16]);

		/* maintain 'config->psks' in configuration cache */

		/*                                                                           */
		/* cases to cover (PSKid means PSK identity):                                */
		/*                                                                           */
		/*                                  Incoming data record                     */
		/*                                  /                   \                    */
		/*                                new                   new                  */
		/*                               PSKid                 PSKid                 */
		/*                             non-empty               empty                 */
		/*                             /      \                /    \                */
		/*                            /        \              /      \               */
		/*                       'host'        'host'      'host'    'host'          */
		/*                       record        record      record    record          */
		/*                        has           has         has       has            */
		/*                     non-empty       empty     non-empty  empty PSK        */
		/*                        PSK           PSK         PSK      |     \         */
		/*                       /   \           |           |       |      \        */
		/*                      /     \          |           |       |       \       */
		/*                     /       \         |           |       |        \      */
		/*            new PSKid       new PSKid  |           |   existing     new    */
		/*             same as         differs   |           |    record     record  */
		/*            old PSKid         from     |           |      |          |     */
		/*           /    |           old PSKid  |           |     done        |     */
		/*          /     |              |       |           |                 |     */
		/*   new PSK    new PSK        delete    |        delete               |     */
		/*    value      value        old PSKid  |       old PSKid             |     */
		/*   same as    differs       and value  |       and value             |     */
		/*     old       from         from psks  |       from psks             |     */
		/*      |        old          hashset    |        hashset              |     */
		/*     done       /           (if ref    |        (if ref              |     */
		/*               /            count=0)   |        count=0)             |     */
		/*              /              /     \  /|           \                /      */
		/*             /              /--------- |            \              /       */
		/*            /              /         \ |             \            /        */
		/*       delete          new PSKid   new PSKid         set pointer in        */
		/*       old PSK          already     not in           'hosts' record        */
		/*        value           in psks      psks             to NULL PSK          */
		/*        from            hashset     hashset                |               */
		/*       string            /   \          \                 done             */
		/*        pool            /     \          \                                 */
		/*         |             /       \          \                                */
		/*       change    PSK value   PSK value    insert                           */
		/*      PSK value  in hashset  in hashset  new PSKid                         */
		/*      for this    same as     differs    and value                         */
		/*       PSKid      new PSK     from new   into psks                         */
		/*         |        value      PSK value    hashset                          */
		/*        done        \           |            /                             */
		/*                     \       replace        /                              */
		/*                      \      PSK value     /                               */
		/*                       \     in hashset   /                                */
		/*                        \    with new    /                                 */
		/*                         \   PSK value  /                                  */
		/*                          \     |      /                                   */
		/*                           \    |     /                                    */
		/*                            set pointer                                    */
		/*                            in 'host'                                      */
		/*                            record to                                      */
		/*                            new PSKid                                      */
		/*                                |                                          */
		/*                               done                                        */
		/*                                                                           */
		/*****************************************************************************/

		psk_owner = NULL;

		if ('\0' == *row[17] || '\0' == *row[18]) /* new PSKid or value empty */
		{
			/* In case of "impossible" errors ("PSK value without identity" or "PSK identity without */
			/* value") assume empty PSK identity and value. These errors should have been prevented */
			/* by validation in frontend/API. Be prepared when making a connection requiring PSK - */
			/* the PSK might not be available. */

			if (1 == found)
			{
				if (NULL == host->tls_dc_psk) /* 'host' record has empty PSK */
					goto done;

				/* 'host' record has non-empty PSK. Unlink and delete PSK. */

				psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

				if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)) &&
					0 == --(psk_i->refcount))
				{
					dc_strpool_release(psk_i->tls_psk_identity);
					dc_strpool_release(psk_i->tls_psk);
					zbx_hashset_remove_direct(&config->psks, psk_i);
				}
			}

			host->tls_dc_psk = NULL;
			goto done;
		}

		/* new PSKid and value non-empty */

		zbx_strlower(row[18]);

		if (1 == found && NULL != host->tls_dc_psk) /* 'host' record has non-empty PSK */
		{
			if (0 == strcmp(host->tls_dc_psk->tls_psk_identity, row[17])) /* new PSKid same as */
																		  /* old PSKid */
			{
				if (0 != strcmp(host->tls_dc_psk->tls_psk, row[18])) /* new PSK value */
																	 /* differs from old */
				{
					if (NULL == (psk_owner = (zbx_ptr_pair_t *)zbx_hashset_search(&psk_owners,
																				  &host->tls_dc_psk->tls_psk_identity)))
					{
						/* change underlying PSK value and 'config->psks' is updated, too */
						dc_strpool_replace(1, &host->tls_dc_psk->tls_psk, row[18]);
					}
					else
					{
						zabbix_log(LOG_LEVEL_WARNING, "conflicting PSK values for PSK identity"
													  " \"%s\" on hosts \"%s\" and \"%s\" (and maybe others)",
								   (char *)psk_owner->first, (char *)psk_owner->second,
								   host->host);
					}
				}

				goto done;
			}

			/* New PSKid differs from old PSKid. Unlink and delete old PSK. */

			psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

			if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)) &&
				0 == --(psk_i->refcount))
			{
				dc_strpool_release(psk_i->tls_psk_identity);
				dc_strpool_release(psk_i->tls_psk);
				zbx_hashset_remove_direct(&config->psks, psk_i);
			}

			host->tls_dc_psk = NULL;
		}

		/* new PSK identity already stored? */

		psk_i_local.tls_psk_identity = row[17];

		if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)))
		{
			/* new PSKid already in psks hashset */

			if (0 != strcmp(psk_i->tls_psk, row[18])) /* PSKid stored but PSK value is different */
			{
				if (NULL == (psk_owner = (zbx_ptr_pair_t *)zbx_hashset_search(&psk_owners,
																			  &psk_i->tls_psk_identity)))
				{
					dc_strpool_replace(1, &psk_i->tls_psk, row[18]);
				}
				else
				{
					zabbix_log(LOG_LEVEL_WARNING, "conflicting PSK values for PSK identity"
												  " \"%s\" on hosts \"%s\" and \"%s\" (and maybe others)",
							   (char *)psk_owner->first, (char *)psk_owner->second,
							   host->host);
				}
			}

			host->tls_dc_psk = psk_i;
			psk_i->refcount++;
			goto done;
		}

		/* insert new PSKid and value into psks hashset */

		dc_strpool_replace(0, &psk_i_local.tls_psk_identity, row[17]);
		dc_strpool_replace(0, &psk_i_local.tls_psk, row[18]);
		psk_i_local.refcount = 1;
		host->tls_dc_psk = zbx_hashset_insert(&config->psks, &psk_i_local, sizeof(ZBX_DC_PSK));
	done:
		if (NULL != host->tls_dc_psk && NULL == psk_owner)
		{
			if (NULL == zbx_hashset_search(&psk_owners, &host->tls_dc_psk->tls_psk_identity))
			{
				/* register this host as the PSK identity owner, against which to report conflicts */

				psk_owner_local.first = (char *)host->tls_dc_psk->tls_psk_identity;
				psk_owner_local.second = (char *)host->host;

				zbx_hashset_insert(&psk_owners, &psk_owner_local, sizeof(psk_owner_local));
			}
		}
#endif
		ZBX_STR2UCHAR(host->tls_connect, row[13]);
		ZBX_STR2UCHAR(host->tls_accept, row[14]);

		if ((HOST_STATUS_PROXY_PASSIVE == status && 0 != (ZBX_TCP_SEC_UNENCRYPTED & host->tls_connect)) ||
			(HOST_STATUS_PROXY_ACTIVE == status && 0 != (ZBX_TCP_SEC_UNENCRYPTED &
														 host->tls_accept)))
		{
			if (NULL != config_vault->token || NULL != config_vault->name)
			{
				zabbix_log(LOG_LEVEL_WARNING, "connection with Zabbix proxy \"%s\" should not be"
											  " unencrypted when using Vault",
						   host->host);
			}
		}

		if (0 == found)
		{
			ZBX_DBROW2UINT64(host->maintenanceid, row[17 + ZBX_HOST_TLS_OFFSET]);
			host->maintenance_status = (unsigned char)atoi(row[7]);
			host->maintenance_type = (unsigned char)atoi(row[8]);
			host->maintenance_from = atoi(row[9]);
			host->data_expected_from = now;

			zbx_vector_ptr_create_ext(&host->interfaces_v, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);

			zbx_vector_dc_httptest_ptr_create_ext(&host->httptests, __config_shmem_malloc_func,
												  __config_shmem_realloc_func, __config_shmem_free_func);
			zbx_vector_dc_item_ptr_create_ext(&host->items, __config_shmem_malloc_func,
											  __config_shmem_realloc_func, __config_shmem_free_func);
		}
		else
		{
			int reset_availability = 0;

			if (HOST_STATUS_MONITORED == status && HOST_STATUS_MONITORED != host->status)
				host->data_expected_from = now;

			/* reset host status if host status has been changed (e.g., if host has been disabled) */
			if (status != host->status)
			{
				zbx_vector_uint64_append(active_avail_diff, host->hostid);

				reset_availability = 1;
			}

			/* reset host status if host proxy assignment has been changed */
			if (proxy_hostid != host->proxy_hostid)
			{
				zbx_vector_uint64_append(active_avail_diff, host->hostid);

				reset_availability = 1;
			}

			if (0 != reset_availability)
			{
				ZBX_DC_INTERFACE *interface;

				for (i = 0; i < host->interfaces_v.values_num; i++)
				{
					interface = (ZBX_DC_INTERFACE *)host->interfaces_v.values[i];
					
					glb_state_host_set_id_interface_avail(host->hostid, interface->interfaceid, INTERFACE_AVAILABLE_FALSE, NULL );
				}
			}

			/* gather hosts that must restart monitoring either by being re-enabled or */
			/* assigned from proxy to server                                           */
			if ((HOST_STATUS_MONITORED == status && HOST_STATUS_MONITORED != host->status) ||
				(0 == proxy_hostid && 0 != host->proxy_hostid))
			{
				zbx_hashset_insert(activated_hosts, &host->hostid, sizeof(host->hostid));
			}
		}

		if (HOST_STATUS_MONITORED == status || HOST_STATUS_NOT_MONITORED == status)
		{
			if (0 != found && 0 != host->proxy_hostid && host->proxy_hostid != proxy_hostid)
			{
				dc_host_deregister_proxy(host, host->proxy_hostid, revision);
			}

			if (0 != proxy_hostid)
			{
				if (0 == found || host->proxy_hostid != proxy_hostid)
				{
					zbx_vector_dc_host_ptr_append(&proxy_hosts, host);
				}
				else
				{
					if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies,
																			&proxy_hostid)))
					{
						proxy->revision = revision;
					}
				}
			}
		}

		host->proxy_hostid = proxy_hostid;

		/* update 'hosts_h' and 'hosts_p' indexes using new data, if not done already */

		if (1 == update_index_h)
		{
			host_h_local.host = dc_strpool_acquire(host->host);
			host_h_local.host_ptr = host;
			zbx_hashset_insert(&config->hosts_h, &host_h_local, sizeof(ZBX_DC_HOST_H));
		}

		if (1 == update_index_p)
		{
			host_p_local.host = dc_strpool_acquire(host->host);
			host_p_local.host_ptr = host;
			zbx_hashset_insert(&config->hosts_p, &host_p_local, sizeof(ZBX_DC_HOST_H));
		}

		/* IPMI hosts */

		ipmi_authtype = (signed char)atoi(row[3]);
		ipmi_privilege = (unsigned char)atoi(row[4]);

		if (ZBX_IPMI_DEFAULT_AUTHTYPE != ipmi_authtype || ZBX_IPMI_DEFAULT_PRIVILEGE != ipmi_privilege ||
			'\0' != *row[5] || '\0' != *row[6]) /* useipmi */
		{
			ipmihost = (ZBX_DC_IPMIHOST *)DCfind_id(&config->ipmihosts, hostid, sizeof(ZBX_DC_IPMIHOST),
													&found);

			ipmihost->ipmi_authtype = ipmi_authtype;
			ipmihost->ipmi_privilege = ipmi_privilege;
			dc_strpool_replace(found, &ipmihost->ipmi_username, row[5]);
			dc_strpool_replace(found, &ipmihost->ipmi_password, row[6]);
		}
		else if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &hostid)))
		{
			/* remove IPMI connection parameters for hosts without IPMI */

			dc_strpool_release(ipmihost->ipmi_username);
			dc_strpool_release(ipmihost->ipmi_password);

			zbx_hashset_remove_direct(&config->ipmihosts, ipmihost);
		}

		/* proxies */

		if (HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status)
		{
			proxy = (ZBX_DC_PROXY *)DCfind_id(&config->proxies, hostid, sizeof(ZBX_DC_PROXY), &found);

			if (0 == found)
			{
				proxy->location = ZBX_LOC_NOWHERE;
				proxy->revision = revision;

				proxy->version_int = ZBX_COMPONENT_VERSION_UNDEFINED;
				proxy->version_str = dc_strpool_intern(ZBX_VERSION_UNDEFINED_STR);
				proxy->compatibility = ZBX_PROXY_VERSION_UNDEFINED;
				proxy->lastaccess = atoi(row[12]);
				proxy->last_cfg_error_time = 0;
				proxy->proxy_delay = 0;
				proxy->nodata_win.flags = ZBX_PROXY_SUPPRESS_DISABLE;
				proxy->nodata_win.values_num = 0;
				proxy->nodata_win.period_end = 0;

				zbx_vector_dc_host_ptr_create_ext(&proxy->hosts, __config_shmem_malloc_func,
												  __config_shmem_realloc_func, __config_shmem_free_func);
				zbx_vector_host_rev_create_ext(&proxy->removed_hosts, __config_shmem_malloc_func,
											   __config_shmem_realloc_func, __config_shmem_free_func);
			}

			proxy->auto_compress = atoi(row[16 + ZBX_HOST_TLS_OFFSET]);
			dc_strpool_replace(found, &proxy->proxy_address, row[15 + ZBX_HOST_TLS_OFFSET]);

			if ((HOST_STATUS_PROXY_PASSIVE == status) &&
				(0 == found || status != host->status))
			{
				proxy->proxy_config_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, CONFIG_PROXYCONFIG_FREQUENCY, now);
				proxy->proxy_data_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, CONFIG_PROXYDATA_FREQUENCY, now);
				proxy->proxy_tasks_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, ZBX_TASK_UPDATE_FREQUENCY, now);

				DCupdate_proxy_queue(proxy);
			}
			else if ((HOST_STATUS_PROXY_ACTIVE == status ) && ZBX_LOC_QUEUE == proxy->location)
			{
				zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = ZBX_LOC_NOWHERE;
			}
			proxy->last_version_error_time = time(NULL);
		}
		else if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
		{
			int i;
			// TODO: since zabbix 5 all proxy removal code is in DCsync_proxy_remove(proxy); - move all cluster code there either
			DCsync_proxy_remove(proxy);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);

			if (ZBX_LOC_QUEUE == proxy->location)
			{
				zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = ZBX_LOC_NOWHERE;
			}

			dc_strpool_release(proxy->proxy_address);

			zbx_hashset_remove_direct(&config->proxies, proxy);
		}

		host->status = status;
	}
	
	conf_hosts_notify_changes(&changed_hosts_ids);
	
	zbx_vector_uint64_destroy(&changed_hosts_ids);

	for (i = 0; i < proxy_hosts.values_num; i++)
		dc_host_register_proxy(proxy_hosts.values[i], proxy_hosts.values[i]->proxy_hostid, revision);

	/* remove deleted hosts from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &rowid)))
			continue;

		hostid = host->hostid;

		/* IPMI hosts */

		if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &hostid)))
		{
			dc_strpool_release(ipmihost->ipmi_username);
			dc_strpool_release(ipmihost->ipmi_password);

			zbx_hashset_remove_direct(&config->ipmihosts, ipmihost);
		}

		/* proxies */

		if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
			DCsync_proxy_remove(proxy);

		/* hosts */

		if (HOST_STATUS_MONITORED == host->status || HOST_STATUS_NOT_MONITORED == host->status)
		{
			host_h_local.host = host->host;
			host_h = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_h, &host_h_local);

			if (NULL != host_h && host == host_h->host_ptr) /* see ZBX-4045 for NULL check */
			{
				dc_strpool_release(host_h->host);
				zbx_hashset_remove_direct(&config->hosts_h, host_h);
			}

			zbx_vector_uint64_append(active_avail_diff, host->hostid);

			if (0 != host->proxy_hostid)
				dc_host_deregister_proxy(host, host->proxy_hostid, revision);
		}
		else if (HOST_STATUS_PROXY_ACTIVE == host->status || HOST_STATUS_PROXY_PASSIVE == host->status)
		{
			host_p_local.host = host->host;
			host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local);

			if (NULL != host_p && host == host_p->host_ptr)
			{
				dc_strpool_release(host_p->host);
				zbx_hashset_remove_direct(&config->hosts_p, host_p);
			}
		}

		dc_strpool_release(host->host);
		dc_strpool_release(host->name);
		dc_strpool_release(host->description);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		dc_strpool_release(host->tls_issuer);
		dc_strpool_release(host->tls_subject);

		/* Maintain 'psks' index. Unlink and delete the PSK identity. */
		if (NULL != host->tls_dc_psk)
		{
			psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

			if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)) &&
				0 == --(psk_i->refcount))
			{
				dc_strpool_release(psk_i->tls_psk_identity);
				dc_strpool_release(psk_i->tls_psk);
				zbx_hashset_remove_direct(&config->psks, psk_i);
			}
		}
#endif
		zbx_vector_ptr_destroy(&host->interfaces_v);
		zbx_vector_dc_item_ptr_destroy(&host->items);
		zbx_hashset_remove_direct(&config->hosts, host);

		zbx_vector_dc_httptest_ptr_destroy(&host->httptests);
	}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_hashset_destroy(&psk_owners);
#endif
	zbx_vector_dc_host_ptr_destroy(&proxy_hosts);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_host_inventory(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	ZBX_DC_HOST_INVENTORY *host_inventory, *host_inventory_auto;
	zbx_uint64_t rowid, hostid;
	int found, ret, i;
	char **row;
	unsigned char tag;
	ZBX_DC_HOST *dc_host;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (NULL == row) 
			break;
		ZBX_STR2UINT64(hostid, row[0]);

		host_inventory = (ZBX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories, hostid,
															sizeof(ZBX_DC_HOST_INVENTORY), &found);

		ZBX_STR2UCHAR(host_inventory->inventory_mode, row[1]);

		/* store new information in host_inventory structure */
		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			dc_strpool_replace(found, &(host_inventory->values[i]), row[i + 2]);

		host_inventory_auto = (ZBX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories_auto, hostid,
																 sizeof(ZBX_DC_HOST_INVENTORY), &found);

		host_inventory_auto->inventory_mode = host_inventory->inventory_mode;

		if (1 == found)
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			{
				if (NULL == host_inventory_auto->values[i])
					continue;

				dc_strpool_release(host_inventory_auto->values[i]);
				host_inventory_auto->values[i] = NULL;
			}
		}
		else
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
				host_inventory_auto->values[i] = NULL;
		}

		if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
			dc_host_update_revision(dc_host, revision);
	}

	/* remove deleted host inventory from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories,
																				  &rowid)))
		{
			continue;
		}

		if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &host_inventory->hostid)))
			dc_host_update_revision(dc_host, revision);

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			dc_strpool_release(host_inventory->values[i]);

		zbx_hashset_remove_direct(&config->host_inventories, host_inventory);

		if (NULL == (host_inventory_auto = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(
						 &config->host_inventories_auto, &rowid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
		{
			if (NULL != host_inventory_auto->values[i])
				dc_strpool_release(host_inventory_auto->values[i]);
		}

		zbx_hashset_remove_direct(&config->host_inventories_auto, host_inventory_auto);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_kvs_paths(const struct zbx_json_parse *jp_kvs_paths, const zbx_config_vault_t *config_vault)
{
	zbx_dc_kvs_path_t *dc_kvs_path;
	zbx_dc_kv_t *dc_kv;
	zbx_kvs_t kvs;
	zbx_hashset_iter_t iter;
	int i, j;
	zbx_vector_ptr_pair_t diff;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_pair_create(&diff);
	zbx_kvs_create(&kvs, 100);

	for (i = 0; i < config->kvs_paths.values_num; i++)
	{
		char *error = NULL;

		dc_kvs_path = (zbx_dc_kvs_path_t *)config->kvs_paths.values[i];

		if (NULL != jp_kvs_paths)
		{
			if (FAIL == zbx_kvs_from_json_by_path_get(dc_kvs_path->path, jp_kvs_paths, &kvs, &error))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot get secrets for path \"%s\": %s",
						   dc_kvs_path->path, error);
				zbx_free(error);
				continue;
			}
		}
		else if (FAIL == zbx_vault_kvs_get(dc_kvs_path->path, &kvs, config_vault, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot get secrets for path \"%s\": %s", dc_kvs_path->path,
					   error);
			zbx_free(error);
			continue;
		}

		zbx_hashset_iter_reset(&dc_kvs_path->kvs, &iter);
		while (NULL != (dc_kv = (zbx_dc_kv_t *)zbx_hashset_iter_next(&iter)))
		{
			zbx_kv_t *kv, kv_local;
			zbx_ptr_pair_t pair;

			kv_local.key = (char *)dc_kv->key;
			if (NULL != (kv = zbx_kvs_search(&kvs, &kv_local)))
			{
				if (0 == zbx_strcmp_null(dc_kv->value, kv->value) && 0 == dc_kv->update)
					continue;
			}
			else if (NULL == dc_kv->value)
				continue;

			pair.first = dc_kv;
			pair.second = kv;
			zbx_vector_ptr_pair_append(&diff, pair);
		}

		if (0 != diff.values_num)
		{
			START_SYNC;

			config->revision.config++;

			for (j = 0; j < diff.values_num; j++)
			{
				zbx_kv_t *kv;

				dc_kv = (zbx_dc_kv_t *)diff.values[j].first;
				kv = (zbx_kv_t *)diff.values[j].second;

				if (NULL != kv)
				{
					dc_strpool_replace(dc_kv->value != NULL ? 1 : 0, &dc_kv->value, kv->value);
				}
				else
				{
					dc_strpool_release(dc_kv->value);
					dc_kv->value = NULL;
				}

				config->um_cache = um_cache_set_value_to_macros(config->um_cache,
																config->revision.config, &dc_kv->macros, dc_kv->value);

				dc_kv->update = 0;
			}

			FINISH_SYNC;
		}

		zbx_vector_ptr_pair_clear(&diff);
		zbx_kvs_clear(&kvs);
	}

	zbx_vector_ptr_pair_destroy(&diff);
	zbx_kvs_destroy(&kvs);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: trying to resolve the macros in host interface                    *
 *                                                                            *
 ******************************************************************************/
static void substitute_host_interface_macros(ZBX_DC_INTERFACE *interface)
{
	int macros;
	char *addr;
	DC_HOST host;

#define STR_CONTAINS_MACROS(str) (NULL != strchr(str, '{'))

	macros = STR_CONTAINS_MACROS(interface->ip) ? 0x01 : 0;
	macros |= STR_CONTAINS_MACROS(interface->dns) ? 0x02 : 0;

	if (0 != macros)
	{
		DCget_host_by_hostid(&host, interface->hostid);

		if (0 != (macros & 0x01))
		{
			addr = zbx_strdup(NULL, interface->ip);
			zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL, NULL, NULL,
										 NULL, &addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == zbx_is_ip(addr) || SUCCEED == zbx_validate_hostname(addr))
				dc_strpool_replace(1, &interface->ip, addr);
			zbx_free(addr);
		}

		if (0 != (macros & 0x02))
		{
			addr = zbx_strdup(NULL, interface->dns);
			zbx_substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL, NULL, NULL,
										 NULL, &addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == zbx_is_ip(addr) || SUCCEED == zbx_validate_hostname(addr))
				dc_strpool_replace(1, &interface->dns, addr);
			zbx_free(addr);
		}
	}
#undef STR_CONTAINS_MACROS
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove interface from SNMP address -> interfaceid index           *
 *                                                                            *
 * Parameters: interface - [IN]                                               *
 *                                                                            *
 ******************************************************************************/
static void dc_interface_snmpaddrs_remove(ZBX_DC_INTERFACE *interface)
{
	ZBX_DC_INTERFACE_ADDR *ifaddr, ifaddr_local;
	int index;

	ifaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

	if ('\0' == *ifaddr_local.addr)
		return;

	if (NULL == (ifaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(&config->interface_snmpaddrs,
																	  &ifaddr_local)))
	{
		return;
	}

	if (FAIL == (index = zbx_vector_uint64_search(&ifaddr->interfaceids, interface->interfaceid,
												  ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	zbx_vector_uint64_remove_noorder(&ifaddr->interfaceids, index);

	if (0 == ifaddr->interfaceids.values_num)
	{
		dc_strpool_release(ifaddr->addr);
		zbx_vector_uint64_destroy(&ifaddr->interfaceids);
		zbx_hashset_remove_direct(&config->interface_snmpaddrs, ifaddr);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: setup SNMP attributes for interface with interfaceid index        *
 *                                                                            *
 * Parameters: interfaceid  - [IN]                                            *
 *             row          - [IN] the row data from DB                       *
 *             bulk_changed - [IN]                                            *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_SNMPINTERFACE *dc_interface_snmp_set(zbx_uint64_t interfaceid, const char **row,
												   unsigned char *bulk_changed)
{
	int found;
	ZBX_DC_SNMPINTERFACE *snmp;
	unsigned char bulk;

	snmp = (ZBX_DC_SNMPINTERFACE *)DCfind_id(&config->interfaces_snmp, interfaceid, sizeof(ZBX_DC_SNMPINTERFACE),
											 &found);

	ZBX_STR2UCHAR(bulk, row[13]);

	if (0 == found)
		*bulk_changed = 1;
	else if (snmp->bulk != bulk)
		*bulk_changed = 1;
	else
		*bulk_changed = 0;

	if (0 != *bulk_changed)
		snmp->bulk = bulk;

	ZBX_STR2UCHAR(snmp->version, row[12]);
	dc_strpool_replace(found, &snmp->community, row[14]);
	dc_strpool_replace(found, &snmp->securityname, row[15]);
	ZBX_STR2UCHAR(snmp->securitylevel, row[16]);
	dc_strpool_replace(found, &snmp->authpassphrase, row[17]);
	dc_strpool_replace(found, &snmp->privpassphrase, row[18]);
	ZBX_STR2UCHAR(snmp->authprotocol, row[19]);
	ZBX_STR2UCHAR(snmp->privprotocol, row[20]);
	dc_strpool_replace(found, &snmp->contextname, row[21]);
	ZBX_STR2UCHAR(snmp->max_repetitions, row[22]);

	return snmp;
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove interface from SNMP address -> interfaceid index           *
 *                                                                            *
 * Parameters: interfaceid - [IN]                                             *
 *                                                                            *
 ******************************************************************************/
static void dc_interface_snmp_remove(zbx_uint64_t interfaceid)
{
	ZBX_DC_SNMPINTERFACE *snmp;

	if (NULL == (snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid)))
		return;

	dc_strpool_release(snmp->community);
	dc_strpool_release(snmp->securityname);
	dc_strpool_release(snmp->authpassphrase);
	dc_strpool_release(snmp->privpassphrase);
	dc_strpool_release(snmp->contextname);

	zbx_hashset_remove_direct(&config->interfaces_snmp, snmp);

	return;
}

void DCsync_interfaces(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_INTERFACE *interface;
	ZBX_DC_INTERFACE_HT *interface_ht, interface_ht_local;
	ZBX_DC_INTERFACE_ADDR *interface_snmpaddr, interface_snmpaddr_local;
	ZBX_DC_HOST *host;

	int found, update_index, ret, i;
	zbx_uint64_t interfaceid, hostid;
	unsigned char type, main_, useip;
	unsigned char reset_snmp_stats;
	zbx_vector_ptr_t interfaces;
	zbx_vector_uint64_t changed_hosts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&interfaces);
	zbx_vector_uint64_create(&changed_hosts);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(interfaceid, row[0]);
		ZBX_STR2UINT64(hostid, row[1]);
		ZBX_STR2UCHAR(type, row[2]);
		ZBX_STR2UCHAR(main_, row[3]);
		ZBX_STR2UCHAR(useip, row[4]);

		/* If there is no host for this interface, skip it. */
		/* This may be possible if the host was added after we synced config for hosts. */
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
			continue;

		zbx_vector_uint64_append(&changed_hosts, hostid);

		interface = (ZBX_DC_INTERFACE *)DCfind_id(&config->interfaces, interfaceid, sizeof(ZBX_DC_INTERFACE),
												  &found);
		zbx_vector_ptr_append(&interfaces, interface);

		/* remove old address->interfaceid index */
		if (0 != found && INTERFACE_TYPE_SNMP == interface->type)
			dc_interface_snmpaddrs_remove(interface);

		/* see whether we should and can update interfaces_ht index at this point */

		update_index = 0;

		if (0 == found || interface->hostid != hostid || interface->type != type || interface->main != main_)
		{
			if (1 == found && 1 == interface->main)
			{
				interface_ht_local.hostid = interface->hostid;
				interface_ht_local.type = interface->type;
				interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht,
																		 &interface_ht_local);

				if (NULL != interface_ht && interface == interface_ht->interface_ptr)
				{
					/* see ZBX-4045 for NULL check in the conditional */
					zbx_hashset_remove(&config->interfaces_ht, &interface_ht_local);
				}
			}

			if (1 == main_)
			{
				interface_ht_local.hostid = hostid;
				interface_ht_local.type = type;
				interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht,
																		 &interface_ht_local);

				if (NULL != interface_ht)
					interface_ht->interface_ptr = interface;
				else
					update_index = 1;
			}
		}

		/* store new information in interface structure */

		reset_snmp_stats = (0 == found || interface->hostid != hostid || interface->type != type ||
							interface->useip != useip);

		interface->hostid = hostid;
		interface->type = type;
		interface->main = main_;
		interface->useip = useip;
		reset_snmp_stats |= (SUCCEED == dc_strpool_replace(found, &interface->ip, row[5]));
		reset_snmp_stats |= (SUCCEED == dc_strpool_replace(found, &interface->dns, row[6]));
		reset_snmp_stats |= (SUCCEED == dc_strpool_replace(found, &interface->port, row[7]));


		if (0 == found)
		{

			//interface->disable_until = atoi(row[9]);

			interface->items_num = 0;
		}

		/* update interfaces_ht index using new data, if not done already */

		if (1 == update_index)
		{
			interface_ht_local.hostid = interface->hostid;
			interface_ht_local.type = interface->type;
			interface_ht_local.interface_ptr = interface;
			zbx_hashset_insert(&config->interfaces_ht, &interface_ht_local, sizeof(ZBX_DC_INTERFACE_HT));
		}

		/* update interface_snmpaddrs for SNMP traps or reset bulk request statistics */

		if (INTERFACE_TYPE_SNMP == interface->type)
		{
			ZBX_DC_SNMPINTERFACE *snmp;
			unsigned char bulk_changed;

			interface_snmpaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

			if ('\0' != *interface_snmpaddr_local.addr)
			{
				if (NULL == (interface_snmpaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(
								 &config->interface_snmpaddrs, &interface_snmpaddr_local)))
				{
					dc_strpool_acquire(interface_snmpaddr_local.addr);

					interface_snmpaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_insert(
						&config->interface_snmpaddrs, &interface_snmpaddr_local,
						sizeof(ZBX_DC_INTERFACE_ADDR));
					zbx_vector_uint64_create_ext(&interface_snmpaddr->interfaceids,
												 __config_shmem_malloc_func,
												 __config_shmem_realloc_func,
												 __config_shmem_free_func);
				}

				zbx_vector_uint64_append(&interface_snmpaddr->interfaceids, interfaceid);
			}

			if (FAIL == zbx_db_is_null(row[12]))
			{
				snmp = dc_interface_snmp_set(interfaceid, (const char **)row, &bulk_changed);

				if (1 == reset_snmp_stats || 0 != bulk_changed)
				{
					snmp->max_succeed = 0;
					snmp->min_fail = MAX_SNMP_ITEMS + 1;
				}
			}
			else
				THIS_SHOULD_NEVER_HAPPEN;
		}

		/* first resolve macros for ip and dns fields in main agent interface  */
		/* because other interfaces might reference main interfaces ip and dns */
		/* with {HOST.IP} and {HOST.DNS} macros                                */
		if (1 == interface->main && INTERFACE_TYPE_AGENT == interface->type)
			substitute_host_interface_macros(interface);

		if (0 == found)
		{
			/* new interface - add it to a list of host interfaces in 'config->hosts' hashset */

			int exists = 0;

			/* It is an error if the pointer is already in the list. Detect it. */

			for (i = 0; i < host->interfaces_v.values_num; i++)
			{
				if (interface == host->interfaces_v.values[i])
				{
					exists = 1;
					break;
				}
			}

			if (0 == exists)
				zbx_vector_ptr_append(&host->interfaces_v, interface);
			else
				THIS_SHOULD_NEVER_HAPPEN;
		}

		dc_host_update_revision(host, revision);

		// registering ip->host index in the state;
		if (interface->useip)
			glb_state_host_register_ip(interface->ip, interface->hostid);
		else
			glb_state_host_register_ip(interface->dns, interface->hostid);
	}

	/* resolve macros in other interfaces */

	for (i = 0; i < interfaces.values_num; i++)
	{
		interface = (ZBX_DC_INTERFACE *)interfaces.values[i];

		if (1 != interface->main || INTERFACE_TYPE_AGENT != interface->type)
			substitute_host_interface_macros(interface);
	}

	/* remove deleted interfaces from buffer */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &rowid)))
			continue;

		/* remove interface from the list of host interfaces in 'config->hosts' hashset */

		if (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &interface->hostid)))
		{
			for (i = 0; i < host->interfaces_v.values_num; i++)
			{
				if (interface == host->interfaces_v.values[i])
				{
					zbx_vector_ptr_remove(&host->interfaces_v, i);
					break;
				}
			}

			dc_host_update_revision(host, revision);
			zbx_vector_uint64_append(&changed_hosts, hostid);
		}

		if (INTERFACE_TYPE_SNMP == interface->type)
		{
			dc_interface_snmpaddrs_remove(interface);
			dc_interface_snmp_remove(interface->interfaceid);
		}

		if (1 == interface->main)
		{
			interface_ht_local.hostid = interface->hostid;
			interface_ht_local.type = interface->type;
			interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht,
																	 &interface_ht_local);

			if (NULL != interface_ht && interface == interface_ht->interface_ptr)
			{
				/* see ZBX-4045 for NULL check in the conditional */
				zbx_hashset_remove(&config->interfaces_ht, &interface_ht_local);
			}
		}

		dc_strpool_release(interface->ip);
		dc_strpool_release(interface->dns);
		dc_strpool_release(interface->port);

		zbx_hashset_remove_direct(&config->interfaces, interface);
	}

	conf_hosts_notify_changes(&changed_hosts);
	zbx_vector_uint64_destroy(&changed_hosts);

	zbx_vector_ptr_destroy(&interfaces);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove item from interfaceid -> itemid index                      *
 *                                                                            *
 * Parameters: interface - [IN] the item                                      *
 *                                                                            *
 ******************************************************************************/
static void dc_interface_snmpitems_remove(ZBX_DC_ITEM *item)
{
	ZBX_DC_INTERFACE_ITEM *ifitem;
	int index;
	zbx_uint64_t interfaceid;

	if (0 == (interfaceid = item->interfaceid))
		return;

	if (NULL == (ifitem = (ZBX_DC_INTERFACE_ITEM *)zbx_hashset_search(&config->interface_snmpitems, &interfaceid)))
		return;

	if (FAIL == (index = zbx_vector_uint64_search(&ifitem->itemids, item->itemid,
												  ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	zbx_vector_uint64_remove_noorder(&ifitem->itemids, index);

	if (0 == ifitem->itemids.values_num)
	{
		zbx_vector_uint64_destroy(&ifitem->itemids);
		zbx_hashset_remove_direct(&config->interface_snmpitems, ifitem);
	}
}

static void dc_masteritem_free(ZBX_DC_MASTERITEM *masteritem)
{
	zbx_vector_uint64_pair_destroy(&masteritem->dep_itemids);
	__config_shmem_free_func(masteritem);
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove itemid from master item dependent itemid vector            *
 *                                                                            *
 * Parameters: master_itemid - [IN] the master item identifier                *
 *             dep_itemid    - [IN] the dependent item identifier             *
 *                                                                            *
 ******************************************************************************/
static void dc_masteritem_remove_depitem(zbx_uint64_t master_itemid, zbx_uint64_t dep_itemid)
{
	ZBX_DC_MASTERITEM *masteritem;
	ZBX_DC_ITEM *item;
	int index;
	zbx_uint64_pair_t pair;

	if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &master_itemid)))
		return;

	if (NULL == (masteritem = item->master_item))
		return;

	pair.first = dep_itemid;
	if (FAIL == (index = zbx_vector_uint64_pair_search(&masteritem->dep_itemids, pair,
													   ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	zbx_vector_uint64_pair_remove_noorder(&masteritem->dep_itemids, index);

	if (0 == masteritem->dep_itemids.values_num)
	{
		dc_masteritem_free(item->master_item);
		item->master_item = NULL;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: update number of items per agent statistics                       *
 *                                                                            *
 * Parameters: interface - [IN/OUT] the interface                             *
 * *           type      - [IN] the item type (ITEM_TYPE_*)                   *
 *             num       - [IN] the number of items (+) added, (-) removed    *
 *                                                                            *
 ******************************************************************************/
static void dc_interface_update_agent_stats(ZBX_DC_INTERFACE *interface, unsigned char type, int num)
{
	if ((NULL != interface) && ((ITEM_TYPE_ZABBIX == type && INTERFACE_TYPE_AGENT == interface->type) ||
								(ITEM_TYPE_SNMP == type && INTERFACE_TYPE_SNMP == interface->type) ||
								(ITEM_TYPE_JMX == type && INTERFACE_TYPE_JMX == interface->type) ||
								(ITEM_TYPE_IPMI == type && INTERFACE_TYPE_IPMI == interface->type)))
		interface->items_num += num;
}

static unsigned char *dup_serialized_expression(const unsigned char *src)
{
	zbx_uint32_t offset, len;
	unsigned char *dst;

	if (NULL == src || '\0' == *src)
		return NULL;

	offset = zbx_deserialize_uint31_compact(src, &len);
	if (0 == len)
		return NULL;

	dst = (unsigned char *)zbx_malloc(NULL, offset + len);
	memcpy(dst, src, offset + len);

	return dst;
}

static unsigned char *config_decode_serialized_expression(const char *src)
{
	unsigned char *dst;
	int data_len, src_len;

	if (NULL == src || '\0' == *src)
		return NULL;

	src_len = strlen(src) * 3 / 4;
	dst = __config_shmem_malloc_func(NULL, src_len);
	str_base64_decode(src, (char *)dst, src_len, &data_len);

	return dst;
}
#define ITEM_ID_TYPE_STATIC 0
#define ITEM_ID_TYPE_TEMPLATED 0x1
#define ITEM_ID_TYPE_DISCOVERED 0x10

static u_int64_t glb_generate_template_item_id(u_int64_t cfg_id, u_int64_t res_id, u_int64_t type)
{
	u_int64_t id = (type << 62) + (res_id << 32) + cfg_id;
	return id;
}
static void decompose_item_id(u_int64_t itemid, u_int64_t *hostid, u_int64_t *temp_itemid)
{
	u_int64_t type = itemid >> 62;

	// LOG_INF("Resource type is %ld", type);

	*hostid = (itemid & (u_int64_t)0x3FFFFFFFFFFFFFFF) >> 32;
	*temp_itemid = (itemid & (u_int64_t)0xFFFFFFFF);
}

static int glb_handle_templated_item(u_int64_t orig_itemid, u_int64_t template_itemid, u_int64_t hostid)
{
	//	LOG_INF("Found reference item %ld - > %ld", orig_itemid, template_itemid);
	u_int64_t ni = glb_generate_template_item_id(template_itemid, hostid, 1);
	u_int64_t ti, hi;
	decompose_item_id(ni, &hi, &ti);
	//	LOG_INF(" in: (host:%ld, config_id:%ld) ->generated id: %ld ->decomposed back (host:%ld, config_id %ld)",
	//			hostid, template_itemid, ni, hi, ti);
}

static void dc_preprocitem_free(ZBX_DC_PREPROCITEM *preprocitem)
{
	zbx_vector_ptr_destroy(&preprocitem->preproc_ops);
	__config_shmem_free_func(preprocitem);
}

/******************************************************************************
 *                                                                            *
 * Purpose: releases trigger dependency list, removing it if necessary        *
 *                                                                            *
 ******************************************************************************/
static int dc_trigger_deplist_release(ZBX_DC_TRIGGER_DEPLIST *trigdep)
{
	if (0 == --trigdep->refcount)
	{
		zbx_vector_ptr_destroy(&trigdep->dependencies);
		zbx_hashset_remove_direct(&config->trigdeps, trigdep);
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: initializes trigger dependency list                               *
 *                                                                            *
 ******************************************************************************/
static void dc_trigger_deplist_init(ZBX_DC_TRIGGER_DEPLIST *trigdep, ZBX_DC_TRIGGER *trigger)
{
	trigdep->refcount = 1;
	trigdep->trigger = trigger;
	zbx_vector_ptr_create_ext(&trigdep->dependencies, __config_shmem_malloc_func, __config_shmem_realloc_func,
							  __config_shmem_free_func);
}

/******************************************************************************
 *                                                                            *
 * Purpose: resets trigger dependency list to release memory allocated by     *
 *          dependencies vector                                               *
 *                                                                            *
 ******************************************************************************/
static void dc_trigger_deplist_reset(ZBX_DC_TRIGGER_DEPLIST *trigdep)
{
	zbx_vector_ptr_destroy(&trigdep->dependencies);
	zbx_vector_ptr_create_ext(&trigdep->dependencies, __config_shmem_malloc_func, __config_shmem_realloc_func,
							  __config_shmem_free_func);
}

static void DCsync_trigdeps(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_TRIGGER_DEPLIST *trigdep_down, *trigdep_up;

	int found, index, ret;
	zbx_uint64_t triggerid_down, triggerid_up;
	ZBX_DC_TRIGGER *trigger_up, *trigger_down;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		/* find trigdep_down pointer */

		ZBX_STR2UINT64(triggerid_down, row[0]);
		if (NULL == (trigger_down = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerid_down)))
			continue;

		ZBX_STR2UINT64(triggerid_up, row[1]);
		if (NULL == (trigger_up = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerid_up)))
			continue;

		trigdep_down = (ZBX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_down,
														   sizeof(ZBX_DC_TRIGGER_DEPLIST), &found);

		if (0 == found)
			dc_trigger_deplist_init(trigdep_down, trigger_down);
		else
			trigdep_down->refcount++;

		trigdep_up = (ZBX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_up,
														 sizeof(ZBX_DC_TRIGGER_DEPLIST), &found);

		if (0 == found)
			dc_trigger_deplist_init(trigdep_up, trigger_up);
		else
			trigdep_up->refcount++;

		zbx_vector_ptr_append(&trigdep_down->dependencies, trigdep_up);
	}

	/* remove deleted trigger dependencies from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(triggerid_down, row[0]);
		if (NULL == (trigdep_down = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps,
																				 &triggerid_down)))
		{
			continue;
		}

		ZBX_STR2UINT64(triggerid_up, row[1]);
		if (NULL != (trigdep_up = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps,
																			   &triggerid_up)))
		{
			dc_trigger_deplist_release(trigdep_up);
		}

		if (SUCCEED != dc_trigger_deplist_release(trigdep_down))
		{
			if (FAIL == (index = zbx_vector_ptr_search(&trigdep_down->dependencies, &triggerid_up,
													   ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				continue;
			}

			if (1 == trigdep_down->dependencies.values_num)
				dc_trigger_deplist_reset(trigdep_down);
			else
				zbx_vector_ptr_remove_noorder(&trigdep_down->dependencies, index);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void DCsync_items(zbx_dbsync_t *sync, zbx_uint64_t revision, int flags, zbx_synced_new_config_t synced,
						 zbx_vector_uint64_t *deleted_itemids)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_HOST *host;

	ZBX_DC_ITEM *item;
	ZBX_DC_NUMITEM *numitem;
	ZBX_DC_SNMPITEM *snmpitem;
	ZBX_DC_IPMIITEM *ipmiitem;
	ZBX_DC_TRAPITEM *trapitem;
	ZBX_DC_DEPENDENTITEM *depitem;
	ZBX_DC_LOGITEM *logitem;
	ZBX_DC_DBITEM *dbitem;
	ZBX_DC_SSHITEM *sshitem;
	ZBX_DC_TELNETITEM *telnetitem;
	ZBX_DC_SIMPLEITEM *simpleitem;
	ZBX_DC_JMXITEM *jmxitem;
	ZBX_DC_CALCITEM *calcitem;
	ZBX_DC_INTERFACE_ITEM *interface_snmpitem;
	ZBX_DC_HTTPITEM *httpitem;
	ZBX_DC_SCRIPTITEM *scriptitem;
	ZBX_DC_ITEM_HK *item_hk, item_hk_local;
	ZBX_DC_INTERFACE *interface;

	time_t now;
	unsigned char status, type, value_type, old_poller_type;
	int found, update_index, ret, i;
	zbx_uint64_t itemid, hostid, interfaceid;
	zbx_vector_ptr_t dep_items;

	zbx_vector_ptr_create(&dep_items);
	poller_item_notify_init();
	
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[0]);
		ZBX_STR2UINT64(hostid, row[1]);
		ZBX_STR2UCHAR(status, row[2]);
		ZBX_STR2UCHAR(type, row[3]);

		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
			continue;

		item = (ZBX_DC_ITEM *)DCfind_id(&config->items, itemid, sizeof(ZBX_DC_ITEM), &found);

		/* template item */
		ZBX_DBROW2UINT64(item->templateid, row[48]);

		if (0 != found && ITEM_TYPE_SNMPTRAP == item->type)
			dc_interface_snmpitems_remove(item);

		/* see whether we should and can update items_hk index at this point */

		update_index = 0;

		if (0 == found || item->hostid != hostid || 0 != strcmp(item->key, row[5]))
		{
			if (1 == found)
			{
				item_hk_local.hostid = item->hostid;
				item_hk_local.key = item->key;

				if (NULL == (item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk,
																			&item_hk_local)))
				{
					/* item keys should be unique for items within a host, otherwise items with  */
					/* same key share index and removal of last added item already cleared index */
					THIS_SHOULD_NEVER_HAPPEN;
				}
				else if (item == item_hk->item_ptr)
				{
					dc_strpool_release(item_hk->key);
					zbx_hashset_remove_direct(&config->items_hk, item_hk);
				}
			}

			item_hk_local.hostid = hostid;
			item_hk_local.key = row[5];
			item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local);

			if (NULL != item_hk)
				item_hk->item_ptr = item;
			else
				update_index = 1;
		}

		/* store new information in item structure */

		item->hostid = hostid;
		item->flags = (unsigned char)atoi(row[18]);
		ZBX_DBROW2UINT64(interfaceid, row[19]);

		dc_strpool_replace(found, &item->history_period, row[22]);
		dc_strpool_replace(found, &item->name, row[27]);
		dc_strpool_replace(found, &item->description, row[49]);

		ZBX_STR2UCHAR(item->inventory_link, row[24]);
		ZBX_DBROW2UINT64(item->valuemapid, row[25]);

		if (0 != (ZBX_FLAG_DISCOVERY_RULE & item->flags))
			value_type = ITEM_VALUE_TYPE_TEXT;
		else
			ZBX_STR2UCHAR(value_type, row[4]);

		if (SUCCEED == dc_strpool_replace(found, &item->key, row[5]))
			flags |= ZBX_ITEM_KEY_CHANGED;

		if (0 == found)
		{
			item->triggers = NULL;
			item->update_triggers = 0;
			//ZBX_STR2UINT64(item->lastlogsize, row[20]);
			item->data_expected_from = now;
			item->location = ZBX_LOC_NOWHERE;
			item->poller_type = ZBX_NO_POLLER;
			item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
			item->delay_ex = NULL;

			if (ZBX_SYNCED_NEW_CONFIG_YES == synced && 0 == host->proxy_hostid)
				flags |= ZBX_ITEM_NEW;

			zbx_vector_ptr_create_ext(&item->tags, __config_shmem_malloc_func, __config_shmem_realloc_func,
									  __config_shmem_free_func);

			zbx_vector_dc_item_ptr_append(&host->items, item);

			item->preproc_item = NULL;
			item->master_item = NULL;
		}
		else
		{
			if (item->type != type)
				flags |= ZBX_ITEM_TYPE_CHANGED;

			if (ITEM_STATUS_ACTIVE == status && ITEM_STATUS_ACTIVE != item->status)
				item->data_expected_from = now;

			if (ITEM_STATUS_ACTIVE == item->status)
			{
				ZBX_DC_INTERFACE *interface_old;

				interface_old = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces,
																	   &item->interfaceid);
				dc_interface_update_agent_stats(interface_old, item->type, -1);
			}
		}

		item->revision = revision;
		dc_host_update_revision(host, revision);

		if (ITEM_STATUS_ACTIVE == status)
		{
			interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &interfaceid);
			dc_interface_update_agent_stats(interface, type, 1);
		}

		item->type = type;
		item->status = status;
		item->value_type = value_type;
		item->interfaceid = interfaceid;

		/* update items_hk index using new data, if not done already */

		if (1 == update_index)
		{
			item_hk_local.hostid = item->hostid;
			item_hk_local.key = dc_strpool_acquire(item->key);
			item_hk_local.item_ptr = item;
			zbx_hashset_insert(&config->items_hk, &item_hk_local, sizeof(ZBX_DC_ITEM_HK));
		}

		/* process item intervals and update item nextcheck */

		if (SUCCEED == dc_strpool_replace(found, &item->delay, row[8]))
		{
			flags |= ZBX_ITEM_DELAY_CHANGED;

			/* reset expanded delay if raw value was changed */
			if (NULL != item->delay_ex)
			{
				dc_strpool_release(item->delay_ex);
				item->delay_ex = NULL;
			}
		}

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			numitem = (ZBX_DC_NUMITEM *)DCfind_id(&config->numitems, itemid, sizeof(ZBX_DC_NUMITEM),
												  &found);

			dc_strpool_replace(found, &numitem->trends_period, row[23]);
			dc_strpool_replace(found, &numitem->units, row[26]);
		}
		else if (NULL != (numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&config->numitems, &itemid)))
		{
			/* remove parameters for non-numeric item */

			dc_strpool_release(numitem->units);
			dc_strpool_release(numitem->trends_period);

			zbx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (ITEM_TYPE_SNMP == item->type)
		{
			snmpitem = (ZBX_DC_SNMPITEM *)DCfind_id(&config->snmpitems, itemid, sizeof(ZBX_DC_SNMPITEM),
													&found);

			if (SUCCEED == dc_strpool_replace(found, &snmpitem->snmp_oid, row[6]))
			{
				if (NULL != strchr(snmpitem->snmp_oid, '{'))
					snmpitem->snmp_oid_type = ZBX_SNMP_OID_TYPE_MACRO;
				else if (NULL != strchr(snmpitem->snmp_oid, '['))
					snmpitem->snmp_oid_type = ZBX_SNMP_OID_TYPE_DYNAMIC;
				else
					snmpitem->snmp_oid_type = ZBX_SNMP_OID_TYPE_NORMAL;
			}
		}
		else if (NULL != (snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &itemid)))
		{
			/* remove SNMP parameters for non-SNMP item */

			dc_strpool_release(snmpitem->snmp_oid);
			zbx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (ZBX_DC_IPMIITEM *)DCfind_id(&config->ipmiitems, itemid, sizeof(ZBX_DC_IPMIITEM),
													&found);

			dc_strpool_replace(found, &ipmiitem->ipmi_sensor, row[7]);
		}
		else if (NULL != (ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems, &itemid)))
		{
			/* remove IPMI parameters for non-IPMI item */
			dc_strpool_release(ipmiitem->ipmi_sensor);
			zbx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type && '\0' != *row[9])
		{
			trapitem = (ZBX_DC_TRAPITEM *)DCfind_id(&config->trapitems, itemid, sizeof(ZBX_DC_TRAPITEM),
													&found);
			dc_strpool_replace(found, &trapitem->trapper_hosts, row[9]);
		}
		else if (NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems, &itemid)))
		{
			/* remove trapper_hosts parameter */
			dc_strpool_release(trapitem->trapper_hosts);
			zbx_hashset_remove_direct(&config->trapitems, trapitem);
		}

		/* dependent items */

		if (ITEM_TYPE_DEPENDENT == item->type && SUCCEED != zbx_db_is_null(row[29]))
		{
			depitem = (ZBX_DC_DEPENDENTITEM *)DCfind_id(&config->dependentitems, itemid,
														sizeof(ZBX_DC_DEPENDENTITEM), &found);

			if (1 == found)
				depitem->last_master_itemid = depitem->master_itemid;
			else
				depitem->last_master_itemid = 0;

			depitem->flags = item->flags;
			ZBX_STR2UINT64(depitem->master_itemid, row[29]);

			if (depitem->last_master_itemid != depitem->master_itemid)
				zbx_vector_ptr_append(&dep_items, depitem);
		}
		else if (NULL != (depitem = (ZBX_DC_DEPENDENTITEM *)zbx_hashset_search(&config->dependentitems,
																			   &itemid)))
		{
			dc_masteritem_remove_depitem(depitem->master_itemid, itemid);
			zbx_hashset_remove_direct(&config->dependentitems, depitem);
		}

		/* log items */

		if (ITEM_VALUE_TYPE_LOG == item->value_type && '\0' != *row[10])
		{
			logitem = (ZBX_DC_LOGITEM *)DCfind_id(&config->logitems, itemid, sizeof(ZBX_DC_LOGITEM),
												  &found);

			dc_strpool_replace(found, &logitem->logtimefmt, row[10]);
		}
		else if (NULL != (logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&config->logitems, &itemid)))
		{
			/* remove logtimefmt parameter */
			dc_strpool_release(logitem->logtimefmt);
			zbx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type && '\0' != *row[11])
		{
			dbitem = (ZBX_DC_DBITEM *)DCfind_id(&config->dbitems, itemid, sizeof(ZBX_DC_DBITEM), &found);

			dc_strpool_replace(found, &dbitem->params, row[11]);
			dc_strpool_replace(found, &dbitem->username, row[14]);
			dc_strpool_replace(found, &dbitem->password, row[15]);
		}
		else if (NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems, &itemid)))
		{
			/* remove db item parameters */
			dc_strpool_release(dbitem->params);
			dc_strpool_release(dbitem->username);
			dc_strpool_release(dbitem->password);

			zbx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (ZBX_DC_SSHITEM *)DCfind_id(&config->sshitems, itemid, sizeof(ZBX_DC_SSHITEM),
												  &found);

			sshitem->authtype = (unsigned short)atoi(row[13]);
			dc_strpool_replace(found, &sshitem->username, row[14]);
			dc_strpool_replace(found, &sshitem->password, row[15]);
			dc_strpool_replace(found, &sshitem->publickey, row[16]);
			dc_strpool_replace(found, &sshitem->privatekey, row[17]);
			dc_strpool_replace(found, &sshitem->params, row[11]);
		}
		else if (NULL != (sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems, &itemid)))
		{
			/* remove SSH item parameters */

			dc_strpool_release(sshitem->username);
			dc_strpool_release(sshitem->password);
			dc_strpool_release(sshitem->publickey);
			dc_strpool_release(sshitem->privatekey);
			dc_strpool_release(sshitem->params);

			zbx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (ZBX_DC_TELNETITEM *)DCfind_id(&config->telnetitems, itemid,
														sizeof(ZBX_DC_TELNETITEM), &found);

			dc_strpool_replace(found, &telnetitem->username, row[14]);
			dc_strpool_replace(found, &telnetitem->password, row[15]);
			dc_strpool_replace(found, &telnetitem->params, row[11]);
		}
		else if (NULL != (telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems, &itemid)))
		{
			/* remove TELNET item parameters */

			dc_strpool_release(telnetitem->username);
			dc_strpool_release(telnetitem->password);
			dc_strpool_release(telnetitem->params);

			zbx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (ZBX_DC_SIMPLEITEM *)DCfind_id(&config->simpleitems, itemid,
														sizeof(ZBX_DC_SIMPLEITEM), &found);

			dc_strpool_replace(found, &simpleitem->username, row[14]);
			dc_strpool_replace(found, &simpleitem->password, row[15]);
		}
		else if (NULL != (simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems, &itemid)))
		{
			/* remove simple item parameters */

			dc_strpool_release(simpleitem->username);
			dc_strpool_release(simpleitem->password);

			zbx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (ZBX_DC_JMXITEM *)DCfind_id(&config->jmxitems, itemid, sizeof(ZBX_DC_JMXITEM),
												  &found);

			dc_strpool_replace(found, &jmxitem->username, row[14]);
			dc_strpool_replace(found, &jmxitem->password, row[15]);
			dc_strpool_replace(found, &jmxitem->jmx_endpoint, row[28]);
		}
		else if (NULL != (jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &itemid)))
		{
			/* remove JMX item parameters */

			dc_strpool_release(jmxitem->username);
			dc_strpool_release(jmxitem->password);
			dc_strpool_release(jmxitem->jmx_endpoint);

			zbx_hashset_remove_direct(&config->jmxitems, jmxitem);
		}

		/* SNMP trap items for current server/proxy */

		if (ITEM_TYPE_SNMPTRAP == item->type && 0 == host->proxy_hostid)
		{
			interface_snmpitem = (ZBX_DC_INTERFACE_ITEM *)DCfind_id(&config->interface_snmpitems,
																	item->interfaceid, sizeof(ZBX_DC_INTERFACE_ITEM), &found);

			if (0 == found)
			{
				zbx_vector_uint64_create_ext(&interface_snmpitem->itemids,
											 __config_shmem_malloc_func,
											 __config_shmem_realloc_func,
											 __config_shmem_free_func);
			}

			zbx_vector_uint64_append(&interface_snmpitem->itemids, itemid);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (ZBX_DC_CALCITEM *)DCfind_id(&config->calcitems, itemid, sizeof(ZBX_DC_CALCITEM),
													&found);

			dc_strpool_replace(found, &calcitem->params, row[11]);

			if (1 == found && NULL != calcitem->formula_bin)
				__config_shmem_free_func((void *)calcitem->formula_bin);

			calcitem->formula_bin = config_decode_serialized_expression(row[49]);
		}
		else if (NULL != (calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems, &itemid)))
		{
			/* remove calculated item parameters */

			if (NULL != calcitem->formula_bin)
				__config_shmem_free_func((void *)calcitem->formula_bin);
			dc_strpool_release(calcitem->params);
			zbx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (ZBX_DC_HTTPITEM *)DCfind_id(&config->httpitems, itemid, sizeof(ZBX_DC_HTTPITEM),
													&found);

			dc_strpool_replace(found, &httpitem->timeout, row[30]);
			dc_strpool_replace(found, &httpitem->url, row[31]);
			dc_strpool_replace(found, &httpitem->query_fields, row[32]);
			dc_strpool_replace(found, &httpitem->posts, row[33]);
			dc_strpool_replace(found, &httpitem->status_codes, row[34]);
			httpitem->follow_redirects = (unsigned char)atoi(row[35]);
			httpitem->post_type = (unsigned char)atoi(row[36]);
			dc_strpool_replace(found, &httpitem->http_proxy, row[37]);
			dc_strpool_replace(found, &httpitem->headers, row[38]);
			httpitem->retrieve_mode = (unsigned char)atoi(row[39]);
			httpitem->request_method = (unsigned char)atoi(row[40]);
			httpitem->output_format = (unsigned char)atoi(row[41]);
			dc_strpool_replace(found, &httpitem->ssl_cert_file, row[42]);
			dc_strpool_replace(found, &httpitem->ssl_key_file, row[43]);
			dc_strpool_replace(found, &httpitem->ssl_key_password, row[44]);
			httpitem->verify_peer = (unsigned char)atoi(row[45]);
			httpitem->verify_host = (unsigned char)atoi(row[46]);
			httpitem->allow_traps = (unsigned char)atoi(row[47]);

			httpitem->authtype = (unsigned char)atoi(row[13]);
			dc_strpool_replace(found, &httpitem->username, row[14]);
			dc_strpool_replace(found, &httpitem->password, row[15]);
			dc_strpool_replace(found, &httpitem->trapper_hosts, row[9]);
		}
		else if (NULL != (httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems, &itemid)))
		{
			dc_strpool_release(httpitem->timeout);
			dc_strpool_release(httpitem->url);
			dc_strpool_release(httpitem->query_fields);
			dc_strpool_release(httpitem->posts);
			dc_strpool_release(httpitem->status_codes);
			dc_strpool_release(httpitem->http_proxy);
			dc_strpool_release(httpitem->headers);
			dc_strpool_release(httpitem->ssl_cert_file);
			dc_strpool_release(httpitem->ssl_key_file);
			dc_strpool_release(httpitem->ssl_key_password);
			dc_strpool_release(httpitem->username);
			dc_strpool_release(httpitem->password);
			dc_strpool_release(httpitem->trapper_hosts);

			zbx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* Script items */

		if (ITEM_TYPE_SCRIPT == item->type)
		{
			scriptitem = (ZBX_DC_SCRIPTITEM *)DCfind_id(&config->scriptitems, itemid,
														sizeof(ZBX_DC_SCRIPTITEM), &found);

			dc_strpool_replace(found, &scriptitem->timeout, row[30]);
			dc_strpool_replace(found, &scriptitem->script, row[11]);

			if (0 == found)
			{
				zbx_vector_ptr_create_ext(&scriptitem->params, __config_shmem_malloc_func,
										  __config_shmem_realloc_func, __config_shmem_free_func);
			}
		}
		else if (NULL != (scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems, &itemid)))
		{
			dc_strpool_release(scriptitem->timeout);
			dc_strpool_release(scriptitem->script);

			zbx_vector_ptr_destroy(&scriptitem->params);
			zbx_hashset_remove_direct(&config->scriptitems, scriptitem);
		}

		if (ITEM_TYPE_WORKER_SERVER == item->type)
		{
			dc_strpool_replace(found, &item->params, row[11]);
		}

		/* it is crucial to update type specific (config->snmpitems, config->ipmiitems, etc.) hashsets before */
		/* attempting to requeue an item because type specific properties are used to arrange items in queues */

		old_poller_type = item->poller_type;

		if (ITEM_STATUS_ACTIVE == item->status && HOST_STATUS_MONITORED == host->status)
		{
			DCitem_poller_type_update(item, host, flags);

			if (SUCCEED == zbx_is_counted_in_item_queue(item->type, item->key))
			{
				char *error = NULL;

				if (FAIL == DCitem_nextcheck_update(item, interface, flags, now, &error))
				{
					zbx_timespec_t ts = {now, 0};

					/* Usual way for an item to become not supported is to receive an error     */
					/* instead of value. Item state and error will be updated by history syncer */
					/* during history sync following a regular procedure with item update in    */
					/* database and config cache, logging etc. There is no need to set          */
					/* ITEM_STATE_NOTSUPPORTED here.                                            */

					if (0 == host->proxy_hostid)
						glb_state_item_set_error(item->itemid, error);
			
					zbx_free(error);
				}
			}
		}
		else
		{
			item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
			item->queue_next_check = 0;
			item->poller_type = ZBX_NO_POLLER;
		}

		/* items that do not support notify-updates are passed to old queuing */

		DEBUG_ITEM(item->itemid, "About to be checked how to poll");
		if (FAIL == glb_might_be_async_polled(item, host) ||
			FAIL == poller_item_add_notify(type, item->key, itemid, hostid))
		{

			DEBUG_ITEM(item->itemid, "Cannot be async polled, adding to zbx queue %s", __func__);
			DCupdate_item_queue(item, old_poller_type);
		}
	}

	/* update dependent item vectors within master items */

	for (i = 0; i < dep_items.values_num; i++)
	{
		zbx_uint64_pair_t pair;

		depitem = (ZBX_DC_DEPENDENTITEM *)dep_items.values[i];
		dc_masteritem_remove_depitem(depitem->last_master_itemid, depitem->itemid);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &depitem->master_itemid)))
			continue;

		pair.first = depitem->itemid;
		pair.second = depitem->flags;

		if (NULL == item->master_item)
		{
			item->master_item = (ZBX_DC_MASTERITEM *)__config_shmem_malloc_func(NULL,
																				sizeof(ZBX_DC_MASTERITEM));

			zbx_vector_uint64_pair_create_ext(&item->master_item->dep_itemids, __config_shmem_malloc_func,
											  __config_shmem_realloc_func, __config_shmem_free_func);
		}

		zbx_vector_uint64_pair_append(&item->master_item->dep_itemids, pair);
	}

	zbx_vector_ptr_destroy(&dep_items);

	if (NULL != deleted_itemids)
		zbx_vector_uint64_reserve(deleted_itemids, sync->remove_num);

	/* remove deleted items from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL != deleted_itemids)
			zbx_vector_uint64_append(deleted_itemids, rowid);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &rowid)))
			continue;

		if (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid)))
		{
			dc_host_update_revision(host, revision);

			if (FAIL != (i = zbx_vector_dc_item_ptr_search(&host->items, item,
														   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_dc_item_ptr_remove(&host->items, i);
			}
		
			if (SUCCEED == glb_might_be_async_polled(item, host))
			{
				DEBUG_ITEM(item->itemid, "Sending poller notify about item removal");
				poller_item_add_notify(item->type, item->key, item->itemid, item->hostid);
			}
		}

		if (ITEM_STATUS_ACTIVE == item->status)
		{
			interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &item->interfaceid);
			dc_interface_update_agent_stats(interface, item->type, -1);
		}

		itemid = item->itemid;

		if (ITEM_TYPE_SNMPTRAP == item->type)
			dc_interface_snmpitems_remove(item);

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&config->numitems, &itemid);

			dc_strpool_release(numitem->units);
			dc_strpool_release(numitem->trends_period);

			zbx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (ITEM_TYPE_SNMP == item->type)
		{
			snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &itemid);
			dc_strpool_release(snmpitem->snmp_oid);
			zbx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems, &itemid);
			dc_strpool_release(ipmiitem->ipmi_sensor);
			zbx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type &&
			NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems, &itemid)))
		{
			dc_strpool_release(trapitem->trapper_hosts);
			zbx_hashset_remove_direct(&config->trapitems, trapitem);
		}

		/* dependent items */

		if (NULL != (depitem = (ZBX_DC_DEPENDENTITEM *)zbx_hashset_search(&config->dependentitems, &itemid)))
		{
			dc_masteritem_remove_depitem(depitem->master_itemid, itemid);
			zbx_hashset_remove_direct(&config->dependentitems, depitem);
		}

		/* log items */

		if (ITEM_VALUE_TYPE_LOG == item->value_type &&
			NULL != (logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&config->logitems, &itemid)))
		{
			dc_strpool_release(logitem->logtimefmt);
			zbx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type &&
			NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems, &itemid)))
		{
			dc_strpool_release(dbitem->params);
			dc_strpool_release(dbitem->username);
			dc_strpool_release(dbitem->password);

			zbx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems, &itemid);

			dc_strpool_release(sshitem->username);
			dc_strpool_release(sshitem->password);
			dc_strpool_release(sshitem->publickey);
			dc_strpool_release(sshitem->privatekey);
			dc_strpool_release(sshitem->params);

			zbx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems, &itemid);

			dc_strpool_release(telnetitem->username);
			dc_strpool_release(telnetitem->password);
			dc_strpool_release(telnetitem->params);

			zbx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems, &itemid);

			dc_strpool_release(simpleitem->username);
			dc_strpool_release(simpleitem->password);

			zbx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &itemid);

			dc_strpool_release(jmxitem->username);
			dc_strpool_release(jmxitem->password);
			dc_strpool_release(jmxitem->jmx_endpoint);

			zbx_hashset_remove_direct(&config->jmxitems, jmxitem);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems, &itemid);
			dc_strpool_release(calcitem->params);

			if (NULL != calcitem->formula_bin)
				__config_shmem_free_func((void *)calcitem->formula_bin);

			zbx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems, &itemid);

			dc_strpool_release(httpitem->timeout);
			dc_strpool_release(httpitem->url);
			dc_strpool_release(httpitem->query_fields);
			dc_strpool_release(httpitem->posts);
			dc_strpool_release(httpitem->status_codes);
			dc_strpool_release(httpitem->http_proxy);
			dc_strpool_release(httpitem->headers);
			dc_strpool_release(httpitem->ssl_cert_file);
			dc_strpool_release(httpitem->ssl_key_file);
			dc_strpool_release(httpitem->ssl_key_password);
			dc_strpool_release(httpitem->username);
			dc_strpool_release(httpitem->password);
			dc_strpool_release(httpitem->trapper_hosts);

			zbx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* Script items */

		if (ITEM_TYPE_SCRIPT == item->type)
		{
			scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems, &itemid);

			dc_strpool_release(scriptitem->timeout);
			dc_strpool_release(scriptitem->script);

			zbx_vector_ptr_destroy(&scriptitem->params);
			zbx_hashset_remove_direct(&config->scriptitems, scriptitem);
		}

		/* items */

		item_hk_local.hostid = item->hostid;
		item_hk_local.key = item->key;

		if (NULL == (item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local)))
		{
			/* item keys should be unique for items within a host, otherwise items with  */
			/* same key share index and removal of last added item already cleared index */
			THIS_SHOULD_NEVER_HAPPEN;
		}
		else if (item == item_hk->item_ptr)
		{
			dc_strpool_release(item_hk->key);
			zbx_hashset_remove_direct(&config->items_hk, item_hk);
		}

		if (ZBX_LOC_QUEUE == item->location)
			zbx_binary_heap_remove_direct(&config->queues[item->poller_type], item->itemid);

		dc_strpool_release(item->key);
		dc_strpool_release(item->delay);
		dc_strpool_release(item->history_period);
		dc_strpool_release(item->name);
		dc_strpool_release(item->description);

		if (NULL != item->delay_ex)
			dc_strpool_release(item->delay_ex);

		if (NULL != item->triggers)
			config->items.mem_free_func(item->triggers);

		zbx_vector_ptr_destroy(&item->tags);

		if (NULL != item->preproc_item)
			dc_preprocitem_free(item->preproc_item);

		if (NULL != item->master_item)
			dc_masteritem_free(item->master_item);

		zbx_hashset_remove_direct(&config->items, item);
	}

	poller_item_notify_flush();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_item_discovery(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid, itemid;
	unsigned char tag;
	int ret, found;
	ZBX_DC_ITEM_DISCOVERY *item_discovery;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[0]);
		item_discovery = (ZBX_DC_ITEM_DISCOVERY *)DCfind_id(&config->item_discovery, itemid,
															sizeof(ZBX_DC_ITEM_DISCOVERY), &found);

		/* LLD item prototype */
		ZBX_STR2UINT64(item_discovery->parent_itemid, row[1]);
	}

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item_discovery = (ZBX_DC_ITEM_DISCOVERY *)zbx_hashset_search(&config->item_discovery,
																				  &rowid)))
		{
			continue;
		}

		zbx_hashset_remove_direct(&config->item_discovery, item_discovery);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_template_items(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid, itemid;
	unsigned char tag;
	int ret, found;
	ZBX_DC_TEMPLATE_ITEM *item;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[0]);
		item = (ZBX_DC_TEMPLATE_ITEM *)DCfind_id(&config->template_items, itemid, sizeof(ZBX_DC_TEMPLATE_ITEM),
												 &found);

		ZBX_STR2UINT64(item->hostid, row[1]);
		ZBX_DBROW2UINT64(item->templateid, row[2]);
	}

	/* remove deleted template items from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (ZBX_DC_TEMPLATE_ITEM *)zbx_hashset_search(&config->template_items, &rowid)))
			continue;

		zbx_hashset_remove_direct(&config->template_items, item);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_prototype_items(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid, itemid;
	unsigned char tag;
	int ret, found;
	ZBX_DC_PROTOTYPE_ITEM *item;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[0]);
		item = (ZBX_DC_PROTOTYPE_ITEM *)DCfind_id(&config->prototype_items, itemid,
												  sizeof(ZBX_DC_PROTOTYPE_ITEM), &found);

		ZBX_STR2UINT64(item->hostid, row[1]);
		ZBX_DBROW2UINT64(item->templateid, row[2]);
	}

	/* remove deleted prototype items from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (ZBX_DC_PROTOTYPE_ITEM *)zbx_hashset_search(&config->prototype_items, &rowid)))
			continue;

		zbx_hashset_remove_direct(&config->prototype_items, item);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

void DCsync_triggers(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_TRIGGER *trigger;

	int found, ret;
	zbx_uint64_t triggerid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(triggerid, row[0]);

		trigger = (ZBX_DC_TRIGGER *)DCfind_id(&config->triggers, triggerid, sizeof(ZBX_DC_TRIGGER), &found);

		/* store new information in trigger structure */

		ZBX_STR2UCHAR(trigger->flags, row[19]);

		if (ZBX_FLAG_DISCOVERY_PROTOTYPE == trigger->flags)
			continue;

		dc_strpool_replace(found, &trigger->description, row[1]);
		dc_strpool_replace(found, &trigger->expression, row[2]);
		dc_strpool_replace(found, &trigger->recovery_expression, row[11]);
		dc_strpool_replace(found, &trigger->correlation_tag, row[13]);
		dc_strpool_replace(found, &trigger->opdata, row[14]);
		dc_strpool_replace(found, &trigger->event_name, row[15]);
		ZBX_STR2UCHAR(trigger->priority, row[4]);
		ZBX_STR2UCHAR(trigger->type, row[5]);
		ZBX_STR2UCHAR(trigger->status, row[9]);
		ZBX_STR2UCHAR(trigger->recovery_mode, row[10]);
		ZBX_STR2UCHAR(trigger->correlation_mode, row[12]);

		if (0 == found)
		{
			dc_strpool_replace(found, &trigger->error, "");
			trigger->locked = 0;
			trigger->timer_revision = 0;

			zbx_vector_ptr_create_ext(&trigger->tags, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);
			trigger->topoindex = 1;
			trigger->itemids = NULL;
		}
		else
		{
			if (NULL != trigger->expression_bin)
				__config_shmem_free_func((void *)trigger->expression_bin);
			if (NULL != trigger->recovery_expression_bin)
				__config_shmem_free_func((void *)trigger->recovery_expression_bin);
		}

		trigger->expression_bin = config_decode_serialized_expression(row[16]);
		trigger->recovery_expression_bin = config_decode_serialized_expression(row[17]);
		trigger->timer = atoi(row[18]);
		trigger->revision = revision;
	}

	/* remove deleted triggers from buffer */
	if (SUCCEED == ret)
	{
		ZBX_DC_ITEM *item;
		zbx_uint64_t *itemid;

		for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
		{
			if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &rowid)))
				continue;

			if (ZBX_FLAG_DISCOVERY_PROTOTYPE != trigger->flags)
			{
				/* force trigger list update for items used in removed trigger */
				if (NULL != trigger->itemids)
				{
					for (itemid = trigger->itemids; 0 != *itemid; itemid++)
					{
						if (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items,
																			  itemid)))
						{
							dc_item_reset_triggers(item, trigger);
						}
					}
				}

				dc_strpool_release(trigger->description);
				dc_strpool_release(trigger->expression);
				dc_strpool_release(trigger->recovery_expression);
				dc_strpool_release(trigger->error);
				dc_strpool_release(trigger->correlation_tag);
				dc_strpool_release(trigger->opdata);
				dc_strpool_release(trigger->event_name);

				zbx_vector_ptr_destroy(&trigger->tags);

				if (NULL != trigger->expression_bin)
					__config_shmem_free_func((void *)trigger->expression_bin);
				if (NULL != trigger->recovery_expression_bin)
					__config_shmem_free_func((void *)trigger->recovery_expression_bin);

				if (NULL != trigger->itemids)
					__config_shmem_free_func((void *)trigger->itemids);
			}

			zbx_hashset_remove_direct(&config->triggers, trigger);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

// static void	DCconfig_sort_triggers_topologically(void);

/******************************************************************************
 *                                                                            *
 * Purpose: releases trigger dependency list, removing it if necessary        *
 *                                                                            *
 ******************************************************************************/
/*
static int	dc_trigger_deplist_release(ZBX_DC_TRIGGER_DEPLIST *trigdep)
{
	if (0 == --trigdep->refcount)
	{
		zbx_vector_ptr_destroy(&trigdep->dependencies);
		zbx_hashset_remove_direct(&config->trigdeps, trigdep);
		return SUCCEED;
	}

	return FAIL;
}
*/

#define ZBX_TIMER_DELAY 30

static int dc_function_calculate_trends_nextcheck(const zbx_dc_um_handle_t *um_handle,
												  const zbx_trigger_timer_t *timer, zbx_uint64_t seed, time_t *nextcheck, char **error)
{
	unsigned int offsets[ZBX_TIME_UNIT_COUNT] = {0, 0, 0, SEC_PER_MIN * 10,
												 SEC_PER_HOUR + SEC_PER_MIN * 10, SEC_PER_HOUR + SEC_PER_MIN * 10,
												 SEC_PER_HOUR + SEC_PER_MIN * 10, SEC_PER_HOUR + SEC_PER_MIN * 10,
												 SEC_PER_HOUR + SEC_PER_MIN * 10};
	unsigned int periods[ZBX_TIME_UNIT_COUNT] = {0, 0, 0, SEC_PER_MIN * 10, SEC_PER_HOUR,
												 SEC_PER_HOUR * 11, SEC_PER_DAY - SEC_PER_HOUR, SEC_PER_DAY - SEC_PER_HOUR,
												 SEC_PER_DAY - SEC_PER_HOUR};

	time_t next;
	struct tm tm;
	char *param, *period_shift;
	int ret = FAIL;
	zbx_time_unit_t trend_base;

	if (NULL == (param = zbx_function_get_param_dyn(timer->parameter, 1)))
	{
		*error = zbx_strdup(NULL, "no first parameter");
		return FAIL;
	}

	if (NULL != um_handle)
	{
		(void)zbx_dc_expand_user_macros(um_handle, &param, &timer->hostid, 1, NULL);
	}
	else
	{
		char *tmp;

		tmp = dc_expand_user_macros_dyn(param, &timer->hostid, 1, ZBX_MACRO_ENV_NONSECURE);
		zbx_free(param);
		param = tmp;
	}

	if (FAIL == zbx_trends_parse_base(param, &trend_base, error))
		goto out;

	if (trend_base < ZBX_TIME_UNIT_HOUR)
	{
		*error = zbx_strdup(NULL, "invalid first parameter");
		goto out;
	}

	localtime_r(&timer->lastcheck, &tm);

	if (ZBX_TIME_UNIT_HOUR == trend_base)
	{
		zbx_tm_round_up(&tm, trend_base);

		if (-1 == (*nextcheck = mktime(&tm)))
		{
			*error = zbx_strdup(NULL, zbx_strerror(errno));
			goto out;
		}

		ret = SUCCEED;
		goto out;
	}

	if (NULL == (period_shift = strchr(param, ':')))
	{
		*error = zbx_strdup(NULL, "invalid first parameter");
		goto out;
	}

	period_shift++;
	next = timer->lastcheck;

	while (SUCCEED == zbx_trends_parse_nextcheck(next, period_shift, nextcheck, error))
	{
		if (*nextcheck > timer->lastcheck)
		{
			ret = SUCCEED;
			break;
		}

		zbx_tm_add(&tm, 1, trend_base);
		if (-1 == (next = mktime(&tm)))
		{
			*error = zbx_strdup(*error, zbx_strerror(errno));
			break;
		}
	}
out:
	if (SUCCEED == ret)
		*nextcheck += (time_t)(offsets[trend_base] + seed % periods[trend_base]);

	zbx_free(param);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: calculate nextcheck for trigger timer                             *
 *                                                                            *
 * Parameters: um_handle - [IN] user macro cache handle (optional)            *
 *             timer     - [IN] the timer                                     *
 *             from      - [IN] the time from which the nextcheck must be     *
 *                              calculated                                    *
 *             seed      - [IN] timer seed to spread out the nextchecks       *
 *                                                                            *
 * Comments: When called within configuration cache lock pass NULL um_handle  *
 *           to directly use user macro cache                                 *
 *                                                                            *
 ******************************************************************************/
static time_t dc_function_calculate_nextcheck(const zbx_dc_um_handle_t *um_handle, const zbx_trigger_timer_t *timer,
											  time_t from, zbx_uint64_t seed)
{
	if (ZBX_TRIGGER_TIMER_FUNCTION_TIME == timer->type || ZBX_TRIGGER_TIMER_TRIGGER == timer->type)
	{
		int nextcheck;

		nextcheck = ZBX_TIMER_DELAY * (int)(from / (time_t)ZBX_TIMER_DELAY) +
					(int)(seed % (zbx_uint64_t)ZBX_TIMER_DELAY);

		while (nextcheck <= from)
			nextcheck += ZBX_TIMER_DELAY;

		return nextcheck;
	}
	else if (ZBX_TRIGGER_TIMER_FUNCTION_TREND == timer->type)
	{
		time_t nextcheck;
		char *error = NULL;

		if (SUCCEED != dc_function_calculate_trends_nextcheck(um_handle, timer, seed, &nextcheck, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot calculate trend function \"" ZBX_FS_UI64 "\" schedule: %s", timer->objectid, error);
			zbx_free(error);

			return 0;
		}

		return nextcheck;
	}

	THIS_SHOULD_NEVER_HAPPEN;

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: create trigger timer based on the trend function                  *
 *                                                                            *
 * Return value:  Created timer or NULL in the case of error.                 *
 *                                                                            *
 ******************************************************************************/
static zbx_trigger_timer_t *dc_trigger_function_timer_create(ZBX_DC_FUNCTION *function, int now)
{
	zbx_trigger_timer_t *timer;
	zbx_uint32_t type;
	ZBX_DC_ITEM *item;

	if (ZBX_FUNCTION_TYPE_TRENDS == function->type)
	{
		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
			return NULL;

		type = ZBX_TRIGGER_TIMER_FUNCTION_TREND;
	}
	else
	{
		type = ZBX_TRIGGER_TIMER_FUNCTION_TIME;
	}

	timer = (zbx_trigger_timer_t *)__config_shmem_malloc_func(NULL, sizeof(zbx_trigger_timer_t));

	timer->objectid = function->functionid;
	timer->triggerid = function->triggerid;
	timer->revision = function->revision;
	timer->lock = 0;
	timer->type = type;
	timer->lastcheck = (time_t)now;

	function->timer_revision = function->revision;

	if (ZBX_FUNCTION_TYPE_TRENDS == function->type)
	{
		dc_strpool_replace(0, &timer->parameter, function->parameter);
		timer->hostid = item->hostid;
	}
	else
	{
		timer->parameter = NULL;
		timer->hostid = 0;
	}

	return timer;
}

/******************************************************************************
 *                                                                            *
 * Purpose: create trigger timer based on the specified trigger               *
 *                                                                            *
 * Return value:  Created timer or NULL in the case of error.                 *
 *                                                                            *
 ******************************************************************************/
static zbx_trigger_timer_t *dc_trigger_timer_create(ZBX_DC_TRIGGER *trigger)
{
	zbx_trigger_timer_t *timer;

	timer = (zbx_trigger_timer_t *)__config_shmem_malloc_func(NULL, sizeof(zbx_trigger_timer_t));
	timer->type = ZBX_TRIGGER_TIMER_TRIGGER;
	timer->objectid = trigger->triggerid;
	timer->triggerid = trigger->triggerid;
	timer->revision = trigger->revision;
	timer->lock = 0;
	timer->parameter = NULL;

	trigger->timer_revision = trigger->revision;

	return timer;
}

/******************************************************************************
 *                                                                            *
 * Purpose: free trigger timer                                                *
 *                                                                            *
 ******************************************************************************/
static void dc_trigger_timer_free(zbx_trigger_timer_t *timer)
{
	if (NULL != timer->parameter)
		dc_strpool_release(timer->parameter);

	__config_shmem_free_func(timer);
}

/******************************************************************************
 *                                                                            *
 * Purpose: schedule trigger timer to be executed at the specified time       *
 *                                                                            *
 * Parameter: timer   - [IN] the timer to schedule                            *
 *            now     - [IN] current time                                     *
 *            eval_ts - [IN] the history snapshot time, by default (NULL)     *
 *                           execution time will be used.                     *
 *            exec_ts - [IN] the tiemer execution time                        *
 *                                                                            *
 ******************************************************************************/
static void dc_schedule_trigger_timer(zbx_trigger_timer_t *timer, int now, const zbx_timespec_t *eval_ts,
									  const zbx_timespec_t *exec_ts)
{
	zbx_binary_heap_elem_t elem;

	if (NULL == eval_ts)
		timer->eval_ts = *exec_ts;
	else
		timer->eval_ts = *eval_ts;

	timer->exec_ts = *exec_ts;
	timer->check_ts.sec = MIN(exec_ts->sec, now + ZBX_TRIGGER_POLL_INTERVAL);
	timer->check_ts.ns = 0;

	elem.key = 0;
	elem.data = (void *)timer;
	zbx_binary_heap_insert(&config->trigger_queue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Purpose: set timer schedule and evaluation times based on functions and    *
 *          old trend function queue                                          *
 *                                                                            *
 ******************************************************************************/
static void dc_schedule_trigger_timers(zbx_hashset_t *trend_queue, int now)
{
	ZBX_DC_FUNCTION *function;
	ZBX_DC_TRIGGER *trigger;
	zbx_trigger_timer_t *timer, *old;
	zbx_timespec_t ts;
	zbx_hashset_iter_t iter;

	ts.ns = 0;

	zbx_hashset_iter_reset(&config->functions, &iter);
	while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_FUNCTION_TYPE_TIMER != function->type && ZBX_FUNCTION_TYPE_TRENDS != function->type)
			continue;

		if (function->timer_revision == function->revision)
			continue;

		if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &function->triggerid)))
			continue;

		if (ZBX_FLAG_DISCOVERY_PROTOTYPE == trigger->flags)
			continue;

		if (TRIGGER_STATUS_ENABLED != trigger->status || TRIGGER_FUNCTIONAL_TRUE != trigger->functional)
			continue;

		if (NULL == (timer = dc_trigger_function_timer_create(function, now)))
			continue;

		if (NULL != trend_queue && NULL != (old = (zbx_trigger_timer_t *)zbx_hashset_search(trend_queue, &timer->objectid)) && old->eval_ts.sec < now + 10 * SEC_PER_MIN)
		{
			/* if the trigger was scheduled during next 10 minutes         */
			/* schedule its evaluation later to reduce server startup load */
			if (old->eval_ts.sec < now + 10 * SEC_PER_MIN)
				ts.sec = now + 10 * SEC_PER_MIN + (int)(timer->triggerid % (10 * SEC_PER_MIN));
			else
				ts.sec = old->eval_ts.sec;

			dc_schedule_trigger_timer(timer, now, &old->eval_ts, &ts);
		}
		else
		{
			if (0 == (ts.sec = (int)dc_function_calculate_nextcheck(NULL, timer, now, timer->triggerid)))
			{
				dc_trigger_timer_free(timer);
				function->timer_revision = 0;
			}
			else
				dc_schedule_trigger_timer(timer, now, NULL, &ts);
		}
	}

	zbx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_FLAG_DISCOVERY_PROTOTYPE == trigger->flags)
			continue;

		if (NULL == trigger->itemids)
			continue;

		if (ZBX_TRIGGER_TIMER_DEFAULT == trigger->timer)
			continue;

		if (trigger->timer_revision == trigger->revision)
			continue;

		if (NULL == (timer = dc_trigger_timer_create(trigger)))
			continue;

		if (0 == (ts.sec = (int)dc_function_calculate_nextcheck(NULL, timer, now, timer->triggerid)))
		{
			dc_trigger_timer_free(timer);
			trigger->timer_revision = 0;
		}
		else
			dc_schedule_trigger_timer(timer, now, NULL, &ts);
	}
}

void DCsync_functions(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	ZBX_DC_ITEM *item;
	ZBX_DC_FUNCTION *function;

	int found, ret;
	zbx_uint64_t itemid, functionid, triggerid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);
		ZBX_STR2UINT64(functionid, row[0]);
		ZBX_STR2UINT64(triggerid, row[4]);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
			continue;

		/* process function information */

		function = (ZBX_DC_FUNCTION *)DCfind_id(&config->functions, functionid, sizeof(ZBX_DC_FUNCTION), &found);

		if (1 == found)
		{
			if (function->itemid != itemid)
			{
				ZBX_DC_ITEM *item_last;

				if (NULL != (item_last = zbx_hashset_search(&config->items, &function->itemid)))
					dc_item_reset_triggers(item_last, NULL);
			}
		}
		else
			function->timer_revision = 0;

		function->triggerid = triggerid;
		function->itemid = itemid;
		dc_strpool_replace(found, &function->function, row[2]);
		dc_strpool_replace(found, &function->parameter, row[3]);

		function->type = zbx_get_function_type(function->function);
		function->revision = revision;

		dc_item_reset_triggers(item, NULL);
	}

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &rowid)))
			continue;

		if (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
			dc_item_reset_triggers(item, NULL);

		dc_strpool_release(function->function);
		dc_strpool_release(function->parameter);

		zbx_hashset_remove_direct(&config->functions, function);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: removes expression from regexp                                    *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_REGEXP *dc_regexp_remove_expression(const char *regexp_name, zbx_uint64_t expressionid)
{
	ZBX_DC_REGEXP *regexp, regexp_local;
	int index;

	regexp_local.name = regexp_name;

	if (NULL == (regexp = (ZBX_DC_REGEXP *)zbx_hashset_search(&config->regexps, &regexp_local)))
		return NULL;

	if (FAIL == (index = zbx_vector_uint64_search(&regexp->expressionids, expressionid,
												  ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return NULL;
	}

	zbx_vector_uint64_remove_noorder(&regexp->expressionids, index);

	return regexp;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates expressions configuration cache                           *
 *                                                                            *
 * Parameters: result - [IN] the result of expressions database select        *
 *                                                                            *
 ******************************************************************************/
void DCsync_expressions(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_hashset_iter_t iter;
	ZBX_DC_EXPRESSION *expression;
	ZBX_DC_REGEXP *regexp, regexp_local;
	zbx_uint64_t expressionid;
	int found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(expressionid, row[1]);
		expression = (ZBX_DC_EXPRESSION *)DCfind_id(&config->expressions, expressionid,
													sizeof(ZBX_DC_EXPRESSION), &found);

		if (0 != found)
			dc_regexp_remove_expression(expression->regexp, expressionid);

		dc_strpool_replace(found, &expression->regexp, row[0]);
		dc_strpool_replace(found, &expression->expression, row[2]);
		ZBX_STR2UCHAR(expression->type, row[3]);
		ZBX_STR2UCHAR(expression->case_sensitive, row[5]);
		expression->delimiter = *row[4];

		regexp_local.name = row[0];

		if (NULL == (regexp = (ZBX_DC_REGEXP *)zbx_hashset_search(&config->regexps, &regexp_local)))
		{
			dc_strpool_replace(0, &regexp_local.name, row[0]);
			zbx_vector_uint64_create_ext(&regexp_local.expressionids,
										 __config_shmem_malloc_func,
										 __config_shmem_realloc_func,
										 __config_shmem_free_func);

			regexp = (ZBX_DC_REGEXP *)zbx_hashset_insert(&config->regexps, &regexp_local,
														 sizeof(ZBX_DC_REGEXP));
		}

		zbx_vector_uint64_append(&regexp->expressionids, expressionid);

		config->revision.expression = revision;
	}

	/* remove regexps with no expressions related to it */
	zbx_hashset_iter_reset(&config->regexps, &iter);

	while (NULL != (regexp = (ZBX_DC_REGEXP *)zbx_hashset_iter_next(&iter)))
	{
		if (0 < regexp->expressionids.values_num)
			continue;

		dc_strpool_release(regexp->name);
		zbx_vector_uint64_destroy(&regexp->expressionids);
		zbx_hashset_iter_remove(&iter);
	}

	/* remove unused expressions */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (expression = (ZBX_DC_EXPRESSION *)zbx_hashset_search(&config->expressions, &rowid)))
			continue;

		if (NULL != (regexp = dc_regexp_remove_expression(expression->regexp, expression->expressionid)))
		{
			if (0 == regexp->expressionids.values_num)
			{
				dc_strpool_release(regexp->name);
				zbx_vector_uint64_destroy(&regexp->expressionids);
				zbx_hashset_remove_direct(&config->regexps, regexp);
			}
		}

		dc_strpool_release(expression->expression);
		dc_strpool_release(expression->regexp);
		zbx_hashset_remove_direct(&config->expressions, expression);
	}

	if (0 != sync->add_num || 0 != sync->update_num || 0 != sync->remove_num)
		config->revision.expression = revision;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates actions configuration cache                               *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - actionid                                                     *
 *           1 - eventsource                                                  *
 *           2 - evaltype                                                     *
 *           3 - formula                                                      *
 *                                                                            *
 ******************************************************************************/
void DCsync_actions(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t actionid;
	zbx_dc_action_t *action;
	int found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(actionid, row[0]);
		action = (zbx_dc_action_t *)DCfind_id(&config->actions, actionid, sizeof(zbx_dc_action_t), &found);

		ZBX_STR2UCHAR(action->eventsource, row[1]);
		ZBX_STR2UCHAR(action->evaltype, row[2]);

		dc_strpool_replace(found, &action->formula, row[3]);

		if (0 == found)
		{
			if (EVENT_SOURCE_INTERNAL == action->eventsource)
				config->internal_actions++;

			if (EVENT_SOURCE_AUTOREGISTRATION == action->eventsource)
				config->auto_registration_actions++;

			zbx_vector_ptr_create_ext(&action->conditions, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);

			zbx_vector_ptr_reserve(&action->conditions, 1);

			action->opflags = ZBX_ACTION_OPCLASS_NONE;
		}
	}

	/* remove deleted actions */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (action = (zbx_dc_action_t *)zbx_hashset_search(&config->actions, &rowid)))
			continue;

		if (EVENT_SOURCE_INTERNAL == action->eventsource)
			config->internal_actions--;

		if (EVENT_SOURCE_AUTOREGISTRATION == action->eventsource)
			config->auto_registration_actions--;

		dc_strpool_release(action->formula);
		zbx_vector_ptr_destroy(&action->conditions);

		zbx_hashset_remove_direct(&config->actions, action);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates action operation class flags in configuration cache       *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - actionid                                                     *
 *           1 - action operation class flags                                 *
 *                                                                            *
 ******************************************************************************/
void DCsync_action_ops(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t actionid;
	zbx_dc_action_t *action;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(actionid, row[0]);

		if (NULL == (action = (zbx_dc_action_t *)zbx_hashset_search(&config->actions, &actionid)))
			continue;

		action->opflags = atoi(row[1]);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: compare two action conditions by their type                       *
 *                                                                            *
 * Comments: This function is used to sort action conditions by type.         *
 *                                                                            *
 ******************************************************************************/
static int dc_compare_action_conditions_by_type(const void *d1, const void *d2)
{
	zbx_dc_action_condition_t *c1 = *(zbx_dc_action_condition_t **)d1;
	zbx_dc_action_condition_t *c2 = *(zbx_dc_action_condition_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(c1->conditiontype, c2->conditiontype);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates action conditions configuration cache                     *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - conditionid                                                  *
 *           1 - actionid                                                     *
 *           2 - conditiontype                                                *
 *           3 - operator                                                     *
 *           4 - value                                                        *
 *                                                                            *
 ******************************************************************************/
void DCsync_action_conditions(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t actionid, conditionid;
	zbx_dc_action_t *action;
	zbx_dc_action_condition_t *condition;
	int found, i, index, ret;
	zbx_vector_ptr_t actions;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&actions);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(actionid, row[1]);

		if (NULL == (action = (zbx_dc_action_t *)zbx_hashset_search(&config->actions, &actionid)))
			continue;

		ZBX_STR2UINT64(conditionid, row[0]);

		condition = (zbx_dc_action_condition_t *)DCfind_id(&config->action_conditions, conditionid,
														   sizeof(zbx_dc_action_condition_t), &found);

		ZBX_STR2UCHAR(condition->conditiontype, row[2]);
		ZBX_STR2UCHAR(condition->op, row[3]);

		dc_strpool_replace(found, &condition->value, row[4]);
		dc_strpool_replace(found, &condition->value2, row[5]);

		if (0 == found)
		{
			condition->actionid = actionid;
			zbx_vector_ptr_append(&action->conditions, condition);
		}

		if (ZBX_CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			zbx_vector_ptr_append(&actions, action);
	}

	/* remove deleted conditions */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (zbx_dc_action_condition_t *)zbx_hashset_search(&config->action_conditions,
																				 &rowid)))
		{
			continue;
		}

		if (NULL != (action = (zbx_dc_action_t *)zbx_hashset_search(&config->actions, &condition->actionid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&action->conditions, condition,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&action->conditions, index);

				if (ZBX_CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
					zbx_vector_ptr_append(&actions, action);
			}
		}

		dc_strpool_release(condition->value);
		dc_strpool_release(condition->value2);

		zbx_hashset_remove_direct(&config->action_conditions, condition);
	}

	/* sort conditions by type */

	zbx_vector_ptr_sort(&actions, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&actions, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < actions.values_num; i++)
	{
		action = (zbx_dc_action_t *)actions.values[i];

		if (ZBX_CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			zbx_vector_ptr_sort(&action->conditions, dc_compare_action_conditions_by_type);
	}

	zbx_vector_ptr_destroy(&actions);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates correlations configuration cache                          *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - correlationid                                                *
 *           1 - name                                                         *
 *           2 - evaltype                                                     *
 *           3 - formula                                                      *
 *                                                                            *
 ******************************************************************************/
void DCsync_correlations(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t correlationid;
	zbx_dc_correlation_t *correlation;
	int found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[0]);

		correlation = (zbx_dc_correlation_t *)DCfind_id(&config->correlations, correlationid,
														sizeof(zbx_dc_correlation_t), &found);

		if (0 == found)
		{
			zbx_vector_ptr_create_ext(&correlation->conditions, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);

			zbx_vector_ptr_create_ext(&correlation->operations, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);
		}

		dc_strpool_replace(found, &correlation->name, row[1]);
		dc_strpool_replace(found, &correlation->formula, row[3]);

		ZBX_STR2UCHAR(correlation->evaltype, row[2]);
	}

	/* remove deleted correlations */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &rowid)))
			continue;

		dc_strpool_release(correlation->name);
		dc_strpool_release(correlation->formula);

		zbx_vector_ptr_destroy(&correlation->conditions);
		zbx_vector_ptr_destroy(&correlation->operations);

		zbx_hashset_remove_direct(&config->correlations, correlation);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get the actual size of correlation condition data depending on    *
 *          its type                                                          *
 *                                                                            *
 * Parameters: type - [IN] the condition type                                 *
 *                                                                            *
 ******************************************************************************/
static size_t dc_corr_condition_get_size(unsigned char type)
{
	switch (type)
	{
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		return offsetof(zbx_dc_corr_condition_t, data) + sizeof(zbx_dc_corr_condition_tag_t);
	case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
		return offsetof(zbx_dc_corr_condition_t, data) + sizeof(zbx_dc_corr_condition_group_t);
	case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
		return offsetof(zbx_dc_corr_condition_t, data) + sizeof(zbx_dc_corr_condition_tag_pair_t);
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		return offsetof(zbx_dc_corr_condition_t, data) + sizeof(zbx_dc_corr_condition_tag_value_t);
	}

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: initializes correlation condition data from database row          *
 *                                                                            *
 * Parameters: condition - [IN] the condition to initialize                   *
 *             found     - [IN] 0 - new condition, 1 - cached condition       *
 *             row       - [IN] the database row containing condition data    *
 *                                                                            *
 ******************************************************************************/
static void dc_corr_condition_init_data(zbx_dc_corr_condition_t *condition, int found, DB_ROW row)
{
	if (ZBX_CORR_CONDITION_OLD_EVENT_TAG == condition->type || ZBX_CORR_CONDITION_NEW_EVENT_TAG == condition->type)
	{
		dc_strpool_replace(found, &condition->data.tag.tag, row[0]);
		return;
	}

	row++;

	if (ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE == condition->type ||
		ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE == condition->type)
	{
		dc_strpool_replace(found, &condition->data.tag_value.tag, row[0]);
		dc_strpool_replace(found, &condition->data.tag_value.value, row[1]);
		ZBX_STR2UCHAR(condition->data.tag_value.op, row[2]);
		return;
	}

	row += 3;

	if (ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP == condition->type)
	{
		ZBX_STR2UINT64(condition->data.group.groupid, row[0]);
		ZBX_STR2UCHAR(condition->data.group.op, row[1]);
		return;
	}

	row += 2;

	if (ZBX_CORR_CONDITION_EVENT_TAG_PAIR == condition->type)
	{
		dc_strpool_replace(found, &condition->data.tag_pair.oldtag, row[0]);
		dc_strpool_replace(found, &condition->data.tag_pair.newtag, row[1]);
		return;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees correlation condition data                                  *
 *                                                                            *
 * Parameters: condition - [IN] the condition                                 *
 *                                                                            *
 ******************************************************************************/
static void corr_condition_free_data(zbx_dc_corr_condition_t *condition)
{
	switch (condition->type)
	{
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		dc_strpool_release(condition->data.tag.tag);
		break;
	case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
		dc_strpool_release(condition->data.tag_pair.oldtag);
		dc_strpool_release(condition->data.tag_pair.newtag);
		break;
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		dc_strpool_release(condition->data.tag_value.tag);
		dc_strpool_release(condition->data.tag_value.value);
		break;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: compare two correlation conditions by their type                  *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int dc_compare_corr_conditions_by_type(const void *d1, const void *d2)
{
	zbx_dc_corr_condition_t *c1 = *(zbx_dc_corr_condition_t **)d1;
	zbx_dc_corr_condition_t *c2 = *(zbx_dc_corr_condition_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(c1->type, c2->type);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates correlation conditions configuration cache                *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - corr_conditionid                                             *
 *           1 - correlationid                                                *
 *           2 - type                                                         *
 *           3 - corr_condition_tag.tag                                       *
 *           4 - corr_condition_tagvalue.tag                                  *
 *           5 - corr_condition_tagvalue.value                                *
 *           6 - corr_condition_tagvalue.operator                             *
 *           7 - corr_condition_group.groupid                                 *
 *           8 - corr_condition_group.operator                                *
 *           9 - corr_condition_tagpair.oldtag                                *
 *          10 - corr_condition_tagpair.newtag                                *
 *                                                                            *
 ******************************************************************************/
void DCsync_corr_conditions(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t conditionid, correlationid;
	zbx_dc_corr_condition_t *condition;
	zbx_dc_correlation_t *correlation;
	int found, ret, i, index;
	unsigned char type;
	size_t condition_size;
	zbx_vector_ptr_t correlations;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&correlations);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations,
																			  &correlationid)))
		{
			continue;
		}

		ZBX_STR2UINT64(conditionid, row[0]);
		ZBX_STR2UCHAR(type, row[2]);

		condition_size = dc_corr_condition_get_size(type);
		condition = (zbx_dc_corr_condition_t *)DCfind_id(&config->corr_conditions, conditionid, condition_size,
														 &found);

		condition->correlationid = correlationid;
		condition->type = type;
		dc_corr_condition_init_data(condition, found, row + 3);

		if (0 == found)
			zbx_vector_ptr_append(&correlation->conditions, condition);

		/* sort the conditions later */
		if (ZBX_CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
			zbx_vector_ptr_append(&correlations, correlation);
	}

	/* remove deleted correlation conditions */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (zbx_dc_corr_condition_t *)zbx_hashset_search(&config->corr_conditions,
																			   &rowid)))
		{
			continue;
		}

		/* remove condition from correlation->conditions vector */
		if (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations,
																			  &condition->correlationid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&correlation->conditions, condition,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				/* sort the conditions later */
				if (ZBX_CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
					zbx_vector_ptr_append(&correlations, correlation);

				zbx_vector_ptr_remove_noorder(&correlation->conditions, index);
			}
		}

		corr_condition_free_data(condition);
		zbx_hashset_remove_direct(&config->corr_conditions, condition);
	}

	/* sort conditions by type */

	zbx_vector_ptr_sort(&correlations, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&correlations, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < correlations.values_num; i++)
	{
		correlation = (zbx_dc_correlation_t *)correlations.values[i];
		zbx_vector_ptr_sort(&correlation->conditions, dc_compare_corr_conditions_by_type);
	}

	zbx_vector_ptr_destroy(&correlations);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates correlation operations configuration cache                *
 *                                                                            *
 * Parameters: result - [IN] the result of correlation operations database    *
 *                           select                                           *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - corr_operationid                                             *
 *           1 - correlationid                                                *
 *           2 - type                                                         *
 *                                                                            *
 ******************************************************************************/
void DCsync_corr_operations(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t operationid, correlationid;
	zbx_dc_corr_operation_t *operation;
	zbx_dc_correlation_t *correlation;
	int found, ret, index;
	unsigned char type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations,
																			  &correlationid)))
		{
			continue;
		}

		ZBX_STR2UINT64(operationid, row[0]);
		ZBX_STR2UCHAR(type, row[2]);

		operation = (zbx_dc_corr_operation_t *)DCfind_id(&config->corr_operations, operationid,
														 sizeof(zbx_dc_corr_operation_t), &found);

		operation->type = type;

		if (0 == found)
		{
			operation->correlationid = correlationid;
			zbx_vector_ptr_append(&correlation->operations, operation);
		}
	}

	/* remove deleted correlation operations */

	/* remove deleted actions */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (operation = (zbx_dc_corr_operation_t *)zbx_hashset_search(&config->corr_operations,
																			   &rowid)))
		{
			continue;
		}

		/* remove operation from correlation->conditions vector */
		if (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations,
																			  &operation->correlationid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&correlation->operations, operation,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&correlation->operations, index);
			}
		}
		zbx_hashset_remove_direct(&config->corr_operations, operation);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int dc_compare_hgroups(const void *d1, const void *d2)
{
	const zbx_dc_hostgroup_t *g1 = *((const zbx_dc_hostgroup_t **)d1);
	const zbx_dc_hostgroup_t *g2 = *((const zbx_dc_hostgroup_t **)d2);

	return strcmp(g1->name, g2->name);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates host groups configuration cache                           *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - groupid                                                      *
 *           1 - name                                                         *
 *                                                                            *
 ******************************************************************************/
void DCsync_hostgroups(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t groupid;
	zbx_dc_hostgroup_t *group;
	int found, ret, index;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(groupid, row[0]);

		group = (zbx_dc_hostgroup_t *)DCfind_id(&config->hostgroups, groupid, sizeof(zbx_dc_hostgroup_t),
												&found);

		if (0 == found)
		{
			group->flags = ZBX_DC_HOSTGROUP_FLAGS_NONE;
			zbx_vector_ptr_append(&config->hostgroups_name, group);

			zbx_hashset_create_ext(&group->hostids, 0, ZBX_DEFAULT_UINT64_HASH_FUNC,
								   ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL, __config_shmem_malloc_func,
								   __config_shmem_realloc_func, __config_shmem_free_func);
		}

		dc_strpool_replace(found, &group->name, row[1]);
	}

	/* remove deleted host groups */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups, &rowid)))
			continue;

		if (FAIL != (index = zbx_vector_ptr_search(&config->hostgroups_name, group,
												   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
		{
			zbx_vector_ptr_remove_noorder(&config->hostgroups_name, index);
		}

		if (ZBX_DC_HOSTGROUP_FLAGS_NONE != group->flags)
			zbx_vector_uint64_destroy(&group->nested_groupids);

		dc_strpool_release(group->name);
		zbx_hashset_destroy(&group->hostids);
		zbx_hashset_remove_direct(&config->hostgroups, group);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates trigger tags in configuration cache                       *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - triggertagid                                                 *
 *           1 - triggerid                                                    *
 *           2 - tag                                                          *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
void DCsync_trigger_tags(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	int found, ret, index;
	zbx_uint64_t triggerid, triggertagid;
	ZBX_DC_TRIGGER *trigger;
	zbx_dc_trigger_tag_t *trigger_tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(triggerid, row[1]);

		if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerid)))
			continue;

		ZBX_STR2UINT64(triggertagid, row[0]);

		trigger_tag = (zbx_dc_trigger_tag_t *)DCfind_id(&config->trigger_tags, triggertagid,
														sizeof(zbx_dc_trigger_tag_t), &found);
		dc_strpool_replace(found, &trigger_tag->tag, row[2]);
		dc_strpool_replace(found, &trigger_tag->value, row[3]);

		if (0 == found)
		{
			trigger_tag->triggerid = triggerid;
			if (ZBX_FLAG_DISCOVERY_PROTOTYPE != trigger->flags)
				zbx_vector_ptr_append(&trigger->tags, trigger_tag);
		}
	}

	/* remove unused trigger tags */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_search(&config->trigger_tags, &rowid)))
			continue;

		if (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &trigger_tag->triggerid)))
		{
			if (ZBX_FLAG_DISCOVERY_PROTOTYPE != trigger->flags)
			{
				if (FAIL != (index = zbx_vector_ptr_search(&trigger->tags, trigger_tag,
														   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
				{
					zbx_vector_ptr_remove_noorder(&trigger->tags, index);

					/* recreate empty tags vector to release used memory */
					if (0 == trigger->tags.values_num)
					{
						zbx_vector_ptr_destroy(&trigger->tags);
						zbx_vector_ptr_create_ext(&trigger->tags, __config_shmem_malloc_func,
												  __config_shmem_realloc_func, __config_shmem_free_func);
					}
				}
			}
		}

		dc_strpool_release(trigger_tag->tag);
		dc_strpool_release(trigger_tag->value);

		zbx_hashset_remove_direct(&config->trigger_tags, trigger_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates item tags in configuration cache                          *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - itemtagid                                                    *
 *           1 - itemid                                                       *
 *           2 - tag                                                          *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
void DCsync_item_tags(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	int found, ret, index;
	zbx_uint64_t itemid, itemtagid;
	ZBX_DC_ITEM *item;
	zbx_dc_item_tag_t *item_tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
			continue;

		ZBX_STR2UINT64(itemtagid, row[0]);

		item_tag = (zbx_dc_item_tag_t *)DCfind_id(&config->item_tags, itemtagid, sizeof(zbx_dc_item_tag_t),
												  &found);
		dc_strpool_replace(found, &item_tag->tag, row[2]);
		dc_strpool_replace(found, &item_tag->value, row[3]);

		if (0 == found)
		{
			item_tag->itemid = itemid;
			zbx_vector_ptr_append(&item->tags, item_tag);
		}
	}

	/* remove unused item tags */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item_tag = (zbx_dc_item_tag_t *)zbx_hashset_search(&config->item_tags, &rowid)))
			continue;

		if (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &item_tag->itemid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&item->tags, item_tag,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&item->tags, index);

				/* recreate empty tags vector to release used memory */
				if (0 == item->tags.values_num)
				{
					zbx_vector_ptr_destroy(&item->tags);
					zbx_vector_ptr_create_ext(&item->tags, __config_shmem_malloc_func,
											  __config_shmem_realloc_func, __config_shmem_free_func);
				}
			}
		}

		dc_strpool_release(item_tag->tag);
		dc_strpool_release(item_tag->value);

		zbx_hashset_remove_direct(&config->item_tags, item_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates host tags in configuration cache                          *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - hosttagid                                                    *
 *           1 - hostid                                                       *
 *           2 - tag                                                          *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
void DCsync_host_tags(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	zbx_dc_host_tag_t *host_tag;
	zbx_dc_host_tag_index_t *host_tag_index_entry;

	int found, index, ret;
	zbx_uint64_t hosttagid, hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hosttagid, row[0]);
		ZBX_STR2UINT64(hostid, row[1]);

		host_tag = (zbx_dc_host_tag_t *)DCfind_id(&config->host_tags, hosttagid,
												  sizeof(zbx_dc_host_tag_t), &found);

		/* store new information in host_tag structure */
		host_tag->hostid = hostid;
		dc_strpool_replace(found, &host_tag->tag, row[2]);
		dc_strpool_replace(found, &host_tag->value, row[3]);

		/* update host_tags_index*/
		if (tag == ZBX_DBSYNC_ROW_ADD)
		{
			host_tag_index_entry = (zbx_dc_host_tag_index_t *)DCfind_id(&config->host_tags_index, hostid,
																		sizeof(zbx_dc_host_tag_index_t), &found);

			if (0 == found)
			{
				zbx_vector_ptr_create_ext(&host_tag_index_entry->tags, __config_shmem_malloc_func,
										  __config_shmem_realloc_func, __config_shmem_free_func);
			}

			zbx_vector_ptr_append(&host_tag_index_entry->tags, host_tag);
		}
	}

	/* remove deleted host tags from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host_tag = (zbx_dc_host_tag_t *)zbx_hashset_search(&config->host_tags, &rowid)))
			continue;

		/* update host_tags_index*/
		host_tag_index_entry = (zbx_dc_host_tag_index_t *)zbx_hashset_search(&config->host_tags_index,
																			 &host_tag->hostid);

		if (NULL != host_tag_index_entry)
		{
			if (FAIL != (index = zbx_vector_ptr_search(&host_tag_index_entry->tags, host_tag,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove(&host_tag_index_entry->tags, index);
			}

			/* remove index entry if it's empty */
			if (0 == host_tag_index_entry->tags.values_num)
			{
				zbx_vector_ptr_destroy(&host_tag_index_entry->tags);
				zbx_hashset_remove_direct(&config->host_tags_index, host_tag_index_entry);
			}
		}

		/* clear host_tag structure */
		dc_strpool_release(host_tag->tag);
		dc_strpool_release(host_tag->value);

		zbx_hashset_remove_direct(&config->host_tags, host_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: compare two item script parameters                                *
 *                                                                            *
 ******************************************************************************/
static int dc_compare_itemscript_param(const void *d1, const void *d2)
{
	zbx_dc_scriptitem_param_t *p1 = *(zbx_dc_scriptitem_param_t **)d1;
	zbx_dc_scriptitem_param_t *p2 = *(zbx_dc_scriptitem_param_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->name, p2->name);
	ZBX_RETURN_IF_NOT_EQUAL(p1->value, p2->value);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: compare two item preprocessing operations by step                 *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int dc_compare_preprocops_by_step(const void *d1, const void *d2)
{
	zbx_dc_preproc_op_t *p1 = *(zbx_dc_preproc_op_t **)d1;
	zbx_dc_preproc_op_t *p2 = *(zbx_dc_preproc_op_t **)d2;

	if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED == p1->type)
		return -1;

	if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED == p2->type)
		return 1;

	ZBX_RETURN_IF_NOT_EQUAL(p1->step, p2->step);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates item preprocessing steps in configuration cache           *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - item_preprocid                                               *
 *           1 - itemid                                                       *
 *           2 - type                                                         *
 *           3 - params                                                       *
 *                                                                            *
 ******************************************************************************/
void DCsync_item_preproc(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t item_preprocid, itemid;
	int found, ret, i, index;
	ZBX_DC_PREPROCITEM *preprocitem = NULL;
	zbx_dc_preproc_op_t *op;
	ZBX_DC_ITEM *item;
	zbx_vector_dc_item_ptr_t items;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_dc_item_ptr_create(&items);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
			continue;

		if (NULL == (preprocitem = item->preproc_item))
		{
			preprocitem = (ZBX_DC_PREPROCITEM *)__config_shmem_malloc_func(NULL, sizeof(ZBX_DC_PREPROCITEM));

			zbx_vector_ptr_create_ext(&preprocitem->preproc_ops, __config_shmem_malloc_func,
									  __config_shmem_realloc_func, __config_shmem_free_func);

			item->preproc_item = preprocitem;
		}
		zbx_vector_dc_item_ptr_append(&items, item);

		ZBX_STR2UINT64(item_preprocid, row[0]);

		op = (zbx_dc_preproc_op_t *)DCfind_id(&config->preprocops, item_preprocid, sizeof(zbx_dc_preproc_op_t),
											  &found);

		ZBX_STR2UCHAR(op->type, row[2]);
		dc_strpool_replace(found, &op->params, row[3]);
		op->step = atoi(row[4]);
		op->error_handler = atoi(row[5]);
		dc_strpool_replace(found, &op->error_handler_params, row[6]);
		
//		char *tmp = zbx_strdup(NULL, op->params);
//		char *nl_pos = strchr(tmp, '\n');
//		if (NULL !=nl_pos) nl_pos[0]=' ';

		DEBUG_ITEM(itemid, "Loaded from config preprocessing step %d operation type %d params '%s'", op->step, op->type, op->params);
//		zbx_free(tmp);

		if (0 == found)
		{
			op->itemid = itemid;
			zbx_vector_ptr_append(&preprocitem->preproc_ops, op);
		}
	}

	/* remove deleted item preprocessing operations */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (op = (zbx_dc_preproc_op_t *)zbx_hashset_search(&config->preprocops, &rowid)))
			continue;

		if (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &op->itemid)) &&
			NULL != (preprocitem = item->preproc_item))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&preprocitem->preproc_ops, op,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&preprocitem->preproc_ops, index);
				zbx_vector_dc_item_ptr_append(&items, item);
			}
		}

		dc_strpool_release(op->params);
		dc_strpool_release(op->error_handler_params);
		zbx_hashset_remove_direct(&config->preprocops, op);
	}

	/* sort item preprocessing operations by step */

	zbx_vector_dc_item_ptr_sort(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_dc_item_ptr_uniq(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < items.values_num; i++)
	{
		item = items.values[i];

		if (NULL == (preprocitem = item->preproc_item))
			continue;

		dc_item_update_revision(item, revision);

		if (0 == preprocitem->preproc_ops.values_num)
		{
			dc_preprocitem_free(preprocitem);
			item->preproc_item = NULL;
		}
		else
			zbx_vector_ptr_sort(&preprocitem->preproc_ops, dc_compare_preprocops_by_step);
	}

	zbx_vector_dc_item_ptr_destroy(&items);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates item script parameters in configuration cache             *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - item_script_paramid                                          *
 *           1 - itemid                                                       *
 *           2 - name                                                         *
 *           3 - value                                                        *
 *                                                                            *
 ******************************************************************************/
void DCsync_itemscript_param(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t item_script_paramid, itemid;
	int found, ret, i, index;
	ZBX_DC_SCRIPTITEM *scriptitem;
	zbx_dc_scriptitem_param_t *scriptitem_params;
	zbx_vector_ptr_t items;
	ZBX_DC_ITEM *dc_item;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&items);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);

		if (NULL == (scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems, &itemid)))
		{
			zabbix_log(LOG_LEVEL_DEBUG,
					   "cannot find parent item for item parameters (itemid=" ZBX_FS_UI64 ")", itemid);
			continue;
		}

		ZBX_STR2UINT64(item_script_paramid, row[0]);
		scriptitem_params = (zbx_dc_scriptitem_param_t *)DCfind_id(&config->itemscript_params,
																   item_script_paramid, sizeof(zbx_dc_scriptitem_param_t), &found);

		dc_strpool_replace(found, &scriptitem_params->name, row[2]);
		dc_strpool_replace(found, &scriptitem_params->value, row[3]);

		if (0 == found)
		{
			scriptitem_params->itemid = itemid;
			zbx_vector_ptr_append(&scriptitem->params, scriptitem_params);
		}

		zbx_vector_ptr_append(&items, scriptitem);
	}

	/* remove deleted item script parameters */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (scriptitem_params =
						 (zbx_dc_scriptitem_param_t *)zbx_hashset_search(&config->itemscript_params, &rowid)))
		{
			continue;
		}

		if (NULL != (scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems,
																		  &scriptitem_params->itemid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&scriptitem->params, scriptitem_params,
													   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&scriptitem->params, index);
				zbx_vector_ptr_append(&items, scriptitem);
			}
		}

		dc_strpool_release(scriptitem_params->name);
		dc_strpool_release(scriptitem_params->value);
		zbx_hashset_remove_direct(&config->itemscript_params, scriptitem_params);
	}

	/* sort item script parameters */

	zbx_vector_ptr_sort(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < items.values_num; i++)
	{
		scriptitem = (ZBX_DC_SCRIPTITEM *)items.values[i];

		if (NULL != (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &scriptitem->itemid)))
			dc_item_update_revision(dc_item, revision);

		if (0 < scriptitem->params.values_num)
			zbx_vector_ptr_sort(&scriptitem->params, dc_compare_itemscript_param);
	}

	zbx_vector_ptr_destroy(&items);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates group hosts in configuration cache                        *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - groupid                                                      *
 *           1 - hostid                                                       *
 *                                                                            *
 ******************************************************************************/
void DCsync_hostgroup_hosts(zbx_dbsync_t *sync)
{
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;

	zbx_dc_hostgroup_t *group = NULL;

	int ret;
	zbx_uint64_t last_groupid = 0, groupid, hostid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(groupid, row[0]);

		if (last_groupid != groupid)
		{
			group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups, &groupid);
			last_groupid = groupid;
		}

		if (NULL == group)
			continue;

		ZBX_STR2UINT64(hostid, row[1]);
		zbx_hashset_insert(&group->hostids, &hostid, sizeof(hostid));
	}

	/* remove deleted group hostids from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(groupid, row[0]);

		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups, &groupid)))
			continue;

		ZBX_STR2UINT64(hostid, row[1]);
		zbx_hashset_remove(&group->hostids, &hostid);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: calculate nextcheck timestamp                                     *
 *                                                                            *
 * Parameters: seend - [IN] the seed                                          *
 *             delay - [IN] the delay in seconds                              *
 *             now   - [IN] current timestamp                                 *
 *                                                                            *
 * Return value: nextcheck value                                              *
 *                                                                            *
 ******************************************************************************/
static time_t dc_calculate_nextcheck(zbx_uint64_t seed, int delay, time_t now)
{
	time_t nextcheck;

	if (0 == delay)
		return ZBX_JAN_2038;

	nextcheck = delay * (now / delay) + (unsigned int)(seed % (unsigned int)delay);

	while (nextcheck <= now)
		nextcheck += delay;

	return nextcheck;
}

static void dc_drule_queue(zbx_dc_drule_t *drule)
{
	zbx_binary_heap_elem_t elem;

	elem.key = drule->druleid;
	elem.data = (const void *)drule;

	if (ZBX_LOC_QUEUE != drule->location)
	{
		zbx_binary_heap_insert(&config->drule_queue, &elem);
		drule->location = ZBX_LOC_QUEUE;
	}
	else
		zbx_binary_heap_update_direct(&config->drule_queue, &elem);
}

static void dc_drule_dequeue(zbx_dc_drule_t *drule)
{
	if (ZBX_LOC_QUEUE == drule->location)
	{
		zbx_binary_heap_remove_direct(&config->drule_queue, drule->druleid);
		drule->location = ZBX_LOC_NOWHERE;
	}
}

static void dc_sync_drules(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row, *delay_str;
	zbx_uint64_t rowid, druleid, proxy_hostid;
	unsigned char tag;
	int found, ret, delay = 0;
	ZBX_DC_PROXY *proxy;
	zbx_dc_drule_t *drule;
	time_t now;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(druleid, row[0]);
		ZBX_DBROW2UINT64(proxy_hostid, row[1]);

		drule = (zbx_dc_drule_t *)DCfind_id(&config->drules, druleid, sizeof(zbx_dc_drule_t), &found);

		ZBX_STR2UCHAR(drule->status, row[3]);

		if (0 == found)
		{
			drule->location = ZBX_LOC_NOWHERE;
			drule->nextcheck = 0;
		}
		else
		{
			if (0 != drule->proxy_hostid && proxy_hostid != drule->proxy_hostid &&
				NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies,
																	&drule->proxy_hostid)))
			{
				proxy->revision = revision;
			}
		}

		drule->proxy_hostid = proxy_hostid;
		if (0 != drule->proxy_hostid)
		{
			if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &drule->proxy_hostid)))
				proxy->revision = revision;
		}

		delay_str = dc_expand_user_macros_dyn(row[2], NULL, 0, ZBX_MACRO_ENV_NONSECURE);
		if (SUCCEED != zbx_is_time_suffix(delay_str, &delay, ZBX_LENGTH_UNLIMITED))
			delay = ZBX_DEFAULT_INTERVAL;
		zbx_free(delay_str);

		if (DRULE_STATUS_MONITORED == drule->status && 0 == drule->proxy_hostid)
		{
			int delay_new = 0;

			if (0 == found && 0 < config->revision.config)
				delay_new = delay > SEC_PER_MIN ? SEC_PER_MIN : delay;
			else if (ZBX_LOC_NOWHERE == drule->location || delay != drule->delay)
				delay_new = delay;

			if (0 != delay_new)
			{
				drule->nextcheck = dc_calculate_nextcheck(drule->druleid, delay_new, now);
				dc_drule_queue(drule);
			}
		}
		else
			dc_drule_dequeue(drule);

		drule->delay = delay;
		drule->revision = revision;
	}

	/* remove deleted discovery rules from cache and update proxy revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (drule = (zbx_dc_drule_t *)zbx_hashset_search(&config->drules, &rowid)))
			continue;

		if (0 != drule->proxy_hostid)
		{
			if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &drule->proxy_hostid)))
				proxy->revision = revision;
		}

		dc_drule_dequeue(drule);
		zbx_hashset_remove_direct(&config->drules, drule);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void dc_sync_dchecks(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid, druleid, dcheckid;
	unsigned char tag;
	int found, ret;
	ZBX_DC_PROXY *proxy;
	zbx_dc_drule_t *drule;
	zbx_dc_dcheck_t *dcheck;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(dcheckid, row[0]);
		ZBX_STR2UINT64(druleid, row[1]);

		if (NULL == (drule = (zbx_dc_drule_t *)zbx_hashset_search(&config->drules, &druleid)))
			continue;

		dcheck = (zbx_dc_dcheck_t *)DCfind_id(&config->dchecks, dcheckid, sizeof(zbx_dc_dcheck_t), &found);
		dcheck->druleid = druleid;

		if (drule->revision == revision)
			continue;

		drule->revision = revision;

		if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &drule->proxy_hostid)))
			proxy->revision = revision;
	}

	/* remove deleted discovery checks from cache and update proxy revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (dcheck = (zbx_dc_dcheck_t *)zbx_hashset_search(&config->dchecks, &rowid)))
			continue;

		if (NULL != (drule = (zbx_dc_drule_t *)zbx_hashset_search(&config->drules, &dcheck->druleid)) &&
			0 != drule->proxy_hostid && drule->revision != revision)
		{
			if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &drule->proxy_hostid)))
				proxy->revision = revision;

			drule->revision = revision;
		}

		zbx_hashset_remove_direct(&config->dchecks, dcheck);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: update host and its proxy revision                                *
 *                                                                            *
 ******************************************************************************/
static int dc_host_update_revision(ZBX_DC_HOST *host, zbx_uint64_t revision)
{
	ZBX_DC_PROXY *proxy;

	if (host->revision == revision)
		return SUCCEED;

	host->revision = revision;

	if (0 == host->proxy_hostid)
		return SUCCEED;

	if (NULL == (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &host->proxy_hostid)))
		return FAIL;

	proxy->revision = revision;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: update item, host and its proxy revision                          *
 *                                                                            *
 ******************************************************************************/
static int dc_item_update_revision(ZBX_DC_ITEM *item, zbx_uint64_t revision)
{
	ZBX_DC_HOST *host;

	if (item->revision == revision)
		return SUCCEED;

	item->revision = revision;

	if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid)))
		return FAIL;

	dc_host_update_revision(host, revision);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: update httptest and its parent object revision                    *
 *                                                                            *
 ******************************************************************************/
static int dc_httptest_update_revision(zbx_dc_httptest_t *httptest, zbx_uint64_t revision)
{
	ZBX_DC_HOST *host;

	if (httptest->revision == revision)
		return SUCCEED;

	httptest->revision = revision;

	if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &httptest->hostid)))
		return FAIL;

	dc_host_update_revision(host, revision);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: update httptest step and its parent object revision               *
 *                                                                            *
 ******************************************************************************/
static int dc_httpstep_update_revision(zbx_dc_httpstep_t *httpstep, zbx_uint64_t revision)
{
	zbx_dc_httptest_t *httptest;

	if (httpstep->revision == revision)
		return SUCCEED;

	httpstep->revision = revision;

	if (NULL == (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests, &httpstep->httptestid)))
		return FAIL;

	return dc_httptest_update_revision(httptest, revision);
}

static void dc_httptest_queue(zbx_dc_httptest_t *httptest)
{
	zbx_binary_heap_elem_t elem;

	elem.key = httptest->httptestid;
	elem.data = (const void *)httptest;

	if (ZBX_LOC_QUEUE != httptest->location)
	{
		zbx_binary_heap_insert(&config->httptest_queue, &elem);
		httptest->location = ZBX_LOC_QUEUE;
	}
	else
		zbx_binary_heap_update_direct(&config->httptest_queue, &elem);
}

static void dc_httptest_dequeue(zbx_dc_httptest_t *httptest)
{
	if (ZBX_LOC_QUEUE == httptest->location)
	{
		zbx_binary_heap_remove_direct(&config->httptest_queue, httptest->httptestid);
		httptest->location = ZBX_LOC_NOWHERE;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: update httpstep and its parent object revision                    *
 *                                                                            *
 ******************************************************************************/
static void dc_sync_httptests(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row, *delay_str;
	zbx_uint64_t rowid, httptestid, hostid;
	unsigned char tag;
	int found, ret, delay;
	ZBX_DC_HOST *host;
	zbx_dc_httptest_t *httptest;
	time_t now;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostid, row[1]);

		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
			continue;

		dc_host_update_revision(host, revision);

		ZBX_STR2UINT64(httptestid, row[0]);

		httptest = (zbx_dc_httptest_t *)DCfind_id(&config->httptests, httptestid, sizeof(zbx_dc_httptest_t),
												  &found);

		ZBX_STR2UCHAR(httptest->status, row[3]);

		if (0 == found)
		{
			httptest->location = ZBX_LOC_NOWHERE;
			httptest->nextcheck = 0;
			zbx_vector_dc_httptest_ptr_append(&host->httptests, httptest);
		}

		delay_str = dc_expand_user_macros_dyn(row[2], &hostid, 1, ZBX_MACRO_ENV_NONSECURE);
		if (SUCCEED != zbx_is_time_suffix(delay_str, &delay, ZBX_LENGTH_UNLIMITED))
			delay = ZBX_DEFAULT_INTERVAL;
		zbx_free(delay_str);

		if (HTTPTEST_STATUS_MONITORED == httptest->status && HOST_STATUS_MONITORED == host->status &&
			0 == host->proxy_hostid)
		{
			int delay_new = 0;

			if (0 == found && 0 < config->revision.config)
				delay_new = delay > SEC_PER_MIN ? SEC_PER_MIN : delay;
			else if (ZBX_LOC_NOWHERE == httptest->location || delay != httptest->delay)
				delay_new = delay;

			if (0 != delay_new)
			{
				httptest->nextcheck = dc_calculate_nextcheck(httptest->httptestid, delay_new, now);
				dc_httptest_queue(httptest);
			}
		}
		else
			dc_httptest_dequeue(httptest);

		httptest->hostid = hostid;
		httptest->delay = delay;
		httptest->revision = revision;
	}

	/* remove deleted httptest rules from cache and update host revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		int index;

		if (NULL == (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests, &rowid)))
			continue;

		if (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &httptest->hostid)))
		{
			dc_host_update_revision(host, revision);

			if (FAIL != (index = zbx_vector_dc_httptest_ptr_search(&host->httptests, httptest,
																   ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_dc_httptest_ptr_remove(&host->httptests, index);
			}
		}

		dc_httptest_dequeue(httptest);
		zbx_hashset_remove_direct(&config->httptests, httptest);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void dc_sync_httptest_fields(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid, httptestid, httptest_fieldid;
	unsigned char tag;
	int found, ret;
	zbx_dc_httptest_t *httptest;
	zbx_dc_httptest_field_t *httptest_field;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(httptestid, row[1]);

		if (NULL == (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests, &httptestid)))
			continue;

		dc_httptest_update_revision(httptest, revision);

		ZBX_STR2UINT64(httptest_fieldid, row[0]);

		httptest_field = (zbx_dc_httptest_field_t *)DCfind_id(&config->httptest_fields, httptest_fieldid,
															  sizeof(zbx_dc_httptest_field_t), &found);

		httptest_field->httptestid = httptestid;
	}

	/* remove deleted httptest fields from cache and update host revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (httptest_field = (zbx_dc_httptest_field_t *)zbx_hashset_search(&config->httptest_fields,
																					&rowid)))
		{
			continue;
		}

		if (NULL != (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests,
																		&httptest_field->httptestid)))
		{
			dc_httptest_update_revision(httptest, revision);
		}

		zbx_hashset_remove_direct(&config->httptest_fields, httptest_field);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void dc_sync_httpsteps(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid, httptestid, httpstepid;
	unsigned char tag;
	int found, ret;
	zbx_dc_httptest_t *httptest;
	zbx_dc_httpstep_t *httpstep;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(httptestid, row[1]);

		if (NULL == (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests, &httptestid)))
			continue;

		dc_httptest_update_revision(httptest, revision);

		httptest->revision = revision;

		ZBX_STR2UINT64(httpstepid, row[0]);

		httpstep = (zbx_dc_httpstep_t *)DCfind_id(&config->httpsteps, httpstepid,
												  sizeof(zbx_dc_httpstep_t), &found);

		httpstep->httptestid = httptestid;
		httpstep->revision = revision;
	}

	/* remove deleted httptest fields from cache and update host revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (httpstep = (zbx_dc_httpstep_t *)zbx_hashset_search(&config->httpsteps,
																		&rowid)))
		{
			continue;
		}

		if (NULL != (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests,
																		&httpstep->httptestid)))
		{
			dc_httptest_update_revision(httptest, revision);
		}

		zbx_hashset_remove_direct(&config->httpsteps, httpstep);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void dc_sync_httpstep_fields(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char **row;
	zbx_uint64_t rowid, httpstep_fieldid, httpstepid;
	unsigned char tag;
	int found, ret;
	zbx_dc_httpstep_t *httpstep;
	zbx_dc_httpstep_field_t *httpstep_field;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(httpstepid, row[1]);

		if (NULL == (httpstep = (zbx_dc_httpstep_t *)zbx_hashset_search(&config->httpsteps, &httpstepid)))
			continue;

		dc_httpstep_update_revision(httpstep, revision);

		ZBX_STR2UINT64(httpstep_fieldid, row[0]);

		httpstep_field = (zbx_dc_httpstep_field_t *)DCfind_id(&config->httpstep_fields, httpstep_fieldid,
															  sizeof(zbx_dc_httpstep_field_t), &found);

		httpstep_field->httpstepid = httpstep_fieldid;
	}

	/* remove deleted httpstep fields from cache and update host revision */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (httpstep_field = (zbx_dc_httpstep_field_t *)zbx_hashset_search(&config->httpstep_fields,
																					&rowid)))
		{
			continue;
		}

		if (NULL != (httpstep = (zbx_dc_httpstep_t *)zbx_hashset_search(&config->httpsteps,
																		&httpstep_field->httpstepid)))
		{
			dc_httpstep_update_revision(httpstep, revision);
		}

		zbx_hashset_remove_direct(&config->httpstep_fields, httpstep_field);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int zbx_default_ptr_pair_ptr_compare_func(const void *d1, const void *d2)
{
	const zbx_ptr_pair_t *p1 = (const zbx_ptr_pair_t *)d1;
	const zbx_ptr_pair_t *p2 = (const zbx_ptr_pair_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->first, p2->first);
	ZBX_RETURN_IF_NOT_EQUAL(p1->second, p2->second);

	return 0;
}

static int zbx_default_ptr_pair_ptr_second_compare_func(const void *d1, const void *d2)
{
	const zbx_ptr_pair_t *p1 = (const zbx_ptr_pair_t *)d1;
	const zbx_ptr_pair_t *p2 = (const zbx_ptr_pair_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->second, p2->second);
	ZBX_RETURN_IF_NOT_EQUAL(p1->first, p2->first);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: add new itemids into trigger itemids array                        *
 *                                                                            *
 * Comments: If trigger is already linked to an item and a new function       *
 *           linking the trigger to that item is being added, then the item   *
 *           triggers will be reset causing itemid to be removed from trigger.*
 *           Because of that itemids always can be simply appended to the     *
 *           existing list without checking for duplicates.                   *
 *                                                                            *
 ******************************************************************************/
static void dc_trigger_add_itemids(ZBX_DC_TRIGGER *trigger, const zbx_vector_uint64_t *itemids)
{
	zbx_uint64_t *itemid;
	int i;

	if (NULL != trigger->itemids)
	{
		int itemids_num = 0;

		for (itemid = trigger->itemids; 0 != *itemid; itemid++)
			itemids_num++;

		trigger->itemids = (zbx_uint64_t *)__config_shmem_realloc_func(trigger->itemids,
																	   sizeof(zbx_uint64_t) * (size_t)(itemids->values_num + itemids_num + 1));
	}
	else
	{
		trigger->itemids = (zbx_uint64_t *)__config_shmem_malloc_func(trigger->itemids,
																	  sizeof(zbx_uint64_t) * (size_t)(itemids->values_num + 1));
		trigger->itemids[0] = 0;
	}

	for (itemid = trigger->itemids; 0 != *itemid; itemid++)
		;

	for (i = 0; i < itemids->values_num; i++)
		*itemid++ = itemids->values[i];

	*itemid = 0;
}

/******************************************************************************
 *                                                                            *
 * Purpose: reset item trigger links and remove corresponding itemids from    *
 *          affected triggers                                                 *
 *                                                                            *
 * Parameters: item            - the item to reset                            *
 *             trigger_exclude - the trigger to exclude                       *
 *                                                                            *
 ******************************************************************************/
static void dc_item_reset_triggers(ZBX_DC_ITEM *item, ZBX_DC_TRIGGER *trigger_exclude)
{
	ZBX_DC_TRIGGER **trigger;

	item->update_triggers = 1;

	if (NULL == item->triggers)
		return;

	for (trigger = item->triggers; NULL != *trigger; trigger++)
	{
		zbx_uint64_t *itemid;

		if (*trigger == trigger_exclude)
			continue;

		if (NULL != (*trigger)->itemids)
		{
			for (itemid = (*trigger)->itemids; 0 != *itemid; itemid++)
			{
				if (item->itemid == *itemid)
				{
					while (0 != (*itemid = itemid[1]))
						itemid++;

					break;
				}
			}
		}
	}

	config->items.mem_free_func(item->triggers);
	item->triggers = NULL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: updates trigger related cache data;                               *
 *              1) time triggers assigned to timer processes                  *
 *              2) trigger functionality (if it uses contain disabled         *
 *                 items/hosts)                                               *
 *              3) list of triggers each item is used by                      *
 *                                                                            *
 *
 * //note - all this thing does - 2 and 3, no 1 code at all
 * // and this might be easily replaced by item<->trigger index
 * // this index must be done for items objects
 *
 * //and it's much mor logical to keep the proper state all the time
 * //instead of periocally updating it
 * //TODO: replace with the relation object and keep it incrementiallty updated
 *
 ******************************************************************************/
static void dc_trigger_update_cache(void)
{
	zbx_hashset_iter_t iter;
	ZBX_DC_TRIGGER *trigger;
	ZBX_DC_FUNCTION *function;
	ZBX_DC_ITEM *item;
	int i, j, k;
	zbx_ptr_pair_t itemtrig;
	zbx_vector_ptr_pair_t itemtrigs;
	ZBX_DC_HOST *host;

	zbx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
		trigger->functional = TRIGGER_FUNCTIONAL_TRUE;

	zbx_vector_ptr_pair_create(&itemtrigs);
	zbx_hashset_iter_reset(&config->functions, &iter);
	while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)) ||
			NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers,
																	&function->triggerid)))
		{
			continue;
		}

		if (ZBX_FLAG_DISCOVERY_PROTOTYPE == trigger->flags)
		{
			trigger->functional = TRIGGER_FUNCTIONAL_FALSE;
			continue;
		}

		/* cache item - trigger link */
		if (0 != item->update_triggers)
		{
			itemtrig.first = item;
			itemtrig.second = trigger;
			zbx_vector_ptr_pair_append(&itemtrigs, itemtrig);
		}

		/* disable functionality for triggers with expression containing */
		/* disabled or not monitored items                               */

		if (TRIGGER_FUNCTIONAL_FALSE == trigger->functional)
			continue;

		if (ITEM_STATUS_DISABLED == item->status ||
			(NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid)) ||
			 HOST_STATUS_NOT_MONITORED == host->status))
		{
			trigger->functional = TRIGGER_FUNCTIONAL_FALSE;
		}
	}

	if (0 != itemtrigs.values_num)
	{
		zbx_vector_uint64_t itemids;

		zbx_vector_ptr_pair_sort(&itemtrigs, zbx_default_ptr_pair_ptr_compare_func);
		zbx_vector_ptr_pair_uniq(&itemtrigs, zbx_default_ptr_pair_ptr_compare_func);

		/* update links from items to triggers */
		for (i = 0; i < itemtrigs.values_num; i++)
		{
			for (j = i + 1; j < itemtrigs.values_num; j++)
			{
				if (itemtrigs.values[i].first != itemtrigs.values[j].first)
					break;
			}

			item = (ZBX_DC_ITEM *)itemtrigs.values[i].first;
			item->update_triggers = 0;
			item->triggers = (ZBX_DC_TRIGGER **)config->items.mem_realloc_func(item->triggers,
																			   (size_t)(j - i + 1) * sizeof(ZBX_DC_TRIGGER *));

			for (k = i; k < j; k++)
				item->triggers[k - i] = (ZBX_DC_TRIGGER *)itemtrigs.values[k].second;

			item->triggers[j - i] = NULL;

			i = j - 1;
		}

		/* update reverse links from trigger to items */

		zbx_vector_uint64_create(&itemids);
		zbx_vector_ptr_pair_sort(&itemtrigs, zbx_default_ptr_pair_ptr_second_compare_func);

		trigger = (ZBX_DC_TRIGGER *)itemtrigs.values[0].second;
		for (i = 0; i < itemtrigs.values_num; i++)
		{
			if (trigger != itemtrigs.values[i].second)
			{
				dc_trigger_add_itemids(trigger, &itemids);
				trigger = (ZBX_DC_TRIGGER *)itemtrigs.values[i].second;
				zbx_vector_uint64_clear(&itemids);
			}

			item = (ZBX_DC_ITEM *)itemtrigs.values[i].first;
			zbx_vector_uint64_append(&itemids, item->itemid);
		}

		if (0 != itemids.values_num)
			dc_trigger_add_itemids(trigger, &itemids);

		zbx_vector_uint64_destroy(&itemids);
	}

	zbx_vector_ptr_pair_destroy(&itemtrigs);
}

/******************************************************************************
 *                                                                            *
 * Purpose: updates hostgroup name index and resets nested group lists        *
 *                                                                            *
 ******************************************************************************/
static void dc_hostgroups_update_cache(void)
{
	zbx_hashset_iter_t iter;
	zbx_dc_hostgroup_t *group;

	zbx_vector_ptr_sort(&config->hostgroups_name, dc_compare_hgroups);

	zbx_hashset_iter_reset(&config->hostgroups, &iter);
	while (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_DC_HOSTGROUP_FLAGS_NONE != group->flags)
		{
			group->flags = ZBX_DC_HOSTGROUP_FLAGS_NONE;
			zbx_vector_uint64_destroy(&group->nested_groupids);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: load trigger queue from database                                  *
 *                                                                            *
 * Comments: This function is called when syncing configuration cache for the *
 *           first time after server start. After loading trigger queue it    *
 *           will clear the corresponding data in database.                   *
 *                                                                            *
 ******************************************************************************/
static void dc_load_trigger_queue(zbx_hashset_t *trend_functions)
{
	DB_RESULT result;
	DB_ROW row;

	result = zbx_db_select("select objectid,type,clock,ns from trigger_queue");

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_trigger_timer_t timer_local, *timer;

		if (ZBX_TRIGGER_TIMER_FUNCTION_TREND != atoi(row[1]))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		ZBX_STR2UINT64(timer_local.objectid, row[0]);

		timer_local.eval_ts.sec = atoi(row[2]);
		timer_local.eval_ts.ns = atoi(row[3]);
		timer = zbx_hashset_insert(trend_functions, &timer_local, sizeof(timer_local));

		/* in the case function was scheduled multiple times use the latest data */
		if (0 > zbx_timespec_compare(&timer->eval_ts, &timer_local.eval_ts))
			timer->eval_ts = timer_local.eval_ts;
	}
	zbx_db_free_result(result);
}

static void zbx_dbsync_process_active_avail_diff(zbx_vector_uint64_t *diff)
{
	int i;

	for (i = 0; i < diff->values_num; i++) 
		glb_state_host_reset(diff->values[i]);


}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates connectors in configuration cache                         *
 *                                                                            *
 * Parameters: sync     - [IN] the db synchronization data                    *
 *             revision - [IN] updated configuration revision                 *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_connectors(zbx_dbsync_t *sync, zbx_uint64_t revision)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		connectorid;
	zbx_dc_connector_t	*connector;
	int			found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(connectorid, row[0]);

		connector = (zbx_dc_connector_t *)DCfind_id(&config->connectors, connectorid,
				sizeof(zbx_dc_connector_t), &found);

		if (0 == found)
		{
			zbx_vector_dc_connector_tag_create_ext(&connector->tags, config->connectors.mem_malloc_func,
					config->connectors.mem_realloc_func, config->connectors.mem_free_func);
		}

		ZBX_STR2UCHAR(connector->protocol, row[1]);
		ZBX_STR2UCHAR(connector->data_type, row[2]);
		dc_strpool_replace(found, &connector->url, row[3]);
		connector->max_records = atoi(row[4]);
		connector->max_senders = atoi(row[5]);
		dc_strpool_replace(found, &connector->timeout, row[6]);
		ZBX_STR2UCHAR(connector->max_attempts, row[7]);
		dc_strpool_replace(found, &connector->token, row[8]);
		dc_strpool_replace(found, &connector->http_proxy, row[9]);
		ZBX_STR2UCHAR(connector->authtype, row[10]);
		dc_strpool_replace(found, &connector->username, row[11]);
		dc_strpool_replace(found, &connector->password, row[12]);
		ZBX_STR2UCHAR(connector->verify_peer, row[13]);
		ZBX_STR2UCHAR(connector->verify_host, row[14]);
		dc_strpool_replace(found, &connector->ssl_cert_file, row[15]);
		dc_strpool_replace(found, &connector->ssl_key_file, row[16]);
		dc_strpool_replace(found, &connector->ssl_key_password, row[17]);
		ZBX_STR2UCHAR(connector->status, row[18]);
		ZBX_STR2UCHAR(connector->tags_evaltype, row[19]);
	}

	/* remove deleted connectors */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (connector = (zbx_dc_connector_t *)zbx_hashset_search(&config->connectors, &rowid)))
			continue;

		zbx_vector_dc_connector_tag_destroy(&connector->tags);
		dc_strpool_release(connector->url);
		dc_strpool_release(connector->timeout);
		dc_strpool_release(connector->token);
		dc_strpool_release(connector->http_proxy);
		dc_strpool_release(connector->username);
		dc_strpool_release(connector->password);
		dc_strpool_release(connector->ssl_cert_file);
		dc_strpool_release(connector->ssl_key_file);
		dc_strpool_release(connector->ssl_key_password);

		zbx_hashset_remove_direct(&config->connectors, connector);
	}

	if (0 != sync->add_num || 0 != sync->update_num || 0 != sync->remove_num)
		config->revision.connector = revision;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


/******************************************************************************
 *                                                                            *
 * Purpose: compare connector tags by tag name for sorting                    *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_connector_tags(const void *d1, const void *d2)
{
	const zbx_dc_connector_tag_t	*tag1 = *(const zbx_dc_connector_tag_t * const *)d1;
	const zbx_dc_connector_tag_t	*tag2 = *(const zbx_dc_connector_tag_t * const *)d2;

	return strcmp(tag1->tag, tag2->tag);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Updates connector tags in configuration cache                     *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_connector_tags(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		connectortagid, connectorid;
	zbx_dc_connector_tag_t	*connector_tag;
	zbx_dc_connector_t	*connector;
	zbx_vector_ptr_t	connectors;
	int			found, ret, index, i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&connectors);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(connectorid, row[1]);
		if (NULL == (connector = (zbx_dc_connector_t *)zbx_hashset_search(&config->connectors,
				&connectorid)))
		{
			continue;
		}

		ZBX_STR2UINT64(connectortagid, row[0]);
		connector_tag = (zbx_dc_connector_tag_t *)DCfind_id(&config->connector_tags, connectortagid,
				sizeof(zbx_dc_connector_tag_t), &found);

		connector_tag->connectorid = connectorid;
		ZBX_STR2UCHAR(connector_tag->op, row[2]);
		dc_strpool_replace(found, &connector_tag->tag, row[3]);
		dc_strpool_replace(found, &connector_tag->value, row[4]);

		if (0 == found)
			zbx_vector_dc_connector_tag_append(&connector->tags, connector_tag);

		zbx_vector_ptr_append(&connectors, connector);
	}

	/* remove deleted connector tags */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (connector_tag = (zbx_dc_connector_tag_t *)zbx_hashset_search(&config->connector_tags,
				&rowid)))
		{
			continue;
		}

		if (NULL != (connector = (zbx_dc_connector_t *)zbx_hashset_search(&config->connectors,
				&connector_tag->connectorid)))
		{
			index = zbx_vector_dc_connector_tag_search(&connector->tags, connector_tag,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			if (FAIL != index)
				zbx_vector_dc_connector_tag_remove_noorder(&connector->tags, index);

			zbx_vector_ptr_append(&connectors, connector);
		}

		dc_strpool_release(connector_tag->tag);
		dc_strpool_release(connector_tag->value);

		zbx_hashset_remove_direct(&config->connector_tags, connector_tag);
	}

	/* sort connector tags */

	zbx_vector_ptr_sort(&connectors, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&connectors, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < connectors.values_num; i++)
	{
		connector = (zbx_dc_connector_t *)connectors.values[i];
		zbx_vector_dc_connector_tag_sort(&connector->tags, dc_compare_connector_tags);
	}

	zbx_vector_ptr_destroy(&connectors);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Synchronize configuration data from database                      *
 *                                                                            *
 ******************************************************************************/
void DCsync_configuration(unsigned char mode, zbx_synced_new_config_t synced, zbx_vector_uint64_t *deleted_itemids,
						  const zbx_config_vault_t *config_vault)
{
	static int sync_status = ZBX_DBSYNC_STATUS_UNKNOWN;

	int i, flags, changelog_num, dberr = ZBX_DB_FAIL;
	double sec, csec, hsec, hisec, htsec, gmsec, hmsec, ifsec, idsec, isec, tisec, pisec, tsec, dsec, fsec,
		expr_sec, csec2, hsec2, hisec2, ifsec2, idsec2, isec2, tisec2, pisec2, tsec2, dsec2, fsec2,
		expr_sec2, action_sec, action_sec2, action_op_sec, action_op_sec2, action_condition_sec,
		action_condition_sec2, trigger_tag_sec, trigger_tag_sec2, host_tag_sec, host_tag_sec2,
		correlation_sec, correlation_sec2, corr_condition_sec, corr_condition_sec2, corr_operation_sec,
		corr_operation_sec2, hgroups_sec, hgroups_sec2, itempp_sec, itempp_sec2, itemscrp_sec, 
		itemscrp_sec2, total, total2, update_sec, maintenance_sec, maintenance_sec2, item_tag_sec,
		item_tag_sec2, um_cache_sec, queues_sec, changelog_sec, drules_sec, drules_sec2, httptest_sec,
		httptest_sec2, connector_sec, connector_sec2;

	zbx_dbsync_t config_sync, hosts_sync, hi_sync, htmpl_sync, gmacro_sync, hmacro_sync, if_sync, items_sync,
		template_items_sync, prototype_items_sync, item_discovery_sync, triggers_sync, tdep_sync,
		func_sync, expr_sync, action_sync, action_op_sync, action_condition_sync, trigger_tag_sync,
		item_tag_sync, host_tag_sync, correlation_sync, corr_condition_sync, corr_operation_sync,
		hgroups_sync, itempp_sync, itemscrp_sync, maintenance_sync, maintenance_period_sync,
		maintenance_tag_sync, maintenance_group_sync, maintenance_host_sync, hgroup_host_sync,
		drules_sync, dchecks_sync, httptest_sync, httptest_field_sync, httpstep_sync,
		httpstep_field_sync, autoreg_host_sync, connector_sync, connector_tag_sync;

	double autoreg_csec, autoreg_csec2, autoreg_host_csec, autoreg_host_csec2;
	zbx_dbsync_t autoreg_config_sync;
	zbx_uint64_t update_flags = 0;
	unsigned char changelog_sync_mode = mode; /* sync mode for objects using incremental sync */

	zbx_hashset_t trend_queue;
	zbx_vector_uint64_t active_avail_diff;
	zbx_hashset_t activated_hosts;
	zbx_uint64_t new_revision = config->revision.config + 1;
	int			connectors_num = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_hashset_create(&activated_hosts, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	sec = zbx_time();
	changelog_num = zbx_dbsync_env_prepare(mode);
	changelog_sec = zbx_time() - sec;

	if (ZBX_DBSYNC_INIT == mode)
	{
		zbx_hashset_create(&trend_queue, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		dc_load_trigger_queue(&trend_queue);
	}
	else if (ZBX_DBSYNC_STATUS_INITIALIZED != sync_status)
		changelog_sync_mode = ZBX_DBSYNC_INIT;

	/* global configuration must be synchronized directly with database */
	zbx_dbsync_init(&config_sync, ZBX_DBSYNC_INIT);

	zbx_dbsync_init(&autoreg_config_sync, mode);
	zbx_dbsync_init(&autoreg_host_sync, mode);
	zbx_dbsync_init(&hosts_sync, changelog_sync_mode);
	zbx_dbsync_init(&hi_sync, mode);
	zbx_dbsync_init(&htmpl_sync, mode);
	zbx_dbsync_init(&gmacro_sync, mode);
	zbx_dbsync_init(&hmacro_sync, mode);
	zbx_dbsync_init(&if_sync, mode);
	zbx_dbsync_init(&items_sync, changelog_sync_mode);
	zbx_dbsync_init(&template_items_sync, mode);
	zbx_dbsync_init(&prototype_items_sync, mode);
	zbx_dbsync_init(&item_discovery_sync, mode);
	zbx_dbsync_init(&triggers_sync, changelog_sync_mode);
	zbx_dbsync_init(&tdep_sync, mode);
	zbx_dbsync_init(&func_sync, changelog_sync_mode);
	zbx_dbsync_init(&expr_sync, mode);
	zbx_dbsync_init(&action_sync, mode);
	/* Action operation sync produces virtual rows with two columns - actionid, opflags. */
	/* Because of this it cannot return the original database select and must always be  */
	/* initialized in update mode.                                                       */
	zbx_dbsync_init(&action_op_sync, ZBX_DBSYNC_UPDATE);

	zbx_dbsync_init(&action_condition_sync, mode);
	zbx_dbsync_init(&trigger_tag_sync, changelog_sync_mode);
	zbx_dbsync_init(&item_tag_sync, changelog_sync_mode);
	zbx_dbsync_init(&host_tag_sync, changelog_sync_mode);
	zbx_dbsync_init(&correlation_sync, mode);
	zbx_dbsync_init(&corr_condition_sync, mode);
	zbx_dbsync_init(&corr_operation_sync, mode);
	zbx_dbsync_init(&hgroups_sync, mode);
	zbx_dbsync_init(&hgroup_host_sync, mode);
	zbx_dbsync_init(&itempp_sync, changelog_sync_mode);
	zbx_dbsync_init(&itemscrp_sync, mode);

	zbx_dbsync_init(&maintenance_sync, mode);
	zbx_dbsync_init(&maintenance_period_sync, mode);
	zbx_dbsync_init(&maintenance_tag_sync, mode);
	zbx_dbsync_init(&maintenance_group_sync, mode);
	zbx_dbsync_init(&maintenance_host_sync, mode);

	zbx_dbsync_init(&drules_sync, changelog_sync_mode);
	zbx_dbsync_init(&dchecks_sync, changelog_sync_mode);

	zbx_dbsync_init(&httptest_sync, changelog_sync_mode);
	zbx_dbsync_init(&httptest_field_sync, changelog_sync_mode);
	zbx_dbsync_init(&httpstep_sync, changelog_sync_mode);
	zbx_dbsync_init(&httpstep_field_sync, changelog_sync_mode);

	zbx_dbsync_init(&connector_sync, changelog_sync_mode);
	zbx_dbsync_init(&connector_tag_sync, changelog_sync_mode);

#ifdef HAVE_ORACLE
	/* With Oracle fetch statements can fail before all data has been fetched. */
	/* In such cache next sync will need to do full scan rather than just      */
	/* applying changelog diff. To detect this problem configuration is synced */
	/* in transaction and error is checked at the end.                         */
	zbx_db_begin();
#endif

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_config(&config_sync))
		goto out;
	csec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_autoreg_psk(&autoreg_config_sync))
		goto out;
	autoreg_csec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_autoreg_host(&autoreg_host_sync))
		goto out;
	autoreg_host_csec = zbx_time() - sec;

	/* sync global configuration settings */
	START_SYNC;
	sec = zbx_time();
	DCsync_config(&config_sync, new_revision, &flags);
	csec2 = zbx_time() - sec;

	/* must be done in the same cache locking with config sync */
	sec = zbx_time();
	DCsync_autoreg_config(&autoreg_config_sync, new_revision);
	autoreg_csec2 = zbx_time() - sec;
	sec = zbx_time();
	DCsync_autoreg_host(&autoreg_host_sync);
	autoreg_host_csec2 = zbx_time() - sec;
	FINISH_SYNC;

	/* sync macro related data, to support macro resolving during configuration sync */

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_host_templates(&htmpl_sync))
		goto out;
	htsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_global_macros(&gmacro_sync))
		goto out;
	gmsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_host_macros(&hmacro_sync))
		goto out;
	hmsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_host_tags(&host_tag_sync))
		goto out;
	host_tag_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_connectors(&connector_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_connector_tags(&connector_tag_sync))
		goto out;
	connector_sec = zbx_time() - sec;

	START_SYNC;
	sec = zbx_time();
	config->um_cache = um_cache_sync(config->um_cache, new_revision, &gmacro_sync, &hmacro_sync, &htmpl_sync,
									 config_vault);
	um_cache_sec = zbx_time() - sec;

	sec = zbx_time();
	DCsync_host_tags(&host_tag_sync);
	host_tag_sec2 = zbx_time() - sec;

	FINISH_SYNC;

	/* postpone configuration sync until macro secrets are received from Zabbix server */
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER) && 0 != config->kvs_paths.values_num &&
		ZBX_DBSYNC_INIT == mode)
	{
		goto clean;
	}

	/* sync host data to support host lookups when resolving macros during configuration sync */
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_hosts(&hosts_sync))
		goto out;
	hsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_host_inventory(&hi_sync))
		goto out;
	hisec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_host_groups(&hgroups_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_host_group_hosts(&hgroup_host_sync))
		goto out;
	hgroups_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_maintenances(&maintenance_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_maintenance_tags(&maintenance_tag_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_maintenance_periods(&maintenance_period_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_maintenance_groups(&maintenance_group_sync))
		goto out;
	if (FAIL == zbx_dbsync_compare_maintenance_hosts(&maintenance_host_sync))
		goto out;
	maintenance_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_prepare_drules(&drules_sync))
		goto out;
	if (FAIL == zbx_dbsync_prepare_dchecks(&dchecks_sync))
		goto out;
	drules_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_prepare_httptests(&httptest_sync))
		goto out;
	if (FAIL == zbx_dbsync_prepare_httptest_fields(&httptest_field_sync))
		goto out;
	if (FAIL == zbx_dbsync_prepare_httpsteps(&httpstep_sync))
		goto out;
	if (FAIL == zbx_dbsync_prepare_httpstep_fields(&httpstep_field_sync))
		goto out;
	httptest_sec = zbx_time() - sec;
	START_SYNC;
	sec = zbx_time();
	zbx_vector_uint64_create(&active_avail_diff);
	DCsync_hosts(&hosts_sync, new_revision, &active_avail_diff, &activated_hosts, config_vault);
	zbx_dbsync_clear_user_macros();
	hsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_host_inventory(&hi_sync, new_revision);
	hisec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_hostgroups(&hgroups_sync);
	DCsync_hostgroup_hosts(&hgroup_host_sync);
	hgroups_sec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_maintenances(&maintenance_sync);
	DCsync_maintenance_tags(&maintenance_tag_sync);
	DCsync_maintenance_groups(&maintenance_group_sync);
	DCsync_maintenance_hosts(&maintenance_host_sync);
	DCsync_maintenance_periods(&maintenance_period_sync);
	maintenance_sec2 = zbx_time() - sec;
	if (0 != hgroups_sync.add_num + hgroups_sync.update_num + hgroups_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_HOST_GROUPS;

	if (0 != maintenance_group_sync.add_num + maintenance_group_sync.update_num + maintenance_group_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_MAINTENANCE_GROUPS;

	if (0 != (update_flags & ZBX_DBSYNC_UPDATE_HOST_GROUPS))
		dc_hostgroups_update_cache();

	/* pre-cache nested groups used in maintenances to allow read lock */
	/* during host maintenance update calculations                     */
	if (0 != (update_flags & (ZBX_DBSYNC_UPDATE_HOST_GROUPS | ZBX_DBSYNC_UPDATE_MAINTENANCE_GROUPS)))
		dc_maintenance_precache_nested_groups();

	sec = zbx_time();
	DCsync_connectors(&connector_sync, new_revision);
	DCsync_connector_tags(&connector_tag_sync);
	connector_sec2 = zbx_time() - sec;

	FINISH_SYNC;
	
	zbx_dbsync_process_active_avail_diff(&active_avail_diff);
	zbx_vector_uint64_destroy(&active_avail_diff);

	/* sync item data to support item lookups when resolving macros during configuration sync */
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_interfaces(&if_sync))
		goto out;
	ifsec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_items(&items_sync))
		goto out;
	isec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_template_items(&template_items_sync))
		goto out;
	tisec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_prototype_items(&prototype_items_sync))
		goto out;
	pisec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_item_discovery(&item_discovery_sync))
		goto out;
	idsec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_item_preprocs(&itempp_sync))
		goto out;
	itempp_sec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_item_script_param(&itemscrp_sync))
		goto out;
	itemscrp_sec = zbx_time() - sec;
	START_SYNC;

	/* resolves macros for interface_snmpaddrs, must be after DCsync_hmacros() */
	sec = zbx_time();
	DCsync_interfaces(&if_sync, new_revision);
	ifsec2 = zbx_time() - sec;
	/* relies on hosts, proxies and interfaces, must be after DCsync_{hosts,interfaces}() */

	sec = zbx_time();
	DCsync_items(&items_sync, new_revision, flags, synced, deleted_itemids);
	isec2 = zbx_time() - sec;
	sec = zbx_time();
	DCsync_template_items(&template_items_sync);
	tisec2 = zbx_time() - sec;
	sec = zbx_time();
	DCsync_prototype_items(&prototype_items_sync);
	pisec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_item_discovery(&item_discovery_sync);
	idsec2 = zbx_time() - sec;

	/* relies on items, must be after DCsync_items() */
	sec = zbx_time();
	DCsync_item_preproc(&itempp_sync, new_revision);
	itempp_sec2 = zbx_time() - sec;

	/* relies on items, must be after DCsync_items() */
	sec = zbx_time();
	DCsync_itemscript_param(&itemscrp_sync, new_revision);
	itemscrp_sec2 = zbx_time() - sec;

	FINISH_SYNC;

	/* sync function data to support function lookups when resolving macros during configuration sync */

	/* relies on items, must be after DCsync_items() */
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_item_tags(&item_tag_sync))
		goto out;
	item_tag_sec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_functions(&func_sync))
		goto out;
	fsec = zbx_time() - sec;

	START_SYNC;
	sec = zbx_time();
	DCsync_functions(&func_sync, new_revision);
	fsec2 = zbx_time() - sec;
	FINISH_SYNC;

	/* sync rest of the data */
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_triggers(&triggers_sync))
		goto out;
	tsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_trigger_dependency(&tdep_sync))
		goto out;
	dsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_expressions(&expr_sync))
		goto out;
	expr_sec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_actions(&action_sync))
		goto out;
	action_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_action_ops(&action_op_sync))
		goto out;
	action_op_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_action_conditions(&action_condition_sync))
		goto out;
	action_condition_sec = zbx_time() - sec;
	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_trigger_tags(&trigger_tag_sync))
		goto out;
	trigger_tag_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_correlations(&correlation_sync))
		goto out;
	correlation_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_corr_conditions(&corr_condition_sync))
		goto out;
	corr_condition_sec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_corr_operations(&corr_operation_sync))
		goto out;
	corr_operation_sec = zbx_time() - sec;

	START_SYNC;

	sec = zbx_time();
	DCsync_triggers(&triggers_sync, new_revision);
	tsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_trigdeps(&tdep_sync);
	dsec2 = zbx_time() - sec;
	sec = zbx_time();
	DCsync_expressions(&expr_sync, new_revision);
	expr_sec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_actions(&action_sync);
	action_sec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_action_ops(&action_op_sync);
	action_op_sec2 = zbx_time() - sec;
	sec = zbx_time();
	DCsync_action_conditions(&action_condition_sync);
	action_condition_sec2 = zbx_time() - sec;
	sec = zbx_time();

	/* relies on triggers, must be after DCsync_triggers() */
	DCsync_trigger_tags(&trigger_tag_sync);
	trigger_tag_sec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_item_tags(&item_tag_sync);
	item_tag_sec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_correlations(&correlation_sync);
	correlation_sec2 = zbx_time() - sec;

	sec = zbx_time();
	/* relies on correlation rules, must be after DCsync_correlations() */
	DCsync_corr_conditions(&corr_condition_sync);
	corr_condition_sec2 = zbx_time() - sec;

	sec = zbx_time();
	/* relies on correlation rules, must be after DCsync_correlations() */
	DCsync_corr_operations(&corr_operation_sync);
	corr_operation_sec2 = zbx_time() - sec;
	sec = zbx_time();
	dc_sync_drules(&drules_sync, new_revision);
	dc_sync_dchecks(&dchecks_sync, new_revision);
	drules_sec2 = zbx_time() - sec;

	sec = zbx_time();
	dc_sync_httptests(&httptest_sync, new_revision);
	dc_sync_httptest_fields(&httptest_field_sync, new_revision);
	dc_sync_httpsteps(&httpstep_sync, new_revision);
	dc_sync_httpstep_fields(&httpstep_field_sync, new_revision);
	httptest_sec2 = zbx_time() - sec;

	sec = zbx_time();

	if (0 != hosts_sync.add_num + hosts_sync.update_num + hosts_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_HOSTS;

	if (0 != items_sync.add_num + items_sync.update_num + items_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_ITEMS;

	if (0 != func_sync.add_num + func_sync.update_num + func_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_FUNCTIONS;

	if (0 != triggers_sync.add_num + triggers_sync.update_num + triggers_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_TRIGGERS;

	if (0 != tdep_sync.add_num + tdep_sync.update_num + tdep_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_TRIGGER_DEPENDENCY;

	if (0 != gmacro_sync.add_num + gmacro_sync.update_num + gmacro_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_MACROS;

	if (0 != hmacro_sync.add_num + hmacro_sync.update_num + hmacro_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_MACROS;

	if (0 != htmpl_sync.add_num + htmpl_sync.update_num + htmpl_sync.remove_num)
		update_flags |= ZBX_DBSYNC_UPDATE_MACROS;

	if (0 != connector_sync.add_num + connector_sync.update_num + connector_sync.remove_num +
			connector_tag_sync.add_num + connector_tag_sync.update_num + connector_tag_sync.remove_num)
	{
		connectors_num = config->connectors.num_data;
	}

	/* update various trigger related links in cache */
	if (0 != (update_flags & (ZBX_DBSYNC_UPDATE_HOSTS | ZBX_DBSYNC_UPDATE_ITEMS | ZBX_DBSYNC_UPDATE_FUNCTIONS |
							  ZBX_DBSYNC_UPDATE_TRIGGERS | ZBX_DBSYNC_UPDATE_MACROS)))
	{
		dc_trigger_update_cache();
		dc_schedule_trigger_timers((ZBX_DBSYNC_INIT == mode ? &trend_queue : NULL), time(NULL));
	}

	update_sec = zbx_time() - sec;

	config->revision.config = new_revision;
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		total = csec + hsec + hisec + htsec + gmsec + hmsec + ifsec + idsec + isec + tisec + pisec + tsec +
				dsec + fsec + expr_sec + action_sec + action_op_sec + action_condition_sec +
				trigger_tag_sec + correlation_sec + corr_condition_sec + corr_operation_sec +
				hgroups_sec + itempp_sec + maintenance_sec + item_tag_sec + drules_sec + httptest_sec +
				connector_sec;
		total2 = csec2 + hsec2 + hisec2 + ifsec2 + idsec2 + isec2 + tisec2 + pisec2 + tsec2 + dsec2 + fsec2 +
				 expr_sec2 + action_op_sec2 + action_sec2 + action_condition_sec2 + trigger_tag_sec2 +
				 correlation_sec2 + corr_condition_sec2 + corr_operation_sec2 + hgroups_sec2 +
				 itempp_sec2 + maintenance_sec2 + item_tag_sec2 + update_sec + um_cache_sec +
				 drules_sec2 + httptest_sec2 + connector_sec2;

		zabbix_log(LOG_LEVEL_DEBUG, "%s() changelog  : sql:" ZBX_FS_DBL " sec (%d records)",
				   __func__, changelog_sec, changelog_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() config     : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, csec, csec2, config_sync.add_num, config_sync.update_num,
				   config_sync.remove_num);

		total += autoreg_csec + autoreg_host_csec;
		total2 += autoreg_csec2 + autoreg_host_csec2;
		zabbix_log(LOG_LEVEL_DEBUG, "%s() autoreg    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, autoreg_csec, autoreg_csec2, autoreg_config_sync.add_num,
				   autoreg_config_sync.update_num, autoreg_config_sync.remove_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() autoreg host    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, autoreg_host_csec, autoreg_host_csec2, autoreg_host_sync.add_num,
				   autoreg_host_sync.update_num, autoreg_host_sync.remove_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() hosts      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, hsec, hsec2, hosts_sync.add_num, hosts_sync.update_num,
				   hosts_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host_invent: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, hisec, hisec2, hi_sync.add_num, hi_sync.update_num,
				   hi_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() templates  : sql:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, htsec, htmpl_sync.add_num, htmpl_sync.update_num,
				   htmpl_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() globmacros : sql:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, gmsec, gmacro_sync.add_num, gmacro_sync.update_num,
				   gmacro_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hostmacros : sql:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, hmsec, hmacro_sync.add_num, hmacro_sync.update_num,
				   hmacro_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() interfaces : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, ifsec, ifsec2, if_sync.add_num, if_sync.update_num,
				   if_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, isec, isec2, items_sync.add_num, items_sync.update_num,
				   items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() template_items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, tisec, tisec2, template_items_sync.add_num,
				   template_items_sync.update_num, template_items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() prototype_items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, pisec, pisec2, prototype_items_sync.add_num,
				   prototype_items_sync.update_num, prototype_items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item_discovery      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, idsec, idsec2, item_discovery_sync.add_num, item_discovery_sync.update_num,
				   item_discovery_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() triggers   : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, tsec, tsec2, triggers_sync.add_num, triggers_sync.update_num,
				   triggers_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trigdeps   : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, dsec, dsec2, tdep_sync.add_num, tdep_sync.update_num,
				   tdep_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trig. tags : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, trigger_tag_sec, trigger_tag_sec2, trigger_tag_sync.add_num,
				   trigger_tag_sync.update_num, trigger_tag_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host tags : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, host_tag_sec, host_tag_sec2, host_tag_sync.add_num,
				   host_tag_sync.update_num, host_tag_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item tags : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, item_tag_sec, item_tag_sec2, item_tag_sync.add_num,
				   item_tag_sync.update_num, item_tag_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() functions  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, fsec, fsec2, func_sync.add_num, func_sync.update_num,
				   func_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() expressions: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, expr_sec, expr_sec2, expr_sync.add_num, expr_sync.update_num,
				   expr_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() actions    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, action_sec, action_sec2, action_sync.add_num, action_sync.update_num,
				   action_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() operations : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, action_op_sec, action_op_sec2, action_op_sync.add_num,
				   action_op_sync.update_num, action_op_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() conditions : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, action_condition_sec, action_condition_sec2,
				   action_condition_sync.add_num, action_condition_sync.update_num,
				   action_condition_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr       : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, correlation_sec, correlation_sec2, correlation_sync.add_num,
				   correlation_sync.update_num, correlation_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr_cond  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, corr_condition_sec, corr_condition_sec2, corr_condition_sync.add_num,
				   corr_condition_sync.update_num, corr_condition_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr_op    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, corr_operation_sec, corr_operation_sec2, corr_operation_sync.add_num,
				   corr_operation_sync.update_num, corr_operation_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hgroups    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, hgroups_sec, hgroups_sec2, hgroups_sync.add_num,
				   hgroups_sync.update_num, hgroups_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item pproc : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, itempp_sec, itempp_sec2, itempp_sync.add_num, itempp_sync.update_num,
				   itempp_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item script param: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, itemscrp_sec, itemscrp_sec2, itemscrp_sync.add_num,
				   itemscrp_sync.update_num, itemscrp_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() maintenance: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, maintenance_sec, maintenance_sec2, maintenance_sync.add_num,
				   maintenance_sync.update_num, maintenance_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() drules     : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, drules_sec, drules_sec2, drules_sync.add_num, drules_sync.update_num,
				   drules_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() dchecks    : (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, dchecks_sync.add_num, dchecks_sync.update_num, dchecks_sync.remove_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() httptests  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, httptest_sec, httptest_sec2, httptest_sync.add_num, httptest_sync.update_num,
				   httptest_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httptestfld : (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, httptest_field_sync.add_num, httptest_field_sync.update_num,
				   httptest_field_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httpsteps   : (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, httpstep_sync.add_num, httpstep_sync.update_num, httpstep_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httpstepfld : (" ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				   __func__, httpstep_field_sync.add_num, httpstep_field_sync.update_num,
				   httpstep_field_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() connector: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
					ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
					__func__, connector_sec, connector_sec2, connector_sync.add_num,
				connector_sync.update_num, connector_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() connector_tag: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
					ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
					__func__, connector_sec, connector_sec2, connector_tag_sync.add_num,
					connector_tag_sync.update_num, connector_tag_sync.remove_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() macro cache: " ZBX_FS_DBL " sec.", __func__, um_cache_sec);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() reindex    : " ZBX_FS_DBL " sec.", __func__, update_sec);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() total sql  : " ZBX_FS_DBL " sec.", __func__, total);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() total sync : " ZBX_FS_DBL " sec.", __func__, total2);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() proxies    : %d (%d slots)", __func__,
				   config->proxies.num_data, config->proxies.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hosts      : %d (%d slots)", __func__,
				   config->hosts.num_data, config->hosts.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hosts_h    : %d (%d slots)", __func__,
				   config->hosts_h.num_data, config->hosts_h.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hosts_p    : %d (%d slots)", __func__,
				   config->hosts_p.num_data, config->hosts_p.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() autoreg_hosts: %d (%d slots)", __func__,
				   config->autoreg_hosts.num_data, config->autoreg_hosts.num_slots);
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zabbix_log(LOG_LEVEL_DEBUG, "%s() psks       : %d (%d slots)", __func__,
				   config->psks.num_data, config->psks.num_slots);
#endif
		zabbix_log(LOG_LEVEL_DEBUG, "%s() ipmihosts  : %d (%d slots)", __func__,
				   config->ipmihosts.num_data, config->ipmihosts.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host_invent: %d (%d slots)", __func__,
				   config->host_inventories.num_data, config->host_inventories.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() glob macros: %d (%d slots)", __func__,
				   config->gmacros.num_data, config->gmacros.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host macros: %d (%d slots)", __func__,
				   config->hmacros.num_data, config->hmacros.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() kvs_paths : %d", __func__, config->kvs_paths.values_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() interfaces : %d (%d slots)", __func__,
				   config->interfaces.num_data, config->interfaces.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() interfaces_snmp : %d (%d slots)", __func__,
				   config->interfaces_snmp.num_data, config->interfaces_snmp.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() interfac_ht: %d (%d slots)", __func__,
				   config->interfaces_ht.num_data, config->interfaces_ht.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() if_snmpitms: %d (%d slots)", __func__,
				   config->interface_snmpitems.num_data, config->interface_snmpitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() if_snmpaddr: %d (%d slots)", __func__,
				   config->interface_snmpaddrs.num_data, config->interface_snmpaddrs.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item_discovery : %d (%d slots)", __func__,
				   config->item_discovery.num_data, config->item_discovery.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items      : %d (%d slots)", __func__,
				   config->items.num_data, config->items.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items_hk   : %d (%d slots)", __func__,
				   config->items_hk.num_data, config->items_hk.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() numitems   : %d (%d slots)", __func__,
				   config->numitems.num_data, config->numitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() preprocitems: %d (%d slots)", __func__,
				   config->preprocops.num_data, config->preprocops.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() snmpitems  : %d (%d slots)", __func__,
				   config->snmpitems.num_data, config->snmpitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() ipmiitems  : %d (%d slots)", __func__,
				   config->ipmiitems.num_data, config->ipmiitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trapitems  : %d (%d slots)", __func__,
				   config->trapitems.num_data, config->trapitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() dependentitems  : %d (%d slots)", __func__,
				   config->dependentitems.num_data, config->dependentitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() logitems   : %d (%d slots)", __func__,
				   config->logitems.num_data, config->logitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() dbitems    : %d (%d slots)", __func__,
				   config->dbitems.num_data, config->dbitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() sshitems   : %d (%d slots)", __func__,
				   config->sshitems.num_data, config->sshitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() telnetitems: %d (%d slots)", __func__,
				   config->telnetitems.num_data, config->telnetitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() simpleitems: %d (%d slots)", __func__,
				   config->simpleitems.num_data, config->simpleitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() jmxitems   : %d (%d slots)", __func__,
				   config->jmxitems.num_data, config->jmxitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() calcitems  : %d (%d slots)", __func__,
				   config->calcitems.num_data, config->calcitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httpitems  : %d (%d slots)", __func__,
				   config->httpitems.num_data, config->httpitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() scriptitems  : %d (%d slots)", __func__,
				   config->scriptitems.num_data, config->scriptitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() functions  : %d (%d slots)", __func__,
				   config->functions.num_data, config->functions.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() triggers   : %d (%d slots)", __func__,
				   config->triggers.num_data, config->triggers.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trigdeps   : %d (%d slots)", __func__,
				   config->trigdeps.num_data, config->trigdeps.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trig. tags : %d (%d slots)", __func__,
				   config->trigger_tags.num_data, config->trigger_tags.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() expressions: %d (%d slots)", __func__,
				   config->expressions.num_data, config->expressions.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() actions    : %d (%d slots)", __func__,
				   config->actions.num_data, config->actions.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() conditions : %d (%d slots)", __func__,
				   config->action_conditions.num_data, config->action_conditions.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr.      : %d (%d slots)", __func__,
				   config->correlations.num_data, config->correlations.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr. conds: %d (%d slots)", __func__,
				   config->corr_conditions.num_data, config->corr_conditions.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr. ops  : %d (%d slots)", __func__,
				   config->corr_operations.num_data, config->corr_operations.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hgroups    : %d (%d slots)", __func__,
				   config->hostgroups.num_data, config->hostgroups.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item procs : %d (%d slots)", __func__,
				   config->preprocops.num_data, config->preprocops.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() maintenance: %d (%d slots)", __func__,
				   config->maintenances.num_data, config->maintenances.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() maint tags : %d (%d slots)", __func__,
				   config->maintenance_tags.num_data, config->maintenance_tags.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() maint time : %d (%d slots)", __func__,
				   config->maintenance_periods.num_data, config->maintenance_periods.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() drules     : %d (%d slots)", __func__,
				   config->drules.num_data, config->drules.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() dchecks    : %d (%d slots)", __func__,
				   config->dchecks.num_data, config->dchecks.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() httptests  : %d (%d slots)", __func__,
				   config->httptests.num_data, config->httptests.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httptestfld: %d (%d slots)", __func__,
				   config->httptest_fields.num_data, config->httptest_fields.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httpsteps  : %d (%d slots)", __func__,
				   config->httpsteps.num_data, config->httpsteps.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() httpstepfld: %d (%d slots)", __func__,
				   config->httpstep_fields.num_data, config->httpstep_fields.num_slots);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() connector: %d (%d slots)", __func__,
				config->connectors.num_data, config->connectors.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() connector tags : %d (%d slots)", __func__,
				config->connector_tags.num_data, config->connector_tags.num_slots);

		for (i = 0; ZBX_POLLER_TYPE_COUNT > i; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() queue[%d]   : %d (%d allocated)", __func__,
					   i, config->queues[i].elems_num, config->queues[i].elems_alloc);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s() pqueue     : %d (%d allocated)", __func__,
				   config->pqueue.elems_num, config->pqueue.elems_alloc);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() timer queue: %d (%d allocated)", __func__,
				   config->trigger_queue.elems_num, config->trigger_queue.elems_alloc);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() changelog  : %d", __func__, zbx_dbsync_env_changelog_num());

		zabbix_log(LOG_LEVEL_DEBUG, "%s() configfree : " ZBX_FS_DBL "%%", __func__,
				   100 * ((double)config_mem->free_size / config_mem->orig_size));

		zabbix_log(LOG_LEVEL_DEBUG, "%s() strings    : %d (%d slots)", __func__,
				   config->strpool.num_data, config->strpool.num_slots);

		zbx_shmem_dump_stats(LOG_LEVEL_DEBUG, config_mem);
	}
	dberr = ZBX_DB_OK;
out:
	if (0 == sync_in_progress)
		START_SYNC;

	config->status->last_update = 0;
	config->sync_ts = time(NULL);
	FINISH_SYNC;
#ifdef HAVE_ORACLE
	if (ZBX_DB_OK == dberr)
		dberr = zbx_db_commit();
	else
		zbx_db_rollback();
#endif
	switch (dberr)
	{
	case ZBX_DB_OK:
		if (ZBX_DBSYNC_INIT != changelog_sync_mode)
			zbx_dbsync_env_flush_changelog();
		else
			sync_status = ZBX_DBSYNC_STATUS_INITIALIZED;
		break;
	case ZBX_DB_FAIL:
		/* non recoverable database error is encountered */
		THIS_SHOULD_NEVER_HAPPEN;
		break;
	case ZBX_DB_DOWN:
		zabbix_log(LOG_LEVEL_WARNING, "Configuration cache has not been fully initialized because of"
									  " database connection problems. Full database scan will be attempted on next"
									  " sync.");
		break;
	}

	if (0 != (update_flags & (ZBX_DBSYNC_UPDATE_HOSTS | ZBX_DBSYNC_UPDATE_ITEMS | ZBX_DBSYNC_UPDATE_MACROS)))
	{
		sec = zbx_time();

		dc_reschedule_items(&activated_hosts);

		if (0 != activated_hosts.num_data)
			dc_reschedule_httptests(&activated_hosts);

		queues_sec = zbx_time() - sec;
		zabbix_log(LOG_LEVEL_DEBUG, "%s() reschedule : " ZBX_FS_DBL " sec.", __func__, queues_sec);
	}

	if (0 != connectors_num && FAIL == zbx_connector_initialized())
	{
		zabbix_log(LOG_LEVEL_WARNING, "connectors cannot be used without connector workers:"
				" please check \"StartConnectors\" configuration parameter");
	}
clean:
	zbx_dbsync_clear(&config_sync);
	zbx_dbsync_clear(&autoreg_config_sync);
	zbx_dbsync_clear(&autoreg_host_sync);
	zbx_dbsync_clear(&hosts_sync);
	zbx_dbsync_clear(&hi_sync);
	zbx_dbsync_clear(&htmpl_sync);
	zbx_dbsync_clear(&gmacro_sync);
	zbx_dbsync_clear(&hmacro_sync);
	zbx_dbsync_clear(&host_tag_sync);
	zbx_dbsync_clear(&if_sync);
	zbx_dbsync_clear(&items_sync);
	zbx_dbsync_clear(&template_items_sync);
	zbx_dbsync_clear(&prototype_items_sync);
	zbx_dbsync_clear(&item_discovery_sync);
	zbx_dbsync_clear(&triggers_sync);
	zbx_dbsync_clear(&tdep_sync);
	zbx_dbsync_clear(&func_sync);
	zbx_dbsync_clear(&expr_sync);
	zbx_dbsync_clear(&action_sync);
	zbx_dbsync_clear(&action_op_sync);
	zbx_dbsync_clear(&action_condition_sync);
	zbx_dbsync_clear(&trigger_tag_sync);
	zbx_dbsync_clear(&correlation_sync);
	zbx_dbsync_clear(&corr_condition_sync);
	zbx_dbsync_clear(&corr_operation_sync);
	zbx_dbsync_clear(&hgroups_sync);
	zbx_dbsync_clear(&itempp_sync);
	zbx_dbsync_clear(&itemscrp_sync);
	zbx_dbsync_clear(&item_tag_sync);
	zbx_dbsync_clear(&maintenance_sync);
	zbx_dbsync_clear(&maintenance_period_sync);
	zbx_dbsync_clear(&maintenance_tag_sync);
	zbx_dbsync_clear(&maintenance_group_sync);
	zbx_dbsync_clear(&maintenance_host_sync);
	zbx_dbsync_clear(&hgroup_host_sync);
	zbx_dbsync_clear(&drules_sync);
	zbx_dbsync_clear(&dchecks_sync);
	zbx_dbsync_clear(&httptest_sync);
	zbx_dbsync_clear(&httptest_field_sync);
	zbx_dbsync_clear(&httpstep_sync);
	zbx_dbsync_clear(&httpstep_field_sync);
	zbx_dbsync_clear(&connector_sync);
	zbx_dbsync_clear(&connector_tag_sync);

	if (ZBX_DBSYNC_INIT == mode)
		zbx_hashset_destroy(&trend_queue);

	zbx_dbsync_env_clear();

	zbx_hashset_destroy(&activated_hosts);

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
		DCdump_configuration();

	poller_item_notify_flush();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Helper functions for configuration cache data structure element comparison *
 * and hash value calculation.                                                *
 *                                                                            *
 * The __config_shmem_XXX_func(), __config_XXX_hash and __config_XXX_compare    *
 * functions are used only inside init_configuration_cache() function to      *
 * initialize internal data structures.                                       *
 *                                                                            *
 ******************************************************************************/

static zbx_hash_t __config_item_hk_hash(const void *data)
{
	const ZBX_DC_ITEM_HK *item_hk = (const ZBX_DC_ITEM_HK *)data;

	zbx_hash_t hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&item_hk->hostid);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(item_hk->key, strlen(item_hk->key), hash);

	return hash;
}

static int __config_item_hk_compare(const void *d1, const void *d2)
{
	const ZBX_DC_ITEM_HK *item_hk_1 = (const ZBX_DC_ITEM_HK *)d1;
	const ZBX_DC_ITEM_HK *item_hk_2 = (const ZBX_DC_ITEM_HK *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(item_hk_1->hostid, item_hk_2->hostid);

	return item_hk_1->key == item_hk_2->key ? 0 : strcmp(item_hk_1->key, item_hk_2->key);
}

static zbx_hash_t __config_host_h_hash(const void *data)
{
	const ZBX_DC_HOST_H *host_h = (const ZBX_DC_HOST_H *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(host_h->host, strlen(host_h->host), ZBX_DEFAULT_HASH_SEED);
}

static int __config_host_h_compare(const void *d1, const void *d2)
{
	const ZBX_DC_HOST_H *host_h_1 = (const ZBX_DC_HOST_H *)d1;
	const ZBX_DC_HOST_H *host_h_2 = (const ZBX_DC_HOST_H *)d2;

	return host_h_1->host == host_h_2->host ? 0 : strcmp(host_h_1->host, host_h_2->host);
}

static zbx_hash_t __config_autoreg_host_h_hash(const void *data)
{
	const ZBX_DC_AUTOREG_HOST *autoreg_host = (const ZBX_DC_AUTOREG_HOST *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(autoreg_host->host, strlen(autoreg_host->host), ZBX_DEFAULT_HASH_SEED);
}

static int __config_autoreg_host_h_compare(const void *d1, const void *d2)
{
	const ZBX_DC_AUTOREG_HOST *autoreg_host_1 = (const ZBX_DC_AUTOREG_HOST *)d1;
	const ZBX_DC_AUTOREG_HOST *autoreg_host_2 = (const ZBX_DC_AUTOREG_HOST *)d2;

	return strcmp(autoreg_host_1->host, autoreg_host_2->host);
}

static zbx_hash_t __config_interface_ht_hash(const void *data)
{
	const ZBX_DC_INTERFACE_HT *interface_ht = (const ZBX_DC_INTERFACE_HT *)data;

	zbx_hash_t hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&interface_ht->hostid);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO((char *)&interface_ht->type, 1, hash);

	return hash;
}

static int __config_interface_ht_compare(const void *d1, const void *d2)
{
	const ZBX_DC_INTERFACE_HT *interface_ht_1 = (const ZBX_DC_INTERFACE_HT *)d1;
	const ZBX_DC_INTERFACE_HT *interface_ht_2 = (const ZBX_DC_INTERFACE_HT *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(interface_ht_1->hostid, interface_ht_2->hostid);
	ZBX_RETURN_IF_NOT_EQUAL(interface_ht_1->type, interface_ht_2->type);

	return 0;
}

static zbx_hash_t __config_interface_addr_hash(const void *data)
{
	const ZBX_DC_INTERFACE_ADDR *interface_addr = (const ZBX_DC_INTERFACE_ADDR *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(interface_addr->addr, strlen(interface_addr->addr), ZBX_DEFAULT_HASH_SEED);
}

static int __config_interface_addr_compare(const void *d1, const void *d2)
{
	const ZBX_DC_INTERFACE_ADDR *interface_addr_1 = (const ZBX_DC_INTERFACE_ADDR *)d1;
	const ZBX_DC_INTERFACE_ADDR *interface_addr_2 = (const ZBX_DC_INTERFACE_ADDR *)d2;

	return (interface_addr_1->addr == interface_addr_2->addr ? 0 : strcmp(interface_addr_1->addr, interface_addr_2->addr));
}

static int __config_snmp_item_compare(const ZBX_DC_ITEM *i1, const ZBX_DC_ITEM *i2)
{
	const ZBX_DC_SNMPITEM *s1;
	const ZBX_DC_SNMPITEM *s2;

	unsigned char f1;
	unsigned char f2;

	ZBX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);
	ZBX_RETURN_IF_NOT_EQUAL(i1->type, i2->type);

	f1 = ZBX_FLAG_DISCOVERY_RULE & i1->flags;
	f2 = ZBX_FLAG_DISCOVERY_RULE & i2->flags;

	ZBX_RETURN_IF_NOT_EQUAL(f1, f2);

	s1 = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &i1->itemid);
	s2 = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &i2->itemid);

	ZBX_RETURN_IF_NOT_EQUAL(s1->snmp_oid_type, s2->snmp_oid_type);

	return 0;
}

static int __config_heap_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM *i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM *i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_next_check, i2->queue_next_check);
	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);

	if (ITEM_TYPE_SNMP != i1->type)
	{
		if (ITEM_TYPE_SNMP != i2->type)
			return 0;

		return -1;
	}
	else
	{
		if (ITEM_TYPE_SNMP != i2->type)
			return +1;

		return __config_snmp_item_compare(i1, i2);
	}
}

static int __config_pinger_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM *i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM *i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_next_check, i2->queue_next_check);
	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);
	ZBX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	return 0;
}

static int __config_java_item_compare(const ZBX_DC_ITEM *i1, const ZBX_DC_ITEM *i2)
{
	const ZBX_DC_JMXITEM *j1;
	const ZBX_DC_JMXITEM *j2;

	ZBX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	j1 = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &i1->itemid);
	j2 = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &i2->itemid);

	ZBX_RETURN_IF_NOT_EQUAL(j1->username, j2->username);
	ZBX_RETURN_IF_NOT_EQUAL(j1->password, j2->password);
	ZBX_RETURN_IF_NOT_EQUAL(j1->jmx_endpoint, j2->jmx_endpoint);

	return 0;
}

static int __config_java_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM *i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM *i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_next_check, i2->queue_next_check);
	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);

	return __config_java_item_compare(i1, i2);
}

static int __config_proxy_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_PROXY *p1 = (const ZBX_DC_PROXY *)e1->data;
	const ZBX_DC_PROXY *p2 = (const ZBX_DC_PROXY *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(p1->nextcheck, p2->nextcheck);

	return 0;
}

/* hash and compare functions for expressions hashset */

static zbx_hash_t __config_regexp_hash(const void *data)
{
	const ZBX_DC_REGEXP *regexp = (const ZBX_DC_REGEXP *)data;

	return ZBX_DEFAULT_STRING_HASH_FUNC(regexp->name);
}

static int __config_regexp_compare(const void *d1, const void *d2)
{
	const ZBX_DC_REGEXP *r1 = (const ZBX_DC_REGEXP *)d1;
	const ZBX_DC_REGEXP *r2 = (const ZBX_DC_REGEXP *)d2;

	return r1->name == r2->name ? 0 : strcmp(r1->name, r2->name);
}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
static zbx_hash_t __config_psk_hash(const void *data)
{
	const ZBX_DC_PSK *psk_i = (const ZBX_DC_PSK *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(psk_i->tls_psk_identity, strlen(psk_i->tls_psk_identity),
										ZBX_DEFAULT_HASH_SEED);
}

static int __config_psk_compare(const void *d1, const void *d2)
{
	const ZBX_DC_PSK *psk_1 = (const ZBX_DC_PSK *)d1;
	const ZBX_DC_PSK *psk_2 = (const ZBX_DC_PSK *)d2;

	return psk_1->tls_psk_identity == psk_2->tls_psk_identity ? 0 : strcmp(psk_1->tls_psk_identity, psk_2->tls_psk_identity);
}
#endif

static int __config_timer_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_trigger_timer_t *t1 = (const zbx_trigger_timer_t *)e1->data;
	const zbx_trigger_timer_t *t2 = (const zbx_trigger_timer_t *)e2->data;

	int ret;

	if (0 != (ret = zbx_timespec_compare(&t1->check_ts, &t2->check_ts)))
		return ret;

	ZBX_RETURN_IF_NOT_EQUAL(t1->triggerid, t2->triggerid);

	if (0 != (ret = zbx_timespec_compare(&t1->eval_ts, &t2->eval_ts)))
		return ret;

	return 0;
}

static int __config_drule_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_dc_drule_t *r1 = (const zbx_dc_drule_t *)e1->data;
	const zbx_dc_drule_t *r2 = (const zbx_dc_drule_t *)e2->data;

	return (int)(r1->nextcheck - r2->nextcheck);
}

static int __config_httptest_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t *e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t *e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_dc_httptest_t *ht1 = (const zbx_dc_httptest_t *)e1->data;
	const zbx_dc_httptest_t *ht2 = (const zbx_dc_httptest_t *)e2->data;

	return (int)(ht1->nextcheck - ht2->nextcheck);
}

static zbx_hash_t __config_session_hash(const void *data)
{
	const zbx_session_t *session = (const zbx_session_t *)data;
	zbx_hash_t hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&session->hostid);
	return ZBX_DEFAULT_STRING_HASH_ALGO(session->token, strlen(session->token), hash);
}

static int __config_session_compare(const void *d1, const void *d2)
{
	const zbx_session_t *s1 = (const zbx_session_t *)d1;
	const zbx_session_t *s2 = (const zbx_session_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(s1->hostid, s2->hostid);
	return strcmp(s1->token, s2->token);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Allocate shared memory for configuration cache                    *
 *                                                                            *
 ******************************************************************************/
int init_configuration_cache(char **error)
{
	int i, k, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() size:" ZBX_FS_UI64, __func__, CONFIG_CONF_CACHE_SIZE);

	if (SUCCEED != (ret = zbx_rwlock_create(&config_lock, ZBX_RWLOCK_CONFIG, error)))
		goto out;

	if (SUCCEED != (ret = zbx_rwlock_create(&config_history_lock, ZBX_RWLOCK_CONFIG_HISTORY, error)))
		goto out;

	if (SUCCEED != (ret = zbx_shmem_create(&config_mem, CONFIG_CONF_CACHE_SIZE, "configuration cache",
										   "CacheSize", 0, error)))
	{
		goto out;
	}

	config = (ZBX_DC_CONFIG *)__config_shmem_malloc_func(NULL, sizeof(ZBX_DC_CONFIG) +
																   (size_t)CONFIG_FORKS[ZBX_PROCESS_TYPE_TIMER] * sizeof(zbx_vector_ptr_t));

#define CREATE_HASHSET(hashset, hashset_size) \
                                              \
	CREATE_HASHSET_EXT(hashset, hashset_size, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC)

#define CREATE_HASHSET_EXT(hashset, hashset_size, hash_func, compare_func)        \
                                                                                  \
	zbx_hashset_create_ext(&hashset, hashset_size, hash_func, compare_func, NULL, \
						   __config_shmem_malloc_func, __config_shmem_realloc_func, __config_shmem_free_func)

	CREATE_HASHSET(config->items, 100);
	CREATE_HASHSET(config->numitems, 0);
	CREATE_HASHSET(config->snmpitems, 0);
	CREATE_HASHSET(config->ipmiitems, 0);
	CREATE_HASHSET(config->trapitems, 0);
	CREATE_HASHSET(config->dependentitems, 0);
	CREATE_HASHSET(config->logitems, 0);
	CREATE_HASHSET(config->dbitems, 0);
	CREATE_HASHSET(config->sshitems, 0);
	CREATE_HASHSET(config->telnetitems, 0);
	CREATE_HASHSET(config->simpleitems, 0);
	CREATE_HASHSET(config->jmxitems, 0);
	CREATE_HASHSET(config->calcitems, 0);
	CREATE_HASHSET(config->httpitems, 0);
	CREATE_HASHSET(config->scriptitems, 0);
	CREATE_HASHSET(config->itemscript_params, 0);
	CREATE_HASHSET(config->template_items, 0);
	CREATE_HASHSET(config->item_discovery, 0);
	CREATE_HASHSET(config->prototype_items, 0);
	CREATE_HASHSET(config->functions, 100);
	CREATE_HASHSET(config->triggers, 100);
	CREATE_HASHSET(config->trigdeps, 0);
	CREATE_HASHSET(config->hosts, 10);
	CREATE_HASHSET(config->proxies, 0);
	CREATE_HASHSET(config->host_inventories, 0);
	CREATE_HASHSET(config->host_inventories_auto, 0);
	CREATE_HASHSET(config->ipmihosts, 0);

	CREATE_HASHSET_EXT(config->gmacros, 0, um_macro_hash, um_macro_compare);
	CREATE_HASHSET_EXT(config->hmacros, 0, um_macro_hash, um_macro_compare);

	CREATE_HASHSET(config->interfaces, 10);
	CREATE_HASHSET(config->interfaces_snmp, 0);
	CREATE_HASHSET(config->interface_snmpitems, 0);
	CREATE_HASHSET(config->expressions, 0);
	CREATE_HASHSET(config->actions, 0);
	CREATE_HASHSET(config->action_conditions, 0);
	CREATE_HASHSET(config->trigger_tags, 0);
	CREATE_HASHSET(config->item_tags, 0);
	CREATE_HASHSET(config->host_tags, 0);
	CREATE_HASHSET(config->host_tags_index, 0);
	CREATE_HASHSET(config->correlations, 0);
	CREATE_HASHSET(config->corr_conditions, 0);
	CREATE_HASHSET(config->corr_operations, 0);
	CREATE_HASHSET(config->hostgroups, 0);
	zbx_vector_ptr_create_ext(&config->hostgroups_name, __config_shmem_malloc_func, __config_shmem_realloc_func,
							  __config_shmem_free_func);

	zbx_vector_ptr_create_ext(&config->kvs_paths, __config_shmem_malloc_func, __config_shmem_realloc_func,
							  __config_shmem_free_func);
	CREATE_HASHSET(config->gmacro_kv, 0);
	CREATE_HASHSET(config->hmacro_kv, 0);

	CREATE_HASHSET(config->preprocops, 0);

	CREATE_HASHSET(config->maintenances, 0);
	CREATE_HASHSET(config->maintenance_periods, 0);
	CREATE_HASHSET(config->maintenance_tags, 0);

	CREATE_HASHSET_EXT(config->items_hk, 100, __config_item_hk_hash, __config_item_hk_compare);
	CREATE_HASHSET_EXT(config->hosts_h, 10, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->hosts_p, 0, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->autoreg_hosts, 10, __config_autoreg_host_h_hash, __config_autoreg_host_h_compare);
	CREATE_HASHSET_EXT(config->interfaces_ht, 10, __config_interface_ht_hash, __config_interface_ht_compare);
	CREATE_HASHSET_EXT(config->interface_snmpaddrs, 0, __config_interface_addr_hash,
					   __config_interface_addr_compare);
	CREATE_HASHSET_EXT(config->regexps, 0, __config_regexp_hash, __config_regexp_compare);

	CREATE_HASHSET_EXT(config->strpool, 100, __config_strpool_hash, __config_strpool_compare);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	CREATE_HASHSET_EXT(config->psks, 0, __config_psk_hash, __config_psk_compare);
#endif

	CREATE_HASHSET(config->connectors, 0);
	CREATE_HASHSET(config->connector_tags, 0);

	for (i = 0; i < ZBX_POLLER_TYPE_COUNT; i++)
	{
		switch (i)
		{
		case ZBX_POLLER_TYPE_JAVA:
			zbx_binary_heap_create_ext(&config->queues[i],
									   __config_java_elem_compare,
									   ZBX_BINARY_HEAP_OPTION_DIRECT,
									   __config_shmem_malloc_func,
									   __config_shmem_realloc_func,
									   __config_shmem_free_func);
			break;
		case ZBX_POLLER_TYPE_PINGER:
			zbx_binary_heap_create_ext(&config->queues[i],
									   __config_pinger_elem_compare,
									   ZBX_BINARY_HEAP_OPTION_DIRECT,
									   __config_shmem_malloc_func,
									   __config_shmem_realloc_func,
									   __config_shmem_free_func);
			break;
		default:
			zbx_binary_heap_create_ext(&config->queues[i],
									   __config_heap_elem_compare,
									   ZBX_BINARY_HEAP_OPTION_DIRECT,
									   __config_shmem_malloc_func,
									   __config_shmem_realloc_func,
									   __config_shmem_free_func);
			break;
		}
	}

	for (i = 0; i < ITEM_TYPE_MAX + 1; i++) {
		config->time_by_poller_type[i] = 0.0;
		config->runs_by_poller_type[i] = 0;
	}
	
	zbx_binary_heap_create_ext(&config->pqueue,
							   __config_proxy_compare,
							   ZBX_BINARY_HEAP_OPTION_DIRECT,
							   __config_shmem_malloc_func,
							   __config_shmem_realloc_func,
							   __config_shmem_free_func);

	zbx_binary_heap_create_ext(&config->trigger_queue,
							   __config_timer_compare,
							   ZBX_BINARY_HEAP_OPTION_EMPTY,
							   __config_shmem_malloc_func,
							   __config_shmem_realloc_func,
							   __config_shmem_free_func);

	zbx_binary_heap_create_ext(&config->drule_queue,
							   __config_drule_compare,
							   ZBX_BINARY_HEAP_OPTION_DIRECT,
							   __config_shmem_malloc_func,
							   __config_shmem_realloc_func,
							   __config_shmem_free_func);

	zbx_binary_heap_create_ext(&config->httptest_queue,
							   __config_httptest_compare,
							   ZBX_BINARY_HEAP_OPTION_DIRECT,
							   __config_shmem_malloc_func,
							   __config_shmem_realloc_func,
							   __config_shmem_free_func);

	CREATE_HASHSET(config->drules, 0);
	CREATE_HASHSET(config->dchecks, 0);

	CREATE_HASHSET(config->httptests, 0);
	CREATE_HASHSET(config->httptest_fields, 0);
	CREATE_HASHSET(config->httpsteps, 0);
	CREATE_HASHSET(config->httpstep_fields, 0);
	for (i = 0; i < ZBX_SESSION_TYPE_COUNT; i++)
		CREATE_HASHSET_EXT(config->sessions[i], 0, __config_session_hash, __config_session_compare);

	config->config = NULL;

	config->status = (ZBX_DC_STATUS *)__config_shmem_malloc_func(NULL, sizeof(ZBX_DC_STATUS));
	config->status->last_update = 0;
	config->status->sync_ts = 0;

	config->availability_diff_ts = 0;
	config->sync_ts = 0;

	config->internal_actions = 0;
	config->auto_registration_actions = 0;

	memset(&config->revision, 0, sizeof(config->revision));

	config->um_cache = um_cache_create();

	/* maintenance data are used only when timers are defined (server) */
	if (0 != CONFIG_FORKS[ZBX_PROCESS_TYPE_TIMER])
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_FALSE;
		config->maintenance_update_flags = (zbx_uint64_t *)__config_shmem_malloc_func(NULL,
																					  sizeof(zbx_uint64_t) * ZBX_MAINTENANCE_UPDATE_FLAGS_NUM());
		memset(config->maintenance_update_flags, 0, sizeof(zbx_uint64_t) * ZBX_MAINTENANCE_UPDATE_FLAGS_NUM());
	}

	config->proxy_lastaccess_ts = time(NULL);

	/* create data session token for proxies */
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
	{
		char *token;

		token = zbx_create_token(0);
		config->session_token = dc_strdup(token);
		zbx_free(token);
	}
	else
		config->session_token = NULL;

	config->last_items_change = time(NULL);

	zbx_dbsync_env_init(config);

#undef CREATE_HASHSET
#undef CREATE_HASHSET_EXT
out:

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Free memory allocated for configuration cache                     *
 *                                                                            *
 ******************************************************************************/
void free_configuration_cache(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	WRLOCK_CACHE;

	config = NULL;
//	glb_config = NULL;

	UNLOCK_CACHE;

	zbx_shmem_destroy(config_mem);
	config_mem = NULL;
	zbx_rwlock_destroy(&config_history_lock);
	zbx_rwlock_destroy(&config_lock);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Parameters: maintenance_status - [IN] maintenance status                   *
 *                                       HOST_MAINTENANCE_STATUS_* flag       *
 *             maintenance_type   - [IN] maintenance type                     *
 *                                       MAINTENANCE_TYPE_* flag              *
 *             type               - [IN] item type                            *
 *                                       ITEM_TYPE_* flag                     *
 *                                                                            *
 * Return value: SUCCEED if host in maintenance without data collection       *
 *               FAIL otherwise                                               *
 *                                                                            *
 ******************************************************************************/
int in_maintenance_without_data_collection(unsigned char maintenance_status, unsigned char maintenance_type,
										   unsigned char type)
{
	if (HOST_MAINTENANCE_STATUS_ON != maintenance_status)
		return FAIL;

	if (MAINTENANCE_TYPE_NODATA != maintenance_type)
		return FAIL;

	if (ITEM_TYPE_INTERNAL == type)
		return FAIL;

	return SUCCEED;
}

static void DCget_host(DC_HOST *dst_host, const ZBX_DC_HOST *src_host)
{
	const ZBX_DC_IPMIHOST *ipmihost;

	dst_host->hostid = src_host->hostid;
	dst_host->proxy_hostid = src_host->proxy_hostid;
	dst_host->status = src_host->status;

	zbx_strscpy(dst_host->host, src_host->host);

	zbx_strlcpy_utf8(dst_host->name, src_host->name, sizeof(dst_host->name));

	dst_host->maintenance_status = src_host->maintenance_status;
	dst_host->maintenance_type = src_host->maintenance_type;
	dst_host->maintenance_from = src_host->maintenance_from;

	dst_host->tls_connect = src_host->tls_connect;
	dst_host->tls_accept = src_host->tls_accept;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_strscpy(dst_host->tls_issuer, src_host->tls_issuer);
	zbx_strscpy(dst_host->tls_subject, src_host->tls_subject);

	if (NULL == src_host->tls_dc_psk)
	{
		*dst_host->tls_psk_identity = '\0';
		*dst_host->tls_psk = '\0';
	}
	else
	{
		zbx_strscpy(dst_host->tls_psk_identity, src_host->tls_dc_psk->tls_psk_identity);
		zbx_strscpy(dst_host->tls_psk, src_host->tls_dc_psk->tls_psk);
	}
#endif
	if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &src_host->hostid)))
	{
		dst_host->ipmi_authtype = ipmihost->ipmi_authtype;
		dst_host->ipmi_privilege = ipmihost->ipmi_privilege;
		zbx_strscpy(dst_host->ipmi_username, ipmihost->ipmi_username);
		zbx_strscpy(dst_host->ipmi_password, ipmihost->ipmi_password);
	}
	else
	{
		dst_host->ipmi_authtype = ZBX_IPMI_DEFAULT_AUTHTYPE;
		dst_host->ipmi_privilege = ZBX_IPMI_DEFAULT_PRIVILEGE;
		*dst_host->ipmi_username = '\0';
		*dst_host->ipmi_password = '\0';
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: Locate host in configuration cache                                *
 *                                                                            *
 * Parameters: host - [OUT] pointer to DC_HOST structure                      *
 *             hostid - [IN] host ID from database                            *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
int DCget_host_by_hostid(DC_HOST *host, zbx_uint64_t hostid)
{
	int ret = FAIL;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
	{
		DCget_host(host, dc_host);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose:                                                                   *
 *     Check access rights for an active proxy and get the proxy ID           *
 *                                                                            *
 * Parameters:                                                                *
 *     host   - [IN] proxy name                                               *
 *     sock   - [IN] connection socket context                                *
 *     hostid - [OUT] proxy ID found in configuration cache                   *
 *     error  - [OUT] error message why access was denied                     *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - access is allowed, FAIL - access denied                      *
 *                                                                            *
 * Comments:                                                                  *
 *     Generating of error messages is done outside of configuration cache    *
 *     locking.                                                               *
 *                                                                            *
 ******************************************************************************/
int DCcheck_proxy_permissions(const char *host, const zbx_socket_t *sock, zbx_uint64_t *hostid, char **error)
{
	const ZBX_DC_HOST *dc_host;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	const char *msg;
	zbx_tls_conn_attr_t attr;

	if (FAIL == zbx_tls_get_attr(sock, &attr, error))
		return FAIL;
#endif

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_proxy(host)))
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(*error, "proxy \"%s\" not found", host);
		return FAIL;
	}

	if (HOST_STATUS_PROXY_ACTIVE != dc_host->status)
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(*error, "proxy \"%s\" is configured in passive mode", host);
		return FAIL;
	}

	if (0 == ((unsigned int)dc_host->tls_accept & sock->connection_type))
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(NULL, "connection of type \"%s\" is not allowed for proxy \"%s\"",
							  zbx_tcp_connection_type_name(sock->connection_type), host);
		return FAIL;
	}
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (FAIL == zbx_tls_validate_attr(sock, &attr, dc_host->tls_issuer, dc_host->tls_subject,
									  NULL == dc_host->tls_dc_psk ? NULL : dc_host->tls_dc_psk->tls_psk_identity, &msg))
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(NULL, "proxy \"%s\": %s", host, msg);
		return FAIL;
	}
#endif

	*hostid = dc_host->hostid;

	UNLOCK_CACHE;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose:                                                                   *
 *     Check access rights for host and get host ID and revision              *
 *                                                                            *
 * Parameters:                                                                *
 *     host     - [IN] host name                                              *
 *     sock     - [IN] connection socket context                              *
 *     hostid   - [OUT] host ID found in configuration cache                  *
 *     revision - [OUT] host configuration revision                           *
 *     error    - [OUT] error message why access was denied                   *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - access is allowed or host not found, FAIL - access denied    *
 *                                                                            *
 * Comments:                                                                  *
 *     Generating of error messages is done outside of configuration cache    *
 *     locking.                                                               *
 *                                                                            *
 ******************************************************************************/
int DCcheck_host_permissions(const char *host, const zbx_socket_t *sock, zbx_uint64_t *hostid,
							 zbx_uint64_t *revision, char **error)
{
	const ZBX_DC_HOST *dc_host;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_conn_attr_t attr;
	const char *msg;

	if (FAIL == zbx_tls_get_attr(sock, &attr, error))
		return FAIL;
#endif

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_host(host)))
	{
		UNLOCK_CACHE;
		*hostid = 0;
		return SUCCEED;
	}

	if (HOST_STATUS_MONITORED != dc_host->status)
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(NULL, "host \"%s\" not monitored", host);
		return FAIL;
	}

	if (0 == ((unsigned int)dc_host->tls_accept & sock->connection_type))
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(NULL, "connection of type \"%s\" is not allowed for host \"%s\"",
							  zbx_tcp_connection_type_name(sock->connection_type), host);
		return FAIL;
	}
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (FAIL == zbx_tls_validate_attr(sock, &attr, dc_host->tls_issuer, dc_host->tls_subject,
									  NULL == dc_host->tls_dc_psk ? NULL : dc_host->tls_dc_psk->tls_psk_identity, &msg))
	{
		UNLOCK_CACHE;
		*error = zbx_dsprintf(NULL, "host \"%s\": %s", host, msg);
		return FAIL;
	}
#endif

	*hostid = dc_host->hostid;
	*revision = MAX(dc_host->revision, config->revision.expression);

	um_cache_get_host_revision(config->um_cache, ZBX_UM_CACHE_GLOBAL_MACRO_HOSTID, revision);
	um_cache_get_host_revision(config->um_cache, *hostid, revision);

	/* configuration is not yet fully synced */
	if (*revision > config->revision.config)
		*revision = config->revision.config;

	UNLOCK_CACHE;

	return SUCCEED;
}

int DCis_autoreg_host_changed(const char *host, unsigned short port, const char *host_metadata,
							  zbx_conn_flags_t flag, const char *interface, int now, int heartbeat)
{
	const ZBX_DC_AUTOREG_HOST *dc_autoreg_host;
	int ret;

	RDLOCK_CACHE;

	if (NULL == (dc_autoreg_host = DCfind_autoreg_host(host)))
	{
		ret = SUCCEED;
	}
	else if (0 != strcmp(dc_autoreg_host->host_metadata, host_metadata))
	{
		ret = SUCCEED;
	}
	else if (dc_autoreg_host->flags != (int)flag)
	{
		ret = SUCCEED;
	}
	else if (ZBX_CONN_IP == flag && (0 != strcmp(dc_autoreg_host->listen_ip, interface) ||
									 dc_autoreg_host->listen_port != port))
	{
		ret = SUCCEED;
	}
	else if (ZBX_CONN_DNS == flag && (0 != strcmp(dc_autoreg_host->listen_dns, interface) ||
									  dc_autoreg_host->listen_port != port))
	{
		ret = SUCCEED;
	}
	else if (0 != heartbeat && heartbeat < now - dc_autoreg_host->timestamp)
	{
		ret = SUCCEED;
	}
	else
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

void DCconfig_update_autoreg_host(const char *host, const char *listen_ip, const char *listen_dns,
								  unsigned short listen_port, const char *host_metadata, zbx_conn_flags_t flags, int now)
{
	ZBX_DC_AUTOREG_HOST *dc_autoreg_host, dc_autoreg_host_local = {.host = host};
	int found;

	WRLOCK_CACHE;

	dc_autoreg_host = (ZBX_DC_AUTOREG_HOST *)zbx_hashset_search(&config->autoreg_hosts, &dc_autoreg_host_local);
	if (NULL == dc_autoreg_host)
	{
		found = 0;
		dc_autoreg_host = zbx_hashset_insert(&config->autoreg_hosts, &dc_autoreg_host_local,
											 sizeof(ZBX_DC_AUTOREG_HOST));
	}
	else

		found = 1;

	dc_strpool_replace(found, &dc_autoreg_host->host, host);
	dc_strpool_replace(found, &dc_autoreg_host->listen_ip, listen_ip);
	dc_strpool_replace(found, &dc_autoreg_host->listen_dns, listen_dns);
	dc_strpool_replace(found, &dc_autoreg_host->host_metadata, host_metadata);
	dc_autoreg_host->flags = flags;
	dc_autoreg_host->timestamp = now;
	dc_autoreg_host->listen_port = listen_port;

	UNLOCK_CACHE;
}

static void autoreg_host_free_data(ZBX_DC_AUTOREG_HOST *autoreg_host)
{
	dc_strpool_release(autoreg_host->host);
	dc_strpool_release(autoreg_host->listen_ip);
	dc_strpool_release(autoreg_host->listen_dns);
	dc_strpool_release(autoreg_host->host_metadata);
}

void DCconfig_delete_autoreg_host(const zbx_vector_ptr_t *autoreg_hosts)
{
	int cached = 0, i;

	/* hosts monitored by Zabbix proxy shouldn't be changed too frequently */
	if (0 == autoreg_hosts->values_num)
		return;

	/* hosts monitored by Zabbix proxy shouldn't be in cache */
	RDLOCK_CACHE;
	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		if (NULL != DCfind_autoreg_host(((const zbx_autoreg_host_t *)autoreg_hosts->values[i])->host))
			cached++;
	}
	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "moved:%d hosts from Zabbix server to Zabbix proxy", cached);

	if (0 == cached)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < autoreg_hosts->values_num; i++)
	{
		ZBX_DC_AUTOREG_HOST *autoreg_host;

		autoreg_host = DCfind_autoreg_host(((const zbx_autoreg_host_t *)autoreg_hosts->values[i])->host);
		if (NULL != autoreg_host)
		{
			autoreg_host_free_data(autoreg_host);
			zbx_hashset_remove_direct(&config->autoreg_hosts, autoreg_host);
		}
	}

	UNLOCK_CACHE;
}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Purpose:                                                                   *
 *     Find PSK with the specified identity in configuration cache            *
 *                                                                            *
 * Parameters:                                                                *
 *     psk_identity - [IN] PSK identity to search for ('\0' terminated)       *
 *     psk_buf      - [OUT] output buffer for PSK value with size             *
 *                    HOST_TLS_PSK_LEN_MAX                                    *
 *     psk_usage    - [OUT] 0 - PSK not found, 1 - found in host PSKs,        *
 *                          2 - found in autoregistration PSK, 3 - found in   *
 *                          both                                              *
 * Return value:                                                              *
 *     PSK length in bytes if PSK found. 0 - if PSK not found.                *
 *                                                                            *
 * Comments:                                                                  *
 *     ATTENTION! This function's address and arguments are described and     *
 *     used in file src/libs/zbxcrypto/tls.c for calling this function by     *
 *     pointer. If you ever change this DCget_psk_by_identity() function      *
 *     arguments or return value do not forget to synchronize changes with    *
 *     the src/libs/zbxcrypto/tls.c.                                          *
 *                                                                            *
 ******************************************************************************/
size_t DCget_psk_by_identity(const unsigned char *psk_identity, unsigned char *psk_buf, unsigned int *psk_usage)
{
	const ZBX_DC_PSK *psk_i;
	ZBX_DC_PSK psk_i_local;
	size_t psk_len = 0;
	unsigned char autoreg_psk_tmp[HOST_TLS_PSK_LEN_MAX];

	*psk_usage = 0;

	psk_i_local.tls_psk_identity = (const char *)psk_identity;

	RDLOCK_CACHE;

	/* Is it among host PSKs? */
	if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)))
	{
		psk_len = zbx_strlcpy((char *)psk_buf, psk_i->tls_psk, HOST_TLS_PSK_LEN_MAX);
		*psk_usage |= ZBX_PSK_FOR_HOST;
	}

	/* Does it match autoregistration PSK? */
	if (0 != strcmp(config->autoreg_psk_identity, (const char *)psk_identity))
	{
		UNLOCK_CACHE;
		return psk_len;
	}

	if (0 == *psk_usage) /* only as autoregistration PSK */
	{
		psk_len = zbx_strlcpy((char *)psk_buf, config->autoreg_psk, HOST_TLS_PSK_LEN_MAX);
		UNLOCK_CACHE;
		*psk_usage |= ZBX_PSK_FOR_AUTOREG;

		return psk_len;
	}

	/* the requested PSK is used as host PSK and as autoregistration PSK */
	zbx_strlcpy((char *)autoreg_psk_tmp, config->autoreg_psk, sizeof(autoreg_psk_tmp));

	UNLOCK_CACHE;

	if (0 == strcmp((const char *)psk_buf, (const char *)autoreg_psk_tmp))
	{
		*psk_usage |= ZBX_PSK_FOR_AUTOREG;
		return psk_len;
	}

	zabbix_log(LOG_LEVEL_WARNING, "host PSK and autoregistration PSK have the same identity \"%s\" but"
								  " different PSK values, autoregistration will not be allowed",
			   psk_identity);
	return psk_len;
}
#endif
/******************************************************************************
 *                                                                            *
 * Purpose:                                                                   *
 *     Copy autoregistration PSK identity and value from configuration cache  *
 *     into caller's buffers                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     psk_identity_buf     - [OUT] buffer for PSK identity                   *
 *     psk_identity_buf_len - [IN] buffer length for PSK identity             *
 *     psk_buf              - [OUT] buffer for PSK value                      *
 *     psk_buf_len          - [IN] buffer length for PSK value                *
 *                                                                            *
 * Comments: if autoregistration PSK is not configured then empty strings     *
 *           will be copied into buffers                                      *
 *                                                                            *
 ******************************************************************************/
void DCget_autoregistration_psk(char *psk_identity_buf, size_t psk_identity_buf_len,
								unsigned char *psk_buf, size_t psk_buf_len)
{
	RDLOCK_CACHE;

	zbx_strlcpy((char *)psk_identity_buf, config->autoreg_psk_identity, psk_identity_buf_len);
	zbx_strlcpy((char *)psk_buf, config->autoreg_psk, psk_buf_len);

	UNLOCK_CACHE;
}

void DCget_interface(DC_INTERFACE *dst_interface, const ZBX_DC_INTERFACE *src_interface)
{
	if (NULL != src_interface)
	{
		dst_interface->interfaceid = src_interface->interfaceid;
		zbx_strscpy(dst_interface->ip_orig, src_interface->ip);
		zbx_strscpy(dst_interface->dns_orig, src_interface->dns);
		zbx_strscpy(dst_interface->port_orig, src_interface->port);
		dst_interface->useip = src_interface->useip;
		dst_interface->type = src_interface->type;
		dst_interface->main = src_interface->main;
	}
	else
	{
		dst_interface->interfaceid = 0;
		*dst_interface->ip_orig = '\0';
		*dst_interface->dns_orig = '\0';
		*dst_interface->port_orig = '\0';
		dst_interface->useip = 1;
		dst_interface->type = INTERFACE_TYPE_UNKNOWN;
		dst_interface->main = 0;
	}

	dst_interface->addr = (1 == dst_interface->useip ? dst_interface->ip_orig : dst_interface->dns_orig);
	dst_interface->port = 0;
}

static ZBX_DC_INTERFACE *DChost_get_default_interface(u_int64_t hostid) {
	ZBX_DC_INTERFACE *iface = NULL;
	ZBX_DC_HOST *host;
	int i;

	if (NULL ==(host = zbx_hashset_search(&config->hosts, &hostid))) 
		return NULL;

	zbx_vector_ptr_t* ifaces = &host->interfaces_v;
	
	if (NULL ==  &host->interfaces_v) {
		return NULL;
	}
	
	for (i = 0; i < host->interfaces_v.values_num; i++) {
		iface = host->interfaces_v.values[i];
	
		if (iface->main) 
			return iface;
	}

	return iface;
}

static void DCget_item(DC_ITEM *dst_item, const ZBX_DC_ITEM *src_item)
{
	const ZBX_DC_LOGITEM *logitem;
	const ZBX_DC_SNMPITEM *snmpitem;
	const ZBX_DC_SNMPINTERFACE *snmp;
	const ZBX_DC_TRAPITEM *trapitem;
	const ZBX_DC_IPMIITEM *ipmiitem;
	const ZBX_DC_DBITEM *dbitem;
	const ZBX_DC_SSHITEM *sshitem;
	const ZBX_DC_TELNETITEM *telnetitem;
	const ZBX_DC_SIMPLEITEM *simpleitem;
	const ZBX_DC_JMXITEM *jmxitem;
	const ZBX_DC_CALCITEM *calcitem;
	const ZBX_DC_INTERFACE *dc_interface;
	const ZBX_DC_HTTPITEM *httpitem;
	const ZBX_DC_SCRIPTITEM *scriptitem;

	dst_item->type = src_item->type;
	dst_item->value_type = src_item->value_type;

	zbx_strscpy(dst_item->key_orig, src_item->key);
	//dst_item->lastlogsize = src_item->lastlogsize;

	dst_item->status = src_item->status;

	zbx_strscpy(dst_item->key_orig, src_item->key);

	dst_item->itemid = src_item->itemid;
	dst_item->flags = src_item->flags;
	dst_item->key = NULL;

	dst_item->delay = zbx_strdup(NULL, src_item->delay); /* not used, should be initialized */

	switch (src_item->value_type)
	{
	case ITEM_VALUE_TYPE_LOG:
		if (NULL != (logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&config->logitems,
																	&src_item->itemid)))
		{
			zbx_strscpy(dst_item->logtimefmt, logitem->logtimefmt);
		}
		else
			*dst_item->logtimefmt = '\0';
		break;
	}

	
	if (0 == src_item->interfaceid ) {
		DEBUG_ITEM(src_item->itemid, "Item has no interface set, finding default interface for the host %ld", src_item->hostid);
		dc_interface = DChost_get_default_interface(src_item->hostid);
	} else
		dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &src_item->interfaceid);
	
	DEBUG_ITEM(dst_item->itemid, "Found interface for the item %ld, ifaceid %ld: %p",src_item->itemid, src_item->interfaceid, dc_interface);
	DCget_interface(&dst_item->interface, dc_interface);

	switch (src_item->type)
	{
	case ITEM_TYPE_WORKER_SERVER:
		dst_item->params = zbx_strdup(NULL, src_item->params);
		break;
	case ITEM_TYPE_SNMP:
		snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &src_item->itemid);
		snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp,
														  &src_item->interfaceid);

		if (NULL != snmpitem && NULL != snmp)
		{
			zbx_strscpy(dst_item->snmp_community_orig, snmp->community);
			zbx_strscpy(dst_item->snmp_oid_orig, snmpitem->snmp_oid);
			zbx_strscpy(dst_item->snmpv3_securityname_orig, snmp->securityname);
			dst_item->snmpv3_securitylevel = snmp->securitylevel;
			zbx_strscpy(dst_item->snmpv3_authpassphrase_orig, snmp->authpassphrase);
			zbx_strscpy(dst_item->snmpv3_privpassphrase_orig, snmp->privpassphrase);
			dst_item->snmpv3_authprotocol = snmp->authprotocol;
			dst_item->snmpv3_privprotocol = snmp->privprotocol;
			zbx_strscpy(dst_item->snmpv3_contextname_orig, snmp->contextname);
			dst_item->snmp_version = snmp->version;
			dst_item->snmp_max_repetitions = snmp->max_repetitions;
		}
		else
		{
			*dst_item->snmp_community_orig = '\0';
			*dst_item->snmp_oid_orig = '\0';
			*dst_item->snmpv3_securityname_orig = '\0';
			dst_item->snmpv3_securitylevel = ZBX_ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV;
			*dst_item->snmpv3_authpassphrase_orig = '\0';
			*dst_item->snmpv3_privpassphrase_orig = '\0';
			dst_item->snmpv3_authprotocol = 0;
			dst_item->snmpv3_privprotocol = 0;
			*dst_item->snmpv3_contextname_orig = '\0';
			dst_item->snmp_version = ZBX_IF_SNMP_VERSION_2;
			dst_item->snmp_max_repetitions = 0;
		}

		dst_item->snmp_community = NULL;
		dst_item->snmp_oid = NULL;
		dst_item->snmpv3_securityname = NULL;
		dst_item->snmpv3_authpassphrase = NULL;
		dst_item->snmpv3_privpassphrase = NULL;
		dst_item->snmpv3_contextname = NULL;
		break;
	case ITEM_TYPE_TRAPPER:
		if (NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems,
																	  &src_item->itemid)))
		{
			zbx_strscpy(dst_item->trapper_hosts, trapitem->trapper_hosts);
		}
		else
		{
			*dst_item->trapper_hosts = '\0';
		}
		break;
	case ITEM_TYPE_IPMI:
		if (NULL != (ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems,
																	  &src_item->itemid)))
		{
			zbx_strscpy(dst_item->ipmi_sensor, ipmiitem->ipmi_sensor);
		}
		else
		{
			*dst_item->ipmi_sensor = '\0';
		}
		break;
	case ITEM_TYPE_DB_MONITOR:
		if (NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems,
																  &src_item->itemid)))
		{
			dst_item->params = zbx_strdup(NULL, dbitem->params);
			zbx_strscpy(dst_item->username_orig, dbitem->username);
			zbx_strscpy(dst_item->password_orig, dbitem->password);
		}
		else
		{
			dst_item->params = zbx_strdup(NULL, "");
			*dst_item->username_orig = '\0';
			*dst_item->password_orig = '\0';
		}
		dst_item->username = NULL;
		dst_item->password = NULL;

		break;
	case ITEM_TYPE_SSH:
		if (NULL != (sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems,
																	&src_item->itemid)))
		{
			dst_item->authtype = sshitem->authtype;
			zbx_strscpy(dst_item->username_orig, sshitem->username);
			zbx_strscpy(dst_item->publickey_orig, sshitem->publickey);
			zbx_strscpy(dst_item->privatekey_orig, sshitem->privatekey);
			zbx_strscpy(dst_item->password_orig, sshitem->password);
			dst_item->params = zbx_strdup(NULL, sshitem->params);
		}
		else
		{
			dst_item->authtype = 0;
			*dst_item->username_orig = '\0';
			*dst_item->publickey_orig = '\0';
			*dst_item->privatekey_orig = '\0';
			*dst_item->password_orig = '\0';
			dst_item->params = zbx_strdup(NULL, "");
		}
		dst_item->username = NULL;
		dst_item->publickey = NULL;
		dst_item->privatekey = NULL;
		dst_item->password = NULL;
		break;
	case ITEM_TYPE_HTTPAGENT:
		if (NULL != (httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems,
																	  &src_item->itemid)))
		{
			zbx_strscpy(dst_item->timeout_orig, httpitem->timeout);
			zbx_strscpy(dst_item->url_orig, httpitem->url);
			zbx_strscpy(dst_item->query_fields_orig, httpitem->query_fields);
			zbx_strscpy(dst_item->status_codes_orig, httpitem->status_codes);
			dst_item->follow_redirects = httpitem->follow_redirects;
			dst_item->post_type = httpitem->post_type;
			zbx_strscpy(dst_item->http_proxy_orig, httpitem->http_proxy);
			dst_item->headers = zbx_strdup(NULL, httpitem->headers);
			dst_item->retrieve_mode = httpitem->retrieve_mode;
			dst_item->request_method = httpitem->request_method;
			dst_item->output_format = httpitem->output_format;
			zbx_strscpy(dst_item->ssl_cert_file_orig, httpitem->ssl_cert_file);
			zbx_strscpy(dst_item->ssl_key_file_orig, httpitem->ssl_key_file);
			zbx_strscpy(dst_item->ssl_key_password_orig, httpitem->ssl_key_password);
			dst_item->verify_peer = httpitem->verify_peer;
			dst_item->verify_host = httpitem->verify_host;
			dst_item->authtype = httpitem->authtype;
			zbx_strscpy(dst_item->username_orig, httpitem->username);
			zbx_strscpy(dst_item->password_orig, httpitem->password);
			dst_item->posts = zbx_strdup(NULL, httpitem->posts);
			dst_item->allow_traps = httpitem->allow_traps;
			zbx_strscpy(dst_item->trapper_hosts, httpitem->trapper_hosts);
		}
		else
		{
			*dst_item->timeout_orig = '\0';
			*dst_item->url_orig = '\0';
			*dst_item->query_fields_orig = '\0';
			*dst_item->status_codes_orig = '\0';
			dst_item->follow_redirects = 0;
			dst_item->post_type = 0;
			*dst_item->http_proxy_orig = '\0';
			dst_item->headers = zbx_strdup(NULL, "");
			dst_item->retrieve_mode = 0;
			dst_item->request_method = 0;
			dst_item->output_format = 0;
			*dst_item->ssl_cert_file_orig = '\0';
			*dst_item->ssl_key_file_orig = '\0';
			*dst_item->ssl_key_password_orig = '\0';
			dst_item->verify_peer = 0;
			dst_item->verify_host = 0;
			dst_item->authtype = 0;
			*dst_item->username_orig = '\0';
			*dst_item->password_orig = '\0';
			dst_item->posts = zbx_strdup(NULL, "");
			dst_item->allow_traps = 0;
			*dst_item->trapper_hosts = '\0';
		}
		dst_item->timeout = NULL;
		dst_item->url = NULL;
		dst_item->query_fields = NULL;
		dst_item->status_codes = NULL;
		dst_item->http_proxy = NULL;
		dst_item->ssl_cert_file = NULL;
		dst_item->ssl_key_file = NULL;
		dst_item->ssl_key_password = NULL;
		dst_item->username = NULL;
		dst_item->password = NULL;
		break;
	case ITEM_TYPE_SCRIPT:
		if (NULL != (scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems,
																		  &src_item->itemid)))
		{
			int i;
			struct zbx_json json;

			zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

			zbx_strscpy(dst_item->timeout_orig, scriptitem->timeout);
			dst_item->params = zbx_strdup(NULL, scriptitem->script);

			for (i = 0; i < scriptitem->params.values_num; i++)
			{
				zbx_dc_scriptitem_param_t *params =
					(zbx_dc_scriptitem_param_t *)(scriptitem->params.values[i]);

				zbx_json_addstring(&json, params->name, params->value, ZBX_JSON_TYPE_STRING);
			}

			dst_item->script_params = zbx_strdup(NULL, json.buffer);
			zbx_json_free(&json);
		}
		else
		{
			*dst_item->timeout_orig = '\0';
			dst_item->params = zbx_strdup(NULL, "");
			dst_item->script_params = zbx_strdup(NULL, "");
		}

		dst_item->timeout = NULL;
		break;
	case ITEM_TYPE_TELNET:
		if (NULL != (telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems,
																		  &src_item->itemid)))
		{
			zbx_strscpy(dst_item->username_orig, telnetitem->username);
			zbx_strscpy(dst_item->password_orig, telnetitem->password);
			dst_item->params = zbx_strdup(NULL, telnetitem->params);
		}
		else
		{
			*dst_item->username_orig = '\0';
			*dst_item->password_orig = '\0';
			dst_item->params = zbx_strdup(NULL, "");
		}
		dst_item->username = NULL;
		dst_item->password = NULL;
		break;
	case ITEM_TYPE_SIMPLE:
		if (NULL != (simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems,
																		  &src_item->itemid)))
		{
			zbx_strscpy(dst_item->username_orig, simpleitem->username);
			zbx_strscpy(dst_item->password_orig, simpleitem->password);
		}
		else
		{
			*dst_item->username_orig = '\0';
			*dst_item->password_orig = '\0';
		}
		dst_item->username = NULL;
		dst_item->password = NULL;
		break;
	case ITEM_TYPE_JMX:
		if (NULL != (jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems,
																	&src_item->itemid)))
		{
			zbx_strscpy(dst_item->username_orig, jmxitem->username);
			zbx_strscpy(dst_item->password_orig, jmxitem->password);
			zbx_strscpy(dst_item->jmx_endpoint_orig, jmxitem->jmx_endpoint);
		}
		else
		{
			*dst_item->username_orig = '\0';
			*dst_item->password_orig = '\0';
			*dst_item->jmx_endpoint_orig = '\0';
		}
		dst_item->username = NULL;
		dst_item->password = NULL;
		dst_item->jmx_endpoint = NULL;
		break;
	case ITEM_TYPE_CALCULATED:
		if (NULL != (calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems,
																	  &src_item->itemid)))
		{
			dst_item->params = zbx_strdup(NULL, calcitem->params);
			dst_item->formula_bin = dup_serialized_expression(calcitem->formula_bin);
		}
		else
		{
			dst_item->params = zbx_strdup(NULL, "");
			dst_item->formula_bin = NULL;
		}

		break;
	default:
		/* nothing to do */;
	}
}

void DCconfig_clean_items(DC_ITEM *items, int *errcodes, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
	{
		if (NULL != errcodes && SUCCEED != errcodes[i])
			continue;

		switch (items[i].type)
		{
		case ITEM_TYPE_HTTPAGENT:
			zbx_free(items[i].headers);
			zbx_free(items[i].posts);
			break;
		case ITEM_TYPE_SCRIPT:
			zbx_free(items[i].script_params);
			ZBX_FALLTHROUGH;
		case ITEM_TYPE_DB_MONITOR:
		case ITEM_TYPE_SSH:
		case ITEM_TYPE_TELNET:
			zbx_free(items[i].params);
			break;
		case ITEM_TYPE_CALCULATED:
			zbx_free(items[i].params);
			zbx_free(items[i].formula_bin);
			break;
		case ITEM_TYPE_WORKER_SERVER:
			zbx_free(items[i].params);
			break;
		}
	
		zbx_free(items[i].delay);
	}
}

void DCget_function(DC_FUNCTION *dst_function, const ZBX_DC_FUNCTION *src_function)
{
	size_t sz_function, sz_parameter;

	dst_function->functionid = src_function->functionid;
	dst_function->triggerid = src_function->triggerid;
	dst_function->itemid = src_function->itemid;
	dst_function->type = src_function->type;

	sz_function = strlen(src_function->function) + 1;
	sz_parameter = strlen(src_function->parameter) + 1;
	dst_function->function = (char *)zbx_malloc(NULL, sz_function + sz_parameter);
	dst_function->parameter = dst_function->function + sz_function;
	memcpy(dst_function->function, src_function->function, sz_function);
	memcpy(dst_function->parameter, src_function->parameter, sz_parameter);
}

void DCget_trigger(DC_TRIGGER *dst_trigger, const ZBX_DC_TRIGGER *src_trigger, unsigned int flags)
{
	int i;

	dst_trigger->triggerid = src_trigger->triggerid;
	dst_trigger->description = strdup_null_safe(src_trigger->description);
	dst_trigger->error = strdup_null_safe(src_trigger->error);
	dst_trigger->timespec.sec = 0;
	dst_trigger->timespec.ns = 0;
	dst_trigger->priority = src_trigger->priority;
	dst_trigger->type = src_trigger->type;
	// dst_trigger->value = glb_state_trigger_get_value(src_trigger->triggerid);//src_trigger->value;
	// dst_trigger->state = src_trigger->state;
	dst_trigger->new_value = TRIGGER_VALUE_UNKNOWN;
	// dst_trigger->lastchange = src_trigger->lastchange;
	glb_state_trigger_get_value_lastchange(src_trigger->triggerid, &dst_trigger->value, &dst_trigger->lastchange);
	dst_trigger->topoindex = src_trigger->topoindex;
	dst_trigger->status = src_trigger->status;
	dst_trigger->recovery_mode = src_trigger->recovery_mode;
	dst_trigger->correlation_mode = src_trigger->correlation_mode;
	dst_trigger->correlation_tag = strdup_null_safe(src_trigger->correlation_tag);
	dst_trigger->opdata = strdup_null_safe(src_trigger->opdata);
	dst_trigger->event_name = strdup_null_safe(src_trigger->event_name);
	dst_trigger->flags = 0;
	dst_trigger->new_error = NULL;
	dst_trigger->expression = strdup_null_safe(src_trigger->expression);
	dst_trigger->recovery_expression = strdup_null_safe(src_trigger->recovery_expression);
	dst_trigger->expression_bin = dup_serialized_expression(src_trigger->expression_bin);
	dst_trigger->recovery_expression_bin = dup_serialized_expression(src_trigger->recovery_expression_bin);

	dst_trigger->eval_ctx = NULL;
	dst_trigger->eval_ctx_r = NULL;

	zbx_vector_ptr_create(&dst_trigger->tags);

	if (0 != src_trigger->tags.values_num)
	{
		zbx_vector_ptr_reserve(&dst_trigger->tags, src_trigger->tags.values_num);

		for (i = 0; i < src_trigger->tags.values_num; i++)
		{
			const zbx_dc_trigger_tag_t *dc_trigger_tag = (const zbx_dc_trigger_tag_t *)
															 src_trigger->tags.values[i];
			zbx_tag_t *tag;

			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
			tag->tag = zbx_strdup(NULL, dc_trigger_tag->tag);
			tag->value = zbx_strdup(NULL, dc_trigger_tag->value);

			zbx_vector_ptr_append(&dst_trigger->tags, tag);
		}
	}

	zbx_vector_uint64_create(&dst_trigger->itemids);

	if (0 != (flags & ZBX_TRIGGER_GET_ITEMIDS) && NULL != src_trigger->itemids)
	{
		zbx_uint64_t *itemid;

		for (itemid = src_trigger->itemids; 0 != *itemid; itemid++)
			;

		zbx_vector_uint64_append_array(&dst_trigger->itemids, src_trigger->itemids,
									   (int)(itemid - src_trigger->itemids));
	}
}

void zbx_free_item_tag(zbx_item_tag_t *item_tag)
{
	zbx_free(item_tag->tag.tag);
	zbx_free(item_tag->tag.value);
	zbx_free(item_tag);
}

void DCclean_trigger(DC_TRIGGER *trigger)
{
	zbx_free(trigger->new_error);
	zbx_free(trigger->error);
	zbx_free(trigger->expression);
	zbx_free(trigger->recovery_expression);
	zbx_free(trigger->description);
	zbx_free(trigger->correlation_tag);
	zbx_free(trigger->opdata);
	zbx_free(trigger->event_name);
	zbx_free(trigger->expression_bin);
	zbx_free(trigger->recovery_expression_bin);

	zbx_vector_ptr_clear_ext(&trigger->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&trigger->tags);

	if (NULL != trigger->eval_ctx)
	{
		zbx_eval_clear(trigger->eval_ctx);
		zbx_free(trigger->eval_ctx);
	}

	if (NULL != trigger->eval_ctx_r)
	{
		zbx_eval_clear(trigger->eval_ctx_r);
		zbx_free(trigger->eval_ctx_r);
	}

	zbx_vector_uint64_destroy(&trigger->itemids);
}

int DCconfig_get_itemid_by_item_key_hostid(u_int64_t hostid, char *key, u_int64_t *itemid)
{

	const ZBX_DC_ITEM *dc_item;

	RDLOCK_CACHE;
	if (NULL == (dc_item = DCfind_item(hostid, key)))
	{
		LOG_DBG("Couldn't find itemd id for key '%ld'->'%s'", hostid, key);
		UNLOCK_CACHE;
		return FAIL;
	}
	*itemid = dc_item->itemid;

	UNLOCK_CACHE;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: locate item in configuration cache by host and key                *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to array of DC_ITEM structures        *
 *             keys     - [IN] list of item keys with host names              *
 *             errcodes - [OUT] SUCCEED if record located and FAIL otherwise  *
 *             num      - [IN] number of elements in items, keys, errcodes    *
 *                                                                            *
 ******************************************************************************/
void DCconfig_get_items_by_keys(DC_ITEM *items, zbx_host_key_t *keys, int *errcodes, size_t num)
{
	size_t i;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_host = DCfind_host(keys[i].host)))
		{
			LOG_DBG("Couldn't find host id for name %s", keys[i].host);
			errcodes[i] = FAIL;
			continue;
		}
		else
		{

			if (NULL == (dc_item = DCfind_item(dc_host->hostid, keys[i].key)))
			{
				LOG_DBG("Couldn't find itemd id for key '%s'->'%s'", keys[i].host, keys[i].key);

				errcodes[i] = FAIL;
				continue;
			}
		}

		DCget_host(&items[i].host, dc_host);
		DCget_item(&items[i], dc_item);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/*****************************************************
 * returns host and item ids for the symbolic hosname
 * and item names
 * ***************************************************/
int DCconfig_get_itemid_by_key(zbx_host_key_t *key, zbx_uint64_pair_t *host_item_ids)
{
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;
	int errcode = SUCCEED;

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_host(key->host)))
	{
		LOG_DBG("Couldn't find host id for name %s", key->host);
		errcode = FAIL;
	}
	else
	{
		host_item_ids->first = dc_host->hostid;

		if (NULL == (dc_item = DCfind_item(dc_host->hostid, key->key)))
		{
			LOG_DBG("Couldn't find itemd id for key '%s'->'%s'", key->host, key->key);
			errcode = FAIL;
		}
		else
			host_item_ids->second = dc_item->itemid;
	}

	UNLOCK_CACHE;
	return errcode;
}

int DCconfig_get_hostid_by_name(const char *host, zbx_uint64_t *hostid)
{
	const ZBX_DC_HOST *dc_host;
	int ret;

	RDLOCK_CACHE;

	if (NULL != (dc_host = DCfind_host(host)))
	{
		*hostid = dc_host->hostid;
		ret = SUCCEED;
	}
	else
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get item with specified ID                                        *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to DC_ITEM structures                 *
 *             itemids  - [IN] array of item IDs                              *
 *             errcodes - [OUT] SUCCEED if item found, otherwise FAIL         *
 *             num      - [IN] number of elements                             *
 *                                                                            *
 ******************************************************************************/
void DCconfig_get_items_by_itemids(DC_ITEM *items, const zbx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t i;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids[i])) ||
			NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&items[i].host, dc_host);
		DCget_item(&items[i], dc_item);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

int DCconfig_get_active_items_count_by_hostid(zbx_uint64_t hostid)
{
	const ZBX_DC_HOST *dc_host;
	int i, num = 0;

	RDLOCK_CACHE;

	if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
	{
		for (i = 0; i < dc_host->items.values_num; i++)
		{
			if (ITEM_TYPE_ZABBIX_ACTIVE == dc_host->items.values[i]->type)
				num++;
		}
	}

	UNLOCK_CACHE;

	return num;
}

void DCconfig_get_active_items_by_hostid(DC_ITEM *items, zbx_uint64_t hostid, int *errcodes, size_t num)
{
	const ZBX_DC_HOST *dc_host;
	size_t i, j = 0;

	RDLOCK_CACHE;

	if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)) &&
		0 != dc_host->items.values_num)
	{
		for (i = 0; i < (size_t)dc_host->items.values_num && j < num; i++)
		{
			const ZBX_DC_ITEM *dc_item;

			dc_item = dc_host->items.values[i];

			if (ITEM_TYPE_ZABBIX_ACTIVE != dc_item->type)
				continue;

			DCget_item(&items[j], dc_item);
			errcodes[j++] = SUCCEED;
		}

		if (0 != j)
		{
			DCget_host(&items[0].host, dc_host);

			for (i = 1; i < j; i++)
				items[i].host = items[0].host;
		}
	}

	UNLOCK_CACHE;

	for (; j < num; j++)
		errcodes[j] = FAIL;
}

static void dc_preproc_dump(zbx_hashset_t *items)
{
	zbx_hashset_iter_t iter;
	zbx_preproc_item_t *item;
	int i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_hashset_iter_reset(items, &iter);
	while (NULL != (item = (zbx_preproc_item_t *)zbx_hashset_iter_next(&iter)))
	{
		zabbix_log(LOG_LEVEL_TRACE, "itemid:" ZBX_FS_UI64 " type:%u value_type:%u revision:" ZBX_FS_UI64,
				   item->itemid, item->type, item->value_type, item->preproc_revision);

		zabbix_log(LOG_LEVEL_TRACE, "  dependent items:");
		for (i = 0; i < item->dep_itemids_num; i++)
		{
			zabbix_log(LOG_LEVEL_TRACE, "    depitemid:" ZBX_FS_UI64 "," ZBX_FS_UI64,
					   item->dep_itemids[i].first, item->dep_itemids[i].second);
		}

		zabbix_log(LOG_LEVEL_TRACE, "  preprocessing steps:");
		for (i = 0; i < item->preproc_ops_num; i++)
		{
			zabbix_log(LOG_LEVEL_TRACE, "    type:%u params:%s error_handler:%u error_handler_params:%s",
					   item->preproc_ops[i].type, ZBX_NULL2EMPTY_STR(item->preproc_ops[i].params),
					   item->preproc_ops[i].error_handler,
					   ZBX_NULL2EMPTY_STR(item->preproc_ops[i].error_handler_params));
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: sync item preprocessing steps with preprocessing manager cache,   *
 *          updating preprocessing revision if any changes were detected      *
 *                                                                            *
 ******************************************************************************/
static void	dc_preproc_sync_preprocitem(zbx_pp_item_preproc_t *preproc, zbx_uint64_t hostid,
		const ZBX_DC_PREPROCITEM *preprocitem)
{
	preproc->steps = (zbx_pp_step_t *)zbx_malloc(NULL, sizeof(zbx_pp_step_t) *
			(size_t)preprocitem->preproc_ops.values_num);

	for (int i = 0; i < preprocitem->preproc_ops.values_num; i++)
	{
		zbx_dc_preproc_op_t	*op = (zbx_dc_preproc_op_t *)preprocitem->preproc_ops.values[i];

		preproc->steps[i].type = op->type;
		preproc->steps[i].error_handler = op->error_handler;

		preproc->steps[i].params = dc_expand_user_macros_dyn(op->params, &hostid, 1, ZBX_MACRO_ENV_NONSECURE);
		preproc->steps[i].error_handler_params = zbx_strdup(NULL, op->error_handler_params);
	}

	preproc->steps_num = preprocitem->preproc_ops.values_num;
}

/******************************************************************************
 *                                                                            *
 * Purpose: sync mater-dependent item links                                   *
 *                                                                            *
 ******************************************************************************/
static void	dc_preproc_sync_masteritem(zbx_pp_item_preproc_t *preproc, const ZBX_DC_MASTERITEM *masteritem)
{
	preproc->dep_itemids = (zbx_uint64_t *)zbx_malloc(NULL,
			sizeof(zbx_uint64_t) * (size_t)masteritem->dep_itemids.values_num);

	for (int i = 0; i < masteritem->dep_itemids.values_num; i++)
		preproc->dep_itemids[i] = masteritem->dep_itemids.values[i].first;

	qsort(preproc->dep_itemids, (size_t)preproc->dep_itemids_num, sizeof(zbx_uint64_t),
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	preproc->dep_itemids_num = masteritem->dep_itemids.values_num;
}

/******************************************************************************
 *                                                                            *
 * Purpose: compare item preprocessing data                                   *
 *                                                                            *
 * Return value: SUCCEED - the item preprocessing data matches                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	dc_preproc_compare(const zbx_pp_item_preproc_t *pp1, const zbx_pp_item_preproc_t *pp2)
{
	if (pp1->type != pp2->type)
		return FAIL;

	if (pp1->value_type != pp2->value_type)
		return FAIL;

	if (pp1->flags != pp2->flags)
		return FAIL;

	if (pp1->dep_itemids_num != pp2->dep_itemids_num)
		return FAIL;

	if (0 != memcmp(pp1->dep_itemids, pp2->dep_itemids, (size_t)pp1->dep_itemids_num * sizeof(zbx_uint64_t)))
		return FAIL;

	if (pp1->steps_num != pp2->steps_num)
		return FAIL;

	for (int i = 0; i < pp1->steps_num; i++)
	{
		if (pp1->steps[i].type != pp2->steps[i].type)
			return FAIL;

		if (pp1->steps[i].error_handler != pp2->steps[i].error_handler)
			return FAIL;

		if (0 != zbx_strcmp_null(pp1->steps[i].params, pp2->steps[i].params))
			return FAIL;

		if (0 != zbx_strcmp_null(pp1->steps[i].error_handler_params, pp2->steps[i].error_handler_params))
			break;
	}

	return SUCCEED;
}


// /******************************************************************************
//  *                                                                            *
//  * Purpose: sync mater-dependent item links                                   *
//  *                                                                            *
//  ******************************************************************************/
// static void dc_preproc_sync_masteritem(zbx_preproc_item_t *item, const ZBX_DC_MASTERITEM *masteritem)
// {
// 	DEBUG_ITEM(item->itemid, "Syncing dependant items");
// 	//if there is no link to master item info, cleaning 
// 	//dependend items vector
// 	if (NULL == masteritem)
// 	{
// 		zbx_free(item->dep_itemids);
// 		item->dep_itemids_num = 0;
// 		DEBUG_ITEM(item->itemid, "No dependant items found");
// 		return;
// 	}

// 	DEBUG_ITEM(item->itemid, "Found %d depend items", masteritem->dep_itemids.values_num);
// 	//if there are more values in master item then in the item,
// 	//reallocating the array 
// 	if (masteritem->dep_itemids.values_num > item->dep_itemids_num)
// 	{
// 		item->dep_itemids = (zbx_uint64_pair_t *)zbx_realloc(item->dep_itemids,
// 															 sizeof(zbx_uint64_pair_t) * masteritem->dep_itemids.values_num);
// 	}
	
// 	//copying all the dependancy data
// 	memcpy(item->dep_itemids, masteritem->dep_itemids.values,
// 		   sizeof(zbx_uint64_pair_t) * masteritem->dep_itemids.values_num);
	
// 	item->dep_itemids_num = masteritem->dep_itemids.values_num;

// 	if (DC_get_debug_item() == item->itemid) {
// 		int i;
// 		for (i = 0; i < item->dep_itemids_num; i++)
// 			DEBUG_ITEM(item->itemid, "PREPROCSYNC: Adding for item %ld dep items info pair: (%ld, %ld)", item->itemid,
// 				item->dep_itemids[i].first,item->dep_itemids[i].second);
// 	}
// }

//syncs preprocessing conf for pp items in the hash items from dc_item 

static void debug_out_pp_item(zbx_pp_item_preproc_t	*pp_item) {

}

static void	dc_preproc_sync_item(zbx_hashset_t *items, ZBX_DC_ITEM *dc_item, zbx_uint64_t revision)
{
	zbx_pp_item_t		*pp_item;
	zbx_pp_item_preproc_t	*preproc;
	DEBUG_ITEM(dc_item->itemid, "Syncing preproc configuration for the item");

	if (NULL == (pp_item = (zbx_pp_item_t *)zbx_hashset_search(items, &dc_item->itemid)))
	{
		zbx_pp_item_t	pp_item_local = {.itemid = dc_item->itemid, .hostid = dc_item->hostid};
		DEBUG_ITEM(dc_item->itemid, "Syncing preproc configuration: created config for the item");
		pp_item = (zbx_pp_item_t *)zbx_hashset_insert(items, &pp_item_local, sizeof(pp_item_local));
	}

	preproc = zbx_pp_item_preproc_create(dc_item->type, dc_item->value_type, dc_item->flags);
	pp_item->revision = revision;

	if (NULL != dc_item->master_item)
		dc_preproc_sync_masteritem(preproc, dc_item->master_item);

	if (NULL != dc_item->preproc_item)
		dc_preproc_sync_preprocitem(preproc, dc_item->hostid, dc_item->preproc_item);

	if (NULL != pp_item->preproc)
	{
		if (SUCCEED == dc_preproc_compare(preproc, pp_item->preproc))
		{
			zbx_pp_item_preproc_release(preproc);
			return;
		}

		zbx_pp_item_preproc_release(pp_item->preproc);
	}

	for (int i = 0; i < preproc->steps_num; i++)
	{
		if (SUCCEED == zbx_pp_preproc_has_history(preproc->steps[i].type))
		{
			preproc->history_num++;
			preproc->mode = ZBX_PP_PROCESS_SERIAL;
		}
	}
		
	pp_item->preproc = preproc;

	if (DC_get_debug_item() == pp_item->itemid) {
		int i = 0;
		char buffer[MAX_STRING_LEN];
		size_t printed=0;

		DEBUG_ITEM(pp_item->itemid,"Synced preproc config to preprocessor: steps %d, depends %d", 
					pp_item->preproc->steps_num, pp_item->preproc->dep_itemids_num);

		for (i = 0; i < pp_item->preproc->steps_num; i++) {
			DEBUG_ITEM(pp_item->itemid, "Step %d type %d, params '%s'", i,
							pp_item->preproc->steps[i].type, pp_item->preproc->steps[i].params);
		}

		for (i = 0; i < pp_item->preproc->dep_itemids_num; i++) {
			printed += zbx_snprintf(buffer + printed, MAX_STRING_LEN, "%ld ", pp_item->preproc->dep_itemids[i]);
		}

		if (pp_item->preproc->dep_itemids_num > 0) 
			DEBUG_ITEM(pp_item->itemid,"Dependant items: [ %s]",buffer);

 	}

}


// static void dc_preproc_sync_item(zbx_hashset_t *items, ZBX_DC_ITEM *dc_item, zbx_uint64_t revision)
// {
// 	zbx_preproc_item_t *pp_item;
// 	DEBUG_ITEM(dc_item->itemid, "Syncing preproc configuration for the item");
// 	//creating the new item if not existed
// 	if (NULL == (pp_item = (zbx_preproc_item_t *)zbx_hashset_search(items, &dc_item->itemid)))
// 	{
// 		DEBUG_ITEM(dc_item->itemid, "Syncing preproc configuration: created config for the item");
// 		zbx_preproc_item_t pp_item_local = {.itemid = dc_item->itemid};
		
// 		pp_item = (zbx_preproc_item_t *)zbx_hashset_insert(items, &pp_item_local, sizeof(pp_item_local));
// 		pp_item->hostid = dc_item->hostid;
// 		pp_item->preproc_revision = revision;
// 	}

// 	pp_item->type = dc_item->type;
// 	pp_item->value_type = dc_item->value_type;
// 	pp_item->revision = revision;
	
// 	//syncing items dependancy information
// 	dc_preproc_sync_masteritem(pp_item, dc_item->master_item);
// 	//syncing items preprocessing steps
// 	dc_preproc_sync_preprocitem(pp_item, dc_item->preproc_item, revision);
// }

static void	dc_preproc_add_item_rec(ZBX_DC_ITEM *dc_item, zbx_vector_dc_item_ptr_t *items_sync)
{
	zbx_vector_dc_item_ptr_append(items_sync, dc_item);

	if (NULL != dc_item->master_item)
	{
		int	i;

		for (i = 0; i < dc_item->master_item->dep_itemids.values_num; i++)
		{
			ZBX_DC_ITEM	*dep_item;

			if (NULL == (dep_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items,
					&dc_item->master_item->dep_itemids.values[i].first)) ||
					ITEM_STATUS_ACTIVE != dep_item->status)
			{
				continue;
			}

			dc_preproc_add_item_rec(dep_item, items_sync);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: get preprocessable items:                                         *
 *              * items with preprocessing steps                              *
 *              * items with dependent items                                  *
 *              * internal items                                              *
 *                                                                            *
 * Parameters: items       - [IN/OUT] hashset with DC_ITEMs                   *
 *             timestamp   - [IN/OUT] timestamp of a last update              *
 *                                                                            *
 ******************************************************************************/
void DCconfig_get_preprocessable_items(zbx_hashset_t *items, zbx_uint64_t *revision, int manager_num)
{
	ZBX_DC_HOST *dc_host;
	zbx_pp_item_t			*pp_item;
	zbx_hashset_iter_t iter;
	int i;
	zbx_uint64_t global_revision = *revision;
	zbx_vector_dc_item_ptr_t items_sync;

	if (config->revision.config == *revision)
		return;

	zbx_vector_dc_item_ptr_create(&items_sync);
	zbx_vector_dc_item_ptr_reserve(&items_sync, 100);

	RDLOCK_CACHE;
	//global revision check
	if (SUCCEED != um_cache_get_host_revision(config->um_cache, 0, &global_revision))
		global_revision = 0;

	zbx_hashset_iter_reset(&config->hosts, &iter);
	//all hosts iteration
	while (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		//alive check
		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;
		//only the process config fetching
		if (manager_num >= 0  && dc_host->hostid % CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR] != manager_num - 1)
			continue;
		
		//iterating on all hosts items
		for (i = 0; i < dc_host->items.values_num; i++)
		{
			ZBX_DC_ITEM *dc_item = dc_host->items.values[i];
			//item alive check
			if (ITEM_STATUS_ACTIVE != dc_item->status || ITEM_TYPE_DEPENDENT == dc_item->type)
				continue;

			if (NULL == dc_item->preproc_item && NULL == dc_item->master_item &&
					ITEM_TYPE_INTERNAL != dc_item->type &&
					ZBX_FLAG_DISCOVERY_RULE != dc_item->flags)
			{
				continue;
			}
				
			if (0 != dc_host->proxy_hostid &&
				FAIL == is_item_processed_by_server(dc_item->type, dc_item->key))
				continue;
			
			dc_preproc_add_item_rec(dc_item, &items_sync);
			
			//just a log
			if (manager_num >= 0 && dc_item->master_itemid > 0)  
				LOG_INF("Processing item %ld, has master item %ld", dc_item->itemid, dc_item->master_itemid);
			
		}

		/* don't check host macro revision if the host does not have locally pre-processable items */
		if (0 == items_sync.values_num)
			continue;
		
		//preprocessor config revision is equal or more recent the config revision
		if (*revision >= global_revision && *revision >= dc_host->revision)
		{
			zbx_uint64_t macro_revision = *revision;
			
			//host has a new revision
			if (SUCCEED != um_cache_get_host_revision(config->um_cache, dc_host->hostid, &macro_revision) ||
				*revision >= macro_revision)
			{
				//setting the new reivisions to be equal to the config ones
				for (i = 0; i < items_sync.values_num; i++)
					if (NULL != (pp_item = (zbx_pp_item_t *)zbx_hashset_search(items, &items_sync.values[i]->itemid)))
						pp_item->revision = config->revision.config;
				//cleaning the vector - there is no need to update the host items preproc config	
				zbx_vector_dc_item_ptr_clear(&items_sync);
			}
		}
		
		//the actual config is copied here
		for (i = 0; i < items_sync.values_num; i++)
			dc_preproc_sync_item(items, items_sync.values[i], config->revision.config);

		zbx_vector_dc_item_ptr_clear(&items_sync);
	}

	*revision = config->revision.config;

	UNLOCK_CACHE;

	/* remove items without preprocessing */

	zbx_hashset_iter_reset(items, &iter);
	while (NULL != (pp_item = (zbx_pp_item_t *)zbx_hashset_iter_next(&iter)))
	{
		if (pp_item->revision == *revision)
			continue;

		zbx_hashset_iter_remove(&iter);
	}

	zbx_vector_dc_item_ptr_destroy(&items_sync);
}

void DCconfig_get_hosts_by_itemids(DC_HOST *hosts, const zbx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t i;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids[i])) ||
			NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&hosts[i], dc_host);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

void DCconfig_get_hosts_by_hostids(DC_HOST *hosts, const zbx_uint64_t *hostids, int *errcodes, int num)
{
	int i;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostids[i])))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_host(&hosts[i], dc_host);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

int DCconfig_trigger_exists(zbx_uint64_t triggerid)
{
	int ret = SUCCEED;

	RDLOCK_CACHE;

	if (NULL == zbx_hashset_search(&config->triggers, &triggerid))
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

void DCconfig_get_triggers_by_triggerids(DC_TRIGGER *triggers, const zbx_uint64_t *triggerids, int *errcode,
										 size_t num)
{
	size_t i;
	const ZBX_DC_TRIGGER *dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{

		if (NULL == (dc_trigger = (const ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerids[i])))
		{
			errcode[i] = FAIL;
			continue;
		}

		DCget_trigger(&triggers[i], dc_trigger, ZBX_TRIGGER_GET_DEFAULT);
		errcode[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get functions by IDs                                              *
 *                                                                            *
 * Parameters: functions   - [OUT] pointer to DC_FUNCTION structures          *
 *             functionids - [IN] array of function IDs                       *
 *             errcodes    - [OUT] SUCCEED if item found, otherwise FAIL      *
 *             num         - [IN] number of elements                          *
 *                                                                            *
 ******************************************************************************/
void DCconfig_get_functions_by_functionids(DC_FUNCTION *functions, zbx_uint64_t *functionids, int *errcodes,
										   size_t num)
{
	size_t i;
	const ZBX_DC_FUNCTION *dc_function;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionids[i])))
		{
			errcodes[i] = FAIL;
			continue;
		}

		DCget_function(&functions[i], dc_function);
		errcodes[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

void DCconfig_clean_functions(DC_FUNCTION *functions, int *errcodes, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		zbx_free(functions[i].function);
	}
}

void DCconfig_clean_triggers(DC_TRIGGER *triggers, int *errcodes, size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		DCclean_trigger(&triggers[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: Lock triggers for specified items so that multiple processes do   *
 *          not process one trigger simultaneously. Otherwise, this leads to  *
 *          problems like multiple successive OK events or escalations being  *
 *          started and not cancelled, because they are not seen in parallel  *
 *          transactions.                                                     *
 *                                                                            *
 * Parameters: history_items - [IN/OUT] list of history items history syncer  *
 *                                    wishes to take for processing; on       *
 *                                    output, the item locked field is set    *
 *                                    to 0 if the corresponding item cannot   *
 *                                    be taken                                *
 *             triggerids  - [OUT] list of trigger IDs that this function has *
 *                                 locked for processing; unlock those using  *
 *                                 DCconfig_unlock_triggers() function        *
 *                                                                            *
 * Comments: This does not solve the problem fully (e.g., ZBX-7484). There is *
 *           a significant time period between the place where we lock the    *
 *           triggers and the place where we process them. So it could happen *
 *           that a configuration cache update happens after we have locked   *
 *           the triggers and it turns out that in the updated configuration  *
 *           there is a new trigger for two of the items that two different   *
 *           history syncers have taken for processing. In that situation,    *
 *           the problem we are solving here might still happen. However,     *
 *           locking triggers makes this problem much less likely and only in *
 *           case configuration changes. On a stable configuration, it should *
 *           work without any problems.                                       *
 *                                                                            *
 * Return value: the number of items available for processing (unlocked).     *
 *                                                                            *
 ******************************************************************************/
// int DCconfig_lock_triggers_by_history_items(zbx_vector_ptr_t *history_items, zbx_vector_uint64_t *triggerids)
// {
// 	int i, j, locked_num = 0;
// 	const ZBX_DC_ITEM *dc_item;
// 	ZBX_DC_TRIGGER *dc_trigger;
// 	zbx_hc_item_t *history_item;

// 	WRLOCK_CACHE;

// 	for (i = 0; i < history_items->values_num; i++)
// 	{
// 		history_item = (zbx_hc_item_t *)history_items->values[i];

// 		if (0 != (ZBX_DC_FLAG_NOVALUE & history_item->tail->flags))
// 			continue;

// 		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &history_item->itemid)))
// 			continue;

// 		if (NULL == dc_item->triggers)
// 			continue;

// 		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
// 		{
// 			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
// 				continue;

// 			if (1 == dc_trigger->locked)
// 			{
// 				locked_num++;
// 				history_item->status = ZBX_HC_ITEM_STATUS_BUSY;
// 				goto next;
// 			}
// 		}

// 		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
// 		{
// 			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
// 				continue;

// 			dc_trigger->locked = 1;
// 			DEBUG_TRIGGER(dc_trigger->triggerid, "Triggerid is locked");
// 			DEBUG_ITEM(dc_item->itemid, "Triggerd %ld is locked for the item history processing", dc_trigger->triggerid);
// 			zbx_vector_uint64_append(triggerids, dc_trigger->triggerid);
// 		}
// 	next:;
// 	}

// 	UNLOCK_CACHE;

// 	return history_items->values_num - locked_num;
// }

/******************************************************************************
 *                                                                            *
 * Purpose: Lock triggers so that multiple processes do not process one       *
 *          trigger simultaneously.                                           *
 *                                                                            *
 * Parameters: triggerids_in  - [IN] ids of triggers to lock                  *
 *             triggerids_out - [OUT] ids of locked triggers                  *
 *                                                                            *
 ******************************************************************************/
void DCconfig_lock_triggers_by_triggerids(zbx_vector_uint64_t *triggerids_in, zbx_vector_uint64_t *triggerids_out)
{
	int i;
	ZBX_DC_TRIGGER *dc_trigger;

	if (0 == triggerids_in->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < triggerids_in->values_num; i++)
	{
		if (NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers,
																	   &triggerids_in->values[i])))
		{
			continue;
		}

		if (1 == dc_trigger->locked)
			continue;

		dc_trigger->locked = 1;
		DEBUG_TRIGGER(dc_trigger->triggerid, "Triggerid is locked");

		zbx_vector_uint64_append(triggerids_out, dc_trigger->triggerid);
	}

	UNLOCK_CACHE;
}

void DCconfig_unlock_triggers(const zbx_vector_uint64_t *triggerids)
{
	int i;
	ZBX_DC_TRIGGER *dc_trigger;

	/* no other process can modify already locked triggers without write lock */
	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers,
																	   &triggerids->values[i])))
		{
			continue;
		}

		dc_trigger->locked = 0;
		DEBUG_TRIGGER(dc_trigger->triggerid, "Triggerid is unlocked");
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Unlocks all locked triggers before doing full history sync at     *
 *          program exit                                                      *
 *                                                                            *
 ******************************************************************************/
void DCconfig_unlock_all_triggers(void)
{
	ZBX_DC_TRIGGER *dc_trigger;
	zbx_hashset_iter_t iter;

	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
	{
		dc_trigger->locked = 0;
		DEBUG_TRIGGER(dc_trigger->triggerid, "Triggerid is locked");
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: check if the expression contains time based functions             *
 *                                                                            *
 * Parameters: expression    - [IN] the original expression                   *
 *             data          - [IN] the parsed and serialized expression      *
 *             trigger_timer - [IN] the trigger time function flags           *
 *                                                                            *
 ******************************************************************************/
static int DCconfig_find_active_time_function(const char *expression, const unsigned char *data,
											  unsigned char trigger_timer)
{
	int i, ret = SUCCEED;
	const ZBX_DC_FUNCTION *dc_function;
	const ZBX_DC_HOST *dc_host;
	const ZBX_DC_ITEM *dc_item;
	zbx_vector_uint64_t functionids;

	zbx_vector_uint64_create(&functionids);
	zbx_get_serialized_expression_functionids(expression, data, &functionids);

	for (i = 0; i < functionids.values_num; i++)
	{
		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
																		 &functionids.values[i])))
		{
			continue;
		}

		if (ZBX_TRIGGER_TIMER_DEFAULT != trigger_timer || ZBX_FUNCTION_TYPE_TRENDS == dc_function->type ||
			ZBX_FUNCTION_TYPE_TIMER == dc_function->type)
		{
			if (NULL == (dc_item = zbx_hashset_search(&config->items, &dc_function->itemid)))
				continue;

			if (NULL == (dc_host = zbx_hashset_search(&config->hosts, &dc_item->hostid)))
				continue;

			if (SUCCEED != DCin_maintenance_without_data_collection(dc_host, dc_item))
				goto out;
		}
	}

	ret = (ZBX_TRIGGER_TIMER_DEFAULT != trigger_timer ? SUCCEED : FAIL);
out:
	zbx_vector_uint64_destroy(&functionids);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets timer triggers from cache                                    *
 *                                                                            *
 * Parameters: trigger_info  - [IN/OUT] triggers                              *
 *             trigger_order - [IN/OUT] triggers in processing order          *
 *             timers        - [IN] timers of triggers to retrieve            *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_triggers_by_timers(zbx_hashset_t *trigger_info, zbx_vector_ptr_t *trigger_order,
								   const zbx_vector_ptr_t *timers)
{
	int i;
	ZBX_DC_TRIGGER *dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)timers->values[i];

		/* skip timers of 'busy' (being processed) triggers */
		if (0 == timer->lock)
			continue;

		if (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &timer->triggerid)))
		{
			DC_TRIGGER *trigger, trigger_local;
			unsigned char flags;

			if (SUCCEED == DCconfig_find_active_time_function(dc_trigger->expression,
															  dc_trigger->expression_bin, dc_trigger->timer & ZBX_TRIGGER_TIMER_EXPRESSION))
			{
				flags = ZBX_DC_TRIGGER_PROBLEM_EXPRESSION;
			}
			else
			{
				if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION != dc_trigger->recovery_mode)
					continue;

				if (TRIGGER_VALUE_PROBLEM != glb_state_trigger_get_value(dc_trigger->triggerid))
					continue;

				if (SUCCEED != DCconfig_find_active_time_function(dc_trigger->recovery_expression,
																  dc_trigger->recovery_expression_bin,
																  dc_trigger->timer & ZBX_TRIGGER_TIMER_RECOVERY_EXPRESSION))
				{
					continue;
				}

				flags = 0;
			}

			trigger_local.triggerid = dc_trigger->triggerid;
			trigger_local.history_idx = -1;
			trigger = (DC_TRIGGER *)zbx_hashset_insert(trigger_info, &trigger_local, sizeof(trigger_local));
			DCget_trigger(trigger, dc_trigger, ZBX_TRIGGER_GET_ALL);

			trigger->timespec = timer->eval_ts;
			trigger->flags = flags;

			zbx_vector_ptr_append(trigger_order, trigger);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: validate trigger timer                                            *
 *                                                                            *
 * Parameters: timer      - [IN] trigger timer                                *
 *             dc_trigger - [OUT] the trigger data                            *
 *                                                                            *
 * Return value: SUCCEED - the timer is valid                                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int trigger_timer_validate(zbx_trigger_timer_t *timer, ZBX_DC_TRIGGER **dc_trigger)
{
	ZBX_DC_FUNCTION *dc_function;

	*dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &timer->triggerid);

	if (0 != (timer->type & ZBX_TRIGGER_TIMER_FUNCTION))
	{
		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &timer->objectid)))
			return FAIL;

		if (dc_function->revision > timer->revision ||
			NULL == *dc_trigger ||
			TRIGGER_STATUS_ENABLED != (*dc_trigger)->status ||
			TRIGGER_FUNCTIONAL_TRUE != (*dc_trigger)->functional)
		{
			if (dc_function->timer_revision == timer->revision)
				dc_function->timer_revision = 0;
			return FAIL;
		}
	}
	else
	{
		if (NULL == (*dc_trigger))
			return FAIL;

		if ((*dc_trigger)->revision > timer->revision ||
			TRIGGER_STATUS_ENABLED != (*dc_trigger)->status ||
			TRIGGER_FUNCTIONAL_TRUE != (*dc_trigger)->functional)
		{
			if ((*dc_trigger)->timer_revision == timer->revision)
				(*dc_trigger)->timer_revision = 0;
			return FAIL;
		}
	}

	return SUCCEED;
}

static void dc_remove_invalid_timer(zbx_trigger_timer_t *timer)
{
	if (0 != (timer->type & ZBX_TRIGGER_TIMER_FUNCTION))
	{
		ZBX_DC_FUNCTION *function;

		if (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
																	  &timer->objectid)) &&
			function->timer_revision == timer->revision)
		{
			function->timer_revision = 0;
		}
	}
	else if (ZBX_TRIGGER_TIMER_TRIGGER == timer->type)
	{
		ZBX_DC_TRIGGER *trigger;

		if (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers,
																	&timer->objectid)) &&
			trigger->timer_revision == timer->revision)
		{
			trigger->timer_revision = 0;
		}
	}

	dc_trigger_timer_free(timer);
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets timers from trigger queue                                    *
 *                                                                            *
 * Parameters: timers     - [OUT] the timer triggers that must be processed   *
 *             now        - [IN] current time                                 *
 *             soft_limit - [IN] the number of timers to return unless timers *
 *                               of the same trigger are split over multiple  *
 *                               batches.                                     *
 *                                                                            *
 *             hard_limit - [IN] the maximum number of timers to return       *
 *                                                                            *
 * Comments: This function locks corresponding triggers in configuration      *
 *           cache.                                                           *
 *           If the returned timer has lock field set, then trigger is        *
 *           already being processed and should not be recalculated.          *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_trigger_timers(zbx_vector_ptr_t *timers, int now, int soft_limit, int hard_limit)
{
	zbx_trigger_timer_t *first_timer = NULL, *timer;
	int found = 0;
	zbx_binary_heap_elem_t *elem;

	RDLOCK_CACHE;

	if (SUCCEED != zbx_binary_heap_empty(&config->trigger_queue))
	{
		elem = zbx_binary_heap_find_min(&config->trigger_queue);
		timer = (zbx_trigger_timer_t *)elem->data;

		if (timer->check_ts.sec <= now)
			found = 1;
	}

	UNLOCK_CACHE;

	if (0 == found)
		return;

	WRLOCK_CACHE;

	while (SUCCEED != zbx_binary_heap_empty(&config->trigger_queue) && timers->values_num < hard_limit)
	{
		ZBX_DC_TRIGGER *dc_trigger;

		elem = zbx_binary_heap_find_min(&config->trigger_queue);
		timer = (zbx_trigger_timer_t *)elem->data;

		if (timer->check_ts.sec > now)
			break;

		/* first_timer stores the first timer from a list of timers of the same trigger with the same */
		/* evaluation timestamp. Reset first_timer if the conditions do not apply.                    */
		if (NULL != first_timer && (timer->triggerid != first_timer->triggerid ||
									0 != zbx_timespec_compare(&timer->eval_ts, &first_timer->eval_ts)))
		{
			first_timer = NULL;
		}

		/* use soft limit to avoid (mostly) splitting multiple functions of the same trigger */
		/* over consequent batches                                                           */
		if (timers->values_num >= soft_limit && NULL == first_timer)
			break;

		zbx_binary_heap_remove_min(&config->trigger_queue);

		if (SUCCEED != trigger_timer_validate(timer, &dc_trigger))
		{
			dc_remove_invalid_timer(timer);
			continue;
		}
		DEBUG_TRIGGER(timer->triggerid, "Got trigger form timer queue");
		zbx_vector_ptr_append(timers, timer);

		/* timers scheduled to executed in future are taken from queue only */
		/* for rescheduling later - skip locking                            */
		if (timer->exec_ts.sec > now)
		{
			/* recalculate next execution time only for timers */
			/* scheduled to evaluate future data period        */
			if (timer->eval_ts.sec > now)
				timer->check_ts.sec = 0;
			continue;
		}

		/* Trigger expression must be calculated using function evaluation time. If a trigger is locked   */
		/* keep rescheduling its timer until trigger is unlocked and can be calculated using the required */
		/* evaluation time. However there are exceptions when evaluation time of a locked trigger is      */
		/* acceptable to evaluate other functions:                                                        */
		/*  1) time functions uses current time, so trigger evaluation time does not affect their results */
		/*  2) trend function of the same trigger with the same evaluation timestamp is being             */
		/*     evaluated by the same process                                                              */
		if (0 == dc_trigger->locked || ZBX_TRIGGER_TIMER_FUNCTION_TREND != timer->type ||
			(NULL != first_timer && 1 == first_timer->lock))
		{
			/* resetting execution timer will cause a new execution time to be set */
			/* when timer is put back into queue                                   */
			timer->check_ts.sec = 0;

			timer->lastcheck = (time_t)now;
		}

		/* remember if the timer locked trigger, so it would unlock during rescheduling */
		if (0 == dc_trigger->locked)
			dc_trigger->locked = timer->lock = 1;

		if (NULL == first_timer)
			first_timer = timer;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: reschedule trigger timers                                         *
 *                                                                            *
 * Comments: Triggers are unlocked by DCconfig_unlock_triggers()              *
 *                                                                            *
 ******************************************************************************/
static void dc_reschedule_trigger_timers(zbx_vector_ptr_t *timers, int now)
{
	int i;

	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)timers->values[i];

		timer->lock = 0;

		/* schedule calculation error can result in 0 execution time */
		if (0 == timer->check_ts.sec)
			dc_remove_invalid_timer(timer);
		else
			dc_schedule_trigger_timer(timer, now, &timer->eval_ts, &timer->check_ts);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: reschedule trigger timers while locking configuration cache       *
 *                                                                            *
 * Comments: Triggers are unlocked by DCconfig_unlock_triggers()              *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_reschedule_trigger_timers(zbx_vector_ptr_t *timers, int now)
{
	int i;
	zbx_dc_um_handle_t *um_handle;

	um_handle = zbx_dc_open_user_macros();

	/* calculate new execution/evaluation time for the evaluated triggers */
	/* (timers with reset execution time)                                 */
	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)timers->values[i];

		if (0 == timer->check_ts.sec)
		{
			if (0 != (timer->check_ts.sec = (int)dc_function_calculate_nextcheck(um_handle, timer, now,
																				 timer->triggerid)))
			{
				timer->eval_ts = timer->check_ts;
			}
		}
	}

	zbx_dc_close_user_macros(um_handle);

	WRLOCK_CACHE;
	dc_reschedule_trigger_timers(timers, now);
	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clears timer trigger queue                                        *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_clear_timer_queue(zbx_vector_ptr_t *timers)
{
	ZBX_DC_FUNCTION *function;
	int i;

	zbx_vector_ptr_reserve(timers, config->trigger_queue.elems_num);

	WRLOCK_CACHE;

	for (i = 0; i < config->trigger_queue.elems_num; i++)
	{
		zbx_trigger_timer_t *timer = (zbx_trigger_timer_t *)config->trigger_queue.elems[i].data;

		if (ZBX_TRIGGER_TIMER_FUNCTION_TREND == timer->type &&
			NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
																	  &timer->objectid)) &&
			function->timer_revision == timer->revision)
		{
			zbx_vector_ptr_append(timers, timer);
		}
		else
			dc_trigger_timer_free(timer);
	}

	zbx_binary_heap_clear(&config->trigger_queue);

	UNLOCK_CACHE;
}

void zbx_dc_free_timers(zbx_vector_ptr_t *timers)
{
	int i;

	WRLOCK_CACHE;

	for (i = 0; i < timers->values_num; i++)
		dc_trigger_timer_free(timers->values[i]);

	UNLOCK_CACHE;
}

void DCfree_triggers(zbx_vector_ptr_t *triggers)
{
	int i;

	for (i = 0; i < triggers->values_num; i++)
		DCclean_trigger((DC_TRIGGER *)triggers->values[i]);

	zbx_vector_ptr_clear(triggers);
}

void DCconfig_update_interface_snmp_stats(zbx_uint64_t interfaceid, int max_snmp_succeed, int min_snmp_fail)
{
	ZBX_DC_SNMPINTERFACE *dc_snmp;

	WRLOCK_CACHE;

	if (NULL != (dc_snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid)) &&
		SNMP_BULK_ENABLED == dc_snmp->bulk)
	{
		if (dc_snmp->max_succeed < max_snmp_succeed)
			dc_snmp->max_succeed = (unsigned char)max_snmp_succeed;

		if (dc_snmp->min_fail > min_snmp_fail)
			dc_snmp->min_fail = (unsigned char)min_snmp_fail;
	}

	UNLOCK_CACHE;
}

static int DCconfig_get_suggested_snmp_vars_nolock(zbx_uint64_t interfaceid, int *bulk)
{
	int num;
	const ZBX_DC_SNMPINTERFACE *dc_snmp;

	dc_snmp = (const ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid);

	if (NULL != bulk)
		*bulk = (NULL == dc_snmp ? SNMP_BULK_DISABLED : dc_snmp->bulk);

	if (NULL == dc_snmp || SNMP_BULK_ENABLED != dc_snmp->bulk)
		return 1;

	/* The general strategy is to multiply request size by 3/2 in order to approach the limit faster. */
	/* However, once we are over the limit, we change the strategy to increasing the value by 1. This */
	/* is deemed better than going backwards from the error because less timeouts are going to occur. */

	if (1 >= dc_snmp->max_succeed || MAX_SNMP_ITEMS + 1 != dc_snmp->min_fail)
		num = dc_snmp->max_succeed + 1;
	else
		num = dc_snmp->max_succeed * 3 / 2;

	if (num < dc_snmp->min_fail)
		return num;

	/* If we have already found the optimal number of variables to query, we wish to base our suggestion on that */
	/* number. If we occasionally get a timeout in this area, it can mean two things: either the device's actual */
	/* limit is a bit lower than that (it can process requests above it, but only sometimes) or a UDP packet in  */
	/* one of the directions was lost. In order to account for the former, we allow ourselves to lower the count */
	/* of variables, but only up to two times. Otherwise, performance will gradually degrade due to the latter.  */

	return MAX(dc_snmp->max_succeed - 2, dc_snmp->min_fail - 1);
}

int DCconfig_get_suggested_snmp_vars(zbx_uint64_t interfaceid, int *bulk)
{
	int ret;

	RDLOCK_CACHE;

	ret = DCconfig_get_suggested_snmp_vars_nolock(interfaceid, bulk);

	UNLOCK_CACHE;

	return ret;
}

static int dc_get_interface_by_type(DC_INTERFACE *interface, zbx_uint64_t hostid, unsigned char type)
{
	int res = FAIL;
	const ZBX_DC_INTERFACE *dc_interface;
	const ZBX_DC_INTERFACE_HT *interface_ht;
	ZBX_DC_INTERFACE_HT interface_ht_local;

	interface_ht_local.hostid = hostid;
	interface_ht_local.type = type;

	if (NULL != (interface_ht = (const ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht,
																				&interface_ht_local)))
	{
		dc_interface = interface_ht->interface_ptr;
		DCget_interface(interface, dc_interface);
		res = SUCCEED;
	}

	return res;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Locate main interface of specified type in configuration cache    *
 *                                                                            *
 * Parameters: interface - [OUT] pointer to DC_INTERFACE structure            *
 *             hostid - [IN] host ID                                          *
 *             type - [IN] interface type                                     *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_interface_by_type(DC_INTERFACE *interface, zbx_uint64_t hostid, unsigned char type)
{
	int res;

	RDLOCK_CACHE;

	res = dc_get_interface_by_type(interface, hostid, type);

	UNLOCK_CACHE;

	return res;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Locate interface in configuration cache                           *
 *                                                                            *
 * Parameters: interface - [OUT] pointer to DC_INTERFACE structure            *
 *             hostid - [IN] host ID                                          *
 *             itemid - [IN] item ID                                          *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_interface(DC_INTERFACE *interface, zbx_uint64_t hostid, zbx_uint64_t itemid)
{
	int res = FAIL, i;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_INTERFACE *dc_interface;

	RDLOCK_CACHE;

	if (0 != itemid)
	{
		if (NULL == (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
			goto unlock;

		if (0 != dc_item->interfaceid)
		{
			if (NULL == (dc_interface = (const ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces,
																					 &dc_item->interfaceid)))
			{
				goto unlock;
			}

			DCget_interface(interface, dc_interface);
			res = SUCCEED;
			goto unlock;
		}

		hostid = dc_item->hostid;
	}

	if (0 == hostid)
		goto unlock;

	for (i = 0; i < (int)ARRSIZE(INTERFACE_TYPE_PRIORITY); i++)
	{
		if (SUCCEED == (res = dc_get_interface_by_type(interface, hostid, INTERFACE_TYPE_PRIORITY[i])))
			break;
	}

unlock:
	UNLOCK_CACHE;

	return res;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get nextcheck for selected queue                                  *
 *                                                                            *
 * Parameters: queue - [IN]                                                   *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for the specified queue        *
 *                                                                            *
 ******************************************************************************/
static int dc_config_get_queue_nextcheck(zbx_binary_heap_t *queue)
{
	int nextcheck;
	const zbx_binary_heap_elem_t *min;
	const ZBX_DC_ITEM *dc_item;

	if (FAIL == zbx_binary_heap_empty(queue))
	{
		min = zbx_binary_heap_find_min(queue);
		dc_item = (const ZBX_DC_ITEM *)min->data;
		nextcheck = dc_item->queue_next_check;
	}
	else
		nextcheck = FAIL;

	return nextcheck;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get nextcheck for selected poller                                 *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for selected poller            *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_poller_nextcheck(unsigned char poller_type)
{
	int nextcheck;
	zbx_binary_heap_t *queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() poller_type:%d", __func__, (int)poller_type);

	queue = &config->queues[poller_type];

	RDLOCK_CACHE;

	nextcheck = dc_config_get_queue_nextcheck(queue);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, nextcheck);

	return nextcheck;
}

static void dc_requeue_item(ZBX_DC_ITEM *dc_item, const ZBX_DC_HOST *dc_host, const ZBX_DC_INTERFACE *dc_interface,
							int flags, int lastclock)
{
	unsigned char old_poller_type;
	int nextcheck;

	nextcheck = DCitem_nextcheck_update(dc_item, dc_interface, flags, lastclock, NULL);

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, flags);
	DEBUG_ITEM(dc_item->itemid, "Updating item from %s", __func__);
	DCupdate_item_queue(dc_item, old_poller_type);
}

/******************************************************************************
 *                                                                            *
 * Purpose: requeues items at the specified time                              *
 *                                                                            *
 * Parameters: dc_item   - [IN] the item to reque                             *
 *             dc_host   - [IN] item's host                                   *
 *             nextcheck - [IN] the scheduled time                            *
 *                                                                            *
 ******************************************************************************/
static void dc_requeue_item_at(ZBX_DC_ITEM *dc_item, ZBX_DC_HOST *dc_host, int nextcheck)
{
	unsigned char old_poller_type;
	dc_item->queue_priority = ZBX_QUEUE_PRIORITY_HIGH;

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, ZBX_ITEM_COLLECTED);
	DEBUG_ITEM(dc_item->itemid, "Updating item from %s", __func__);
	DCupdate_item_queue(dc_item, old_poller_type);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get array of items for selected poller                            *
 *                                                                            *
 * Parameters: poller_type    - [IN] poller type (ZBX_POLLER_TYPE_...)        *
 *             config_timeout - [IN]                                          *
 *             items          - [OUT] array of items                          *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Comments: Items leave the queue only through this function. Pollers must   *
 *           always return the items they have taken using DCrequeue_items()  *
 *           or DCpoller_requeue_items().                                     *
 *                                                                            *
 *           Currently batch polling is supported only for JMX, SNMP and      *
 *           icmpping* simple checks. In other cases only single item is      *
 *           retrieved.                                                       *
 *                                                                            *
 *           IPMI poller queue are handled by DCconfig_get_ipmi_poller_items()*
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_poller_items(unsigned char poller_type, int config_timeout, DC_ITEM **items)
{
	int now, num = 0, max_items;
	zbx_binary_heap_t *queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() poller_type:%d", __func__, (int)poller_type);

	now = time(NULL);

	queue = &config->queues[poller_type];

	switch (poller_type)
	{
	case ZBX_POLLER_TYPE_JAVA:
		max_items = MAX_JAVA_ITEMS;
		break;
	case ZBX_POLLER_TYPE_PINGER:
		max_items = MAX_PINGER_ITEMS;
		break;
	default:
		max_items = 1;
	}

	WRLOCK_CACHE;

	while (num < max_items && FAIL == zbx_binary_heap_empty(queue))
	{
		int disable_until;
		const zbx_binary_heap_elem_t *min;
		ZBX_DC_HOST *dc_host;
		ZBX_DC_INTERFACE *dc_interface;
		ZBX_DC_ITEM *dc_item;
		static const ZBX_DC_ITEM *dc_item_prev = NULL;
		int disabled_until; 
		int can_poll;

		min = zbx_binary_heap_find_min(queue);
		dc_item = (ZBX_DC_ITEM *)min->data;

		if (dc_item->queue_next_check > now)
		{
			break;
		}

		if (0 != num)
		{
			if (ITEM_TYPE_SNMP == dc_item_prev->type)
			{
				if (0 != __config_snmp_item_compare(dc_item_prev, dc_item))
					break;
			}
			else if (ITEM_TYPE_JMX == dc_item_prev->type)
			{
				if (0 != __config_java_item_compare(dc_item_prev, dc_item))
					break;
			}
		}

		zbx_binary_heap_remove_min(queue);
		dc_item->location = ZBX_LOC_NOWHERE;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &dc_item->interfaceid);

		if (HOST_STATUS_MONITORED != dc_host->status || (0 != dc_host->proxy_hostid &&
														 SUCCEED != is_item_processed_by_server(dc_item->type, dc_item->key)))
		{
			continue;
		}

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, dc_interface, ZBX_ITEM_COLLECTED, now);
			continue;
		}
		
		can_poll =  glb_state_host_is_id_interface_pollable(dc_item->hostid, dc_item->interfaceid, &disabled_until);

		/* don't apply unreachable item/host throttling for prioritized items */
		if (ZBX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
					
			if ( SUCCEED == can_poll)
			{
				/* move reachable items on reachable hosts to normal pollers */
				if (ZBX_POLLER_TYPE_UNREACHABLE == poller_type &&
					ZBX_QUEUE_PRIORITY_LOW != dc_item->queue_priority)
				{
					dc_requeue_item(dc_item, dc_host, dc_interface, ZBX_ITEM_COLLECTED, now);
					continue;
				}
			}
			else
			{
				/* move items on unreachable hosts to unreachable pollers or    */
				/* postpone checks on hosts that have been checked recently and */
				/* are still unreachable                                        */
				if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_JAVA == poller_type ||
					disable_until > now)
				{
					dc_requeue_item(dc_item, dc_host, dc_interface,
									ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE, now);
					continue;
				}

			}
		}

		if (0 == num)
		{
			if (ZBX_POLLER_TYPE_NORMAL == poller_type && ITEM_TYPE_SNMP == dc_item->type &&
				0 == (ZBX_FLAG_DISCOVERY_RULE & dc_item->flags))
			{
				ZBX_DC_SNMPITEM *snmpitem;

				snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &dc_item->itemid);

				if (ZBX_SNMP_OID_TYPE_NORMAL == snmpitem->snmp_oid_type ||
					ZBX_SNMP_OID_TYPE_DYNAMIC == snmpitem->snmp_oid_type)
				{
					max_items = DCconfig_get_suggested_snmp_vars_nolock(dc_item->interfaceid, NULL);
				}
			}

			if (1 < max_items)
				*items = zbx_malloc(NULL, sizeof(DC_ITEM) * max_items);
		}

		dc_item_prev = dc_item;
		dc_item->location = ZBX_LOC_POLLER;
		DCget_host(&(*items)[num].host, dc_host);
		DCget_item(&(*items)[num], dc_item);
		num++;
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

typedef struct
{
	u_int64_t itemid;
	int time;
	unsigned char status;
	char is_async_polled;

} glb_changed_item_t;

static int process_collected_items(void *poll_data, zbx_vector_uint64_t *itemids)
{
	int i = 0;
	int num = 0;

	RDLOCK_CACHE;

	while (i < itemids->values_num)
	{ //&& ( glb_ms_time() - 1000 ) < start_time ) {
		ZBX_DC_ITEM *zbx_dc_item;
		ZBX_DC_HOST *zbx_dc_host;

		DC_ITEM dc_item;

		int errcode = SUCCEED;
		AGENT_RESULT result;
		DEBUG_ITEM(itemids->values[i], "Regenerating config");

		if (NULL != (zbx_dc_item = zbx_hashset_search(&config->items, &itemids->values[i])) &&
			NULL != (zbx_dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &zbx_dc_item->hostid)))
		{

			DEBUG_ITEM(itemids->values[i], "Found item and host data");

			if (FAIL == glb_might_be_async_polled(zbx_dc_item, zbx_dc_host))
			{

				DEBUG_ITEM(itemids->values[i], "Item shouldn't be processed anymore, removing from async polling (if it's there)");
				glb_poller_delete_item(itemids->values[i]);
				i++;
				continue;
			}

			if (HOST_STATUS_MONITORED != zbx_dc_host->status ||
				SUCCEED == DCin_maintenance_without_data_collection(zbx_dc_host, zbx_dc_item) ||
				ITEM_STATUS_DISABLED == zbx_dc_item->status)
			{
				DEBUG_ITEM(itemids->values[i], "Removing item due to it's disabled or its host is disabled/in maintenace without collection");
				glb_poller_delete_item(itemids->values[i]);
				i++;
				continue;
			}

			DCget_item(&dc_item, zbx_dc_item);
			DCget_host(&dc_item.host, zbx_dc_host);

			UNLOCK_CACHE; // need to unlock hrere due to zbx_prepare_items might need WRLOCK for macros

			DEBUG_ITEM(itemids->values[i], "Calling create item func");

			zbx_prepare_items(&dc_item, &errcode, 1, &result, MACRO_EXPAND_YES);

			glb_poller_create_item(&dc_item);
		
			zbx_clean_items(&dc_item, 1, &result);
			DCconfig_clean_items(&dc_item, &errcode, 1);
			zbx_free_agent_result(&result);

			RDLOCK_CACHE;
		}
		else
		{
			DEBUG_ITEM(itemids->values[i], "NOT found item and host data, or host/item is disabled, deleting item from the poller");
			glb_poller_delete_item(itemids->values[i]);
		}

		num++;
		i++;
	}
	UNLOCK_CACHE;
	return num;
}

int DCconfig_get_glb_poller_items_by_ids(void *poll_data, zbx_vector_uint64_t *itemids)
{
	int num = 0;

	if (itemids->values_num > 0)
	{
		LOG_DBG("Will process %d items", itemids->values_num);
		num = process_collected_items(poll_data, itemids);
	}

	return num;
}

#ifdef HAVE_OPENIPMI
/******************************************************************************
 *                                                                            *
 * Purpose: Get array of items for IPMI poller                                *
 *                                                                            *
 * Parameters: now            - [IN] current timestamp                        *
 *             items_num      - [IN] the number of items to get               *
 *             config_timeout - [IN]                                          *
 *             items          - [OUT] array of items                          *
 *             nextcheck      - [OUT] the next scheduled check                *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Comments: IPMI items leave the queue only through this function. IPMI      *
 *           manager must always return the items they have taken using       *
 *           DCrequeue_items() or DCpoller_requeue_items().                   *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_ipmi_poller_items(int now, int items_num, int config_timeout, DC_ITEM *items, int *nextcheck)
{
	int num = 0;
	zbx_binary_heap_t *queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->queues[ZBX_POLLER_TYPE_IPMI];

	WRLOCK_CACHE;

	while (num < items_num && FAIL == zbx_binary_heap_empty(queue))
	{
		int disable_until, iface_avail;
		const zbx_binary_heap_elem_t *min;
		ZBX_DC_HOST *dc_host;
		ZBX_DC_INTERFACE *dc_interface;
		ZBX_DC_ITEM *dc_item;

		min = zbx_binary_heap_find_min(queue);
		dc_item = (ZBX_DC_ITEM *)min->data;

		if (dc_item->queue_next_check > now)
			break;

		zbx_binary_heap_remove_min(queue);
		dc_item->location = ZBX_LOC_NOWHERE;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (NULL == (dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces,
																		   &dc_item->interfaceid)))
		{
			continue;
		}

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, dc_interface, ZBX_ITEM_COLLECTED, now);
			continue;
		}

		iface_avail = glb_state_host_is_id_interface_pollable(dc_item->hostid, dc_item->interfaceid, &disable_until);
		/* don't apply unreachable item/host throttling for prioritized items */
		if (ZBX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
			if ( FAIL == iface_avail )
			{
				if (disable_until > now)
				{
					dc_requeue_item(dc_item, dc_host, dc_interface,
									ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE, now);
					continue;
				}

			}
		}

		dc_item->location = ZBX_LOC_POLLER;
		DCget_host(&items[num].host, dc_host);
		DCget_item(&items[num], dc_item);
		num++;
	}

	*nextcheck = dc_config_get_queue_nextcheck(&config->queues[ZBX_POLLER_TYPE_IPMI]);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}
#endif /* HAVE_OPENIPMI */

/******************************************************************************
 *                                                                            *
 * Purpose: get array of interface IDs for the specified address              *
 *                                                                            *
 * Return value: number of interface IDs returned                             *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_snmp_interfaceids_by_addr(const char *addr, zbx_uint64_t **interfaceids)
{
	int count = 0, i;
	const ZBX_DC_INTERFACE_ADDR *dc_interface_snmpaddr;
	ZBX_DC_INTERFACE_ADDR dc_interface_snmpaddr_local;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() addr:'%s'", __func__, addr);

	dc_interface_snmpaddr_local.addr = addr;

	RDLOCK_CACHE;

	if (NULL == (dc_interface_snmpaddr = (const ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(
					 &config->interface_snmpaddrs, &dc_interface_snmpaddr_local)))
	{
		goto unlock;
	}

	*interfaceids = (zbx_uint64_t *)zbx_malloc(*interfaceids, dc_interface_snmpaddr->interfaceids.values_num *
																  sizeof(zbx_uint64_t));

	for (i = 0; i < dc_interface_snmpaddr->interfaceids.values_num; i++)
		(*interfaceids)[i] = dc_interface_snmpaddr->interfaceids.values[i];

	count = i;
unlock:
	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, count);

	return count;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get array of snmp trap items for the specified interfaceid        *
 *                                                                            *
 * Return value: number of items returned                                     *
 *                                                                            *
 ******************************************************************************/
size_t DCconfig_get_snmp_items_by_interfaceid(zbx_uint64_t interfaceid, DC_ITEM **items)
{
	size_t items_num = 0, items_alloc = 8;
	int i;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_INTERFACE_ITEM *dc_interface_snmpitem;
	const ZBX_DC_INTERFACE *dc_interface;
	const ZBX_DC_HOST *dc_host;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() interfaceid:" ZBX_FS_UI64, __func__, interfaceid);

	RDLOCK_CACHE;

	if (NULL == (dc_interface = (const ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &interfaceid)))
		goto unlock;

	if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_interface->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	if (NULL == (dc_interface_snmpitem = (const ZBX_DC_INTERFACE_ITEM *)zbx_hashset_search(
					 &config->interface_snmpitems, &interfaceid)))
	{
		goto unlock;
	}

	*items = (DC_ITEM *)zbx_malloc(*items, items_alloc * sizeof(DC_ITEM));

	for (i = 0; i < dc_interface_snmpitem->itemids.values_num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items,
																 &dc_interface_snmpitem->itemids.values[i])))
		{
			continue;
		}

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
			continue;

		if (items_num == items_alloc)
		{
			items_alloc += 8;
			*items = (DC_ITEM *)zbx_realloc(*items, items_alloc * sizeof(DC_ITEM));
		}

		DCget_host(&(*items)[items_num].host, dc_host);
		DCget_item(&(*items)[items_num], dc_item);
		items_num++;
	}
unlock:
	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():" ZBX_FS_SIZE_T, __func__, (zbx_fs_size_t)items_num);

	return items_num;
}

static void dc_requeue_items(const zbx_uint64_t *itemids, const int *lastclocks, const int *errcodes, size_t num)
{
	size_t i;
	ZBX_DC_ITEM *dc_item;
	ZBX_DC_HOST *dc_host;
	ZBX_DC_INTERFACE *dc_interface;

	for (i = 0; i < num; i++)
	{
		if (FAIL == errcodes[i])
			continue;

		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids[i])))
			continue;

		if (ZBX_LOC_POLLER == dc_item->location)
			dc_item->location = ZBX_LOC_NOWHERE;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED != zbx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			continue;

		dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &dc_item->interfaceid);

		switch (errcodes[i])
		{
		case SUCCEED:
		case NOTSUPPORTED:
		case AGENT_ERROR:
		case CONFIG_ERROR:
		case SIG_ERROR:
			dc_item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
			dc_requeue_item(dc_item, dc_host, dc_interface, ZBX_ITEM_COLLECTED, lastclocks[i]);
			break;
		case NETWORK_ERROR:
			// case PROCESSED:

		case GATEWAY_ERROR:
		case TIMEOUT_ERROR:
			dc_item->queue_priority = ZBX_QUEUE_PRIORITY_LOW;
			dc_requeue_item(dc_item, dc_host, dc_interface,
							ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE, time(NULL));
			break;
		default:
			zabbix_log(LOG_LEVEL_INFORMATION, "Unknown errcode: %d", errcodes[i]);
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

void DCrequeue_items(const zbx_uint64_t *itemids, const int *lastclocks,
					 const int *errcodes, size_t num)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, lastclocks, errcodes, num);

	UNLOCK_CACHE;
}

void DCpoller_requeue_items(const zbx_uint64_t *itemids, const int *lastclocks,
							const int *errcodes, size_t num, unsigned char poller_type, int *nextcheck)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, lastclocks, errcodes, num);
	*nextcheck = dc_config_get_queue_nextcheck(&config->queues[poller_type]);

	UNLOCK_CACHE;
}

#ifdef HAVE_OPENIPMI
/******************************************************************************
 *                                                                            *
 * Purpose: requeue unreachable items                                         *
 *                                                                            *
 * Parameters: itemids     - [IN] the item id array                           *
 *             itemids_num - [IN] the number of values in itemids array       *
 *                                                                            *
 * Comments: This function is used when items must be put back in the queue   *
 *           without polling them. For example if a poller has taken a batch  *
 *           of items from queue, host becomes unreachable during while       *
 *           polling the items, so the unpolled items of the same host must   *
 *           be returned to queue without updating their status.              *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_requeue_unreachable_items(zbx_uint64_t *itemids, size_t itemids_num)
{
	size_t i;
	ZBX_DC_ITEM *dc_item;
	ZBX_DC_HOST *dc_host;
	ZBX_DC_INTERFACE *dc_interface;

	WRLOCK_CACHE;

	for (i = 0; i < itemids_num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids[i])))
			continue;

		if (ZBX_LOC_POLLER == dc_item->location)
			dc_item->location = ZBX_LOC_NOWHERE;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &dc_item->interfaceid);

		dc_requeue_item(dc_item, dc_host, dc_interface, ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE,
						time(NULL));
	}

	UNLOCK_CACHE;
}
#endif /* HAVE_OPENIPMI */

/******************************************************************************
 *                                                                            *
 * Comments: helper function for trigger dependency checking                  *
 *                                                                            *
 * Parameters: trigdep        - [IN] the trigger dependency data              *
 *             level          - [IN] the trigger dependency level             *
 *             triggerids     - [IN] the currently processing trigger ids     *
 *                                   for bulk trigger operations              *
 *                                   (optional, can be NULL)                  *
 *             master_triggerids - [OUT] unresolved master trigger ids        *
 *                                   for bulk trigger operations              *
 *                                   (optional together with triggerids       *
 *                                   parameter)                               *
 *                                                                            *
 * Return value: SUCCEED - trigger dependency check succeed / was unresolved  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: With bulk trigger processing a master trigger can be in the same *
 *           batch as dependent trigger. In this case it might be impossible  *
 *           to perform dependency check based on cashed trigger values. The  *
 *           unresolved master trigger ids will be added to master_triggerids *
 *           vector, so the dependency check can be performed after a new     *
 *           master trigger value has been calculated.                        *
 *                                                                            *
 ******************************************************************************/
static int DCconfig_check_trigger_dependencies_rec(const ZBX_DC_TRIGGER_DEPLIST *trigdep, int level,
												   const zbx_vector_uint64_t *triggerids, zbx_vector_uint64_t *master_triggerids)
{
	int i;
	const ZBX_DC_TRIGGER *next_trigger;
	const ZBX_DC_TRIGGER_DEPLIST *next_trigdep;

	if (ZBX_TRIGGER_DEPENDENCY_LEVELS_MAX < level)
	{
		zabbix_log(LOG_LEVEL_CRIT, "recursive trigger dependency is too deep (triggerid:" ZBX_FS_UI64 ")",
				   trigdep->triggerid);
		return SUCCEED;
	}

	if (0 != trigdep->dependencies.values_num)
	{
		for (i = 0; i < trigdep->dependencies.values_num; i++)
		{
			next_trigdep = (const ZBX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[i];

			if (NULL != (next_trigger = next_trigdep->trigger) &&
				TRIGGER_STATUS_ENABLED == next_trigger->status &&
				TRIGGER_FUNCTIONAL_TRUE == next_trigger->functional)
			{

				if (NULL == triggerids || FAIL == zbx_vector_uint64_bsearch(triggerids,
																			next_trigger->triggerid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					if (TRIGGER_VALUE_PROBLEM == glb_state_trigger_get_value(next_trigger->triggerid))
						return FAIL;
				}
				else
					zbx_vector_uint64_append(master_triggerids, next_trigger->triggerid);
			}

			if (FAIL == DCconfig_check_trigger_dependencies_rec(next_trigdep, level + 1, triggerids,
																master_triggerids))
			{
				return FAIL;
			}
		}
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: check whether any of trigger dependencies have value PROBLEM      *
 *                                                                            *
 * Return value: SUCCEED - trigger can change its value                       *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
int DCconfig_check_trigger_dependencies(zbx_uint64_t triggerid)
{
	int ret = SUCCEED;
	const ZBX_DC_TRIGGER_DEPLIST *trigdep;

	RDLOCK_CACHE;

	if (NULL != (trigdep = (const ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps, &triggerid)))
		ret = DCconfig_check_trigger_dependencies_rec(trigdep, 0, NULL, NULL);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get statistics of the database cache                              *
 *                                                                            *
 ******************************************************************************/
void *DCconfig_get_stats(int request)
{
	static zbx_uint64_t value_uint;
	static double value_double;

	switch (request)
	{
	case ZBX_CONFSTATS_BUFFER_TOTAL:
		value_uint = config_mem->orig_size;
		return &value_uint;
	case ZBX_CONFSTATS_BUFFER_USED:
		value_uint = config_mem->orig_size - config_mem->free_size;
		return &value_uint;
	case ZBX_CONFSTATS_BUFFER_FREE:
		value_uint = config_mem->free_size;
		return &value_uint;
	case ZBX_CONFSTATS_BUFFER_PUSED:
		value_double = 100 * (double)(config_mem->orig_size - config_mem->free_size) /
					   config_mem->orig_size;
		return &value_double;
	case ZBX_CONFSTATS_BUFFER_PFREE:
		value_double = 100 * (double)config_mem->free_size / config_mem->orig_size;
		return &value_double;
	default:
		return NULL;
	}
}

static void DCget_proxy(DC_PROXY *dst_proxy, const ZBX_DC_PROXY *src_proxy)
{
	const ZBX_DC_HOST *host;
	ZBX_DC_INTERFACE_HT *interface_ht, interface_ht_local;

	dst_proxy->hostid = src_proxy->hostid;
	dst_proxy->proxy_config_nextcheck = src_proxy->proxy_config_nextcheck;
	dst_proxy->proxy_data_nextcheck = src_proxy->proxy_data_nextcheck;
	dst_proxy->proxy_tasks_nextcheck = src_proxy->proxy_tasks_nextcheck;
	dst_proxy->last_cfg_error_time = src_proxy->last_cfg_error_time;
	zbx_strlcpy(dst_proxy->version_str, src_proxy->version_str, sizeof(dst_proxy->version_str));
	dst_proxy->version_int = src_proxy->version_int;
	dst_proxy->compatibility = src_proxy->compatibility;
	dst_proxy->lastaccess = src_proxy->lastaccess;
	dst_proxy->auto_compress = src_proxy->auto_compress;
	dst_proxy->last_version_error_time = src_proxy->last_version_error_time;

	dst_proxy->revision = src_proxy->revision;
	dst_proxy->macro_revision = config->um_cache->revision;

	if (NULL != (host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &src_proxy->hostid)))
	{
		zbx_strscpy(dst_proxy->host, host->host);
		zbx_strscpy(dst_proxy->proxy_address, src_proxy->proxy_address);

		dst_proxy->tls_connect = host->tls_connect;
		dst_proxy->tls_accept = host->tls_accept;
		dst_proxy->proxy_type = host->status;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zbx_strscpy(dst_proxy->tls_issuer, host->tls_issuer);
		zbx_strscpy(dst_proxy->tls_subject, host->tls_subject);

		if (NULL == host->tls_dc_psk)
		{
			*dst_proxy->tls_psk_identity = '\0';
			*dst_proxy->tls_psk = '\0';
		}
		else
		{
			zbx_strscpy(dst_proxy->tls_psk_identity, host->tls_dc_psk->tls_psk_identity);
			zbx_strscpy(dst_proxy->tls_psk, host->tls_dc_psk->tls_psk);
		}
#endif
	}
	else
	{
		/* DCget_proxy() is called only from DCconfig_get_proxypoller_hosts(), which is called only from */
		/* process_proxy(). So, this branch should never happen. */
		*dst_proxy->host = '\0';
		*dst_proxy->proxy_address = '\0';
		dst_proxy->tls_connect = ZBX_TCP_SEC_TLS_PSK; /* set PSK to deliberately fail in this case */
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		*dst_proxy->tls_psk_identity = '\0';
		*dst_proxy->tls_psk = '\0';
#endif
		THIS_SHOULD_NEVER_HAPPEN;
	}

	interface_ht_local.hostid = src_proxy->hostid;
	interface_ht_local.type = INTERFACE_TYPE_UNKNOWN;

	if (NULL != (interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht,
																		  &interface_ht_local)))
	{
		const ZBX_DC_INTERFACE *interface = interface_ht->interface_ptr;

		zbx_strscpy(dst_proxy->addr_orig, interface->useip ? interface->ip : interface->dns);
		zbx_strscpy(dst_proxy->port_orig, interface->port);
	}
	else
	{
		*dst_proxy->addr_orig = '\0';
		*dst_proxy->port_orig = '\0';
	}

	dst_proxy->addr = NULL;
	dst_proxy->port = 0;
}

int DCconfig_get_last_sync_time(void)
{
	return config->sync_ts;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get array of proxies for proxy poller                             *
 *                                                                            *
 * Parameters: hosts - [OUT] array of hosts                                   *
 *             max_hosts - [IN] elements in hosts array                       *
 *                                                                            *
 * Return value: number of proxies in hosts array                             *
 *                                                                            *
 * Comments: Proxies leave the queue only through this function. Pollers must *
 *           always return the proxies they have taken using DCrequeue_proxy. *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_proxypoller_hosts(DC_PROXY *proxies, int max_hosts)
{
	int now, num = 0;
	zbx_binary_heap_t *queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	queue = &config->pqueue;

	WRLOCK_CACHE;

	while (num < max_hosts && FAIL == zbx_binary_heap_empty(queue))
	{
		const zbx_binary_heap_elem_t *min;
		ZBX_DC_PROXY *dc_proxy;

		min = zbx_binary_heap_find_min(queue);
		dc_proxy = (ZBX_DC_PROXY *)min->data;
		
	//	LOG_INF("PROXY POLLER: nextchek check");
	//	LOG_INF("PROXY POLLER: Got proxy %ld, nextcheck in %d", dc_proxy->hostid, dc_proxy->nextcheck - now);
		
		if (dc_proxy->nextcheck > now)
			break;

		zbx_binary_heap_remove_min(queue);
		dc_proxy->location = ZBX_LOC_POLLER;

		DCget_proxy(&proxies[num], dc_proxy);
		num++;
	}

	UNLOCK_CACHE;
	//LOG_INF("PROXY POLLER: returning %d proxies to check", num);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Get nextcheck for passive proxies                                 *
 *                                                                            *
 * Return value: nextcheck or FAIL if no passive proxies in queue             *
 *                                                                            *
 ******************************************************************************/
int DCconfig_get_proxypoller_nextcheck(void)
{
	int nextcheck;
	zbx_binary_heap_t *queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->pqueue;

	RDLOCK_CACHE;

	if (FAIL == zbx_binary_heap_empty(queue))
	{
		const zbx_binary_heap_elem_t *min;
		const ZBX_DC_PROXY *dc_proxy;

		min = zbx_binary_heap_find_min(queue);
		dc_proxy = (const ZBX_DC_PROXY *)min->data;

		nextcheck = dc_proxy->nextcheck;
	}
	else
		nextcheck = FAIL;

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, nextcheck);

	return nextcheck;
}

void DCrequeue_proxy(zbx_uint64_t hostid, unsigned char update_nextcheck, int proxy_conn_err)
{
	time_t now;
	ZBX_DC_HOST *dc_host;
	ZBX_DC_PROXY *dc_proxy;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() update_nextcheck:%d", __func__, (int)update_nextcheck);

	now = time(NULL);

	WRLOCK_CACHE;

	if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)) &&
		NULL != (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
	{
		if (ZBX_LOC_POLLER == dc_proxy->location)
			dc_proxy->location = ZBX_LOC_NOWHERE;

		/* set or clear passive proxy misconfiguration error timestamp */
		if (SUCCEED == proxy_conn_err)
			dc_proxy->last_cfg_error_time = 0;
		else if (CONFIG_ERROR == proxy_conn_err)
			dc_proxy->last_cfg_error_time = (int)now;

		if (HOST_STATUS_PROXY_PASSIVE == dc_host->status )
		{
			int nextcheck = calculate_proxy_nextcheck(hostid, CONFIG_PROXYCONFIG_FREQUENCY, now);

			if (0 != (update_nextcheck & ZBX_PROXY_CONFIG_NEXTCHECK))
			{
				dc_proxy->proxy_config_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, CONFIG_PROXYCONFIG_FREQUENCY, now);
			}

			if (0 != (update_nextcheck & ZBX_PROXY_DATA_NEXTCHECK))
			{
				dc_proxy->proxy_data_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, CONFIG_PROXYDATA_FREQUENCY, now);
			}
			if (0 != (update_nextcheck & ZBX_PROXY_TASKS_NEXTCHECK))
			{
				dc_proxy->proxy_tasks_nextcheck = (int)calculate_proxy_nextcheck(
					hostid, ZBX_TASK_UPDATE_FREQUENCY, now);
			}
			
			//LOG_INF("PROXY POLLER: finished proxy processing, %s, next data check in %d", dc_host->host, dc_proxy->proxy_data_nextcheck - time(NULL));
			DCupdate_proxy_queue(dc_proxy);
		}
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: expand user macros in string returning new string with resolved   *
 *          macros                                                            *
 *                                                                            *
 ******************************************************************************/
char *dc_expand_user_macros_dyn(const char *text, const zbx_uint64_t *hostids, int hostids_num, int env)
{
	zbx_token_t token;
	int pos = 0, last_pos = 0;
	char *str = NULL, *name = NULL, *context = NULL;
	size_t str_alloc = 0, str_offset = 0;

	if ('\0' == *text)
		return zbx_strdup(NULL, text);

	for (; SUCCEED == zbx_token_find(text, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		const char *value = NULL;

		if (ZBX_TOKEN_USER_MACRO != token.type)
			continue;

		zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + last_pos, token.loc.l - (size_t)last_pos);
		um_cache_resolve_const(config->um_cache, hostids, hostids_num, text + token.loc.l, env, &value);

		if (NULL != value)
		{
			zbx_strcpy_alloc(&str, &str_alloc, &str_offset, value);
		}
		else
		{
			zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + token.loc.l,
							  token.loc.r - token.loc.l + 1);
		}

		zbx_free(name);
		zbx_free(context);

		pos = (int)token.loc.r;
		last_pos = pos + 1;
	}

	zbx_strcpy_alloc(&str, &str_alloc, &str_offset, text + last_pos);

	return str;
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees the item queue data vector created by DCget_item_queue()    *
 *                                                                            *
 * Parameters: queue - [IN] the item queue data vector to free                *
 *                                                                            *
 ******************************************************************************/
void DCfree_item_queue(zbx_vector_ptr_t *queue)
{
	int i;

	for (i = 0; i < queue->values_num; i++)
		zbx_free(queue->values[i]);
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves vector of delayed items                                 *
 *                                                                            *
 * Parameters: queue - [OUT] the vector of delayed items (optional)           *
 *             from  - [IN] the minimum delay time in seconds (non-negative)  *
 *             to    - [IN] the maximum delay time in seconds or              *
 *                          ZBX_QUEUE_TO_INFINITY if there is no limit        *
 *                                                                            *
 * Return value: the number of delayed items                                  *
 *                                                                            *
 ******************************************************************************/
int DCget_item_queue(zbx_vector_ptr_t *queue, int from, int to)
{
	zbx_hashset_iter_t iter;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;
	int now, nitems = 0, data_expected_from, delay;
	zbx_queue_item_t *queue_item;

	now = (int)time(NULL);

	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (dc_host = (const ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		int i;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		for (i = 0; i < dc_host->items.values_num; i++)
		{
			const ZBX_DC_INTERFACE *dc_interface;
			char *delay_s;
			int ret;

			dc_item = dc_host->items.values[i];

			if (ITEM_STATUS_ACTIVE != dc_item->status)
				continue;

			if (SUCCEED != zbx_is_counted_in_item_queue(dc_item->type, dc_item->key))
				continue;

			if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
				continue;

			switch (dc_item->type)
			{
			case ITEM_TYPE_ZABBIX:
			case ITEM_TYPE_SNMP:
			case ITEM_TYPE_IPMI:
			case ITEM_TYPE_JMX:
				if (NULL == (dc_interface = (const ZBX_DC_INTERFACE *)zbx_hashset_search(
								 &config->interfaces, &dc_item->interfaceid)))
				{
					continue;
				}

	//			if (INTERFACE_AVAILABLE_TRUE != glb_state_hosts_get_id_interface_avail(dc_item->hostid, dc_interface->interfaceid))
	//				continue;
				break;
			case ITEM_TYPE_ZABBIX_ACTIVE:
				if (dc_host->data_expected_from >
					(data_expected_from = dc_item->data_expected_from))
				{
					data_expected_from = dc_host->data_expected_from;
				}

				delay_s = dc_expand_user_macros_dyn(dc_item->delay, &dc_item->hostid, 1,
													ZBX_MACRO_ENV_NONSECURE);
				ret = zbx_interval_preproc(delay_s, &delay, NULL, NULL);
				zbx_free(delay_s);

				if (SUCCEED != ret)
					continue;
				if (data_expected_from + delay > now)
					continue;
				break;
			}

			int nextcheck = glb_state_item_get_nextcheck(dc_item->itemid);

			if (now - nextcheck < from || (ZBX_QUEUE_TO_INFINITY != to && now - nextcheck >= to))
			{
				continue;
			}

			if (NULL != queue)
			{
				queue_item = (zbx_queue_item_t *)zbx_malloc(NULL, sizeof(zbx_queue_item_t));
				queue_item->itemid = dc_item->itemid;
				queue_item->type = dc_item->type;
				queue_item->nextcheck = dc_item->queue_next_check;
				queue_item->proxy_hostid = dc_host->proxy_hostid;

				zbx_vector_ptr_append(queue, queue_item);
			}
			nitems++;
		}
	}

	UNLOCK_CACHE;

	return nitems;
}

static void get_trigger_statistics(zbx_hashset_t *triggers)
{
	zbx_hashset_iter_t iter;
	ZBX_DC_TRIGGER *dc_trigger;

	zbx_hashset_iter_reset(triggers, &iter);
	/* loop over triggers to gather enabled and disabled trigger statistics */
	while (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_FLAG_DISCOVERY_PROTOTYPE == dc_trigger->flags || NULL == dc_trigger->itemids)
			continue;

		switch (dc_trigger->status)
		{
		case TRIGGER_STATUS_ENABLED:
			// todo: this statistics should be calculated inside glb_state_triggers in the Iterator
			if (TRIGGER_FUNCTIONAL_TRUE == dc_trigger->functional)
			{
				switch (glb_state_trigger_get_value(dc_trigger->triggerid))
				{
				case TRIGGER_VALUE_OK:
					config->status->triggers_enabled_ok++;
					break;
				case TRIGGER_VALUE_PROBLEM:
					config->status->triggers_enabled_problem++;
					break;
				case TRIGGER_VALUE_UNKNOWN:
				case TRIGGER_VALUE_NONE:
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
				}

				break;
			}
			ZBX_FALLTHROUGH;
		case TRIGGER_STATUS_DISABLED:
			config->status->triggers_disabled++;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

static void get_host_statistics(ZBX_DC_HOST *dc_host, ZBX_DC_PROXY *dc_proxy, int reset)
{
	int i;
	const ZBX_DC_ITEM *dc_item;
	ZBX_DC_HOST *dc_proxy_host = NULL;

	if (0 != dc_host->proxy_hostid)
		dc_proxy_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_host->proxy_hostid);

	/* loop over items to gather per-host and per-proxy statistics */
	for (i = 0; i < dc_host->items.values_num; i++)
	{
		dc_item = dc_host->items.values[i];

		if (ZBX_FLAG_DISCOVERY_NORMAL != dc_item->flags && ZBX_FLAG_DISCOVERY_CREATED != dc_item->flags)
			continue;

		switch (dc_item->status)
		{
		case ITEM_STATUS_ACTIVE:
			if (HOST_STATUS_MONITORED == dc_host->status)
			{
				if (SUCCEED == reset)
				{
					int delay;
					char *delay_s;

					delay_s = dc_expand_user_macros_dyn(dc_item->delay, &dc_item->hostid, 1,
														ZBX_MACRO_ENV_NONSECURE);

					if (SUCCEED == zbx_interval_preproc(delay_s, &delay, NULL, NULL) &&
						0 != delay)
					{
						config->status->required_performance += 1.0 / delay;

						if (NULL != dc_proxy)
							dc_proxy->required_performance += 1.0 / delay;
					}

					zbx_free(delay_s);
				}

				int state = glb_state_item_get_oper_state(dc_item->itemid);
				switch (state)
				{
				case ITEM_STATE_UNKNOWN:
				case ITEM_STATE_NORMAL:
					config->status->items_active_normal++;
					dc_host->items_active_normal++;
					if (NULL != dc_proxy_host) {
					
						dc_proxy_host->items_active_normal++;
					}
					break;
				case ITEM_STATE_NOTSUPPORTED:
					config->status->items_active_notsupported++;
					dc_host->items_active_notsupported++;
					if (NULL != dc_proxy_host)
						dc_proxy_host->items_active_notsupported++;
					break;
				default:
					zabbix_log(LOG_LEVEL_WARNING, "Unknown item state %d", state);
					THIS_SHOULD_NEVER_HAPPEN;
				}

				break;
			}
			ZBX_FALLTHROUGH;
		case ITEM_STATUS_DISABLED:
			config->status->items_disabled++;
			if (NULL != dc_proxy_host)
				dc_proxy_host->items_disabled++;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_status_update                                                 *
 *                                                                            *
 * Purpose: check when status information stored in configuration cache was   *
 *          updated last time and update it if necessary                      *
 *                                                                            *
 * Comments: This function gathers the following information:                 *
 *             - number of enabled hosts (total and per proxy)                *
 *             - number of disabled hosts (total and per proxy)               *
 *             - number of enabled and supported items (total, per host and   *
 *                                                                 per proxy) *
 *             - number of enabled and not supported items (total, per host   *
 *                                                             and per proxy) *
 *             - number of disabled items (total and per proxy)               *
 *             - number of enabled triggers with value OK                     *
 *             - number of enabled triggers with value PROBLEM                *
 *             - number of disabled triggers                                  *
 *             - required performance (total and per proxy)                   *
 *           Gathered information can then be displayed in the frontend (see  *
 *           "status.get" request) and used in calculation of zabbix[] items. *
 *                                                                            *
 * NOTE: Always call this function before accessing information stored in     *
 *       config->status as well as host and required performance counters     *
 *       stored in elements of config->proxies and item counters in elements  *
 *       of config->hosts.                                                    *
 *                                                                            *
 ******************************************************************************/
static void dc_status_update(void)
{
#define ZBX_STATUS_LIFETIME SEC_PER_MIN

	zbx_hashset_iter_t iter;
	ZBX_DC_HOST *dc_host;
	int reset;

	if (0 != config->status->last_update && config->status->last_update + ZBX_STATUS_LIFETIME > time(NULL))
		return;

	if (config->status->sync_ts != config->sync_ts)
		reset = SUCCEED;
	else
		reset = FAIL;

	/* reset global counters */

	config->status->hosts_monitored = 0;
	config->status->hosts_not_monitored = 0;
	config->status->items_active_normal = 0;
	config->status->items_active_notsupported = 0;
	config->status->items_disabled = 0;
	config->status->triggers_enabled_ok = 0;
	config->status->triggers_enabled_problem = 0;
	config->status->triggers_disabled = 0;

	if (SUCCEED == reset)
		config->status->required_performance = 0.0;

	/* loop over proxies to reset per-proxy host and required performance counters */

	if (SUCCEED == reset)
	{
		ZBX_DC_PROXY *dc_proxy;
		ZBX_DC_HOST *dc_host;

		zbx_hashset_iter_reset(&config->proxies, &iter);

		while (NULL != (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
		{
			dc_proxy->hosts_monitored = 0;
			dc_proxy->hosts_not_monitored = 0;
			dc_proxy->required_performance = 0.0;

			if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_proxy->hostid))) {
				dc_host->items_active_normal = 0;
				dc_host->items_active_notsupported = 0;
				dc_host->items_disabled = 0;
			}

		}
	}

	/* loop over hosts */

	zbx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		ZBX_DC_PROXY *dc_proxy = NULL;

		/* reset per-host/per-proxy item counters */
//		if (161793 == dc_host->hostid)
//			LOG_INF("Before zeroing: has active normal %d, active non supported %d, disabled %d",
//				dc_host->items_active_normal, dc_host->items_active_notsupported, dc_host->items_disabled);

		if (HOST_STATUS_PROXY_ACTIVE != dc_host->status && 
			HOST_STATUS_PROXY_PASSIVE != dc_host->status) {
			dc_host->items_active_normal = 0;
			dc_host->items_active_notsupported = 0;
			dc_host->items_disabled = 0;
		}
		
		/* gather per-proxy statistics of enabled and disabled hosts */
		switch (dc_host->status)
		{
		case HOST_STATUS_MONITORED:
			config->status->hosts_monitored++;
			if (0 == dc_host->proxy_hostid)
				break;

			if (SUCCEED == reset)
			{
				if (NULL == (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies,
																		   &dc_host->proxy_hostid)))
				{
					break;
				}

				dc_proxy->hosts_monitored++;
			}
			break;
		case HOST_STATUS_NOT_MONITORED:
			config->status->hosts_not_monitored++;
			if (0 == dc_host->proxy_hostid)
				break;

			if (SUCCEED == reset)
			{
				if (NULL == (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies,
																		   &dc_host->proxy_hostid)))
				{
					break;
				}

				dc_proxy->hosts_not_monitored++;
			}
			break;
		}

		get_host_statistics(dc_host, dc_proxy, reset);
	}

	get_trigger_statistics(&config->triggers);

	config->status->sync_ts = config->sync_ts;
	config->status->last_update = time(NULL);

#undef ZBX_STATUS_LIFETIME
}

/******************************************************************************
 *                                                                            *
 * Purpose: return the number of active items                                 *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active items                                   *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t DCget_item_count(zbx_uint64_t hostid)
{
	zbx_uint64_t count;
	const ZBX_DC_HOST *dc_host;

	WRLOCK_CACHE;

	dc_status_update();

	if (0 == hostid)
		count = config->status->items_active_normal + config->status->items_active_notsupported;
	else if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
		count = dc_host->items_active_normal + dc_host->items_active_notsupported;
	else
		count = 0;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Purpose: return the number of active unsupported items                     *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active unsupported items                       *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t DCget_item_unsupported_count(zbx_uint64_t hostid)
{
	zbx_uint64_t count;
	const ZBX_DC_HOST *dc_host;

	WRLOCK_CACHE;

	dc_status_update();

	if (0 == hostid)
		count = config->status->items_active_notsupported;
	else if (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
		count = dc_host->items_active_notsupported;
	else
		count = 0;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Purpose: count active triggers                                             *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t DCget_trigger_count(void)
{
	zbx_uint64_t count;

	WRLOCK_CACHE;

	dc_status_update();

	count = config->status->triggers_enabled_ok + config->status->triggers_enabled_problem;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Purpose: count monitored and not monitored hosts                           *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t DCget_host_count(void)
{
	zbx_uint64_t nhosts;

	WRLOCK_CACHE;

	dc_status_update();

	nhosts = config->status->hosts_monitored;

	UNLOCK_CACHE;

	return nhosts;
}

/******************************************************************************
 *                                                                            *
 * Return value: the required nvps number                                     *
 *                                                                            *
 ******************************************************************************/
double DCget_required_performance(void)
{
	double nvps;

	WRLOCK_CACHE;

	dc_status_update();

	nvps = config->status->required_performance;

	UNLOCK_CACHE;

	return nvps;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves all internal metrics of the configuration cache         *
 *                                                                            *
 * Parameters: stats - [OUT] the configuration cache statistics               *
 *                                                                            *
 ******************************************************************************/
void DCget_count_stats_all(zbx_config_cache_info_t *stats)
{
	WRLOCK_CACHE;

	dc_status_update();

	stats->hosts = config->status->hosts_monitored;
	stats->items = config->status->items_active_normal + config->status->items_active_notsupported;
	stats->items_unsupported = config->status->items_active_notsupported;
	stats->requiredperformance = config->status->required_performance;

	UNLOCK_CACHE;
}

static void proxy_counter_ui64_push(zbx_vector_ptr_t *vector, zbx_uint64_t proxyid, zbx_uint64_t counter)
{
	zbx_proxy_counter_t *proxy_counter;

	proxy_counter = (zbx_proxy_counter_t *)zbx_malloc(NULL, sizeof(zbx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.ui64 = counter;
	zbx_vector_ptr_append(vector, proxy_counter);
}

static void proxy_counter_dbl_push(zbx_vector_ptr_t *vector, zbx_uint64_t proxyid, double counter)
{
	zbx_proxy_counter_t *proxy_counter;

	proxy_counter = (zbx_proxy_counter_t *)zbx_malloc(NULL, sizeof(zbx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.dbl = counter;
	zbx_vector_ptr_append(vector, proxy_counter);
}

void DCget_status(zbx_vector_ptr_t *hosts_monitored, zbx_vector_ptr_t *hosts_not_monitored,
				  zbx_vector_ptr_t *items_active_normal, zbx_vector_ptr_t *items_active_notsupported,
				  zbx_vector_ptr_t *items_disabled, zbx_uint64_t *triggers_enabled_ok,
				  zbx_uint64_t *triggers_enabled_problem, zbx_uint64_t *triggers_disabled,
				  zbx_vector_ptr_t *required_performance)
{
	zbx_hashset_iter_t iter;
	const ZBX_DC_PROXY *dc_proxy;
	const ZBX_DC_HOST *dc_proxy_host;

	WRLOCK_CACHE;

	dc_status_update();

	proxy_counter_ui64_push(hosts_monitored, 0, config->status->hosts_monitored);
	proxy_counter_ui64_push(hosts_not_monitored, 0, config->status->hosts_not_monitored);
	proxy_counter_ui64_push(items_active_normal, 0, config->status->items_active_normal);
	proxy_counter_ui64_push(items_active_notsupported, 0, config->status->items_active_notsupported);
	proxy_counter_ui64_push(items_disabled, 0, config->status->items_disabled);
	*triggers_enabled_ok = config->status->triggers_enabled_ok;
	*triggers_enabled_problem = config->status->triggers_enabled_problem;
	*triggers_disabled = config->status->triggers_disabled;
	proxy_counter_dbl_push(required_performance, 0, config->status->required_performance);

	zbx_hashset_iter_reset(&config->proxies, &iter);

	while (NULL != (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
	{
		proxy_counter_ui64_push(hosts_monitored, dc_proxy->hostid, dc_proxy->hosts_monitored);
		proxy_counter_ui64_push(hosts_not_monitored, dc_proxy->hostid, dc_proxy->hosts_not_monitored);

		if (NULL != (dc_proxy_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_proxy->hostid)))
		{
			proxy_counter_ui64_push(items_active_normal, dc_proxy->hostid,
									dc_proxy_host->items_active_normal);
			proxy_counter_ui64_push(items_active_notsupported, dc_proxy->hostid,
									dc_proxy_host->items_active_notsupported);
			proxy_counter_ui64_push(items_disabled, dc_proxy->hostid, dc_proxy_host->items_disabled);
		}

		proxy_counter_dbl_push(required_performance, dc_proxy->hostid, dc_proxy->required_performance);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves global expression data from cache                       *
 *                                                                            *
 * Parameters: expressions  - [OUT] a vector of expression data pointers      *
 *             names        - [IN] a vector containing expression names       *
 *             names_num    - [IN] the number of items in names vector        *
 *                                                                            *
 * Comment: The expressions vector contains allocated data, which must be     *
 *          freed afterwards with zbx_regexp_clean_expressions() function.    *
 *                                                                            *
 ******************************************************************************/
void DCget_expressions_by_names(zbx_vector_expression_t *expressions, const char *const *names, int names_num)
{
	int i, iname;
	const ZBX_DC_EXPRESSION *expression;
	const ZBX_DC_REGEXP *regexp;
	ZBX_DC_REGEXP search_regexp;

	RDLOCK_CACHE;

	for (iname = 0; iname < names_num; iname++)
	{
		search_regexp.name = names[iname];

		if (NULL != (regexp = (const ZBX_DC_REGEXP *)zbx_hashset_search(&config->regexps, &search_regexp)))
		{
			for (i = 0; i < regexp->expressionids.values_num; i++)
			{
				zbx_uint64_t expressionid = regexp->expressionids.values[i];
				zbx_expression_t *rxp;

				if (NULL == (expression = (const ZBX_DC_EXPRESSION *)zbx_hashset_search(
								 &config->expressions, &expressionid)))
				{
					continue;
				}

				rxp = (zbx_expression_t *)zbx_malloc(NULL, sizeof(zbx_expression_t));
				rxp->name = zbx_strdup(NULL, regexp->name);
				rxp->expression = zbx_strdup(NULL, expression->expression);
				rxp->exp_delimiter = expression->delimiter;
				rxp->case_sensitive = expression->case_sensitive;
				rxp->expression_type = expression->type;

				zbx_vector_expression_append(expressions, rxp);
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves regular expression data from cache                      *
 *                                                                            *
 * Parameters: expressions  - [OUT] a vector of expression data pointers      *
 *             name         - [IN] the regular expression name                *
 *                                                                            *
 * Comment: The expressions vector contains allocated data, which must be     *
 *          freed afterwards with zbx_regexp_clean_expressions() function.    *
 *                                                                            *
 ******************************************************************************/
void DCget_expressions_by_name(zbx_vector_expression_t *expressions, const char *name)
{
	DCget_expressions_by_names(expressions, &name, 1);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Returns time since which data is expected for the given item. We  *
 *          would not mind not having data for the item before that time, but *
 *          since that time we expect data to be coming.                      *
 *                                                                            *
 * Parameters: itemid  - [IN]                                                 *
 *             seconds - [OUT] the time data is expected as a Unix timestamp  *
 *                                                                            *
 ******************************************************************************/
int DCget_data_expected_from(zbx_uint64_t itemid, int *seconds)
{
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;
	int ret = FAIL;

	RDLOCK_CACHE;

	if (NULL == (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		goto unlock;

	if (ITEM_STATUS_ACTIVE != dc_item->status)
		goto unlock;

	if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	*seconds = MAX(dc_item->data_expected_from, dc_host->data_expected_from);

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get host identifiers for the specified list of functions          *
 *                                                                            *
 * Parameters: functionids     - [IN]                                         *
 *             functionids_num - [IN]                                         *
 *             hostids         - [OUT]                                        *
 *                                                                            *
 * Comments: this function must be used only by configuration syncer          *
 *                                                                            *
 ******************************************************************************/
void dc_get_hostids_by_functionids(const zbx_uint64_t *functionids, int functionids_num,
								   zbx_vector_uint64_t *hostids)
{
	const ZBX_DC_FUNCTION *function;
	const ZBX_DC_ITEM *item;
	int i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (function = (const ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
																			&functionids[i])))
		{
			continue;
		}

		if (NULL != (item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
			zbx_vector_uint64_append(hostids, item->hostid);
	}

	zbx_vector_uint64_sort(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get function host ids grouped by an object (trigger) id           *
 *                                                                            *
 * Parameters: functionids - [IN]                                             *
 *             hostids     - [OUT]                                            *
 *                                                                            *
 ******************************************************************************/
void DCget_hostids_by_functionids(zbx_vector_uint64_t *functionids, zbx_vector_uint64_t *hostids)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	dc_get_hostids_by_functionids(functionids->values, functionids->values_num, hostids);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hostids->values_num);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids     - [IN]                                         *
 *             functionids_num - [IN]                                         *
 *             hosts           - [OUT]                                        *
 *                                                                            *
 ******************************************************************************/
static void dc_get_hosts_by_functionids(const zbx_uint64_t *functionids, int functionids_num, zbx_hashset_t *hosts)
{
	const ZBX_DC_FUNCTION *dc_function;
	const ZBX_DC_ITEM *dc_item;
	const ZBX_DC_HOST *dc_host;
	DC_HOST host;
	int i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (dc_function = (const ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
																			   &functionids[i])))
		{
			continue;
		}

		if (NULL == (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &dc_function->itemid)))
			continue;

		if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		DCget_host(&host, dc_host);
		zbx_hashset_insert(hosts, &host, sizeof(host));
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids - [IN]                                             *
 *             hosts       - [OUT]                                            *
 *                                                                            *
 ******************************************************************************/
void DCget_hosts_by_functionids(const zbx_vector_uint64_t *functionids, zbx_hashset_t *hosts)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	dc_get_hosts_by_functionids(functionids->values, functionids->values_num, hosts);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hosts->num_data);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get number of enabled internal actions                            *
 *                                                                            *
 * Return value: number of enabled internal actions                           *
 *                                                                            *
 ******************************************************************************/
unsigned int DCget_internal_action_count(void)
{
	unsigned int count;

	RDLOCK_CACHE;

	count = config->internal_actions;

	UNLOCK_CACHE;

	return count;
}

unsigned int DCget_auto_registration_action_count(void)
{
	unsigned int count;

	RDLOCK_CACHE;

	count = config->auto_registration_actions;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get global configuration data                                     *
 *                                                                            *
 * Parameters: cfg   - [OUT] the global configuration data                    *
 *             flags - [IN] the flags specifying fields to get,               *
 *                          see ZBX_CONFIG_FLAGS_ defines                     *
 *                                                                            *
 * Comments: It's recommended to cleanup 'cfg' structure after use with       *
 *           zbx_config_clean() function even if only simple fields were      *
 *           requested.                                                       *
 *                                                                            *
 ******************************************************************************/
void zbx_config_get(zbx_config_t *cfg, zbx_uint64_t flags)
{
	LOG_DBG("In %s(): getting config, flags are %ld", __func__,  flags);

	RDLOCK_CACHE;
	if (0 != (flags & ZBX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int i;

		cfg->severity_name = (char **)zbx_malloc(NULL, TRIGGER_SEVERITY_COUNT * sizeof(char *));

		for (i = 0; i < TRIGGER_SEVERITY_COUNT; i++)
			cfg->severity_name[i] = zbx_strdup(NULL, config->config->severity_name[i]);
	}

	if (0 != (flags & ZBX_CONFIG_FLAGS_DISCOVERY_GROUPID))
		cfg->discovery_groupid = config->config->discovery_groupid;

	if (0 != (flags & ZBX_CONFIG_FLAGS_DEFAULT_INVENTORY_MODE))
		cfg->default_inventory_mode = config->config->default_inventory_mode;

	if (0 != (flags & ZBX_CONFIG_FLAGS_SNMPTRAP_LOGGING))
		cfg->snmptrap_logging = config->config->snmptrap_logging;

	if (0 != (flags & ZBX_CONFIG_FLAGS_HOUSEKEEPER))
		cfg->hk = config->config->hk;

	if (0 != (flags & ZBX_CONFIG_FLAGS_DB_EXTENSION))
	{
		cfg->db.extension = zbx_strdup(NULL, config->config->db.extension);
	}

	if (0 != (flags & ZBX_CONFIG_FLAGS_AUTOREG_TLS_ACCEPT))
		cfg->autoreg_tls_accept = config->config->autoreg_tls_accept;

	if (0 != (flags & ZBX_CONFIG_FLAGS_DEFAULT_TIMEZONE))
		cfg->default_timezone = zbx_strdup(NULL, config->config->default_timezone);

	if (0 != (flags & ZBX_CONFIG_FLAGS_AUDITLOG_ENABLED))
		cfg->auditlog_enabled = config->config->auditlog_enabled;

	UNLOCK_CACHE;

	cfg->flags = flags;
	LOG_DBG("In %s(): finished ", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get housekeeping mode for history and trends tables               *
 *                                                                            *
 * Parameters: history_mode - [OUT] history housekeeping mode, can be either  *
 *                                  disabled, enabled or partitioning         *
 *             trends_mode  - [OUT] trends housekeeping mode, can be either   *
 *                                  disabled, enabled or partitioning         *
 *                                                                            *
 ******************************************************************************/
// void	zbx_config_get_hk_mode(unsigned char *history_mode, unsigned char *trends_mode)
// {
// 	RDLOCK_CACHE;
// 	*history_mode = config->config->hk.history_mode;
// 	*trends_mode = config->config->hk.trends_mode;
// 	UNLOCK_CACHE;
// }

/******************************************************************************
 *                                                                            *
 * Purpose: cleans global configuration data structure filled                 *
 *          by zbx_config_get() function                                      *
 *                                                                            *
 * Parameters: cfg   - [IN] the global configuration data                     *
 *                                                                            *
 ******************************************************************************/
void zbx_config_clean(zbx_config_t *cfg)
{
	if (0 != (cfg->flags & ZBX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int i;

		for (i = 0; i < TRIGGER_SEVERITY_COUNT; i++)
			zbx_free(cfg->severity_name[i]);

		zbx_free(cfg->severity_name);
	}

	if (0 != (cfg->flags & ZBX_CONFIG_FLAGS_DB_EXTENSION))
		zbx_free(cfg->db.extension);

	if (0 != (cfg->flags & ZBX_CONFIG_FLAGS_DEFAULT_TIMEZONE))
		zbx_free(cfg->default_timezone);
}

/******************************************************************************
 *                                                                            *
 * Purpose: sets availability timestamp to current time for the specified     *
 *          interfaces                                                        *
 *                                                                            *
 * Parameters: interfaceids - [IN] the interfaces identifiers                 *
 *                                                                            *
 ******************************************************************************/
// void DCtouch_interfaces_availability(const zbx_vector_uint64_t *interfaceids)
// {
// 	ZBX_DC_INTERFACE *dc_interface;
// 	int i, now;

// 	zabbix_log(LOG_LEVEL_DEBUG, "In %s() interfaceids:%d", __func__, interfaceids->values_num);

// 	now = time(NULL);

// 	WRLOCK_CACHE;

// 	for (i = 0; i < interfaceids->values_num; i++)
// 	{
// 		if (NULL != (dc_interface = zbx_hashset_search(&config->interfaces, &interfaceids->values[i])))
// 			dc_interface->availability_ts = now;
// 	}

// 	UNLOCK_CACHE;

// 	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
// }

/******************************************************************************
 *                                                                            *
 * Purpose: sets timestamp of the last availability update                    *
 *                                                                            *
 * Parameter: ts - [IN] the last availability update timestamp                *
 *                                                                            *
 * Comments: This function is used only by proxies when preparing host        *
 *           availability data to be sent to server.                          *
 *                                                                            *
 ******************************************************************************/
void zbx_set_availability_diff_ts(int ts)
{
	/* this data can't be accessed simultaneously from multiple processes - locking is not necessary */
	config->availability_diff_ts = ts;
}

int zbx_get_availability_diff_ts()
{
	return config->availability_diff_ts;
}

int  server_time_is_changed(int new_time) {
	if (config->server_time == new_time)
		return FAIL;
	config->server_time = new_time;
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees correlation condition                                       *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void corr_condition_clean(zbx_corr_condition_t *condition)
{
	switch (condition->type)
	{
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		zbx_free(condition->data.tag.tag);
		break;
	case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
		zbx_free(condition->data.tag_pair.oldtag);
		zbx_free(condition->data.tag_pair.newtag);
		break;
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		zbx_free(condition->data.tag_value.tag);
		zbx_free(condition->data.tag_value.value);
		break;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: frees global correlation rule                                     *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void dc_correlation_free(zbx_correlation_t *correlation)
{
	zbx_free(correlation->name);
	zbx_free(correlation->formula);

	zbx_vector_ptr_clear_ext(&correlation->operations, zbx_ptr_free);
	zbx_vector_ptr_destroy(&correlation->operations);
	zbx_vector_ptr_destroy(&correlation->conditions);

	zbx_free(correlation);
}

/******************************************************************************
 *                                                                            *
 * Purpose: copies cached correlation condition to memory                     *
 *                                                                            *
 * Parameter: dc_condition - [IN] the condition to copy                       *
 *            condition    - [OUT] the destination condition                  *
 *                                                                            *
 * Return value: The cloned correlation condition.                            *
 *                                                                            *
 ******************************************************************************/
static void dc_corr_condition_copy(const zbx_dc_corr_condition_t *dc_condition, zbx_corr_condition_t *condition)
{
	condition->type = dc_condition->type;

	switch (condition->type)
	{
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		condition->data.tag.tag = zbx_strdup(NULL, dc_condition->data.tag.tag);
		break;
	case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
		condition->data.tag_pair.oldtag = zbx_strdup(NULL, dc_condition->data.tag_pair.oldtag);
		condition->data.tag_pair.newtag = zbx_strdup(NULL, dc_condition->data.tag_pair.newtag);
		break;
	case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
		/* break; is not missing here */
	case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		condition->data.tag_value.tag = zbx_strdup(NULL, dc_condition->data.tag_value.tag);
		condition->data.tag_value.value = zbx_strdup(NULL, dc_condition->data.tag_value.value);
		condition->data.tag_value.op = dc_condition->data.tag_value.op;
		break;
	case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
		condition->data.group.groupid = dc_condition->data.group.groupid;
		condition->data.group.op = dc_condition->data.group.op;
		break;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: clones cached correlation operation to memory                     *
 *                                                                            *
 * Parameter: operation - [IN] the operation to clone                         *
 *                                                                            *
 * Return value: The cloned correlation operation.                            *
 *                                                                            *
 ******************************************************************************/
static zbx_corr_operation_t *zbx_dc_corr_operation_dup(const zbx_dc_corr_operation_t *dc_operation)
{
	zbx_corr_operation_t *operation;

	operation = (zbx_corr_operation_t *)zbx_malloc(NULL, sizeof(zbx_corr_operation_t));
	operation->type = dc_operation->type;

	return operation;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clones cached correlation formula, generating it if necessary     *
 *                                                                            *
 * Parameter: correlation - [IN] the correlation                              *
 *                                                                            *
 * Return value: The cloned correlation formula.                              *
 *                                                                            *
 ******************************************************************************/
static char *dc_correlation_formula_dup(const zbx_dc_correlation_t *dc_correlation)
{
#define ZBX_OPERATION_TYPE_UNKNOWN 0
#define ZBX_OPERATION_TYPE_OR 1
#define ZBX_OPERATION_TYPE_AND 2

	char *formula = NULL;
	const char *op = NULL;
	size_t formula_alloc = 0, formula_offset = 0;
	int i, last_type = -1, last_op = ZBX_OPERATION_TYPE_UNKNOWN;
	const zbx_dc_corr_condition_t *dc_condition;
	zbx_uint64_t last_id;

	if (ZBX_CONDITION_EVAL_TYPE_EXPRESSION == dc_correlation->evaltype || 0 ==
																			  dc_correlation->conditions.values_num)
	{
		return zbx_strdup(NULL, dc_correlation->formula);
	}

	dc_condition = (const zbx_dc_corr_condition_t *)dc_correlation->conditions.values[0];

	switch (dc_correlation->evaltype)
	{
	case ZBX_CONDITION_EVAL_TYPE_OR:
		op = " or";
		break;
	case ZBX_CONDITION_EVAL_TYPE_AND:
		op = " and";
		break;
	}

	if (NULL != op)
	{
		zbx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" ZBX_FS_UI64 "}",
						   dc_condition->corr_conditionid);

		for (i = 1; i < dc_correlation->conditions.values_num; i++)
		{
			dc_condition = (const zbx_dc_corr_condition_t *)dc_correlation->conditions.values[i];

			zbx_strcpy_alloc(&formula, &formula_alloc, &formula_offset, op);
			zbx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, " {" ZBX_FS_UI64 "}",
							   dc_condition->corr_conditionid);
		}

		return formula;
	}

	last_id = dc_condition->corr_conditionid;
	last_type = dc_condition->type;

	for (i = 1; i < dc_correlation->conditions.values_num; i++)
	{
		dc_condition = (const zbx_dc_corr_condition_t *)dc_correlation->conditions.values[i];

		if (last_type == dc_condition->type)
		{
			if (last_op != ZBX_OPERATION_TYPE_OR)
				zbx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, '(');

			zbx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" ZBX_FS_UI64 "} or ", last_id);
			last_op = ZBX_OPERATION_TYPE_OR;
		}
		else
		{
			zbx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" ZBX_FS_UI64 "}", last_id);

			if (last_op == ZBX_OPERATION_TYPE_OR)
				zbx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, ')');

			zbx_strcpy_alloc(&formula, &formula_alloc, &formula_offset, " and ");

			last_op = ZBX_OPERATION_TYPE_AND;
		}

		last_type = dc_condition->type;
		last_id = dc_condition->corr_conditionid;
	}

	zbx_snprintf_alloc(&formula, &formula_alloc, &formula_offset, "{" ZBX_FS_UI64 "}", last_id);

	if (last_op == ZBX_OPERATION_TYPE_OR)
		zbx_chrcpy_alloc(&formula, &formula_alloc, &formula_offset, ')');

	return formula;

#undef ZBX_OPERATION_TYPE_UNKNOWN
#undef ZBX_OPERATION_TYPE_OR
#undef ZBX_OPERATION_TYPE_AND
}

void zbx_dc_correlation_rules_init(zbx_correlation_rules_t *rules)
{
	zbx_vector_ptr_create(&rules->correlations);
	zbx_hashset_create_ext(&rules->conditions, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
						   (zbx_clean_func_t)corr_condition_clean, ZBX_DEFAULT_MEM_MALLOC_FUNC,
						   ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

	rules->sync_ts = 0;
}

void zbx_dc_correlation_rules_clean(zbx_correlation_rules_t *rules)
{
	zbx_vector_ptr_clear_ext(&rules->correlations, (zbx_clean_func_t)dc_correlation_free);
	zbx_hashset_clear(&rules->conditions);
}

void zbx_dc_correlation_rules_free(zbx_correlation_rules_t *rules)
{
	zbx_dc_correlation_rules_clean(rules);
	zbx_vector_ptr_destroy(&rules->correlations);
	zbx_hashset_destroy(&rules->conditions);
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets correlation rules from configuration cache                   *
 *                                                                            *
 * Parameter: rules   - [IN/OUT] the correlation rules                        *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_correlation_rules_get(zbx_correlation_rules_t *rules)
{
	int i;
	zbx_hashset_iter_t iter;
	const zbx_dc_correlation_t *dc_correlation;
	const zbx_dc_corr_condition_t *dc_condition;
	zbx_correlation_t *correlation;
	zbx_corr_condition_t *condition, condition_local;

	RDLOCK_CACHE;

	/* The correlation rules are refreshed only if the sync timestamp   */
	/* does not match current configuration cache sync timestamp. This  */
	/* allows to locally cache the correlation rules.                   */
	if (config->sync_ts == rules->sync_ts)
	{
		UNLOCK_CACHE;
		return;
	}

	zbx_dc_correlation_rules_clean(rules);

	zbx_hashset_iter_reset(&config->correlations, &iter);
	while (NULL != (dc_correlation = (const zbx_dc_correlation_t *)zbx_hashset_iter_next(&iter)))
	{
		correlation = (zbx_correlation_t *)zbx_malloc(NULL, sizeof(zbx_correlation_t));
		correlation->correlationid = dc_correlation->correlationid;
		correlation->evaltype = dc_correlation->evaltype;
		correlation->name = zbx_strdup(NULL, dc_correlation->name);
		correlation->formula = dc_correlation_formula_dup(dc_correlation);
		zbx_vector_ptr_create(&correlation->conditions);
		zbx_vector_ptr_create(&correlation->operations);

		for (i = 0; i < dc_correlation->conditions.values_num; i++)
		{
			dc_condition = (const zbx_dc_corr_condition_t *)dc_correlation->conditions.values[i];
			condition_local.corr_conditionid = dc_condition->corr_conditionid;
			condition = (zbx_corr_condition_t *)zbx_hashset_insert(&rules->conditions, &condition_local,
																   sizeof(condition_local));
			dc_corr_condition_copy(dc_condition, condition);
			zbx_vector_ptr_append(&correlation->conditions, condition);
		}

		for (i = 0; i < dc_correlation->operations.values_num; i++)
		{
			zbx_vector_ptr_append(&correlation->operations, zbx_dc_corr_operation_dup(
																(const zbx_dc_corr_operation_t *)dc_correlation->operations.values[i]));
		}

		zbx_vector_ptr_append(&rules->correlations, correlation);
	}

	rules->sync_ts = config->sync_ts;

	UNLOCK_CACHE;

	zbx_vector_ptr_sort(&rules->correlations, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Purpose: cache nested group identifiers                                    *
 *                                                                            *
 ******************************************************************************/
void dc_hostgroup_cache_nested_groupids(zbx_dc_hostgroup_t *parent_group)
{
	zbx_dc_hostgroup_t *group;

	if (0 == (parent_group->flags & ZBX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS))
	{
		int index, len;

		zbx_vector_uint64_create_ext(&parent_group->nested_groupids, __config_shmem_malloc_func,
									 __config_shmem_realloc_func, __config_shmem_free_func);

		index = zbx_vector_ptr_bsearch(&config->hostgroups_name, parent_group, dc_compare_hgroups);
		len = strlen(parent_group->name);

		while (++index < config->hostgroups_name.values_num)
		{
			group = (zbx_dc_hostgroup_t *)config->hostgroups_name.values[index];

			if (0 != strncmp(group->name, parent_group->name, len))
				break;

			if ('\0' == group->name[len] || '/' == group->name[len])
				zbx_vector_uint64_append(&parent_group->nested_groupids, group->groupid);
		}

		parent_group->flags |= ZBX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: pre-caches nested groups for groups used in running maintenances  *
 *                                                                            *
 ******************************************************************************/
static void dc_maintenance_precache_nested_groups(void)
{
	zbx_hashset_iter_t iter;
	zbx_dc_maintenance_t *maintenance;
	zbx_vector_uint64_t groupids;
	int i;
	zbx_dc_hostgroup_t *group;

	if (0 == config->maintenances.num_data)
		return;

	zbx_vector_uint64_create(&groupids);
	zbx_hashset_iter_reset(&config->maintenances, &iter);
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_MAINTENANCE_RUNNING != maintenance->state)
			continue;

		zbx_vector_uint64_append_array(&groupids, maintenance->groupids.values,
									   maintenance->groupids.values_num);
	}

	zbx_vector_uint64_sort(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	for (i = 0; i < groupids.values_num; i++)
	{
		if (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups,
																	  &groupids.values[i])))
		{
			dc_hostgroup_cache_nested_groupids(group);
		}
	}

	zbx_vector_uint64_destroy(&groupids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets nested group ids for the specified host group                *
 *          (including the target group id)                                   *
 *                                                                            *
 * Parameter: groupid         - [IN] the parent group identifier              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void dc_get_nested_hostgroupids(zbx_uint64_t groupid, zbx_vector_uint64_t *nested_groupids)
{
	zbx_dc_hostgroup_t *parent_group;

	zbx_vector_uint64_append(nested_groupids, groupid);

	/* The target group id will not be found in the configuration cache if target group was removed */
	/* between call to this function and the configuration cache look-up below. The target group id */
	/* is nevertheless returned so that the SELECT statements of the callers work even if no group  */
	/* was found.                                                                                   */

	if (NULL != (parent_group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups, &groupid)))
	{
		dc_hostgroup_cache_nested_groupids(parent_group);

		if (0 != parent_group->nested_groupids.values_num)
		{
			zbx_vector_uint64_append_array(nested_groupids, parent_group->nested_groupids.values,
										   parent_group->nested_groupids.values_num);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets nested group ids for the specified host groups               *
 *                                                                            *
 * Parameter: groupids        - [IN] the parent group identifiers             *
 *            groupids_num    - [IN] the number of parent groups              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_nested_hostgroupids(zbx_uint64_t *groupids, int groupids_num, zbx_vector_uint64_t *nested_groupids)
{
	int i;

	WRLOCK_CACHE;

	for (i = 0; i < groupids_num; i++)
		dc_get_nested_hostgroupids(groupids[i], nested_groupids);

	UNLOCK_CACHE;

	zbx_vector_uint64_sort(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets hostids belonging to the group and its nested groups         *
 *                                                                            *
 * Parameter: name    - [IN] the group name                                   *
 *            hostids - [OUT] the hostids                                     *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_hostids_by_group_name(const char *name, zbx_vector_uint64_t *hostids)
{
	int i;
	zbx_vector_uint64_t groupids;
	zbx_dc_hostgroup_t group_local, *group;

	zbx_vector_uint64_create(&groupids);

	group_local.name = name;

	WRLOCK_CACHE;

	if (FAIL != (i = zbx_vector_ptr_bsearch(&config->hostgroups_name, &group_local, dc_compare_hgroups)))
	{
		group = (zbx_dc_hostgroup_t *)config->hostgroups_name.values[i];
		dc_get_nested_hostgroupids(group->groupid, &groupids);
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_sort(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	RDLOCK_CACHE;

	for (i = 0; i < groupids.values_num; i++)
	{
		zbx_hashset_iter_t iter;
		zbx_uint64_t *phostid;

		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups,
																	  &groupids.values[i])))
		{
			continue;
		}

		zbx_hashset_iter_reset(&group->hostids, &iter);

		while (NULL != (phostid = (zbx_uint64_t *)zbx_hashset_iter_next(&iter)))
			zbx_vector_uint64_append(hostids, *phostid);
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_destroy(&groupids);

	zbx_vector_uint64_sort(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Purpose: gets active proxy data by its name from configuration cache       *
 *                                                                            *
 * Parameters:                                                                *
 *     name  - [IN] the proxy name                                            *
 *     proxy - [OUT] the proxy data                                           *
 *     error - [OUT] error message                                            *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - proxy data were retrieved successfully                       *
 *     FAIL    - failed to retrieve proxy data, error message is set          *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_get_active_proxy_by_name(const char *name, DC_PROXY *proxy, char **error)
{
	int ret = FAIL;
	const ZBX_DC_HOST *dc_host;
	const ZBX_DC_PROXY *dc_proxy;

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_proxy(name)))
	{
		*error = zbx_dsprintf(*error, "proxy \"%s\" not found", name);
		goto out;
	}

	if (HOST_STATUS_PROXY_ACTIVE != dc_host->status)
	{
		*error = zbx_dsprintf(*error, "recieved hello/heartbeat \"%s\" not from active proxy", name);
		goto out;
	}

	if (NULL == (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->hostid)))
	{
		*error = zbx_dsprintf(*error, "proxy \"%s\" not found in configuration cache", name);
		goto out;
	}

	DCget_proxy(proxy, dc_proxy);
	ret = SUCCEED;
out:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: find proxyid and type for given proxy name                        *
 *                                                                            *
 * Parameters:                                                                *
 *     name    - [IN] the proxy name                                          *
 *     proxyid - [OUT] the proxyid                                            *
 *     type    - [OUT] the type of a proxy                                    *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - id/type were retrieved successfully                          *
 *     FAIL    - failed to find proxy in cache                                *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_get_proxyid_by_name(const char *name, zbx_uint64_t *proxyid, unsigned char *type)
{
	int ret = FAIL;
	const ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	if (NULL != (dc_host = DCfind_proxy(name)))
	{
		if (NULL != type)
			*type = dc_host->status;

		*proxyid = dc_host->hostid;

		ret = SUCCEED;
	}

	UNLOCK_CACHE;

	return ret;
}

int zbx_dc_update_passive_proxy_nextcheck(zbx_uint64_t proxyid)
{
	int ret = SUCCEED;
	ZBX_DC_PROXY *dc_proxy;

	WRLOCK_CACHE;

	if (NULL == (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &proxyid)))
		ret = FAIL;
	else
		dc_proxy->proxy_config_nextcheck = (int)time(NULL);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieve proxyids for all cached proxies                          *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_all_proxies(zbx_vector_cached_proxy_ptr_t *proxies)
{
	ZBX_DC_HOST_H *dc_host;
	zbx_hashset_iter_t iter;

	RDLOCK_CACHE;

	zbx_vector_cached_proxy_ptr_reserve(proxies, (size_t)config->hosts_p.num_data);
	zbx_hashset_iter_reset(&config->hosts_p, &iter);

	while (NULL != (dc_host = (ZBX_DC_HOST_H *)zbx_hashset_iter_next(&iter)))
	{
		zbx_cached_proxy_t *proxy;

		proxy = (zbx_cached_proxy_t *)zbx_malloc(NULL, sizeof(zbx_cached_proxy_t));

		proxy->name = zbx_strdup(NULL, dc_host->host_ptr->host);
		proxy->hostid = dc_host->host_ptr->hostid;
		proxy->status = dc_host->host_ptr->status;

		zbx_vector_cached_proxy_ptr_append(proxies, proxy);
	}

	UNLOCK_CACHE;
}

void zbx_cached_proxy_free(zbx_cached_proxy_t *proxy)
{
	zbx_free(proxy->name);
	zbx_free(proxy);
}

int zbx_dc_get_proxy_name_type_by_id(zbx_uint64_t proxyid, int *status, char **name)
{
	int ret = SUCCEED;
	ZBX_DC_HOST *dc_host;

	RDLOCK_CACHE;

	if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &proxyid)))
		ret = FAIL;
	else
	{
		*status = dc_host->status;
		*name = zbx_strdup(NULL, dc_host->host);
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get data of all network interfaces for a host in configuration    *
 *          cache                                                             *
 *                                                                            *
 * Parameter: hostid     - [IN] the host identifier                           *
 *            interfaces - [OUT] array with interface data                    *
 *            n          - [OUT] number of allocated 'interfaces' elements    *
 *                                                                            *
 * Return value: SUCCEED - interface data retrieved successfully              *
 *               FAIL    - host not found                                     *
 *                                                                            *
 * Comments: if host is found but has no interfaces (should not happen) this  *
 *           function sets 'n' to 0 and no memory is allocated for            *
 *           'interfaces'. It is a caller responsibility to deallocate        *
 *           memory of 'interfaces' and its components.                       *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_get_host_interfaces(zbx_uint64_t hostid, DC_INTERFACE2 **interfaces, int *n)
{
	const ZBX_DC_HOST *host;
	int i, ret = FAIL;

	if (0 == hostid)
		return FAIL;

	RDLOCK_CACHE;

	/* find host entry in 'config->hosts' hashset */

	if (NULL == (host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* allocate memory for results */

	if (0 < (*n = host->interfaces_v.values_num))
		*interfaces = (DC_INTERFACE2 *)zbx_malloc(NULL, sizeof(DC_INTERFACE2) * (size_t)*n);

	/* copy data about all host interfaces */

	for (i = 0; i < *n; i++)
	{
		const ZBX_DC_INTERFACE *src = (const ZBX_DC_INTERFACE *)host->interfaces_v.values[i];
		DC_INTERFACE2 *dst = *interfaces + i;

		dst->interfaceid = src->interfaceid;
		dst->type = src->type;
		dst->main = src->main;
		dst->useip = src->useip;
		zbx_strscpy(dst->ip_orig, src->ip);
		zbx_strscpy(dst->dns_orig, src->dns);
		zbx_strscpy(dst->port_orig, src->port);
		dst->addr = (1 == src->useip ? dst->ip_orig : dst->dns_orig);

		if (INTERFACE_TYPE_SNMP == dst->type)
		{
			ZBX_DC_SNMPINTERFACE *snmp;

			if (NULL == (snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp,
																		   &dst->interfaceid)))
			{
				zbx_free(*interfaces);
				goto unlock;
			}

			dst->bulk = snmp->bulk;
			dst->snmp_version = snmp->version;
		}
	}

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: update automatic inventory in configuration cache                 *
 *                                                                            *
 ******************************************************************************/
void DCconfig_update_inventory_values(const zbx_vector_ptr_t *inventory_values)
{
	ZBX_DC_HOST_INVENTORY *host_inventory = NULL;
	int i;

	WRLOCK_CACHE;

	for (i = 0; i < inventory_values->values_num; i++)
	{
		const zbx_inventory_value_t *inventory_value = (zbx_inventory_value_t *)inventory_values->values[i];
		const char **value;

		if (NULL == host_inventory || inventory_value->hostid != host_inventory->hostid)
		{
			host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories_auto,
																		 &inventory_value->hostid);

			if (NULL == host_inventory)
				continue;
		}

		value = &host_inventory->values[inventory_value->idx];

		dc_strpool_replace((NULL != *value ? 1 : 0), value, inventory_value->value);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 * Comments: This function must be called inside configuration cache read     *
 *           (or write) lock.                                                 *
 *                                                                            *
 ******************************************************************************/
static int dc_get_host_inventory_value_by_hostid(zbx_uint64_t hostid, char **replace_to, int value_idx)
{
	const ZBX_DC_HOST_INVENTORY *dc_inventory;

	if (NULL != (dc_inventory = (const ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories_auto,
																				  &hostid)) &&
		NULL != dc_inventory->values[value_idx])
	{
		*replace_to = zbx_strdup(*replace_to, dc_inventory->values[value_idx]);
		return SUCCEED;
	}

	if (NULL != (dc_inventory = (const ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories,
																				  &hostid)))
	{
		*replace_to = zbx_strdup(*replace_to, dc_inventory->values[value_idx]);
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int DCget_host_inventory_value_by_itemid(zbx_uint64_t itemid, char **replace_to, int value_idx)
{
	const ZBX_DC_ITEM *dc_item;
	int ret = FAIL;

	RDLOCK_CACHE;

	if (NULL != (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		ret = dc_get_host_inventory_value_by_hostid(dc_item->hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int DCget_host_inventory_value_by_hostid(zbx_uint64_t hostid, char **replace_to, int value_idx)
{
	int ret;

	RDLOCK_CACHE;

	ret = dc_get_host_inventory_value_by_hostid(hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: checks/returns trigger dependencies for a set of triggers         *
 *                                                                            *
 * Parameter: triggerids  - [IN] the currently processing trigger ids         *
 *            deps        - [OUT] list of dependency check results for failed *
 *                                or unresolved dependencies                  *
 *                                                                            *
 * Comments: This function returns list of zbx_trigger_dep_t structures       *
 *           for failed or unresolved dependency checks.                      *
 *           Dependency check is failed if any of the master triggers that    *
 *           are not being processed in this batch (present in triggerids     *
 *           vector) has a problem value.                                     *
 *           Dependency check is unresolved if a master trigger is being      *
 *           processed in this batch (present in triggerids vector) and no    *
 *           other master triggers have problem value.                        *
 *           Dependency check is successful if all master triggers (if any)   *
 *           have OK value and are not being processed in this batch.         *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_trigger_dependencies(const zbx_vector_uint64_t *triggerids, zbx_vector_ptr_t *deps)
{
	int i, ret;
	const ZBX_DC_TRIGGER_DEPLIST *trigdep;
	zbx_vector_uint64_t masterids;
	zbx_trigger_dep_t *dep;

	zbx_vector_uint64_create(&masterids);
	zbx_vector_uint64_reserve(&masterids, 64);

	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (trigdep = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps,
																			&triggerids->values[i])))
		{
			continue;
		}

		if (FAIL == (ret = DCconfig_check_trigger_dependencies_rec(trigdep, 0, triggerids, &masterids)) ||
			0 != masterids.values_num)
		{
			dep = (zbx_trigger_dep_t *)zbx_malloc(NULL, sizeof(zbx_trigger_dep_t));
			dep->triggerid = triggerids->values[i];
			zbx_vector_uint64_create(&dep->masterids);

			if (SUCCEED == ret)
			{
				dep->status = ZBX_TRIGGER_DEPENDENCY_UNRESOLVED;
				zbx_vector_uint64_append_array(&dep->masterids, masterids.values, masterids.values_num);
			}
			else
				dep->status = ZBX_TRIGGER_DEPENDENCY_FAIL;

			zbx_vector_ptr_append(deps, dep);
		}

		zbx_vector_uint64_clear(&masterids);
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_destroy(&masterids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: reschedules items that are processed by the target daemon         *
 *                                                                            *
 * Parameter: itemids       - [IN] the item identifiers                       *
 *            nextcheck     - [IN] the scheduled time                         *
 *            proxy_hostids - [OUT] the proxy_hostids of the given itemids    *
 *                                  (optional, can be NULL)                   *
 *                                                                            *
 * Comments: On server this function reschedules items monitored by server.   *
 *           On proxy only items monitored by the proxy is accessible, so     *
 *           all items can be safely rescheduled.                             *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_reschedule_items(const zbx_vector_uint64_t *itemids, int nextcheck, zbx_uint64_t *proxy_hostids)
{
	int i;
	ZBX_DC_ITEM *dc_item;
	ZBX_DC_HOST *dc_host;
	zbx_uint64_t proxy_hostid;

	WRLOCK_CACHE;

	for (i = 0; i < itemids->values_num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids->values[i])) ||
			NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot perform check now for itemid [" ZBX_FS_UI64 "]"
										  ": item is not in cache",
					   itemids->values[i]);

			proxy_hostid = 0;
		}
		else if (ZBX_JAN_2038 == glb_state_item_get_nextcheck(dc_item->itemid))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot perform check now for item \"%s\" on host \"%s\""
										  ": item configuration error",
					   dc_item->key, dc_host->host);

			proxy_hostid = 0;
		}
		else if (0 == (proxy_hostid = dc_host->proxy_hostid) ||
				 SUCCEED == is_item_processed_by_server(dc_item->type, dc_item->key))
		{
			if (SUCCEED == glb_might_be_async_polled(dc_item, dc_host) ) {
				poller_item_add_notify(dc_item->type, dc_item->key, dc_item->itemid, dc_host->hostid);
				
				LOG_INF("Added poll now notify for async item %ld", dc_item->itemid);

			} else {
				dc_requeue_item_at(dc_item, dc_host, nextcheck);
				proxy_hostid = 0;
			}
		}

		if (NULL != proxy_hostids)
			proxy_hostids[i] = proxy_hostid;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: stop suppress mode of the nodata() trigger                        *
 *                                                                            *
 * Parameter: subscriptions - [IN] the array of trigger id and time of values *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_proxy_update_nodata(zbx_vector_uint64_pair_t *subscriptions)
{
	ZBX_DC_PROXY *proxy = NULL;
	int i;
	zbx_uint64_pair_t p;

	WRLOCK_CACHE;

	for (i = 0; i < subscriptions->values_num; i++)
	{
		p = subscriptions->values[i];

		if ((NULL == proxy || p.first != proxy->hostid) &&
			NULL == (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &p.first)))
		{
			continue;
		}

		if (0 == (proxy->nodata_win.flags & ZBX_PROXY_SUPPRESS_ACTIVE))
			continue;

		if (0 != (proxy->nodata_win.flags & ZBX_PROXY_SUPPRESS_MORE) &&
			(int)p.second > proxy->nodata_win.period_end)
		{
			continue;
		}

		proxy->nodata_win.values_num--;

		if (0 < proxy->nodata_win.values_num || 0 != (proxy->nodata_win.flags & ZBX_PROXY_SUPPRESS_MORE))
			continue;

		proxy->nodata_win.flags = ZBX_PROXY_SUPPRESS_DISABLE;
		proxy->nodata_win.period_end = 0;
		proxy->nodata_win.values_num = 0;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: updates changed proxy data in configuration cache and updates     *
 *          diff flags to reflect the updated data                            *
 *                                                                            *
 * Parameter: diff - [IN/OUT] the properties to update                        *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_update_proxy(zbx_proxy_diff_t *diff)
{
	ZBX_DC_PROXY *proxy;

	WRLOCK_CACHE;

	if (diff->lastaccess < config->proxy_lastaccess_ts)
		diff->lastaccess = config->proxy_lastaccess_ts;

	if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &diff->hostid)))
	{
		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS))
		{
			int lost = 0; /* communication lost */

			if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_CONFIG))
			{
				int delay = diff->lastaccess - proxy->lastaccess;

				if (NET_DELAY_MAX < delay)
					lost = 1;
			}

			if (0 == lost && proxy->lastaccess != diff->lastaccess)
				proxy->lastaccess = diff->lastaccess;

			/* proxy last access in database is updated separately in  */
			/* every ZBX_PROXY_LASTACCESS_UPDATE_FREQUENCY seconds     */
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION))
		{
			if (0 != strcmp(proxy->version_str, diff->version_str))
				dc_strpool_replace(1, &proxy->version_str, diff->version_str);

			if (proxy->version_int != diff->version_int)
			{
				proxy->version_int = diff->version_int;
				proxy->compatibility = diff->compatibility;
			}
			else
				diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
		{
			if (proxy->auto_compress == diff->compress)
				diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS);
			proxy->auto_compress = diff->compress;
		}
		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR))
		{
			proxy->last_version_error_time = diff->last_version_error_time;
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_PROXYDELAY))
		{
			proxy->proxy_delay = diff->proxy_delay;
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_PROXYDELAY);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_SUPPRESS_WIN))
		{
			zbx_proxy_suppress_t *ps_win = &proxy->nodata_win, *ds_win = &diff->nodata_win;

			if ((ps_win->flags & ZBX_PROXY_SUPPRESS_ACTIVE) != (ds_win->flags & ZBX_PROXY_SUPPRESS_ACTIVE))
			{
				ps_win->period_end = ds_win->period_end;
			}

			ps_win->flags = ds_win->flags;

			if (0 > ps_win->values_num) /* some new values were processed faster than old */
				ps_win->values_num = 0; /* we will suppress more                          */

			ps_win->values_num += ds_win->values_num;
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_SUPPRESS_WIN);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: returns proxy lastaccess changes since last lastaccess request    *
 *                                                                            *
 * Parameter: lastaccess - [OUT] last access updates for proxies that need    *
 *                               to be synced with database, sorted by        *
 *                               hostid                                       *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_proxy_lastaccess(zbx_vector_uint64_pair_t *lastaccess)
{
	ZBX_DC_PROXY *proxy;
	int now;

	if (ZBX_PROXY_LASTACCESS_UPDATE_FREQUENCY < (now = time(NULL)) - config->proxy_lastaccess_ts)
	{
		zbx_hashset_iter_t iter;

		WRLOCK_CACHE;

		zbx_hashset_iter_reset(&config->proxies, &iter);

		while (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
		{
			if (proxy->lastaccess >= config->proxy_lastaccess_ts)
			{
				zbx_uint64_pair_t pair = {proxy->hostid, proxy->lastaccess};

				zbx_vector_uint64_pair_append(lastaccess, pair);
			}
		}

		config->proxy_lastaccess_ts = now;

		UNLOCK_CACHE;

		zbx_vector_uint64_pair_sort(lastaccess, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: returns session token                                             *
 *                                                                            *
 * Return value: pointer to session token (NULL for server).                  *
 *                                                                            *
 * Comments: The session token is generated during configuration cache        *
 *           initialization and is not changed later. Therefore no locking    *
 *           is required.                                                     *
 *                                                                            *
 ******************************************************************************/
const char *zbx_dc_get_session_token(void)
{
	return config->session_token;
}

/******************************************************************************
 *                                                                            *
 * Purpose: return session, create a new session if none found                *
 *                                                                            *
 * Parameter: hostid - [IN] the host (proxy) identifier                       *
 *            token  - [IN] the session token (not NULL)                      *
 *                                                                            *
 * Return value: pointer to data session.                                     *
 *                                                                            *
 * Comments: The last_valueid property of the returned session object can be  *
 *           updated directly without locking cache because only one data     *
 *           session is updated at the same time and after retrieving the     *
 *           session object will not be deleted for 24 hours.                 *
 *                                                                            *
 ******************************************************************************/
zbx_session_t *zbx_dc_get_or_create_session(zbx_uint64_t hostid, const char *token,
											zbx_session_type_t session_type)
{
	zbx_session_t *session, session_local;
	time_t now;

	now = time(NULL);
	session_local.hostid = hostid;
	session_local.token = token;

	RDLOCK_CACHE;
	session = (zbx_session_t *)zbx_hashset_search(&config->sessions[session_type], &session_local);
	UNLOCK_CACHE;

	if (NULL == session)
	{
		session_local.last_id = 0;
		session_local.lastaccess = now;

		WRLOCK_CACHE;
		session_local.token = dc_strdup(token);
		session = (zbx_session_t *)zbx_hashset_insert(&config->sessions[session_type], &session_local,
													  sizeof(session_local));
		UNLOCK_CACHE;
	}
	else
		session->lastaccess = now;

	return session;
}

/******************************************************************************
 *                                                                            *
 * Purpose: update session revision/lastaccess in cache or create new session *
 *          if necessary                                                      *
 *                                                                            *
 * Parameter: hostid - [IN] the host (proxy) identifier                       *
 *            token  - [IN] the session token (not NULL)                      *
 *            session_config_revision - [IN] the session configuration        *
 *                          revision                                          *
 *            dc_revision - [OUT] - the cached configuration revision         *
 *                                                                            *
 * Return value: The number of created sessions                               *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_register_config_session(zbx_uint64_t hostid, const char *token, zbx_uint64_t session_config_revision,
								   zbx_dc_revision_t *dc_revision)
{
	zbx_session_t *session, session_local;
	time_t now;

	now = time(NULL);
	session_local.hostid = hostid;
	session_local.token = token;

	RDLOCK_CACHE;
	if (NULL != (session = (zbx_session_t *)zbx_hashset_search(&config->sessions[ZBX_SESSION_TYPE_CONFIG],
															   &session_local)))
	{
		/* one session cannot be updated at the same time by different processes,            */
		/* so updating its properties without reallocating memory can be done with read lock */
		session->last_id = session_config_revision;
		session_local.lastaccess = now;
	}
	*dc_revision = config->revision;
	UNLOCK_CACHE;

	if (NULL != session)
		return 0;

	session_local.last_id = session_config_revision;
	session_local.lastaccess = now;

	WRLOCK_CACHE;
	session_local.token = dc_strdup(token);
	zbx_hashset_insert(&config->sessions[ZBX_SESSION_TYPE_CONFIG], &session_local, sizeof(session_local));
	UNLOCK_CACHE;

	return 1; /* a session was created */
}

/******************************************************************************
 *                                                                            *
 * Purpose: removes data sessions not accessed for 25 hours                   *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_cleanup_sessions(void)
{
	zbx_session_t *session;
	zbx_hashset_iter_t iter;
	time_t now;
	int i;

	now = time(NULL);

	WRLOCK_CACHE;

	for (i = 0; i < ZBX_SESSION_TYPE_COUNT; i++)
	{
		zbx_hashset_iter_reset(&config->sessions[i], &iter);
		while (NULL != (session = (zbx_session_t *)zbx_hashset_iter_next(&iter)))
		{
			/* should be more than MAX_ACTIVE_CHECKS_REFRESH_FREQUENCY */
			if (session->lastaccess + SEC_PER_DAY + SEC_PER_HOUR <= now)
			{
				__config_shmem_free_func((char *)session->token);
				zbx_hashset_iter_remove(&iter);
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: removes autoreg hosts not accessed for 25 hours                   *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_cleanup_autoreg_host(void)
{
	ZBX_DC_AUTOREG_HOST *autoreg_host;
	zbx_hashset_iter_t iter;
	time_t now;

	now = time(NULL);

	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->autoreg_hosts, &iter);
	while (NULL != (autoreg_host = (ZBX_DC_AUTOREG_HOST *)zbx_hashset_iter_next(&iter)))
	{
		/* should be more than MAX_ACTIVE_CHECKS_REFRESH_FREQUENCY */
		if (autoreg_host->timestamp + SEC_PER_DAY + SEC_PER_HOUR <= now)
		{
			autoreg_host_free_data(autoreg_host);
			zbx_hashset_remove_direct(&config->autoreg_hosts, autoreg_host);
		}
	}

	UNLOCK_CACHE;
}

static void zbx_gather_item_tags(ZBX_DC_ITEM *item, zbx_vector_ptr_t *item_tags)
{
	zbx_dc_item_tag_t *dc_tag;
	zbx_item_tag_t *tag;
	int i;

	for (i = 0; i < item->tags.values_num; i++)
	{
		dc_tag = (zbx_dc_item_tag_t *)item->tags.values[i];
		tag = (zbx_item_tag_t *)zbx_malloc(NULL, sizeof(zbx_item_tag_t));
		tag->tag.tag = zbx_strdup(NULL, dc_tag->tag);
		tag->tag.value = zbx_strdup(NULL, dc_tag->value);
		zbx_vector_ptr_append(item_tags, tag);
	}
}

static void zbx_gather_tags_from_host(zbx_uint64_t hostid, zbx_vector_ptr_t *item_tags)
{
	zbx_dc_host_tag_index_t *dc_tag_index;
	zbx_dc_host_tag_t *dc_tag;
	zbx_item_tag_t *tag;
	int i;

	if (NULL != (dc_tag_index = zbx_hashset_search(&config->host_tags_index, &hostid)))
	{
		for (i = 0; i < dc_tag_index->tags.values_num; i++)
		{
			dc_tag = (zbx_dc_host_tag_t *)dc_tag_index->tags.values[i];
			tag = (zbx_item_tag_t *)zbx_malloc(NULL, sizeof(zbx_item_tag_t));
			tag->tag.tag = zbx_strdup(NULL, dc_tag->tag);
			tag->tag.value = zbx_strdup(NULL, dc_tag->value);
			zbx_vector_ptr_append(item_tags, tag);
		}
	}
}

static void zbx_gather_tags_from_template_chain(zbx_uint64_t itemid, zbx_vector_ptr_t *item_tags)
{
	ZBX_DC_TEMPLATE_ITEM *item;

	if (NULL != (item = (ZBX_DC_TEMPLATE_ITEM *)zbx_hashset_search(&config->template_items, &itemid)))
	{
		zbx_gather_tags_from_host(item->hostid, item_tags);

		if (0 != item->templateid)
			zbx_gather_tags_from_template_chain(item->templateid, item_tags);
	}
}

void zbx_get_item_tags(zbx_uint64_t itemid, zbx_vector_ptr_t *item_tags)
{
	ZBX_DC_ITEM *item;
	zbx_item_tag_t *tag;
	int n, i;

	if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		return;

	n = item_tags->values_num;

	zbx_gather_item_tags(item, item_tags);

	zbx_gather_tags_from_host(item->hostid, item_tags);

	if (0 != item->templateid)
		zbx_gather_tags_from_template_chain(item->templateid, item_tags);

	/* check for discovered item */
	if (ZBX_FLAG_DISCOVERY_CREATED == item->flags)
	{
		ZBX_DC_ITEM_DISCOVERY *item_discovery;

		if (NULL != (item_discovery = (ZBX_DC_ITEM_DISCOVERY *)zbx_hashset_search(&config->item_discovery,
																				  &itemid)))
		{
			ZBX_DC_PROTOTYPE_ITEM *prototype_item;

			if (NULL != (prototype_item = (ZBX_DC_PROTOTYPE_ITEM *)zbx_hashset_search(
							 &config->prototype_items, &item_discovery->parent_itemid)))
			{
				if (0 != prototype_item->templateid)
					zbx_gather_tags_from_template_chain(prototype_item->templateid, item_tags);
			}
		}
	}

	/* assign hostid and itemid values to newly gathered tags */
	for (i = n; i < item_tags->values_num; i++)
	{
		tag = (zbx_item_tag_t *)item_tags->values[i];
		tag->hostid = item->hostid;
		tag->itemid = item->itemid;
	}
}

void zbx_dc_get_item_tags(zbx_uint64_t itemid, zbx_vector_ptr_t *item_tags)
{
	RDLOCK_CACHE;

	zbx_get_item_tags(itemid, item_tags);

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves proxy suppress window data from the cache               *
 *                                                                            *
 * Parameters: hostid     - [IN] proxy host id                                *
 *             nodata_win - [OUT] suppress window data                        *
 *             lastaccess - [OUT] proxy last access time                      *
 *                                                                            *
 * Return value: SUCCEED - the data is retrieved                              *
 *               FAIL    - the data cannot be retrieved, proxy not found in   *
 *                         configuration cache                                *
 *                                                                            *
 ******************************************************************************/
int DCget_proxy_nodata_win(zbx_uint64_t hostid, zbx_proxy_suppress_t *nodata_win, int *lastaccess)
{
	const ZBX_DC_PROXY *dc_proxy;
	int ret;

	RDLOCK_CACHE;

	if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
	{
		const zbx_proxy_suppress_t *proxy_nodata_win = &dc_proxy->nodata_win;

		nodata_win->period_end = proxy_nodata_win->period_end;
		nodata_win->values_num = proxy_nodata_win->values_num;
		nodata_win->flags = proxy_nodata_win->flags;
		*lastaccess = dc_proxy->lastaccess;
		ret = SUCCEED;
	}
	else
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves proxy delay from the cache                              *
 *                                                                            *
 * Parameters: name  - [IN] proxy host name                                   *
 *             delay - [OUT] proxy delay                                      *
 *             error - [OUT]                                                  *
 *                                                                            *
 * Return value: SUCCEED - proxy delay is retrieved                           *
 *               FAIL    - proxy delay cannot be retrieved                    *
 *                                                                            *
 ******************************************************************************/
int DCget_proxy_delay_by_name(const char *name, int *delay, char **error)
{
	const ZBX_DC_HOST *dc_host;
	const ZBX_DC_PROXY *dc_proxy;
	int ret;

	RDLOCK_CACHE;
	dc_host = DCfind_proxy(name);

	if (NULL == dc_host)
	{
		*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", name);
		ret = FAIL;
	}
	else
	{
		if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->hostid)))
		{
			*delay = dc_proxy->proxy_delay;
			ret = SUCCEED;
		}
		else
		{
			*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", name);
			ret = FAIL;
		}
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: retrieves proxy lastaccess from the cache by name                 *
 *                                                                            *
 * Parameters: name       - [IN] proxy host name                              *
 *             lastaccess - [OUT] proxy lastaccess                            *
 *             error      - [OUT]                                             *
 *                                                                            *
 * Return value: SUCCEED - proxy lastaccess is retrieved                      *
 *               FAIL    - proxy lastaccess cannot be retrieved               *
 *                                                                            *
 ******************************************************************************/
int DCget_proxy_lastaccess_by_name(const char *name, int *lastaccess, char **error)
{
	const ZBX_DC_HOST *dc_host;
	const ZBX_DC_PROXY *dc_proxy;
	int ret;

	RDLOCK_CACHE;
	dc_host = DCfind_proxy(name);

	if (NULL == dc_host)
	{
		*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", name);
		ret = FAIL;
	}
	else
	{
		if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->hostid)))
		{
			*lastaccess = dc_proxy->lastaccess;
			ret = SUCCEED;
		}
		else
		{
			*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", name);
			ret = FAIL;
		}
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get data of all proxies from configuration cache and pack into    *
 *          JSON for LLD                                                      *
 *                                                                            *
 * Parameter: data   - [OUT] JSON with proxy data                             *
 *            error  - [OUT] error message                                    *
 *                                                                            *
 * Return value: SUCCEED - interface data in JSON, 'data' is allocated        *
 *               FAIL    - proxy not found, 'error' message is allocated      *
 *                                                                            *
 * Comments: Allocates memory.                                                *
 *           If there are no proxies, an empty JSON {"data":[]} is returned.  *
 *                                                                            *
 ******************************************************************************/
int zbx_proxy_discovery_get(char **data, char **error)
{
	int i, ret = SUCCEED;
	zbx_vector_cached_proxy_ptr_t proxies;
	struct zbx_json json;

	WRLOCK_CACHE;

	dc_status_update();

	UNLOCK_CACHE;

	zbx_json_initarray(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_vector_cached_proxy_ptr_create(&proxies);
	zbx_dc_get_all_proxies(&proxies);

	RDLOCK_CACHE;

	for (i = 0; i < proxies.values_num; i++)
	{
		zbx_cached_proxy_t *proxy;
		const ZBX_DC_HOST *dc_host;
		const ZBX_DC_PROXY *dc_proxy;

		proxy = proxies.values[i];

		zbx_json_addobject(&json, NULL);

		zbx_json_addstring(&json, "name", proxy->name, ZBX_JSON_TYPE_STRING);

		if (HOST_STATUS_PROXY_PASSIVE == proxy->status)
			zbx_json_addstring(&json, "passive", "true", ZBX_JSON_TYPE_INT);
		else
			zbx_json_addstring(&json, "passive", "false", ZBX_JSON_TYPE_INT);

		dc_host = DCfind_proxy(proxy->name);

		if (NULL == dc_host)
		{
			*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", proxy->name);
			ret = FAIL;
			goto clean;
		}
		else
		{
			unsigned int encryption;

			if (HOST_STATUS_PROXY_PASSIVE == proxy->status)
				encryption = dc_host->tls_connect;
			else
				encryption = dc_host->tls_accept;

			if (0 < (encryption & ZBX_TCP_SEC_UNENCRYPTED))
				zbx_json_addstring(&json, "unencrypted", "true", ZBX_JSON_TYPE_INT);
			else
				zbx_json_addstring(&json, "unencrypted", "false", ZBX_JSON_TYPE_INT);

			if (0 < (encryption & ZBX_TCP_SEC_TLS_PSK))
				zbx_json_addstring(&json, "psk", "true", ZBX_JSON_TYPE_INT);
			else
				zbx_json_addstring(&json, "psk", "false", ZBX_JSON_TYPE_INT);

			if (0 < (encryption & ZBX_TCP_SEC_TLS_CERT))
				zbx_json_addstring(&json, "cert", "true", ZBX_JSON_TYPE_INT);
			else
				zbx_json_addstring(&json, "cert", "false", ZBX_JSON_TYPE_INT);

			zbx_json_adduint64(&json, "items", dc_host->items_active_normal + dc_host->items_active_notsupported);

			if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies,
																			 &dc_host->hostid)))
			{
				if (1 == dc_proxy->auto_compress)
					zbx_json_addstring(&json, "compression", "true", ZBX_JSON_TYPE_INT);
				else
					zbx_json_addstring(&json, "compression", "false", ZBX_JSON_TYPE_INT);

				zbx_json_addstring(&json, "version", dc_proxy->version_str, ZBX_JSON_TYPE_STRING);

				zbx_json_adduint64(&json, "compatibility", dc_proxy->compatibility);

				if (0 < dc_proxy->lastaccess)
					zbx_json_addint64(&json, "last_seen", time(NULL) - dc_proxy->lastaccess);
				else
					zbx_json_addint64(&json, "last_seen", -1);

				zbx_json_adduint64(&json, "hosts", dc_proxy->hosts_monitored);

				zbx_json_addfloat(&json, "requiredperformance", dc_proxy->required_performance);
			}
			else
			{
				*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.",
									  proxy->name);
				ret = FAIL;
				goto clean;
			}
		}
		zbx_json_close(&json);
	}
clean:
	UNLOCK_CACHE;

	if (SUCCEED == ret)
	{
		zbx_json_close(&json);
		*data = zbx_strdup(NULL, json.buffer);
	}

	zbx_json_free(&json);
	zbx_vector_cached_proxy_ptr_clear_ext(&proxies, zbx_cached_proxy_free);
	zbx_vector_cached_proxy_ptr_destroy(&proxies);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: returns server/proxy instance id                                  *
 *                                                                            *
 * Return value: the instance id                                              *
 *                                                                            *
 ******************************************************************************/
const char *zbx_dc_get_instanceid(void)
{
	/* instanceid is initialized during the first configuration cache synchronization */
	/* and is never updated - so it can be accessed without locking cache             */
	return config->config->instanceid;
}

/******************************************************************************
 *                                                                            *
 * Parameters: params - [IN] the function parameters                          *
 *             hostid - [IN] host of the item used in function                *
 *                                                                            *
 * Return value: The function parameters with expanded user macros.           *
 *                                                                            *
 ******************************************************************************/
char *zbx_dc_expand_user_macros_in_func_params(const char *params, zbx_uint64_t hostid)
{
	const char *ptr;
	size_t params_len;
	char *buf;
	size_t buf_alloc, buf_offset = 0, sep_pos;
	zbx_dc_um_handle_t *um_handle;

	if ('\0' == *params)
		return zbx_strdup(NULL, "");

	buf_alloc = params_len = strlen(params);
	buf = zbx_malloc(NULL, buf_alloc);

	um_handle = zbx_dc_open_user_macros();

	for (ptr = params; ptr < params + params_len; ptr += sep_pos + 1)
	{
		size_t param_pos, param_len;
		int quoted;
		char *param;

		zbx_function_param_parse(ptr, &param_pos, &param_len, &sep_pos);

		param = zbx_function_param_unquote_dyn(ptr + param_pos, param_len, &quoted);
		(void)zbx_dc_expand_user_macros(um_handle, &param, &hostid, 1, NULL);

		if (SUCCEED == zbx_function_param_quote(&param, quoted))
			zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, param);
		else
			zbx_strncpy_alloc(&buf, &buf_alloc, &buf_offset, ptr + param_pos, param_len);

		if (',' == ptr[sep_pos])
			zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, ',');

		zbx_free(param);
	}

	zbx_dc_close_user_macros(um_handle);

	return buf;
}


int zbx_dc_maintenance_has_tags(void)
{
	int ret;

	RDLOCK_CACHE;
	ret = config->maintenance_tags.num_data != 0 ? SUCCEED : FAIL;
	UNLOCK_CACHE;

	return ret;
}

/* external user macro cache API */

/******************************************************************************
 *                                                                            *
 * Purpose: open handle for user macro resolving in the specified security    *
 *          level                                                             *
 *                                                                            *
 * Parameters: macro_env - [IN] - the macro resolving environment:            *
 *                                  ZBX_MACRO_ENV_NONSECURE                   *
 *                                  ZBX_MACRO_ENV_SECURE                      *
 *                                  ZBX_MACRO_ENV_DEFAULT (last opened or     *
 *                                    non-secure environment)                 *
 *                                                                            *
 * Return value: the handle for macro resolving, must be closed with          *
 *        zbx_dc_close_user_macros()                                          *
 *                                                                            *
 * Comments: First handle will lock user macro cache in configuration cache.  *
 *           Consequent openings within the same process without closing will *
 *           reuse the locked cache until all opened caches are closed.       *
 *                                                                            *
 ******************************************************************************/
static zbx_dc_um_handle_t *dc_open_user_macros(unsigned char macro_env)
{
	zbx_dc_um_handle_t *handle;
	static zbx_um_cache_t *um_cache = NULL;

	handle = (zbx_dc_um_handle_t *)zbx_malloc(NULL, sizeof(zbx_dc_um_handle_t));

	if (NULL != dc_um_handle)
	{
		if (ZBX_MACRO_ENV_DEFAULT == macro_env)
			macro_env = dc_um_handle->macro_env;
	}
	else
	{
		if (ZBX_MACRO_ENV_DEFAULT == macro_env)
			macro_env = ZBX_MACRO_ENV_NONSECURE;
	}

	handle->macro_env = macro_env;
	handle->prev = dc_um_handle;
	handle->cache = &um_cache;

	dc_um_handle = handle;

	return handle;
}

zbx_dc_um_handle_t *zbx_dc_open_user_macros(void)
{
	return dc_open_user_macros(ZBX_MACRO_ENV_DEFAULT);
}

zbx_dc_um_handle_t *zbx_dc_open_user_macros_secure(void)
{
	return dc_open_user_macros(ZBX_MACRO_ENV_SECURE);
}

zbx_dc_um_handle_t *zbx_dc_open_user_macros_masked(void)
{
	return dc_open_user_macros(ZBX_MACRO_ENV_NONSECURE);
}

static const zbx_um_cache_t *dc_um_get_cache(const zbx_dc_um_handle_t *um_handle)
{
	if (NULL == *um_handle->cache)
	{
		WRLOCK_CACHE;
		*um_handle->cache = config->um_cache;
		config->um_cache->refcount++;
		UNLOCK_CACHE;
	}

	return *um_handle->cache;
}

/******************************************************************************
 *                                                                            *
 * Purpose: closes user macro resolving handle                                *
 *                                                                            *
 * Comments: Closing the last opened handle within process will release locked*
 *           user macro cache in the configuration cache.                     *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_close_user_macros(zbx_dc_um_handle_t *um_handle)
{
	if (NULL == um_handle->prev && NULL != *um_handle->cache)
	{

		WRLOCK_CACHE;
		um_cache_release(*um_handle->cache);
		UNLOCK_CACHE;

		*um_handle->cache = NULL;
	}

	dc_um_handle = um_handle->prev;
	zbx_free(um_handle);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get user macro using the specified hosts                          *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_user_macro(const zbx_dc_um_handle_t *um_handle, const char *macro, const zbx_uint64_t *hostids,
						   int hostids_num, char **value)
{
	um_cache_resolve(dc_um_get_cache(um_handle), hostids, hostids_num, macro, um_handle->macro_env, value);
}

/******************************************************************************
 *                                                                            *
 * Purpose: expand user macros in the specified text value                    *
 *                                                                            *
 * Parameters: um_handle   - [IN] the user macro cache handle                 *
 *             text        - [IN/OUT] the text value with macros to expand    *
 *             hostids     - [IN] an array of host identifiers                *
 *             hostids_num - [IN] the number of host identifiers              *
 *             error       - [OUT] the error message                          *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_expand_user_macros(const zbx_dc_um_handle_t *um_handle, char **text, const zbx_uint64_t *hostids,
							  int hostids_num, char **error)
{
	zbx_token_t token;
	int pos = 0, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'", __func__, *text);

	for (; SUCCEED == zbx_token_find(*text, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		const char *value = NULL;

		if (ZBX_TOKEN_USER_MACRO != token.type)
			continue;

		um_cache_resolve_const(dc_um_get_cache(um_handle), hostids, hostids_num, *text + token.loc.l,
							   um_handle->macro_env, &value);

		if (NULL == value)
		{
			if (NULL != error)
			{
				*error = zbx_dsprintf(NULL, "unknown user macro \"%.*s\"",
									  (int)(token.loc.r - token.loc.l + 1), *text + token.loc.l);
				goto out;
			}
		}
		else
			zbx_replace_string(text, token.loc.l, &token.loc.r, value);

		pos = (int)token.loc.r;
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() '%s'", __func__, *text);

	return ret;
}

typedef struct
{
	ZBX_DC_ITEM *item;
	ZBX_DC_HOST *host;
	char *delay_ex;
	zbx_uint64_t proxy_hostid;
} zbx_item_delay_t;

ZBX_PTR_VECTOR_DECL(item_delay, zbx_item_delay_t *)
ZBX_PTR_VECTOR_IMPL(item_delay, zbx_item_delay_t *)

static void zbx_item_delay_free(zbx_item_delay_t *item_delay)
{
	zbx_free(item_delay->delay_ex);
	zbx_free(item_delay);
}

/******************************************************************************
 *                                                                            *
 * Purpose: check if item must be activated because its host has changed      *
 *          monitoring status to 'active' or unassigned from proxy            *
 *                                                                            *
 * Parameters: item            - [IN] the item to check                       *
 *             host            - [IN] the item's host                         *
 *             activated hosts - [IN] the activated host identifiers          *
 *             activated_items - [OUT] items to be rescheduled because host   *
 *                                     being activated                        *
 *                                                                            *
 ******************************************************************************/
static void dc_check_item_activation(ZBX_DC_ITEM *item, ZBX_DC_HOST *host,
									 const zbx_hashset_t *activated_hosts, zbx_vector_ptr_pair_t *activated_items)
{
	zbx_ptr_pair_t pair;

	if (ZBX_LOC_NOWHERE != item->location)
		return;

	if (0 != host->proxy_hostid && SUCCEED != is_item_processed_by_server(item->type, item->key))
		return;

	if (NULL == zbx_hashset_search(activated_hosts, &host->hostid))
		return;

	pair.first = item;
	pair.second = host;

	zbx_vector_ptr_pair_append(activated_items, pair);
}
/******************************************************************************
 *                                                                            *
 * Purpose: get items with changed expanded delay value                       *
 *                                                                            *
 * Parameters: activated_hosts - [IN]                                         *
 *             items           - [OUT] items to be rescheduled because of     *
 *                                     delay changes                          *
 *             activated_items - [OUT] items to be rescheduled because host   *
 *                                     being activated                        *
 *                                                                            *
 * Comments: This function is used only by configuration syncer, so it cache  *
 *           locking is not needed to access data changed only by the syncer  *
 *           itself.                                                          *
 *                                                                            *
 ******************************************************************************/
static void dc_get_items_to_reschedule(const zbx_hashset_t *activated_hosts, zbx_vector_item_delay_t *items,
									   zbx_vector_ptr_pair_t *activated_items)
{
	zbx_hashset_iter_t iter;
	ZBX_DC_ITEM *item;
	ZBX_DC_HOST *host;
	char *delay_ex;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_hashset_iter_reset(&config->items, &iter);
	while (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
	{
		if (ITEM_STATUS_ACTIVE != item->status ||
			SUCCEED != zbx_is_counted_in_item_queue(item->type, item->key))
		{
			continue;
		}

		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != host->status)
			continue;

		if (NULL == strstr(item->delay, "{$"))
		{
			/* neither new item revision or the last one had macro in delay */
			if (NULL == item->delay_ex)
			{
				dc_check_item_activation(item, host, activated_hosts, activated_items);
				continue;
			}

			delay_ex = NULL;
		}
		else
			delay_ex = dc_expand_user_macros_dyn(item->delay, &item->hostid, 1, ZBX_MACRO_ENV_NONSECURE);

		if (0 != zbx_strcmp_null(item->delay_ex, delay_ex))
		{
			zbx_item_delay_t *item_delay;

			item_delay = (zbx_item_delay_t *)zbx_malloc(NULL, sizeof(zbx_item_delay_t));
			item_delay->item = item;
			item_delay->host = host;
			item_delay->delay_ex = delay_ex;
			item_delay->proxy_hostid = host->proxy_hostid;

			zbx_vector_item_delay_append(items, item_delay);
		}
		else
		{
			zbx_free(delay_ex);
			dc_check_item_activation(item, host, activated_hosts, activated_items);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() items:%d", __func__, items->values_num);
}

static void dc_reschedule_item(ZBX_DC_ITEM *item, const ZBX_DC_HOST *host, int now)
{
	int old_nextcheck = glb_state_item_get_nextcheck(item->itemid);
	char *error = NULL;

	if (SUCCEED == DCitem_nextcheck_update(item, NULL, ZBX_ITEM_DELAY_CHANGED, now, &error))
	{
		if (ZBX_LOC_NOWHERE == item->location)
			DCitem_poller_type_update(item, host, ZBX_ITEM_COLLECTED);

		DCupdate_item_queue(item, item->poller_type);
	}
	// else
	//{
	//	zbx_timespec_t ts = {now, 0};
	//	dc_add_history(item->itemid, item->value_type, 0, NULL, &ts, ITEM_STATE_NOTSUPPORTED, error);
	//	zbx_free(error);
	// }
}

/******************************************************************************
 *                                                                            *
 * Purpose: reschedule items with macros in delay/period that will not be     *
 *          checked in next minute                                            *
 *                                                                            *
 * Comments: This must be done after configuration cache sync to ensure that  *
 *           user macro changes affects item queues.                          *
 *                                                                            *
 ******************************************************************************/
static void dc_reschedule_items(const zbx_hashset_t *activated_hosts)
{
	zbx_vector_item_delay_t items;
	zbx_vector_ptr_pair_t activated_items;

	zbx_vector_item_delay_create(&items);
	zbx_vector_ptr_pair_create(&activated_items);

	dc_get_items_to_reschedule(activated_hosts, &items, &activated_items);

	if (0 != items.values_num || 0 != activated_items.values_num)
	{
		int i, now;

		now = (int)time(NULL);

		WRLOCK_CACHE;

		for (i = 0; i < items.values_num; i++)
		{
			ZBX_DC_ITEM *item = items.values[i]->item;

			if (NULL == items.values[i]->delay_ex)
			{
				/* Macro is removed form item delay, which means item was already */
				/* rescheduled by syncer. Just reset the delay_ex in cache.       */
				dc_strpool_release(item->delay_ex);
				item->delay_ex = NULL;
				continue;
			}

			if (0 != items.values[i]->proxy_hostid)
			{
				/* update nextcheck for active and monitored by proxy items */
				/* for queue requests by frontend.                          */
				// if (NULL != item->delay_ex)
				(void)DCitem_nextcheck_update(item, NULL, ZBX_ITEM_DELAY_CHANGED, now, NULL);

			}
			else if (NULL != item->delay_ex)
				dc_reschedule_item(item, items.values[i]->host, now);

			dc_strpool_replace(NULL != item->delay_ex, &item->delay_ex, items.values[i]->delay_ex);
		}

		for (i = 0; i < activated_items.values_num; i++)
			dc_reschedule_item(activated_items.values[i].first, activated_items.values[i].second, now);

		UNLOCK_CACHE;

		//dc_flush_history();
	}

	zbx_vector_ptr_pair_destroy(&activated_items);
	zbx_vector_item_delay_clear_ext(&items, zbx_item_delay_free);
	zbx_vector_item_delay_destroy(&items);
}

/******************************************************************************
 *                                                                            *
 * Purpose: reschedule httptests on hosts that were re-enabled or unassigned  *
 *          from proxy                                                        *
 *                                                                            *
 * Comments: Cache is not locked for read access because this function is     *
 *           called from configuration syncer and nobody else can add/remove  *
 *           objects or change their configuration.                           *
 *                                                                            *
 ******************************************************************************/
static void dc_reschedule_httptests(zbx_hashset_t *activated_hosts)
{
	zbx_vector_dc_httptest_ptr_t httptests;
	zbx_hashset_iter_t iter;
	int i;
	zbx_uint64_t *phostid;
	ZBX_DC_HOST *host;
	time_t now;

	zbx_vector_dc_httptest_ptr_create(&httptests);

	now = time(NULL);

	zbx_hashset_iter_reset(activated_hosts, &iter);
	while (NULL != (phostid = (zbx_uint64_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, phostid)))
			continue;

		for (i = 0; i < host->httptests.values_num; i++)
		{
			if (ZBX_LOC_NOWHERE != host->httptests.values[i]->location)
				continue;

			zbx_vector_dc_httptest_ptr_append(&httptests, host->httptests.values[i]);
		}
	}

	if (0 != httptests.values_num)
	{
		WRLOCK_CACHE;

		for (i = 0; i < httptests.values_num; i++)
		{
			zbx_dc_httptest_t *httptest = httptests.values[i];

			httptest->nextcheck = dc_calculate_nextcheck(httptest->httptestid, httptest->delay, now);
			dc_httptest_queue(httptest);
		}

		UNLOCK_CACHE;
	}

	zbx_vector_dc_httptest_ptr_destroy(&httptests);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get next drule to be processed                                    *
 *                                                                            *
 * Parameter: now       - [IN] the current timestamp                          *
 *            druleid   - [OUT] the id of drule to be processed               *
 *            nextcheck - [OUT] the timestamp of next drule to be processed,  *
 *                              if there is no rule to be processed now and   *
 *                              the queue is not empty. 0 otherwise           *
 *                                                                            *
 * Return value: SUCCEED - the drule id was returned successfully             *
 *               FAIL    - no drules are scheduled at current time            *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_drule_next(time_t now, zbx_uint64_t *druleid, time_t *nextcheck)
{
	zbx_binary_heap_elem_t *elem;
	zbx_dc_drule_t *drule;
	int ret = FAIL;

	*nextcheck = 0;

	WRLOCK_CACHE;

	if (FAIL == zbx_binary_heap_empty(&config->drule_queue))
	{
		elem = zbx_binary_heap_find_min(&config->drule_queue);
		drule = (zbx_dc_drule_t *)elem->data;

		if (drule->nextcheck <= now)
		{
			zbx_binary_heap_remove_min(&config->drule_queue);
			*druleid = drule->druleid;
			drule->location = ZBX_LOC_POLLER;
			ret = SUCCEED;
		}
		else
			*nextcheck = drule->nextcheck;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: queue drule to be processed according to the delay                *
 *                                                                            *
 * Parameter: now      - [IN] the current timestamp                           *
 *            druleid  - [IN] the id of drule to be queued                    *
 *            delay    - [IN] the number of seconds between drule processing  *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_drule_queue(time_t now, zbx_uint64_t druleid, int delay)
{
	zbx_dc_drule_t *drule;

	WRLOCK_CACHE;

	if (NULL != (drule = (zbx_dc_drule_t *)zbx_hashset_search(&config->drules, &druleid)))
	{
		drule->delay = delay;
		drule->nextcheck = dc_calculate_nextcheck(drule->druleid, drule->delay, now);
		dc_drule_queue(drule);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get next httptest to be processed                                 *
 *                                                                            *
 * Parameter: now        - [IN] the current timestamp                         *
 *            httptestid - [OUT] the id of httptest to be processed           *
 *            nextcheck  - [OUT] the timestamp of next httptest to be         *
 *                               processed, if there is no httptest to be     *
 *                               processed now and the queue is not empty.    *
 *                               0 - otherwise                                *
 *                                                                            *
 * Return value: SUCCEED - the httptest id was returned successfully          *
 *               FAIL    - no httptests are scheduled at current time         *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_httptest_next(time_t now, zbx_uint64_t *httptestid, time_t *nextcheck)
{
	zbx_binary_heap_elem_t *elem;
	zbx_dc_httptest_t *httptest;
	int ret = FAIL;
	ZBX_DC_HOST *dc_host;

	*nextcheck = 0;

	WRLOCK_CACHE;

	while (FAIL == zbx_binary_heap_empty(&config->httptest_queue))
	{
		elem = zbx_binary_heap_find_min(&config->httptest_queue);
		httptest = (zbx_dc_httptest_t *)elem->data;

		if (httptest->nextcheck <= now)
		{
			zbx_binary_heap_remove_min(&config->httptest_queue);
			httptest->location = ZBX_LOC_NOWHERE;

			if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &httptest->hostid)))
				continue;

			if (HOST_STATUS_MONITORED != dc_host->status || 0 != dc_host->proxy_hostid)
				continue;

			if (HOST_MAINTENANCE_STATUS_ON == dc_host->maintenance_status &&
				MAINTENANCE_TYPE_NODATA == dc_host->maintenance_type)
			{
				httptest->nextcheck = dc_calculate_nextcheck(httptest->httptestid, httptest->delay, now);
				dc_httptest_queue(httptest);

				continue;
			}

			httptest->location = ZBX_LOC_POLLER;
			*httptestid = httptest->httptestid;

			ret = SUCCEED;
		}
		else
			*nextcheck = httptest->nextcheck;

		break;
	}

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Purpose: queue httptest to be processed according to the delay             *
 *                                                                            *
 * Parameter: now        - [IN] the current timestamp                         *
 *            httptestid - [IN] the id of httptest to be queued               *
 *            delay      - [IN] the number of seconds between httptest        *
 *                              processing                                    *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_httptest_queue(time_t now, zbx_uint64_t httptestid, int delay)
{
	zbx_dc_httptest_t *httptest;

	WRLOCK_CACHE;

	if (NULL != (httptest = (zbx_dc_httptest_t *)zbx_hashset_search(&config->httptests, &httptestid)))
	{
		httptest->delay = delay;
		httptest->nextcheck = dc_calculate_nextcheck(httptest->httptestid, httptest->delay, now);
		dc_httptest_queue(httptest);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get the configuration revision received from server               *
 *                                                                            *
 * Comments: The revision is accessed without locking because no other process*
 *           can access it at the same time.                                  *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t zbx_dc_get_received_revision(void)
{
	return config->revision.upstream;
}

/******************************************************************************
 *                                                                            *
 * Purpose: cache the configuration revision received from server             *
 *                                                                            *
 * Comments: The revision is updated without locking because no other process *
 *           can access it at the same time.                                  *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_update_received_revision(zbx_uint64_t revision)
{
	config->revision.upstream = revision;
}

/******************************************************************************
 *                                                                            *
 * Purpose: get hosts/httptests for proxy configuration update                *
 *                                                                            *
 * Parameters: proxy_hostid    - [IN]                                         *
 *             revision        - [IN] the current proxy configuration revision*
 *             hostids         - [OUT] the monitored hosts                    *
 *             updated_hostids - [OUT] the hosts updated since specified      *
 *                                     configuration revision, sorted         *
 *             removed_hostids - [OUT] the hosts removed since specified      *
 *                                     configuration revision, sorted         *
 *             httptestids     - [OUT] the web scenarios monitored by proxy   *
 *                                                                            *
 ******************************************************************************/
void zbx_dc_get_proxy_config_updates(zbx_uint64_t proxy_hostid, zbx_uint64_t revision, zbx_vector_uint64_t *hostids,
									 zbx_vector_uint64_t *updated_hostids, zbx_vector_uint64_t *removed_hostids,
									 zbx_vector_uint64_t *httptestids)
{
	ZBX_DC_PROXY *proxy;

	RDLOCK_CACHE;

	if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &proxy_hostid)))
	{
		int i, j;

		zbx_vector_uint64_reserve(hostids, (size_t)proxy->hosts.values_num);

		for (i = 0; i < proxy->hosts.values_num; i++)
		{
			ZBX_DC_HOST *host = proxy->hosts.values[i];

			zbx_vector_uint64_append(hostids, host->hostid);

			if (host->revision > revision)
			{
				zbx_vector_uint64_append(updated_hostids, host->hostid);

				for (j = 0; j < host->httptests.values_num; j++)
					zbx_vector_uint64_append(httptestids, host->httptests.values[j]->httptestid);
			}
		}

		/* skip when full sync */
		if (0 != revision)
		{
			for (i = 0; i < proxy->removed_hosts.values_num;)
			{
				if (proxy->removed_hosts.values[i].revision > revision)
				{
					zbx_vector_uint64_append(removed_hostids, proxy->removed_hosts.values[i].hostid);

					/* this operation can be done with read lock:                  */
					/*   - removal from vector does not allocate/free memory       */
					/*   - two configuration requests for the same proxy cannot be */
					/*     processed at the same time                              */
					/*   - configuration syncer uses write lock to update          */
					/*     removed hosts on proxy                                  */
					zbx_vector_host_rev_remove_noorder(&proxy->removed_hosts, i);
				}
				else
					i++;
			}
		}
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_sort(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(updated_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(removed_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(httptestids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

void zbx_dc_get_macro_updates(const zbx_vector_uint64_t *hostids, const zbx_vector_uint64_t *updated_hostids,
							  zbx_uint64_t revision, zbx_vector_uint64_t *macro_hostids, int *global,
							  zbx_vector_uint64_t *del_macro_hostids)
{
	zbx_vector_uint64_t hostids_tmp, globalids;
	zbx_uint64_t globalhostid = 0;

	/* force full sync for updated hosts (in the case host was assigned to proxy) */
	/* and revision based sync for the monitored hosts (except updated hosts that */
	/* were already synced)                                                       */

	zbx_vector_uint64_create(&hostids_tmp);
	if (0 != hostids->values_num)
	{
		zbx_vector_uint64_append_array(&hostids_tmp, hostids->values, hostids->values_num);
		zbx_vector_uint64_setdiff(&hostids_tmp, updated_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	zbx_vector_uint64_create(&globalids);

	RDLOCK_CACHE;

	/* check revision of global macro 'host' (hostid 0) */
	um_cache_get_macro_updates(config->um_cache, &globalhostid, 1, revision, &globalids, del_macro_hostids);

	if (0 != hostids_tmp.values_num)
	{
		um_cache_get_macro_updates(config->um_cache, hostids_tmp.values, hostids_tmp.values_num, revision,
								   macro_hostids, del_macro_hostids);
	}

	if (0 != updated_hostids->values_num)
	{
		um_cache_get_macro_updates(config->um_cache, updated_hostids->values, updated_hostids->values_num, 0,
								   macro_hostids, del_macro_hostids);
	}

	UNLOCK_CACHE;

	*global = (0 < globalids.values_num ? SUCCEED : FAIL);

	if (0 != macro_hostids->values_num)
		zbx_vector_uint64_sort(macro_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	if (0 != del_macro_hostids->values_num)
		zbx_vector_uint64_sort(del_macro_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_uint64_destroy(&globalids);
	zbx_vector_uint64_destroy(&hostids_tmp);
}

void zbx_dc_get_unused_macro_templates(zbx_hashset_t *templates, const zbx_vector_uint64_t *hostids,
									   zbx_vector_uint64_t *templateids)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	um_cache_get_unused_templates(config->um_cache, templates, hostids, templateids);

	UNLOCK_CACHE;

	if (0 != templateids->values_num)
		zbx_vector_uint64_sort(templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() templateids_num:%d", __func__, templateids->values_num);
}

u_int64_t DC_get_debug_item()
{
	return config->debug_item;
}
u_int64_t DC_get_debug_trigger()
{
	return config->debug_trigger;
}

void DC_set_debug_item(uint64_t id)
{
	WRLOCK_CACHE;
	config->debug_item = id;
	UNLOCK_CACHE;
	DEBUG_ITEM(id, "STARTED DEBUG");
}

void DC_set_debug_trigger(uint64_t id)
{
	WRLOCK_CACHE;
	config->debug_trigger = id;
	UNLOCK_CACHE;
	DEBUG_TRIGGER(id, "STARTED DEBUG");
}

int zbx_dc_get_item_type(zbx_uint64_t itemid, int *value_type)
{
	ZBX_DC_ITEM *item;
	int ret = FAIL;

	RDLOCK_CACHE;

	if (NULL != (item = zbx_hashset_search(&config->items, &itemid)))
	{
		*value_type = item->value_type;
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

void DCget_host_items(u_int64_t hostid, zbx_vector_uint64_t *items)
{
	ZBX_DC_HOST *host;
	ZBX_DC_ITEM *item;
	int i;

	RDLOCK_CACHE;

	if (NULL == (host = zbx_hashset_search(&config->hosts, &hostid)))
	{
		UNLOCK_CACHE;
		return;
	}
	for (i = 0; i < host->items.values_num; i++)
	{
		item = host->items.values[i];
		zbx_vector_uint64_append(items, item->itemid);
	}

	UNLOCK_CACHE;
}

void DC_notify_changed_items(zbx_vector_uint64_t *items)
{
	int i;

	RDLOCK_CACHE;

	for (i = 0; i < items->values_num; i++)
	{
		ZBX_DC_ITEM *item;
		ZBX_DC_HOST *host;
		DEBUG_ITEM(items->values[i], "Processing item in notify change");

		if (NULL == (item = zbx_hashset_search(&config->items, &items->values[i])))
		{
			DEBUG_ITEM(items->values[i], "Couldn't find item in items on notify change");
			continue;
		}
		if (NULL == (host = zbx_hashset_search(&config->hosts, &item->hostid)))
		{
			DEBUG_ITEM(items->values[i], "Couldn't host in find in items om notify change");
			continue;
		}
		if (SUCCEED == glb_might_be_async_polled(item, host))
		{
			DEBUG_ITEM(item->itemid, "Doing iface poller_item_add_notify");
			poller_item_add_notify(item->type, item->key, item->itemid, item->hostid);
		}
	}

	UNLOCK_CACHE;
}

u_int64_t DC_config_get_hostid_by_itemid(u_int64_t itemid) {
	ZBX_DC_ITEM *item;
	u_int64_t hostid = 0;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid)))
	{
		DEBUG_ITEM(itemid, "Couldn't find item in items cache to find host");
		hostid = item->hostid;
	}
	
	UNLOCK_CACHE;
	return hostid;
}

int DC_config_get_hostid_by_interfaceid(u_int64_t interfaceid, u_int64_t *hostid) {
	ZBX_DC_INTERFACE *interface;
	int ret = FAIL;

	RDLOCK_CACHE;
	
	if (NULL != (interface = zbx_hashset_search(&config->interfaces, &interfaceid))){
		*hostid = interface->hostid;
		ret = SUCCEED;
	}
	
	UNLOCK_CACHE;
	
	return ret;
}

int DC_config_get_type_by_interfaceid(u_int64_t interfaceid, unsigned char *type) {
	ZBX_DC_INTERFACE *interface;
	int ret = FAIL;

	RDLOCK_CACHE;
	
	if (NULL != (interface = zbx_hashset_search(&config->interfaces, &interfaceid))){
		*type = interface->type;
		ret = SUCCEED;
	}
	
	UNLOCK_CACHE;
	
	return ret;
}

int DC_config_get_host_description(u_int64_t hostid, char **host_description) {
	ZBX_DC_HOST *host;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (host = zbx_hashset_search(&config->hosts, &hostid))) {
		*host_description = zbx_strdup(NULL, host->description);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

int DC_config_get_item_key(u_int64_t itemid, char **item_key) {
	ZBX_DC_ITEM *item;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid))) {
		*item_key = zbx_strdup(*item_key, item->key);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

int DC_config_get_item_name(u_int64_t itemid, char **item_name) {
	ZBX_DC_ITEM *item;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid))) {
		*item_name = zbx_strdup(*item_name, item->name);
		ret = SUCCEED;
		LOG_INF("in %s: got value %s", __func__, *item_name);
	}

	UNLOCK_CACHE;
	return ret;
}

int DC_config_get_item_description(u_int64_t itemid, char **item_descr) {
	ZBX_DC_ITEM *item;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid))) {
		*item_descr = zbx_strdup(*item_descr, item->description);
		LOG_INF("in %s: got value %s", __func__, *item_descr);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

int  DC_config_get_item_proxy_name(u_int64_t itemid, char **proxy_name) {
	ZBX_DC_ITEM *item;
	ZBX_DC_HOST *proxy_host;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid)) && 
		NULL != (proxy_host = zbx_hashset_search(&config->hosts, &item->hostid) )) { 
		
		*proxy_name = zbx_strdup(*proxy_name, proxy_host->name);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

int  DC_config_get_item_proxy_description(u_int64_t itemid, char **proxy_name) {
	ZBX_DC_ITEM *item;
	ZBX_DC_HOST *proxy_host;
	int ret = FAIL;

	RDLOCK_CACHE;
	if (NULL != (item = zbx_hashset_search(&config->items, &itemid)) && 
		NULL != (proxy_host = zbx_hashset_search(&config->hosts, &item->hostid) )) { 
		
		*proxy_name = zbx_strdup(*proxy_name, proxy_host->description);
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}

int  DC_get_item_valuetype_valuemapid(u_int64_t itemid, u_int64_t *valuemapid, unsigned char *value_type, char **units) {
	ZBX_DC_ITEM *item;
	ZBX_DC_NUMITEM *numitem;
	int ret = FAIL;

	RDLOCK_CACHE;
	
	if ( NULL != (item = zbx_hashset_search(&config->items, &itemid))  && 
		 NULL != (numitem = zbx_hashset_search(&config->numitems, &itemid)) )   {
		
		*valuemapid = item->valuemapid;
		*value_type = item->value_type;
		*units = zbx_strdup(*units, numitem->units);

		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;

}

void	zbx_recalc_time_period(int *ts_from, int table_group)
{
#define HK_CFG_UPDATE_INTERVAL	5
	time_t			least_ts = 0, now;
	zbx_config_t		cfg;
	static time_t		last_cfg_retrieval = 0;
	static zbx_config_hk_t	hk;

	now = time(NULL);

	if (HK_CFG_UPDATE_INTERVAL < now - last_cfg_retrieval)
	{
		last_cfg_retrieval = now;

		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_HOUSEKEEPER);
		hk = cfg.hk;
	}

	if (ZBX_RECALC_TIME_PERIOD_HISTORY == table_group)
	{
		if (1 != hk.history_global)
			return;

		least_ts = now - hk.history;
	}
	else if (ZBX_RECALC_TIME_PERIOD_TRENDS == table_group)
	{
		if (1 != hk.trends_global)
			return;

		least_ts = now - hk.trends + 1;
	}

	if (least_ts > *ts_from)
		*ts_from = (int)least_ts;
#undef HK_CFG_UPDATE_INTERVAL
}

void DC_account_sync_poller_time(int item_type, double time_spent) {
	//LOG_INF("Accounted time spent %0.9f for item type %d", time_spent, item_type);
	WRLOCK_CACHE;
	config->time_by_poller_type[item_type] += time_spent;
	config->runs_by_poller_type[item_type]++;
	UNLOCK_CACHE;
}

void DC_get_account_sync_poller_time(char **out) {
	int i;
	double total_time = 0;
	u_int64_t total_runs = 1 ;
	size_t	alloc = 0, offset = 0;

	RDLOCK_CACHE;

	for (i=0; i< ITEM_TYPE_MAX; i++) {
		total_time += config->time_by_poller_type[i];
		total_runs += config->runs_by_poller_type[i];
	}
	total_time += config->time_by_poller_type[ITEM_TYPE_MAX];
	
	if (total_time > 5)
		for (i = 0; i < ITEM_TYPE_MAX + 1; i++) 
			if (config->time_by_poller_type[i] > 0.01) {
				if ( i != ITEM_TYPE_MAX)
					zbx_strlog_alloc(LOG_LEVEL_DEBUG, out, &alloc, &offset, "Item type %d time spent %0.2f%, runs %d% (%lld total)", i, 
						(config->time_by_poller_type[i] * 100)/total_time, (config->runs_by_poller_type[i] * 100)/total_runs, 
						config->runs_by_poller_type[i]); 
				else 
					zbx_strlog_alloc(LOG_LEVEL_DEBUG, out, &alloc, &offset, "Idle time %0.2f%, total runs %lld",  
						(config->time_by_poller_type[i] * 100)/total_time, total_runs); 

			}
	UNLOCK_CACHE;
}

#ifdef HAVE_TESTS
#include "../../../tests/libs/zbxdbcache/dc_item_poller_type_update_test.c"
#include "../../../tests/libs/zbxdbcache/dc_function_calculate_nextcheck_test.c"
#endif
