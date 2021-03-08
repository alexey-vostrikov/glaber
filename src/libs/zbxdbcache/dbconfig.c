/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
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

#include <stddef.h>

#include "common.h"
#include "log.h"
#include "threads.h"
#include "dbcache.h"
#include "ipc.h"
#include "mutexs.h"
#include "memalloc.h"
#include "zbxserver.h"
#include "zbxalgo.h"
#include "zbxregexp.h"
#include "cfg.h"
#include "zbxtasks.h"
#include "../zbxcrypto/tls_tcp_active.h"
#include "../../zabbix_server/glb_poller/glb_poller.h"
#include "../../zabbix_server/poller/poller.h"

#define ZBX_DBCONFIG_IMPL
#include "dbconfig.h"
#include "dbsync.h"
#include "proxy.h"
#include "actions.h"
#include "zbxtrends.h"
#include "zbxvault.h"
#include "preproc.h"

int	sync_in_progress = 0;

#define START_SYNC	WRLOCK_CACHE; sync_in_progress = 1
#define FINISH_SYNC	sync_in_progress = 0; UNLOCK_CACHE

#define ZBX_LOC_NOWHERE	0
#define ZBX_LOC_QUEUE	1
#define ZBX_LOC_POLLER	2

#define ZBX_SNMP_OID_TYPE_NORMAL	0
#define ZBX_SNMP_OID_TYPE_DYNAMIC	1
#define ZBX_SNMP_OID_TYPE_MACRO		2

/* trigger is functional unless its expression contains disabled or not monitored items */
#define TRIGGER_FUNCTIONAL_TRUE		0
#define TRIGGER_FUNCTIONAL_FALSE	1

/* trigger contains time functions and is also scheduled by timer queue */
#define ZBX_TRIGGER_TIMER_UNKNOWN	0
#define ZBX_TRIGGER_TIMER_QUEUE		1

/* item priority in poller queue */
#define ZBX_QUEUE_PRIORITY_HIGH		0
#define ZBX_QUEUE_PRIORITY_NORMAL	1
#define ZBX_QUEUE_PRIORITY_LOW		2

/* shorthand macro for calling in_maintenance_without_data_collection() */
#define DCin_maintenance_without_data_collection(dc_host, dc_item)			\
		in_maintenance_without_data_collection(dc_host->maintenance_status,	\
				dc_host->maintenance_type, dc_item->type)

/******************************************************************************
 *                                                                            *
 * Function: zbx_value_validator_func_t                                       *
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

ZBX_DC_CONFIG	*config = NULL;
zbx_rwlock_t	config_lock = ZBX_RWLOCK_NULL;
static zbx_mem_info_t	*config_mem;

extern unsigned char	program_type;
extern int		CONFIG_TIMER_FORKS;

extern int CONFIG_POLLER_FORKS;
extern int CONFIG_IPMIPOLLER_FORKS;
extern int CONFIG_JAVAPOLLER_FORKS;
extern int CONFIG_PINGER_FORKS;
extern int CONFIG_DISABLE_INPOLLER_PREPROC;
extern int CONFIG_CLUSTER_SERVER_ID;
extern u_int64_t CONFIG_DEBUG_ITEM;
extern u_int64_t CONFIG_DEBUG_HOST;
extern char * CONFIG_HOSTNAME;
extern int CONFIG_PREPROCMAN_FORKS;
extern int  CONFIG_GLB_REQUEUE_TIME;
extern int CONFIG_GLB_SNMP_FORKS;
extern int CONFIG_GLB_PINGER_FORKS;

ZBX_MEM_FUNC_IMPL(__config, config_mem)

char *server_get_topology(DC_PROXY *proxy);
//int	process_history_data(DC_ITEM *items, zbx_agent_value_t *values, int *errcodes, size_t values_num);
static void	dc_maintenance_precache_nested_groups(void);

/* by default the macro environment is non-secure and all secret macros are masked with ****** */
static unsigned char	macro_env = ZBX_MACRO_ENV_NONSECURE;
extern char		*CONFIG_VAULTDBPATH;
extern char		*CONFIG_VAULTTOKEN;
/******************************************************************************
 *                                                                            *
 * Function: dc_strdup                                                        *
 *                                                                            *
 * Purpose: copies string into configuration cache shared memory              *
 *                                                                            *
 ******************************************************************************/
static char	*dc_strdup(const char *source)
{
	char	*dst;
	size_t	len;

	len = strlen(source) + 1;
	dst = (char *)__config_mem_malloc_func(NULL, len);
	memcpy(dst, source, len);
	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: is_item_processed_by_server                                      *
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
 *           | Zabbix internal  | zabbix[proxy,<proxyname>,lastaccess] |      *
 *           | Zabbix internal  | zabbix[proxy,<proxyname>,delay]      |      *
 *           | Zabbix aggregate | *                                    |      *
 *           | Calculated       | *                                    |      *
 *           '------------------+--------------------------------------'      *
 *                                                                            *
 ******************************************************************************/
int	is_item_processed_by_server(unsigned char type, const char *key)
{
	int	ret = FAIL;

	switch (type)
	{
		case ITEM_TYPE_AGGREGATE:
		case ITEM_TYPE_CALCULATED:
			ret = SUCCEED;
			break;

		case ITEM_TYPE_INTERNAL:
			if (0 == strncmp(key, "zabbix[", 7))
			{
				AGENT_REQUEST	request;
				char		*arg1, *arg2, *arg3;

				init_request(&request);

				if (SUCCEED != parse_item_key(key, &request) || 3 != request.nparam)
					goto clean;

				arg1 = get_rparam(&request, 0);
				arg2 = get_rparam(&request, 1);
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
				else if (0 == strcmp(arg1, "proxy") &&
						(0 == strcmp(arg3, "lastaccess") || 0 == strcmp(arg3, "delay")))
					ret = SUCCEED;
clean:
				free_request(&request);
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
			if (SUCCEED == cmp_key_id(key, SERVER_ICMPPING_KEY) ||
					SUCCEED == cmp_key_id(key, SERVER_ICMPPINGSEC_KEY) ||
					SUCCEED == cmp_key_id(key, SERVER_ICMPPINGLOSS_KEY))
			{
				if (0 == CONFIG_PINGER_FORKS)
					break;

				return ZBX_POLLER_TYPE_PINGER;
			}
			ZBX_FALLTHROUGH;
		case ITEM_TYPE_ZABBIX:
		case ITEM_TYPE_SNMP:
		case ITEM_TYPE_INTERNAL:
		case ITEM_TYPE_AGGREGATE:
		case ITEM_TYPE_EXTERNAL:
		case ITEM_TYPE_DB_MONITOR:
		case ITEM_TYPE_SSH:
		case ITEM_TYPE_TELNET:
		case ITEM_TYPE_CALCULATED:
		case ITEM_TYPE_HTTPAGENT:
		case ITEM_TYPE_SCRIPT:
			if (0 == CONFIG_POLLER_FORKS)
				break;

			return ZBX_POLLER_TYPE_NORMAL;
		case ITEM_TYPE_IPMI:
			if (0 == CONFIG_IPMIPOLLER_FORKS)
				break;

			return ZBX_POLLER_TYPE_IPMI;
		case ITEM_TYPE_JMX:
			if (0 == CONFIG_JAVAPOLLER_FORKS)
				break;

			return ZBX_POLLER_TYPE_JAVA;
	}

	return ZBX_NO_POLLER;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_is_counted_in_item_queue                                     *
 *                                                                            *
 * Purpose: determine whether the given item type is counted in item queue    *
 *                                                                            *
 * Return value: SUCCEED if item is counted in the queue, FAIL otherwise      *
 *                                                                            *
 ******************************************************************************/
int	zbx_is_counted_in_item_queue(unsigned char type, const char *key)
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
 * Function: get_item_nextcheck_seed                                          *
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
static zbx_uint64_t	get_item_nextcheck_seed(zbx_uint64_t itemid, zbx_uint64_t interfaceid, unsigned char type,
		const char *key)
{
	if (ITEM_TYPE_JMX == type)
		return interfaceid;

	if (ITEM_TYPE_SNMP == type)
	{
		ZBX_DC_SNMPINTERFACE	*snmp;

		if (NULL == (snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid))
				|| SNMP_BULK_ENABLED != snmp->bulk)
		{
			return itemid;
		}

		return interfaceid;
	}

	if (ITEM_TYPE_SIMPLE == type)
	{
		if (SUCCEED == cmp_key_id(key, SERVER_ICMPPING_KEY) ||
				SUCCEED == cmp_key_id(key, SERVER_ICMPPINGSEC_KEY) ||
				SUCCEED == cmp_key_id(key, SERVER_ICMPPINGLOSS_KEY))
		{
			return interfaceid;
		}
	}

	return itemid;
}

static int	DCget_disable_until(const ZBX_DC_ITEM *item, const ZBX_DC_HOST *host);

#define ZBX_ITEM_COLLECTED		0x01	/* force item rescheduling after new value collection */
#define ZBX_HOST_UNREACHABLE		0x02
#define ZBX_ITEM_KEY_CHANGED		0x04
#define ZBX_ITEM_TYPE_CHANGED		0x08
#define ZBX_ITEM_DELAY_CHANGED		0x10

static int	DCitem_nextcheck_update(ZBX_DC_ITEM *item, const ZBX_DC_HOST *host, int flags, int now, char **error)
{
	zbx_uint64_t		seed;
	int			simple_interval;
	zbx_custom_interval_t	*custom_intervals;
	int			disable_until;

	if (0 == (flags & ZBX_ITEM_COLLECTED) && 0 != item->nextcheck &&
			0 == (flags & ZBX_ITEM_KEY_CHANGED) && 0 == (flags & ZBX_ITEM_TYPE_CHANGED) &&
			0 == (flags & ZBX_ITEM_DELAY_CHANGED))
	{
		return SUCCEED;	/* avoid unnecessary nextcheck updates when syncing items in cache */
	}

	seed = get_item_nextcheck_seed(item->itemid, item->interfaceid, item->type, item->key);
	//seed = item->hostid;

	if (SUCCEED != zbx_interval_preproc(item->delay, &simple_interval, &custom_intervals, error))
	{
		/* Polling items with invalid update intervals repeatedly does not make sense because they */
		/* can only be healed by editing configuration (either update interval or macros involved) */
		/* and such changes will be detected during configuration synchronization. DCsync_items()  */
		/* detects item configuration changes affecting check scheduling and passes them in flags. */

		item->nextcheck = ZBX_JAN_2038;
		item->schedulable = 0;

		if (CONFIG_DEBUG_ITEM == item->itemid || CONFIG_DEBUG_HOST == item->hostid) 
			zabbix_log(LOG_LEVEL_INFORMATION,"Debug item: %ld at %s: nextcheck is set to %d (+%d sec)",
								item->itemid,__func__, item->nextcheck, item->nextcheck-time(NULL));
			
		return FAIL;
	}

	if (0 != (flags & ZBX_HOST_UNREACHABLE) && 0 != (disable_until =
			DCget_disable_until(item, host)))
	{
		item->nextcheck = calculate_item_nextcheck_unreachable(simple_interval,
				custom_intervals, disable_until);
	}
	else
	{
		/* supported items and items that could not have been scheduled previously, but had */
		/* their update interval fixed, should be scheduled using their update intervals */
		item->nextcheck = calculate_item_nextcheck(seed, item->type, simple_interval,
				custom_intervals, now);
	}

	zbx_custom_interval_free(custom_intervals);

	item->schedulable = 1;

	return SUCCEED;
}

static void	DCitem_poller_type_update(ZBX_DC_ITEM *dc_item, const ZBX_DC_HOST *dc_host, int flags)
{
	unsigned char	poller_type;

	if (0 != dc_host->proxy_hostid && SUCCEED != is_item_processed_by_server(dc_item->type, dc_item->key))
	{
		dc_item->poller_type = ZBX_NO_POLLER;
		if (CONFIG_DEBUG_ITEM == dc_item->itemid) 
			zabbix_log(LOG_LEVEL_INFORMATION,"Debug item: %ld at %s: set poller type to %d",dc_item->itemid,__func__,ZBX_NO_POLLER);
		return;
	}
	poller_type = poller_by_item(dc_item->type, dc_item->key);
	
	if (0 != (flags & ZBX_HOST_UNREACHABLE))
	{
		if (ZBX_POLLER_TYPE_NORMAL == poller_type || ZBX_POLLER_TYPE_JAVA == poller_type)
			poller_type = ZBX_POLLER_TYPE_UNREACHABLE;

		if (CONFIG_DEBUG_ITEM == dc_item->itemid) 
			zabbix_log(LOG_LEVEL_INFORMATION,"Debug item: %ld at %s: set poller type to %d",dc_item->itemid,__func__,poller_type);
		dc_item->poller_type = poller_type;
		return;
	}

	if (0 != (flags & ZBX_ITEM_COLLECTED))
	{
		if (CONFIG_DEBUG_ITEM == dc_item->itemid) 
			zabbix_log(LOG_LEVEL_INFORMATION,"Debug item: %ld at %s: set poller type to %d",dc_item->itemid,__func__,poller_type);
		dc_item->poller_type = poller_type;
		return;
	}

	if (ZBX_POLLER_TYPE_UNREACHABLE != dc_item->poller_type ||
			(ZBX_POLLER_TYPE_NORMAL != poller_type && ZBX_POLLER_TYPE_JAVA != poller_type))
	{
		dc_item->poller_type = poller_type;
		if (CONFIG_DEBUG_ITEM == dc_item->itemid) 
			zabbix_log(LOG_LEVEL_INFORMATION,"Debug item: %ld at %s: set poller type to %d",dc_item->itemid,__func__,poller_type);
	}
}

static int	DCget_disable_until(const ZBX_DC_ITEM *item, const ZBX_DC_HOST *host)
{
	switch (item->type)
	{
		case ITEM_TYPE_ZABBIX:
			if (0 != host->errors_from)
				return host->disable_until;
			break;
		case ITEM_TYPE_SNMP:
			if (0 != host->snmp_errors_from)
				return host->snmp_disable_until;
			break;
		case ITEM_TYPE_IPMI:
			if (0 != host->ipmi_errors_from)
				return host->ipmi_disable_until;
			break;
		case ITEM_TYPE_JMX:
			if (0 != host->jmx_errors_from)
				return host->jmx_disable_until;
			break;
		default:
			/* nothing to do */;
	}

	return 0;
}

static void	DCincrease_disable_until(const ZBX_DC_ITEM *item, ZBX_DC_HOST *host, int now)
{
	switch (item->type)
	{
		case ITEM_TYPE_ZABBIX:
			if (0 != host->errors_from)
				host->disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_SNMP:
			if (0 != host->snmp_errors_from)
				host->snmp_disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_IPMI:
			if (0 != host->ipmi_errors_from)
				host->ipmi_disable_until = now + CONFIG_TIMEOUT;
			break;
		case ITEM_TYPE_JMX:
			if (0 != host->jmx_errors_from)
				host->jmx_disable_until = now + CONFIG_TIMEOUT;
			break;
		default:
			/* nothing to do */;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCfind_id                                                        *
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
void	*DCfind_id(zbx_hashset_t *hashset, zbx_uint64_t id, size_t size, int *found)
{
	void		*ptr;
	zbx_uint64_t	buffer[1024];	/* adjust buffer size to accommodate any type DCfind_id() can be called for */

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

static ZBX_DC_ITEM	*DCfind_item(zbx_uint64_t hostid, const char *key)
{
	ZBX_DC_ITEM_HK	*item_hk, item_hk_local;

	item_hk_local.hostid = hostid;
	item_hk_local.key = key;

	if (NULL == (item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local)))
		return NULL;
	else
		return item_hk->item_ptr;
}

static ZBX_DC_HOST	*DCfind_host(const char *host)
{
	ZBX_DC_HOST_H	*host_h, host_h_local;

	host_h_local.host = host;

	if (NULL == (host_h = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_h, &host_h_local)))
		return NULL;
	else
		return host_h->host_ptr;
}

/******************************************************************************
 *                                                                            *
 * Function: DCfind_proxy                                                     *
 *                                                                            *
 * Purpose: Find a record with proxy details in configuration cache using the *
 *          proxy name                                                        *
 *                                                                            *
 * Parameters: host - [IN] proxy name                                         *
 *                                                                            *
 * Return value: pointer to record if found or NULL otherwise                 *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_HOST	*DCfind_proxy(const char *host)
{
	ZBX_DC_HOST_H	*host_p, host_p_local;

	host_p_local.host = host;

	if (NULL == (host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local)))
		return NULL;
	else
		return host_p->host_ptr;
}

/* private strpool functions */

#define	REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

static zbx_hash_t	__config_strpool_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	__config_strpool_compare(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}

static const char	*zbx_strpool_intern(const char *str)
{
	void		*record;
	zbx_uint32_t	*refcount;

	if ( NULL == str ) 
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

void	zbx_strpool_release(const char *str)
{
	zbx_uint32_t	*refcount;

	if (NULL == str) 
		return;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	if (0 == --(*refcount))
		zbx_hashset_remove(&config->strpool, str - REFCOUNT_FIELD_SIZE);
}

static const char	*zbx_strpool_acquire(const char *str)
{
	zbx_uint32_t	*refcount;

	refcount = (zbx_uint32_t *)(str - REFCOUNT_FIELD_SIZE);
	(*refcount)++;

	return str;
}

int	DCstrpool_replace(int found, const char **curr, const char *new_str)
{
	if (1 == found && NULL != new_str)
	{
		if (0 == strcmp(*curr, new_str))
			return FAIL;
		
		zbx_strpool_release(*curr);
	}

	*curr = zbx_strpool_intern(new_str);

	return SUCCEED;	/* indicate that the string has been replaced */
}

static void	DCupdate_item_queue(ZBX_DC_ITEM *item, unsigned char old_poller_type, int old_nextcheck)
{
	zbx_binary_heap_elem_t	elem;

	if (ZBX_LOC_POLLER == item->location)
		return;

	if (ZBX_LOC_QUEUE == item->location && old_poller_type != item->poller_type)
	{
		item->location = ZBX_LOC_NOWHERE;
		zbx_binary_heap_remove_direct(&config->queues[old_poller_type], item->itemid);
	}

	if (item->poller_type == ZBX_NO_POLLER)
		return;

	if (ZBX_LOC_QUEUE == item->location && old_nextcheck == item->nextcheck)
		return;

	elem.key = item->itemid;
	elem.data = (const void *)item;

	if (ZBX_LOC_QUEUE != item->location)
	{
		item->location = ZBX_LOC_QUEUE;
		zbx_binary_heap_insert(&config->queues[item->poller_type], &elem);
	}
	else
		zbx_binary_heap_update_direct(&config->queues[item->poller_type], &elem);
}

static void	DCupdate_proxy_queue(ZBX_DC_PROXY *proxy)
{
	zbx_binary_heap_elem_t	elem;

	if (ZBX_LOC_POLLER == proxy->location)
		return;

	proxy->nextcheck = proxy->server_hello_nextsend;
	if (proxy->proxy_data_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_data_nextcheck;
	if (proxy->proxy_config_nextcheck < proxy->nextcheck)
		proxy->nextcheck = proxy->proxy_config_nextcheck;
	
	elem.key = proxy->hostid;
	elem.data = (const void *)proxy;

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
 * Function: config_gmacro_add_index                                          *
 *                                                                            *
 * Purpose: adds global macro index                                           *
 *                                                                            *
 * Parameters: gmacro_index - [IN/OUT] a global macro index hashset           *
 *             gmacro       - [IN] the macro to index                         *
 *                                                                            *
 * Return value: The macro index record.                                      *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_GMACRO_M	*config_gmacro_add_index(zbx_hashset_t *gmacro_index, ZBX_DC_GMACRO *gmacro)
{
	ZBX_DC_GMACRO_M	*gmacro_m, gmacro_m_local;

	gmacro_m_local.macro = gmacro->macro;

	if (NULL == (gmacro_m = (ZBX_DC_GMACRO_M *)zbx_hashset_search(gmacro_index, &gmacro_m_local)))
	{
		gmacro_m_local.macro = zbx_strpool_acquire(gmacro->macro);
		zbx_vector_ptr_create_ext(&gmacro_m_local.gmacros, __config_mem_malloc_func, __config_mem_realloc_func,
				__config_mem_free_func);

		gmacro_m = (ZBX_DC_GMACRO_M *)zbx_hashset_insert(gmacro_index, &gmacro_m_local, sizeof(ZBX_DC_GMACRO_M));
	}

	zbx_vector_ptr_append(&gmacro_m->gmacros, gmacro);
	return gmacro_m;
}

/******************************************************************************
 *                                                                            *
 * Function: config_gmacro_remove_index                                       *
 *                                                                            *
 * Purpose: removes global macro index                                        *
 *                                                                            *
 * Parameters: gmacro_index - [IN/OUT] a global macro index hashset           *
 *             gmacro       - [IN] the macro to remove                        *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_GMACRO_M	*config_gmacro_remove_index(zbx_hashset_t *gmacro_index, ZBX_DC_GMACRO *gmacro)
{
	ZBX_DC_GMACRO_M	*gmacro_m, gmacro_m_local;
	int		index;

	gmacro_m_local.macro = gmacro->macro;

	if (NULL != (gmacro_m = (ZBX_DC_GMACRO_M *)zbx_hashset_search(gmacro_index, &gmacro_m_local)))
	{
		if (FAIL != (index = zbx_vector_ptr_search(&gmacro_m->gmacros, gmacro, ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			zbx_vector_ptr_remove(&gmacro_m->gmacros, index);
	}
	return gmacro_m;
}

/******************************************************************************
 *                                                                            *
 * Function: config_gmacro_context_compare                                    *
 *                                                                            *
 * Purpose: comparison function to sort global macro vector by context        *
 *          operator, value and macro name                                    *
 *                                                                            *
 ******************************************************************************/
static int	config_gmacro_context_compare(const void *d1, const void *d2)
{
	const ZBX_DC_GMACRO	*m1 = *(const ZBX_DC_GMACRO **)d1;
	const ZBX_DC_GMACRO	*m2 = *(const ZBX_DC_GMACRO **)d2;

	/* macros without context have higher priority than macros with */
	if (NULL == m1->context)
		return NULL == m2->context ? 0 : -1;

	if (NULL == m2->context)
		return 1;

	/* CONDITION_OPERATOR_EQUAL (0) has higher priority than CONDITION_OPERATOR_REGEXP (8) */
	ZBX_RETURN_IF_NOT_EQUAL(m1->context_op, m2->context_op);

	return strcmp(m1->context, m2->context);
}

/******************************************************************************
 *                                                                            *
 * Function: config_hmacro_add_index                                          *
 *                                                                            *
 * Purpose: adds host macro index                                             *
 *                                                                            *
 * Parameters: hmacro_index - [IN/OUT] a host macro index hashset             *
 *             hmacro       - [IN] the macro to index                         *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_HMACRO_HM	*config_hmacro_add_index(zbx_hashset_t *hmacro_index, ZBX_DC_HMACRO *hmacro)
{
	ZBX_DC_HMACRO_HM	*hmacro_hm, hmacro_hm_local;

	hmacro_hm_local.hostid = hmacro->hostid;
	hmacro_hm_local.macro = hmacro->macro;

	if (NULL == (hmacro_hm = (ZBX_DC_HMACRO_HM *)zbx_hashset_search(hmacro_index, &hmacro_hm_local)))
	{
		hmacro_hm_local.macro = zbx_strpool_acquire(hmacro->macro);
		zbx_vector_ptr_create_ext(&hmacro_hm_local.hmacros, __config_mem_malloc_func, __config_mem_realloc_func,
				__config_mem_free_func);

		hmacro_hm = (ZBX_DC_HMACRO_HM *)zbx_hashset_insert(hmacro_index, &hmacro_hm_local, sizeof(ZBX_DC_HMACRO_HM));
	}

	zbx_vector_ptr_append(&hmacro_hm->hmacros, hmacro);
	return hmacro_hm;
}

/******************************************************************************
 *                                                                            *
 * Function: config_hmacro_remove_index                                       *
 *                                                                            *
 * Purpose: removes host macro index                                          *
 *                                                                            *
 * Parameters: hmacro_index - [IN/OUT] a host macro index hashset             *
 *             hmacro       - [IN] the macro name to remove                   *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_HMACRO_HM	*config_hmacro_remove_index(zbx_hashset_t *hmacro_index, ZBX_DC_HMACRO *hmacro)
{
	ZBX_DC_HMACRO_HM	*hmacro_hm, hmacro_hm_local;
	int			index;

	hmacro_hm_local.hostid = hmacro->hostid;
	hmacro_hm_local.macro = hmacro->macro;

	if (NULL != (hmacro_hm = (ZBX_DC_HMACRO_HM *)zbx_hashset_search(hmacro_index, &hmacro_hm_local)))
	{
		if (FAIL != (index = zbx_vector_ptr_search(&hmacro_hm->hmacros, hmacro, ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			zbx_vector_ptr_remove(&hmacro_hm->hmacros, index);
	}
	return hmacro_hm;
}

/******************************************************************************
 *                                                                            *
 * Function: config_hmacro_context_compare                                    *
 *                                                                            *
 * Purpose: comparison function to sort host macro vector by context          *
 *          operator, value and macro name                                    *
 *                                                                            *
 ******************************************************************************/
static int	config_hmacro_context_compare(const void *d1, const void *d2)
{
	const ZBX_DC_HMACRO	*m1 = *(const ZBX_DC_HMACRO **)d1;
	const ZBX_DC_HMACRO	*m2 = *(const ZBX_DC_HMACRO **)d2;

	/* macros without context have higher priority than macros with */
	if (NULL == m1->context)
		return NULL == m2->context ? 0 : -1;

	if (NULL == m2->context)
		return 1;

	/* CONDITION_OPERATOR_EQUAL (0) has higher priority than CONDITION_OPERATOR_REGEXP (8) */
	ZBX_RETURN_IF_NOT_EQUAL(m1->context_op, m2->context_op);

	return strcmp(m1->context, m2->context);
}

static int	dc_compare_kvs_path(const void *d1, const void *d2)
{
	const zbx_dc_kvs_path_t	*ptr1 = *((const zbx_dc_kvs_path_t **)d1);
	const zbx_dc_kvs_path_t	*ptr2 = *((const zbx_dc_kvs_path_t **)d2);

	return strcmp(ptr1->path, ptr2->path);
}

static zbx_hash_t	dc_kv_hash(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC(((zbx_dc_kv_t *)data)->key);
}

static int	dc_kv_compare(const void *d1, const void *d2)
{
	return strcmp(((zbx_dc_kv_t *)d1)->key, ((zbx_dc_kv_t *)d2)->key);
}

static zbx_dc_kv_t	*config_kvs_path_add(const char *path, const char *key)
{
	zbx_dc_kvs_path_t	*kvs_path, kvs_path_local;
	zbx_dc_kv_t		*kv, kv_local;
	int			i;

	kvs_path_local.path = path;

	if (FAIL == (i = zbx_vector_ptr_search(&config->kvs_paths, &kvs_path_local, dc_compare_kvs_path)))
	{
		kvs_path = (zbx_dc_kvs_path_t *)__config_mem_malloc_func(NULL, sizeof(zbx_dc_kvs_path_t));
		DCstrpool_replace(0, &kvs_path->path, path);
		zbx_vector_ptr_append(&config->kvs_paths, kvs_path);

		zbx_hashset_create_ext(&kvs_path->kvs, 0, dc_kv_hash, dc_kv_compare, NULL,
				__config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func);
		kv = NULL;
	}
	else
	{
		kvs_path = (zbx_dc_kvs_path_t *)config->kvs_paths.values[i];
		kv_local.key = key;
		kv = (zbx_dc_kv_t *)zbx_hashset_search(&kvs_path->kvs, &kv_local);
	}

	if (NULL == kv)
	{
		DCstrpool_replace(0, &kv_local.key, key);
		kv_local.value = NULL;
		kv_local.refcount = 0;

		kv = (zbx_dc_kv_t *)zbx_hashset_insert(&kvs_path->kvs, &kv_local, sizeof(zbx_dc_kv_t));
	}

	kv->refcount++;

	return kv;
}

static void	config_kvs_path_remove(const char *value, zbx_dc_kv_t *kv)
{
	zbx_dc_kvs_path_t	*kvs_path, kvs_path_local;
	int			i;
	char			*path, *key;

	if (0 != --kv->refcount)
		return;

	zbx_strsplit(value, ':', &path, &key);
	zbx_free(key);

	zbx_strpool_release(kv->key);
	if (NULL != kv->value)
		zbx_strpool_release(kv->value);

	kvs_path_local.path = path;

	if (FAIL == (i = zbx_vector_ptr_search(&config->kvs_paths, &kvs_path_local, dc_compare_kvs_path)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		goto clean;
	}
	kvs_path = (zbx_dc_kvs_path_t *)config->kvs_paths.values[i];

	zbx_hashset_remove_direct(&kvs_path->kvs, kv);

	if (0 == kvs_path->kvs.num_data)
	{
		zbx_strpool_release(kvs_path->path);
		__config_mem_free_func(kvs_path);
		zbx_vector_ptr_remove_noorder(&config->kvs_paths, i);
	}
clean:
	zbx_free(path);
}

/******************************************************************************
 *                                                                            *
 * Function: set_hk_opt                                                       *
 *                                                                            *
 * Purpose: sets and validates global housekeeping option                     *
 *                                                                            *
 * Parameters: value     - [OUT] housekeeping setting                         *
 *             non_zero  - [IN] 0 if value is allowed to be zero, 1 otherwise *
 *             value_min - [IN] minimal acceptable setting value              *
 *             value_raw - [IN] setting value to validate                     *
 *                                                                            *
 ******************************************************************************/
static int	set_hk_opt(int *value, int non_zero, int value_min, const char *value_raw)
{
	if (SUCCEED != is_time_suffix(value_raw, value, ZBX_LENGTH_UNLIMITED))
		return FAIL;

	if (0 != non_zero && 0 == *value)
		return FAIL;

	if (0 != *value && (value_min > *value || ZBX_HK_PERIOD_MAX < *value))
		return FAIL;

	return SUCCEED;
}

static int	DCsync_config(zbx_dbsync_t *sync, int *flags)
{
	const ZBX_TABLE	*config_table;

	const char	*selected_fields[] = {"discovery_groupid", "snmptrap_logging",
					"severity_name_0", "severity_name_1", "severity_name_2", "severity_name_3",
					"severity_name_4", "severity_name_5", "hk_events_mode", "hk_events_trigger",
					"hk_events_internal", "hk_events_discovery", "hk_events_autoreg",
					"hk_services_mode", "hk_services", "hk_audit_mode", "hk_audit",
					"hk_sessions_mode", "hk_sessions", "hk_history_mode", "hk_history_global",
					"hk_history", "hk_trends_mode", "hk_trends_global", "hk_trends",
					"default_inventory_mode", "db_extension", "autoreg_tls_accept",
					"compression_status", "compression_availability", "compress_older",
					"instanceid", "default_timezone"};	/* sync with zbx_dbsync_compare_config() */
	const char	*row[ARRSIZE(selected_fields)];
	size_t		i;
	int		j, found = 1, ret;
	char		**db_row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*flags = 0;

	if (NULL == config->config)
	{
		found = 0;
		config->config = (ZBX_DC_CONFIG_TABLE *)__config_mem_malloc_func(NULL, sizeof(ZBX_DC_CONFIG_TABLE));
	}

	if (SUCCEED != (ret = zbx_dbsync_next(sync, &rowid, &db_row, &tag)))
	{
		/* load default config data */

		if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
			zabbix_log(LOG_LEVEL_ERR, "no records in table 'config'");

		config_table = DBget_table("config");

		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = DBget_field(config_table, selected_fields[i])->default_value;
	}
	else
	{
		for (i = 0; i < ARRSIZE(selected_fields); i++)
			row[i] = db_row[i];
	}

	/* store the config data */

	if (NULL != row[0])
		ZBX_STR2UINT64(config->config->discovery_groupid, row[0]);
	else
		config->config->discovery_groupid = ZBX_DISCOVERY_GROUPID_UNDEFINED;

	ZBX_STR2UCHAR(config->config->snmptrap_logging, row[1]);
	config->config->default_inventory_mode = atoi(row[25]);
	DCstrpool_replace(found, (const char **)&config->config->db.extension, row[26]);
	ZBX_STR2UCHAR(config->config->autoreg_tls_accept, row[27]);
	ZBX_STR2UCHAR(config->config->db.history_compression_status, row[28]);
	ZBX_STR2UCHAR(config->config->db.history_compression_availability, row[29]);

	if (SUCCEED != is_time_suffix(row[30], &config->config->db.history_compress_older, ZBX_LENGTH_UNLIMITED))
	{
		zabbix_log(LOG_LEVEL_WARNING, "invalid history compression age: %s", row[30]);
		config->config->db.history_compress_older = 0;
	}

	for (j = 0; TRIGGER_SEVERITY_COUNT > j; j++)
		DCstrpool_replace(found, &config->config->severity_name[j], row[2 + j]);

	/* instance id cannot be changed - update it only at first sync to avoid read locks later */
	if (0 == found)
		DCstrpool_replace(found, &config->config->instanceid, row[31]);

#if TRIGGER_SEVERITY_COUNT != 6
#	error "row indexes below are based on assumption of six trigger severity levels"
#endif

	/* read housekeeper configuration */

	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.events_mode = atoi(row[8])) &&
			(SUCCEED != set_hk_opt(&config->config->hk.events_trigger, 1, SEC_PER_DAY, row[9]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_internal, 1, SEC_PER_DAY, row[10]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_discovery, 1, SEC_PER_DAY, row[11]) ||
			SUCCEED != set_hk_opt(&config->config->hk.events_autoreg, 1, SEC_PER_DAY, row[12])))
	{
		zabbix_log(LOG_LEVEL_WARNING, "trigger, internal, network discovery and auto-registration data"
				" housekeeping will be disabled due to invalid settings");
		config->config->hk.events_mode = ZBX_HK_OPTION_DISABLED;
	}

	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.services_mode = atoi(row[13])) &&
			SUCCEED != set_hk_opt(&config->config->hk.services, 1, SEC_PER_DAY, row[14]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "IT services data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.services_mode = ZBX_HK_OPTION_DISABLED;
	}

	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.audit_mode = atoi(row[15])) &&
			SUCCEED != set_hk_opt(&config->config->hk.audit, 1, SEC_PER_DAY, row[16]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "audit data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.audit_mode = ZBX_HK_OPTION_DISABLED;
	}

	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.sessions_mode = atoi(row[17])) &&
			SUCCEED != set_hk_opt(&config->config->hk.sessions, 1, SEC_PER_DAY, row[18]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "user sessions data housekeeping will be disabled due to invalid"
				" settings");
		config->config->hk.sessions_mode = ZBX_HK_OPTION_DISABLED;
	}

	config->config->hk.history_mode = atoi(row[19]);
	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.history_global = atoi(row[20])) &&
			SUCCEED != set_hk_opt(&config->config->hk.history, 0, ZBX_HK_HISTORY_MIN, row[21]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "history data housekeeping will be disabled and all items will"
				" store their history due to invalid global override settings");
		config->config->hk.history_mode = ZBX_HK_MODE_DISABLED;
		config->config->hk.history = 1;	/* just enough to make 0 == items[i].history condition fail */
	}

#ifdef HAVE_POSTGRESQL
	if (ZBX_HK_MODE_DISABLED != config->config->hk.history_mode &&
			ZBX_HK_OPTION_ENABLED == config->config->hk.history_global &&
			0 == zbx_strcmp_null(config->config->db.extension, ZBX_CONFIG_DB_EXTENSION_TIMESCALE))
	{
		config->config->hk.history_mode = ZBX_HK_MODE_PARTITION;
	}
#endif

	config->config->hk.trends_mode = atoi(row[22]);
	if (ZBX_HK_OPTION_ENABLED == (config->config->hk.trends_global = atoi(row[23])) &&
			SUCCEED != set_hk_opt(&config->config->hk.trends, 0, ZBX_HK_TRENDS_MIN, row[24]))
	{
		zabbix_log(LOG_LEVEL_WARNING, "trends data housekeeping will be disabled and all numeric items"
				" will store their history due to invalid global override settings");
		config->config->hk.trends_mode = ZBX_HK_MODE_DISABLED;
		config->config->hk.trends = 1;	/* just enough to make 0 == items[i].trends condition fail */
	}

#ifdef HAVE_POSTGRESQL
	if (ZBX_HK_MODE_DISABLED != config->config->hk.trends_mode &&
			ZBX_HK_OPTION_ENABLED == config->config->hk.trends_global &&
			0 == zbx_strcmp_null(config->config->db.extension, ZBX_CONFIG_DB_EXTENSION_TIMESCALE))
	{
		config->config->hk.trends_mode = ZBX_HK_MODE_PARTITION;
	}
#endif
	DCstrpool_replace(found, &config->config->default_timezone, row[32]);

	if (SUCCEED == ret && SUCCEED == zbx_dbsync_next(sync, &rowid, &db_row, &tag))	/* table must have */
		zabbix_log(LOG_LEVEL_ERR, "table 'config' has multiple records");	/* only one record */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return SUCCEED;
}

static void	DCsync_autoreg_config(zbx_dbsync_t *sync)
{
	/* sync this function with zbx_dbsync_compare_autoreg_psk() */
	int		ret;
	char		**db_row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &db_row, &tag)))
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
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_proxy_remove(ZBX_DC_PROXY *proxy)
{
	if (ZBX_LOC_QUEUE == proxy->location)
	{
		zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
		proxy->location = ZBX_LOC_NOWHERE;
	}

	zbx_strpool_release(proxy->proxy_address);
	zbx_hashset_remove_direct(&config->proxies, proxy);
}

static void	DCsync_hosts(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	ZBX_DC_HOST	*host;
	ZBX_DC_IPMIHOST	*ipmihost;
	ZBX_DC_PROXY	*proxy;
	ZBX_DC_HOST_H	*host_h, host_h_local, *host_p, host_p_local;

	int		found;
	int		update_index_h, update_index_p, ret;
	zbx_uint64_t	hostid, proxy_hostid;
	unsigned char	status;
	time_t		now;
	signed char	ipmi_authtype;
	unsigned char	ipmi_privilege;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	ZBX_DC_PSK	*psk_i, psk_i_local;
	zbx_ptr_pair_t	*psk_owner, psk_owner_local;
	zbx_hashset_t	psk_owners;
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_hashset_create(&psk_owners, 0, ZBX_DEFAULT_PTR_HASH_FUNC, ZBX_DEFAULT_PTR_COMPARE_FUNC);
#endif
	now = time(NULL);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_DBROW2UINT64(proxy_hostid, row[1]);
		ZBX_STR2UCHAR(status, row[22]);

		host = (ZBX_DC_HOST *)DCfind_id(&config->hosts, hostid, sizeof(ZBX_DC_HOST), &found);

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

				if (NULL != host_h && host == host_h->host_ptr)	/* see ZBX-4045 for NULL check */
				{
					zbx_strpool_release(host_h->host);
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
		else if ((HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status
				|| HOST_STATUS_SERVER == status) &&
				(0 == found || 0 != strcmp(host->host, row[2])))
		{
			if (1 == found)
			{
				host_p_local.host = host->host;
				host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local);

				if (NULL != host_p && host == host_p->host_ptr)
				{
					zbx_strpool_release(host_p->host);
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

		DCstrpool_replace(found, &host->host, row[2]);
		DCstrpool_replace(found, &host->name, row[23]);
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		DCstrpool_replace(found, &host->tls_issuer, row[31]);
		DCstrpool_replace(found, &host->tls_subject, row[32]);

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

		if ('\0' == *row[33] || '\0' == *row[34])	/* new PSKid or value empty */
		{
			/* In case of "impossible" errors ("PSK value without identity" or "PSK identity without */
			/* value") assume empty PSK identity and value. These errors should have been prevented */
			/* by validation in frontend/API. Be prepared when making a connection requiring PSK - */
			/* the PSK might not be available. */

			if (1 == found)
			{
				if (NULL == host->tls_dc_psk)	/* 'host' record has empty PSK */
					goto done;

				/* 'host' record has non-empty PSK. Unlink and delete PSK. */

				psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

				if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)) &&
						0 == --(psk_i->refcount))
				{
					zbx_strpool_release(psk_i->tls_psk_identity);
					zbx_strpool_release(psk_i->tls_psk);
					zbx_hashset_remove_direct(&config->psks, psk_i);
				}
			}

			host->tls_dc_psk = NULL;
			goto done;
		}

		/* new PSKid and value non-empty */

		zbx_strlower(row[34]);

		if (1 == found && NULL != host->tls_dc_psk)	/* 'host' record has non-empty PSK */
		{
			if (0 == strcmp(host->tls_dc_psk->tls_psk_identity, row[33]))	/* new PSKid same as */
											/* old PSKid */
			{
				if (0 != strcmp(host->tls_dc_psk->tls_psk, row[34]))	/* new PSK value */
											/* differs from old */
				{
					if (NULL == (psk_owner = (zbx_ptr_pair_t *)zbx_hashset_search(&psk_owners,
							&host->tls_dc_psk->tls_psk_identity)))
					{
						/* change underlying PSK value and 'config->psks' is updated, too */
						DCstrpool_replace(1, &host->tls_dc_psk->tls_psk, row[34]);
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
				zbx_strpool_release(psk_i->tls_psk_identity);
				zbx_strpool_release(psk_i->tls_psk);
				zbx_hashset_remove_direct(&config->psks, psk_i);
			}

			host->tls_dc_psk = NULL;
		}

		/* new PSK identity already stored? */

		psk_i_local.tls_psk_identity = row[33];

		if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)))
		{
			/* new PSKid already in psks hashset */

			if (0 != strcmp(psk_i->tls_psk, row[34]))	/* PSKid stored but PSK value is different */
			{
				if (NULL == (psk_owner = (zbx_ptr_pair_t *)zbx_hashset_search(&psk_owners, &psk_i->tls_psk_identity)))
				{
					DCstrpool_replace(1, &psk_i->tls_psk, row[34]);
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

		DCstrpool_replace(0, &psk_i_local.tls_psk_identity, row[33]);
		DCstrpool_replace(0, &psk_i_local.tls_psk, row[34]);
		psk_i_local.refcount = 1;
		host->tls_dc_psk = zbx_hashset_insert(&config->psks, &psk_i_local, sizeof(ZBX_DC_PSK));
done:
		if (NULL != host->tls_dc_psk && NULL == psk_owner)
		{
			if (NULL == (psk_owner = (zbx_ptr_pair_t *)zbx_hashset_search(&psk_owners, &host->tls_dc_psk->tls_psk_identity)))
			{
				/* register this host as the PSK identity owner, against which to report conflicts */

				psk_owner_local.first = (char *)host->tls_dc_psk->tls_psk_identity;
				psk_owner_local.second = (char *)host->host;

				zbx_hashset_insert(&psk_owners, &psk_owner_local, sizeof(psk_owner_local));
			}
		}
#endif
		ZBX_STR2UCHAR(host->tls_connect, row[29]);
		ZBX_STR2UCHAR(host->tls_accept, row[30]);

		if ((HOST_STATUS_PROXY_PASSIVE == status && 0 != (ZBX_TCP_SEC_UNENCRYPTED & host->tls_connect)) ||
				(HOST_STATUS_PROXY_ACTIVE == status && 0 != (ZBX_TCP_SEC_UNENCRYPTED & host->tls_accept)))
		{
			if (NULL != CONFIG_VAULTTOKEN)
			{
				zabbix_log(LOG_LEVEL_WARNING, "connection with Zabbix proxy \"%s\" should not be"
						" unencrypted when using Vault", host->host);
			}
		}

		if (0 == found)
		{
			ZBX_DBROW2UINT64(host->maintenanceid, row[33 + ZBX_HOST_TLS_OFFSET]);
			host->maintenance_status = (unsigned char)atoi(row[7]);
			host->maintenance_type = (unsigned char)atoi(row[8]);
			host->maintenance_from = atoi(row[9]);
			host->data_expected_from = now;
			host->update_items = 0;

			host->errors_from = atoi(row[10]);
			host->available = (unsigned char)atoi(row[11]);
			host->disable_until = atoi(row[12]);
			host->snmp_errors_from = atoi(row[13]);
			host->snmp_available = (unsigned char)atoi(row[14]);
			host->snmp_disable_until = atoi(row[15]);
			host->ipmi_errors_from = atoi(row[16]);
			host->ipmi_available = (unsigned char)atoi(row[17]);
			host->ipmi_disable_until = atoi(row[18]);
			host->jmx_errors_from = atoi(row[19]);
			host->jmx_available = (unsigned char)atoi(row[20]);
			host->jmx_disable_until = atoi(row[21]);
			host->availability_ts = now;

			DCstrpool_replace(0, &host->error, row[25]);
			DCstrpool_replace(0, &host->snmp_error, row[26]);
			DCstrpool_replace(0, &host->ipmi_error, row[27]);
			DCstrpool_replace(0, &host->jmx_error, row[28]);

			host->items_num = 0;
			host->snmp_items_num = 0;
			host->ipmi_items_num = 0;
			host->jmx_items_num = 0;

			host->reset_availability = 0;
			/* by default hosts are cluster-enabled so if cluster misconfigured or no proxy poller is running, then 
			server will work with all the hosts */
			host->cluster_state = ZBX_CLUSTER_HOST_STATE_ACTIVE;
			
			zbx_vector_ptr_create_ext(&host->interfaces_v, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);

		}
		else
		{
			if (HOST_STATUS_MONITORED == status && HOST_STATUS_MONITORED != host->status)
				host->data_expected_from = now;

			/* reset host status if host status has been changed (e.g., if host has been disabled) */
			if (status != host->status)
				host->reset_availability = 1;

			/* reset host status if host proxy assignment has been changed */
			if (proxy_hostid != host->proxy_hostid)
				host->reset_availability = 1;
		}

		host->proxy_hostid = proxy_hostid;

		/* update 'hosts_h' and 'hosts_p' indexes using new data, if not done already */

		if (1 == update_index_h)
		{
			host_h_local.host = zbx_strpool_acquire(host->host);
			host_h_local.host_ptr = host;
			zbx_hashset_insert(&config->hosts_h, &host_h_local, sizeof(ZBX_DC_HOST_H));
		}

		if (1 == update_index_p)
		{
			host_p_local.host = zbx_strpool_acquire(host->host);
			host_p_local.host_ptr = host;
			zbx_hashset_insert(&config->hosts_p, &host_p_local, sizeof(ZBX_DC_HOST_H));
		}

		/* IPMI hosts */

		ipmi_authtype = (signed char)atoi(row[3]);
		ipmi_privilege = (unsigned char)atoi(row[4]);

		if (ZBX_IPMI_DEFAULT_AUTHTYPE != ipmi_authtype || ZBX_IPMI_DEFAULT_PRIVILEGE != ipmi_privilege ||
				'\0' != *row[5] || '\0' != *row[6])	/* useipmi */
		{
			ipmihost = (ZBX_DC_IPMIHOST *)DCfind_id(&config->ipmihosts, hostid, sizeof(ZBX_DC_IPMIHOST), &found);

			ipmihost->ipmi_authtype = ipmi_authtype;
			ipmihost->ipmi_privilege = ipmi_privilege;
			DCstrpool_replace(found, &ipmihost->ipmi_username, row[5]);
			DCstrpool_replace(found, &ipmihost->ipmi_password, row[6]);
		}
		else if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &hostid)))
		{
			/* remove IPMI connection parameters for hosts without IPMI */

			zbx_strpool_release(ipmihost->ipmi_username);
			zbx_strpool_release(ipmihost->ipmi_password);

			zbx_hashset_remove_direct(&config->ipmihosts, ipmihost);
		}

		/* proxies */

		if (HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_PROXY_PASSIVE == status
		|| HOST_STATUS_SERVER == status || HOST_STATUS_DOMAIN == status)
		{
			proxy = (ZBX_DC_PROXY *)DCfind_id(&config->proxies, hostid, sizeof(ZBX_DC_PROXY), &found);

			if (0 == found)
			{
				proxy->location = ZBX_LOC_NOWHERE;
				proxy->version = 0;
				proxy->lastaccess = atoi(row[24]);
				proxy->last_cfg_error_time = 0;
				proxy->proxy_delay = 0;
				proxy->nodata_win.flags = ZBX_PROXY_SUPPRESS_DISABLE;
				proxy->nodata_win.values_num = 0;
				proxy->nodata_win.period_end = 0;


				//cluster fields init
				zbx_vector_uint64_create_ext(&proxy->cluster_domains,
					 __config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func);
		
				zbx_vector_uint64_create_ext(&proxy->connected_servers,
					 __config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func);

				zbx_vector_uint64_pair_create_ext(&proxy->proxy_hosts,
					 __config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func);
		
				zbx_vector_ptr_create_ext(&proxy->cluster_rerouted_data,
					 __config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func);
					
				proxy->cluster_id=0;
				proxy->cluster_failed_hello_count=0;
				proxy->cluster_lastheard=0;
				proxy->cluster_state=ZBX_CLUSTER_SERVER_STATE_DOWN;
				proxy->cluster_topology_version=0;

			}

			proxy->auto_compress = atoi(row[32 + ZBX_HOST_TLS_OFFSET]);
			DCstrpool_replace(found, &proxy->proxy_address, row[31 + ZBX_HOST_TLS_OFFSET]);

			if ((HOST_STATUS_SERVER == status || HOST_STATUS_PROXY_PASSIVE == status) && (0 == found || status != host->status))
			{
				proxy->server_hello_nextsend = (int)calculate_proxy_nextcheck(
						hostid, ZBX_CLUSTER_HELLO_FREQUENCY , now);
				
				DCupdate_proxy_queue(proxy);
			}
			else if ((HOST_STATUS_PROXY_ACTIVE == status || HOST_STATUS_DOMAIN == status) 
					&& ZBX_LOC_QUEUE == proxy->location)
			{
				zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = ZBX_LOC_NOWHERE;
			}
			proxy->last_version_error_time = time(NULL);
		}
		else  if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
		{
			int i;
			//TODO: since 5 all proxy removal code is in DCsync_proxy_remove(proxy); - move all cluster code there either
			if (ZBX_LOC_QUEUE == proxy->location)
			{
				zbx_binary_heap_remove_direct(&config->pqueue, proxy->hostid);
				proxy->location = ZBX_LOC_NOWHERE;
			}

			zbx_strpool_release(proxy->proxy_address);
			zbx_vector_uint64_destroy(&proxy->cluster_domains);
			zbx_vector_uint64_destroy(&proxy->connected_servers);
			zbx_vector_uint64_pair_destroy(&proxy->proxy_hosts);

			for ( i=0; i < proxy->cluster_rerouted_data.values_num; i++) {
				zbx_strpool_release((const char *)&proxy->cluster_rerouted_data.values[i]);
			}
			
			zbx_vector_ptr_destroy(&proxy->cluster_rerouted_data);

			zbx_hashset_remove_direct(&config->proxies, proxy);
		}

			
		 
			

		host->status = status;
	}

	/* remove deleted hosts from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &rowid)))
			continue;

		hostid = host->hostid;

		/* IPMI hosts */

		if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &hostid)))
		{
			zbx_strpool_release(ipmihost->ipmi_username);
			zbx_strpool_release(ipmihost->ipmi_password);

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

			if (NULL != host_h && host == host_h->host_ptr)	/* see ZBX-4045 for NULL check */
			{
				zbx_strpool_release(host_h->host);
				zbx_hashset_remove_direct(&config->hosts_h, host_h);
			}
		}
		else if (HOST_STATUS_PROXY_ACTIVE == host->status || HOST_STATUS_PROXY_PASSIVE == host->status)
		{
			host_p_local.host = host->host;
			host_p = (ZBX_DC_HOST_H *)zbx_hashset_search(&config->hosts_p, &host_p_local);

			if (NULL != host_p && host == host_p->host_ptr)
			{
				zbx_strpool_release(host_p->host);
				zbx_hashset_remove_direct(&config->hosts_p, host_p);
			}
		}

		zbx_strpool_release(host->host);
		zbx_strpool_release(host->name);

		zbx_strpool_release(host->error);
		zbx_strpool_release(host->snmp_error);
		zbx_strpool_release(host->ipmi_error);
		zbx_strpool_release(host->jmx_error);
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zbx_strpool_release(host->tls_issuer);
		zbx_strpool_release(host->tls_subject);

		/* Maintain 'psks' index. Unlink and delete the PSK identity. */
		if (NULL != host->tls_dc_psk)
		{
			psk_i_local.tls_psk_identity = host->tls_dc_psk->tls_psk_identity;

			if (NULL != (psk_i = (ZBX_DC_PSK *)zbx_hashset_search(&config->psks, &psk_i_local)) &&
					0 == --(psk_i->refcount))
			{
				zbx_strpool_release(psk_i->tls_psk_identity);
				zbx_strpool_release(psk_i->tls_psk);
				zbx_hashset_remove_direct(&config->psks, psk_i);
			}
		}
#endif
		zbx_vector_ptr_destroy(&host->interfaces_v);
		zbx_hashset_remove_direct(&config->hosts, host);
	}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_hashset_destroy(&psk_owners);
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_host_inventory(zbx_dbsync_t *sync)
{
	ZBX_DC_HOST_INVENTORY	*host_inventory, *host_inventory_auto;
	zbx_uint64_t		rowid, hostid;
	int			found, ret, i;
	char			**row;
	unsigned char		tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostid, row[0]);

		host_inventory = (ZBX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories, hostid, sizeof(ZBX_DC_HOST_INVENTORY), &found);

		ZBX_STR2UCHAR(host_inventory->inventory_mode, row[1]);

		/* store new information in host_inventory structure */
		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			DCstrpool_replace(found, &(host_inventory->values[i]), row[i + 2]);

		host_inventory_auto = (ZBX_DC_HOST_INVENTORY *)DCfind_id(&config->host_inventories_auto, hostid, sizeof(ZBX_DC_HOST_INVENTORY),
				&found);

		host_inventory_auto->inventory_mode = host_inventory->inventory_mode;

		if (1 == found)
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			{
				if (NULL == host_inventory_auto->values[i])
					continue;

				zbx_strpool_release(host_inventory_auto->values[i]);
				host_inventory_auto->values[i] = NULL;
			}
		}
		else
		{
			for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
				host_inventory_auto->values[i] = NULL;
		}
	}

	/* remove deleted host inventory from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories, &rowid)))
			continue;

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
			zbx_strpool_release(host_inventory->values[i]);

		zbx_hashset_remove_direct(&config->host_inventories, host_inventory);

		if (NULL == (host_inventory_auto = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories_auto, &rowid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		for (i = 0; i < HOST_INVENTORY_FIELD_COUNT; i++)
		{
			if (NULL != host_inventory_auto->values[i])
				zbx_strpool_release(host_inventory_auto->values[i]);
		}

		zbx_hashset_remove_direct(&config->host_inventories_auto, host_inventory_auto);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_htmpls(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;

	ZBX_DC_HTMPL		*htmpl = NULL;

	int			found, i, index, ret;
	zbx_uint64_t		_hostid = 0, hostid, templateid;
	zbx_vector_ptr_t	sort;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&sort);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(templateid, row[1]);

		if (_hostid != hostid || 0 == _hostid)
		{
			_hostid = hostid;

			htmpl = (ZBX_DC_HTMPL *)DCfind_id(&config->htmpls, hostid, sizeof(ZBX_DC_HTMPL), &found);

			if (0 == found)
			{
				zbx_vector_uint64_create_ext(&htmpl->templateids,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				zbx_vector_uint64_reserve(&htmpl->templateids, 1);
			}

			zbx_vector_ptr_append(&sort, htmpl);
		}

		zbx_vector_uint64_append(&htmpl->templateids, templateid);
	}

	/* remove deleted host templates from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(hostid, row[0]);

		if (NULL == (htmpl = (ZBX_DC_HTMPL *)zbx_hashset_search(&config->htmpls, &hostid)))
			continue;

		ZBX_STR2UINT64(templateid, row[1]);

		if (-1 == (index = zbx_vector_uint64_search(&htmpl->templateids, templateid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			continue;
		}

		if (1 == htmpl->templateids.values_num)
		{
			zbx_vector_uint64_destroy(&htmpl->templateids);
			zbx_hashset_remove_direct(&config->htmpls, htmpl);
		}
		else
		{
			zbx_vector_uint64_remove_noorder(&htmpl->templateids, index);
			zbx_vector_ptr_append(&sort, htmpl);
		}
	}

	/* sort the changed template lists */

	zbx_vector_ptr_sort(&sort, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&sort, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < sort.values_num; i++)
	{
		htmpl = (ZBX_DC_HTMPL *)sort.values[i];
		zbx_vector_uint64_sort(&htmpl->templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	zbx_vector_ptr_destroy(&sort);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_gmacros(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag, context_op, type;
	ZBX_DC_GMACRO		*gmacro;
	int			found, context_existed, update_index, ret, i;
	zbx_uint64_t		globalmacroid;
	char			*macro = NULL, *context = NULL, *path = NULL, *key = NULL;
	zbx_vector_ptr_t	indexes;
	ZBX_DC_GMACRO_M		*gmacro_m;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&indexes);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(globalmacroid, row[0]);
		ZBX_STR2UCHAR(type, row[3]);

		if (SUCCEED != zbx_user_macro_parse_dyn(row[1], &macro, &context, NULL, &context_op))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse user macro \"%s\"", row[1]);
			continue;
		}

		if (ZBX_MACRO_VALUE_VAULT == type)
		{
			zbx_free(path);
			zbx_free(key);
			zbx_strsplit(row[2], ':', &path, &key);
			if (NULL == key)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot parse user macro \"%s\" Vault location \"%s\":"
						" missing separator \":\"", row[1], row[2]);
				continue;
			}

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && NULL != CONFIG_VAULTDBPATH &&
					0 == strcasecmp(CONFIG_VAULTDBPATH, path) &&
					(0 == strcasecmp(key, ZBX_PROTO_TAG_PASSWORD)
							|| 0 == strcasecmp(key, ZBX_PROTO_TAG_USERNAME)))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot parse macro \"%s\" Vault location \"%s\":"
						" database credentials should not be used with Vault macros",
						row[1], row[2]);
				continue;
			}
		}

		gmacro = (ZBX_DC_GMACRO *)DCfind_id(&config->gmacros, globalmacroid, sizeof(ZBX_DC_GMACRO), &found);

		/* see whether we should and can update gmacros_m index at this point */
		update_index = 0;

		if (0 == found || 0 != strcmp(gmacro->macro, macro) || 0 != zbx_strcmp_null(gmacro->context, context) ||
				gmacro->context_op != context_op)
		{
			if (1 == found)
			{
				gmacro_m = config_gmacro_remove_index(&config->gmacros_m, gmacro);
				zbx_vector_ptr_append(&indexes, gmacro_m);
			}

			update_index = 1;
		}

		if (0 != found && NULL != gmacro->kv)
			config_kvs_path_remove(gmacro->value, gmacro->kv);

		if (ZBX_MACRO_VALUE_VAULT == type)
			gmacro->kv = config_kvs_path_add(path, key);
		else
			gmacro->kv = NULL;

		/* store new information in macro structure */
		gmacro->type = type;
		gmacro->context_op = context_op;
		DCstrpool_replace(found, &gmacro->macro, macro);
		DCstrpool_replace(found, &gmacro->value, row[2]);

		context_existed = (1 == found && NULL != gmacro->context);

		if (NULL == context)
		{
			/* release the context if it was removed from the macro */
			if (1 == context_existed)
				zbx_strpool_release(gmacro->context);

			gmacro->context = NULL;
		}
		else
		{
			/* replace the existing context (1) or add context to macro (0) */
			DCstrpool_replace(context_existed, &gmacro->context, context);
		}

		/* update gmacros_m index using new data */
		if (1 == update_index)
		{
			gmacro_m = config_gmacro_add_index(&config->gmacros_m, gmacro);
			zbx_vector_ptr_append(&indexes, gmacro_m);
		}
	}

	/* remove deleted global macros from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (gmacro = (ZBX_DC_GMACRO *)zbx_hashset_search(&config->gmacros, &rowid)))
			continue;

		if (NULL != gmacro->kv)
			config_kvs_path_remove(gmacro->value, gmacro->kv);

		gmacro_m = config_gmacro_remove_index(&config->gmacros_m, gmacro);
		zbx_vector_ptr_append(&indexes, gmacro_m);

		zbx_strpool_release(gmacro->macro);
		zbx_strpool_release(gmacro->value);

		if (NULL != gmacro->context)
			zbx_strpool_release(gmacro->context);

		zbx_hashset_remove_direct(&config->gmacros, gmacro);
	}

	zbx_vector_ptr_sort(&indexes, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&indexes, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < indexes.values_num; i++)
	{
		gmacro_m = (ZBX_DC_GMACRO_M *)indexes.values[i];
		if (0 == gmacro_m->gmacros.values_num)
		{
			zbx_strpool_release(gmacro_m->macro);
			zbx_vector_ptr_destroy(&gmacro_m->gmacros);
			zbx_hashset_remove_direct(&config->gmacros_m, gmacro_m);
		}
		else
			zbx_vector_ptr_sort(&gmacro_m->gmacros, config_gmacro_context_compare);
	}

	zbx_free(key);
	zbx_free(path);
	zbx_free(context);
	zbx_free(macro);
	zbx_vector_ptr_destroy(&indexes);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_hmacros(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag, context_op, type;
	ZBX_DC_HMACRO		*hmacro;
	int			found, context_existed, update_index, ret, i;
	zbx_uint64_t		hostmacroid, hostid;
	char			*macro = NULL, *context = NULL, *path = NULL, *key = NULL;
	zbx_vector_ptr_t	indexes;
	ZBX_DC_HMACRO_HM	*hmacro_hm;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&indexes);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(hostmacroid, row[0]);
		ZBX_STR2UINT64(hostid, row[1]);
		ZBX_STR2UCHAR(type, row[4]);

		if (SUCCEED != zbx_user_macro_parse_dyn(row[2], &macro, &context, NULL, &context_op))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse host \"%s\" macro \"%s\"", row[1], row[2]);
			continue;
		}

		if (ZBX_MACRO_VALUE_VAULT == type)
		{
			zbx_free(path);
			zbx_free(key);
			zbx_strsplit(row[3], ':', &path, &key);
			if (NULL == key)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot parse host \"%s\" macro \"%s\" Vault location"
						" \"%s\": missing separator \":\"", row[1], row[2], row[3]);
				continue;
			}

			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && NULL != CONFIG_VAULTDBPATH &&
					0 == strcasecmp(CONFIG_VAULTDBPATH, path) &&
					(0 == strcasecmp(key, ZBX_PROTO_TAG_PASSWORD)
							|| 0 == strcasecmp(key, ZBX_PROTO_TAG_USERNAME)))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot parse host \"%s\" macro \"%s\" Vault location"
						" \"%s\": database credentials should not be used with Vault macros",
						row[1], row[2], row[3]);
				continue;
			}
		}

		hmacro = (ZBX_DC_HMACRO *)DCfind_id(&config->hmacros, hostmacroid, sizeof(ZBX_DC_HMACRO), &found);

		/* see whether we should and can update hmacros_hm index at this point */
		update_index = 0;

		if (0 == found || hmacro->hostid != hostid || 0 != strcmp(hmacro->macro, macro) ||
				0 != zbx_strcmp_null(hmacro->context, context) || hmacro->context_op != context_op)
		{
			if (1 == found)
			{
				hmacro_hm = config_hmacro_remove_index(&config->hmacros_hm, hmacro);
				zbx_vector_ptr_append(&indexes, hmacro_hm);
			}

			update_index = 1;
		}

		if (0 != found && NULL != hmacro->kv)
			config_kvs_path_remove(hmacro->value, hmacro->kv);

		if (ZBX_MACRO_VALUE_VAULT == type)
			hmacro->kv = config_kvs_path_add(path, key);
		else
			hmacro->kv = NULL;

		/* store new information in macro structure */
		hmacro->hostid = hostid;
		hmacro->type = type;
		hmacro->context_op = context_op;
		DCstrpool_replace(found, &hmacro->macro, macro);
		DCstrpool_replace(found, &hmacro->value, row[3]);

		context_existed = (1 == found && NULL != hmacro->context);

		if (NULL == context)
		{
			/* release the context if it was removed from the macro */
			if (1 == context_existed)
				zbx_strpool_release(hmacro->context);

			hmacro->context = NULL;
		}
		else
		{
			/* replace the existing context (1) or add context to macro (0) */
			DCstrpool_replace(context_existed, &hmacro->context, context);
		}

		/* update hmacros_hm index using new data */
		if (1 == update_index)
		{
			hmacro_hm = config_hmacro_add_index(&config->hmacros_hm, hmacro);
			zbx_vector_ptr_append(&indexes, hmacro_hm);
		}
	}

	/* remove deleted host macros from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (hmacro = (ZBX_DC_HMACRO *)zbx_hashset_search(&config->hmacros, &rowid)))
			continue;

		if (NULL != hmacro->kv)
			config_kvs_path_remove(hmacro->value, hmacro->kv);

		hmacro_hm = config_hmacro_remove_index(&config->hmacros_hm, hmacro);
		zbx_vector_ptr_append(&indexes, hmacro_hm);

		zbx_strpool_release(hmacro->macro);
		zbx_strpool_release(hmacro->value);

		if (NULL != hmacro->context)
			zbx_strpool_release(hmacro->context);

		zbx_hashset_remove_direct(&config->hmacros, hmacro);
	}

	zbx_vector_ptr_sort(&indexes, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&indexes, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < indexes.values_num; i++)
	{
		hmacro_hm = (ZBX_DC_HMACRO_HM *)indexes.values[i];
		if (0 == hmacro_hm->hmacros.values_num)
		{
			zbx_strpool_release(hmacro_hm->macro);
			zbx_vector_ptr_destroy(&hmacro_hm->hmacros);
			zbx_hashset_remove_direct(&config->hmacros_hm, hmacro_hm);
		}
		else
			zbx_vector_ptr_sort(&hmacro_hm->hmacros, config_hmacro_context_compare);
	}

	zbx_free(key);
	zbx_free(path);
	zbx_free(context);
	zbx_free(macro);
	zbx_vector_ptr_destroy(&indexes);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static int	DCsync_kvs_paths(const struct zbx_json_parse *jp_kvs_paths)
{
	zbx_dc_kvs_path_t	*dc_kvs_path;
	zbx_dc_kv_t		*dc_kv;
	zbx_hashset_t		kvs;
	zbx_hashset_iter_t	iter;
	int			i, j, ret;
	zbx_vector_ptr_pair_t	diff;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_pair_create(&diff);
	zbx_hashset_create_ext(&kvs, 100, zbx_vault_kv_hash, zbx_vault_kv_compare, zbx_vault_kv_clean,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

	for (i = 0; i < config->kvs_paths.values_num; i++)
	{
		char	*error = NULL;

		dc_kvs_path = (zbx_dc_kvs_path_t *)config->kvs_paths.values[i];

		if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		{
			if (NULL == jp_kvs_paths)
			{
				ret = FAIL;
				goto fail;
			}

			if (FAIL == zbx_vault_json_kvs_get(dc_kvs_path->path, jp_kvs_paths, &kvs, &error))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot get secrets for path \"%s\": %s",
						dc_kvs_path->path, error);
				zbx_free(error);
				continue;
			}

		}
		else if (FAIL == zbx_vault_kvs_get(dc_kvs_path->path, &kvs, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot get secrets for path \"%s\": %s", dc_kvs_path->path, error);
			zbx_free(error);
			continue;
		}

		zbx_hashset_iter_reset(&dc_kvs_path->kvs, &iter);
		while (NULL != (dc_kv = (zbx_dc_kv_t *)zbx_hashset_iter_next(&iter)))
		{
			zbx_kv_t	*kv, kv_local;
			zbx_ptr_pair_t	pair;

			kv_local.key = (char *)dc_kv->key;
			if (NULL != (kv = zbx_hashset_search(&kvs, &kv_local)))
			{
				if (0 == zbx_strcmp_null(dc_kv->value, kv->value))
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

			for (j = 0; j < diff.values_num; j++)
			{
				zbx_kv_t	*kv;

				dc_kv = (zbx_dc_kv_t *)diff.values[j].first;
				kv = (zbx_kv_t *)diff.values[j].second;

				if (NULL != kv)
				{
					DCstrpool_replace(dc_kv->value != NULL ? 1 : 0, &dc_kv->value, kv->value);
					continue;
				}

				zbx_strpool_release(dc_kv->value);
				dc_kv->value = NULL;
			}

			FINISH_SYNC;
		}

		zbx_vector_ptr_pair_clear(&diff);
		zbx_hashset_clear(&kvs);
	}
	ret = SUCCEED;
fail:
	zbx_vector_ptr_pair_destroy(&diff);
	zbx_hashset_destroy(&kvs);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: substitute_host_interface_macros                                 *
 *                                                                            *
 * Purpose: trying to resolve the macros in host interface                    *
 *                                                                            *
 ******************************************************************************/
static void	substitute_host_interface_macros(ZBX_DC_INTERFACE *interface)
{
	int	macros;
	char	*addr;
	DC_HOST	host;

	macros = STR_CONTAINS_MACROS(interface->ip) ? 0x01 : 0;
	macros |= STR_CONTAINS_MACROS(interface->dns) ? 0x02 : 0;

	if (0 != macros)
	{
		DCget_host_by_hostid(&host, interface->hostid);

		if (0 != (macros & 0x01))
		{
			addr = zbx_strdup(NULL, interface->ip);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL, NULL,
					&addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == is_ip(addr) || SUCCEED == zbx_validate_hostname(addr))
				DCstrpool_replace(1, &interface->ip, addr);
			zbx_free(addr);
		}

		if (0 != (macros & 0x02))
		{
			addr = zbx_strdup(NULL, interface->dns);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL, NULL,
					&addr, MACRO_TYPE_INTERFACE_ADDR, NULL, 0);
			if (SUCCEED == is_ip(addr) || SUCCEED == zbx_validate_hostname(addr))
				DCstrpool_replace(1, &interface->dns, addr);
			zbx_free(addr);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmpaddrs_remove                                    *
 *                                                                            *
 * Purpose: remove interface from SNMP address -> interfaceid index           *
 *                                                                            *
 * Parameters: interface - [IN] the interface                                 *
 *                                                                            *
 ******************************************************************************/
static void	dc_interface_snmpaddrs_remove(ZBX_DC_INTERFACE *interface)
{
	ZBX_DC_INTERFACE_ADDR	*ifaddr, ifaddr_local;
	int			index;

	ifaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

	if ('\0' == *ifaddr_local.addr)
		return;

	if (NULL == (ifaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(&config->interface_snmpaddrs, &ifaddr_local)))
		return;

	if (FAIL == (index = zbx_vector_uint64_search(&ifaddr->interfaceids, interface->interfaceid,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
	{
		return;
	}

	zbx_vector_uint64_remove_noorder(&ifaddr->interfaceids, index);

	if (0 == ifaddr->interfaceids.values_num)
	{
		zbx_strpool_release(ifaddr->addr);
		zbx_vector_uint64_destroy(&ifaddr->interfaceids);
		zbx_hashset_remove_direct(&config->interface_snmpaddrs, ifaddr);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmp_set                                            *
 *                                                                            *
 * Purpose: setup SNMP attributes for interface with interfaceid index        *
 *                                                                            *
 * Parameters: interface - [IN] the interface                                 *
 *             row       - [IN] the row data from DB                          *
 *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_SNMPINTERFACE	*dc_interface_snmp_set(zbx_uint64_t interfaceid, const char **row,
		unsigned char *bulk_changed)
{
	int			found;
	ZBX_DC_SNMPINTERFACE	*snmp;
	unsigned char		bulk;

	snmp = (ZBX_DC_SNMPINTERFACE *)DCfind_id(&config->interfaces_snmp, interfaceid, sizeof(ZBX_DC_SNMPINTERFACE),
			&found);

	ZBX_STR2UCHAR(bulk, row[9]);

	if (0 == found)
		*bulk_changed = 1;
	else if (snmp->bulk != bulk)
		*bulk_changed = 1;
	else
		*bulk_changed = 0;

	if (0 != *bulk_changed)
		snmp->bulk = bulk;

	ZBX_STR2UCHAR(snmp->version, row[8]);
	DCstrpool_replace(found, &snmp->community, row[10]);
	DCstrpool_replace(found, &snmp->securityname, row[11]);
	ZBX_STR2UCHAR(snmp->securitylevel, row[12]);
	DCstrpool_replace(found, &snmp->authpassphrase, row[13]);
	DCstrpool_replace(found, &snmp->privpassphrase, row[14]);
	ZBX_STR2UCHAR(snmp->authprotocol, row[15]);
	ZBX_STR2UCHAR(snmp->privprotocol, row[16]);
	DCstrpool_replace(found, &snmp->contextname, row[17]);

	return snmp;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmp_remove                                    *
 *                                                                            *
 * Purpose: remove interface from SNMP address -> interfaceid index           *
 *                                                                            *
 * Parameters: interface - [IN] the interface                                 *
 *                                                                            *
 ******************************************************************************/
static void	dc_interface_snmp_remove(zbx_uint64_t interfaceid)
{
	ZBX_DC_SNMPINTERFACE	*snmp;

	if (NULL == (snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &interfaceid)))
		return;

	zbx_strpool_release(snmp->community);
	zbx_strpool_release(snmp->securityname);
	zbx_strpool_release(snmp->authpassphrase);
	zbx_strpool_release(snmp->privpassphrase);
	zbx_strpool_release(snmp->contextname);

	zbx_hashset_remove_direct(&config->interfaces_snmp, snmp);

	return;
}

static void	DCsync_interfaces(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;

	ZBX_DC_INTERFACE	*interface;
	ZBX_DC_INTERFACE_HT	*interface_ht, interface_ht_local;
	ZBX_DC_INTERFACE_ADDR	*interface_snmpaddr, interface_snmpaddr_local;
	ZBX_DC_HOST		*host;

	int			found, update_index, ret, i;
	zbx_uint64_t		interfaceid, hostid;
	unsigned char		type, main_, useip;
	unsigned char		reset_snmp_stats;
	zbx_vector_ptr_t	interfaces;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&interfaces);

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

		interface = (ZBX_DC_INTERFACE *)DCfind_id(&config->interfaces, interfaceid, sizeof(ZBX_DC_INTERFACE), &found);
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
				interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht, &interface_ht_local);

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
				interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht, &interface_ht_local);

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
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->ip, row[5]));
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->dns, row[6]));
		reset_snmp_stats |= (SUCCEED == DCstrpool_replace(found, &interface->port, row[7]));

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
			ZBX_DC_SNMPINTERFACE	*snmp;
			unsigned char		bulk_changed;

			interface_snmpaddr_local.addr = (0 != interface->useip ? interface->ip : interface->dns);

			if ('\0' != *interface_snmpaddr_local.addr)
			{
				if (NULL == (interface_snmpaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(&config->interface_snmpaddrs,
						&interface_snmpaddr_local)))
				{
					zbx_strpool_acquire(interface_snmpaddr_local.addr);

					interface_snmpaddr = (ZBX_DC_INTERFACE_ADDR *)zbx_hashset_insert(&config->interface_snmpaddrs,
							&interface_snmpaddr_local, sizeof(ZBX_DC_INTERFACE_ADDR));
					zbx_vector_uint64_create_ext(&interface_snmpaddr->interfaceids,
							__config_mem_malloc_func,
							__config_mem_realloc_func,
							__config_mem_free_func);
				}

				zbx_vector_uint64_append(&interface_snmpaddr->interfaceids, interfaceid);
			}

			if (FAIL == DBis_null(row[8]))
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

			int	exists = 0;

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
			interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht, &interface_ht_local);

			if (NULL != interface_ht && interface == interface_ht->interface_ptr)
			{
				/* see ZBX-4045 for NULL check in the conditional */
				zbx_hashset_remove(&config->interfaces_ht, &interface_ht_local);
			}
		}

		zbx_strpool_release(interface->ip);
		zbx_strpool_release(interface->dns);
		zbx_strpool_release(interface->port);

		zbx_hashset_remove_direct(&config->interfaces, interface);
	}

	zbx_vector_ptr_destroy(&interfaces);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_interface_snmpitems_remove                                    *
 *                                                                            *
 * Purpose: remove item from interfaceid -> itemid index                      *
 *                                                                            *
 * Parameters: interface - [IN] the item                                      *
 *                                                                            *
 ******************************************************************************/
static void	dc_interface_snmpitems_remove(ZBX_DC_ITEM *item)
{
	ZBX_DC_INTERFACE_ITEM	*ifitem;
	int			index;
	zbx_uint64_t		interfaceid;

	if (0 == (interfaceid = item->interfaceid))
		return;

	if (NULL == (ifitem = (ZBX_DC_INTERFACE_ITEM *)zbx_hashset_search(&config->interface_snmpitems, &interfaceid)))
		return;

	if (FAIL == (index = zbx_vector_uint64_search(&ifitem->itemids, item->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		return;

	zbx_vector_uint64_remove_noorder(&ifitem->itemids, index);

	if (0 == ifitem->itemids.values_num)
	{
		zbx_vector_uint64_destroy(&ifitem->itemids);
		zbx_hashset_remove_direct(&config->interface_snmpitems, ifitem);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_masteritem_remove_depitem                                     *
 *                                                                            *
 * Purpose: remove itemid from master item dependent itemid vector            *
 *                                                                            *
 * Parameters: master_itemid - [IN] the master item identifier                *
 *             dep_itemid    - [IN] the dependent item identifier             *
 *                                                                            *
 ******************************************************************************/
static void	dc_masteritem_remove_depitem(zbx_uint64_t master_itemid, zbx_uint64_t dep_itemid)
{
	ZBX_DC_MASTERITEM	*masteritem;
	int			index;
	zbx_uint64_pair_t	pair;

	if (NULL == (masteritem = (ZBX_DC_MASTERITEM *)zbx_hashset_search(&config->masteritems, &master_itemid)))
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
		zbx_vector_uint64_pair_destroy(&masteritem->dep_itemids);
		zbx_hashset_remove_direct(&config->masteritems, masteritem);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_host_update_agent_stats                                       *
 *                                                                            *
 * Purpose: update number of items per agent statistics                       *
 *                                                                            *
 * Parameters: host - [IN] the host                                           *
 *             type - [IN] the item type (ITEM_TYPE_*)                        *
 *             num  - [IN] the number of items (+) added, (-) removed         *
 *                                                                            *
 ******************************************************************************/
static void	dc_host_update_agent_stats(ZBX_DC_HOST *host, unsigned char type, int num)
{
	switch (type)
	{
		case ITEM_TYPE_ZABBIX:
			host->items_num += num;
			break;
		case ITEM_TYPE_SNMP:
			host->snmp_items_num += num;
			break;
		case ITEM_TYPE_IPMI:
			host->ipmi_items_num += num;
			break;
		case ITEM_TYPE_JMX:
			host->jmx_items_num += num;
	}
}

static void	DCsync_items(zbx_dbsync_t *sync, int flags)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;

	ZBX_DC_HOST		*host;

	ZBX_DC_ITEM		*item;
	ZBX_DC_NUMITEM		*numitem;
	ZBX_DC_SNMPITEM		*snmpitem;
	ZBX_DC_IPMIITEM		*ipmiitem;
	ZBX_DC_TRAPITEM		*trapitem;
	ZBX_DC_DEPENDENTITEM	*depitem;
	ZBX_DC_LOGITEM		*logitem;
	ZBX_DC_DBITEM		*dbitem;
	ZBX_DC_SSHITEM		*sshitem;
	ZBX_DC_TELNETITEM	*telnetitem;
	ZBX_DC_SIMPLEITEM	*simpleitem;
	ZBX_DC_JMXITEM		*jmxitem;
	ZBX_DC_CALCITEM		*calcitem;
	ZBX_DC_INTERFACE_ITEM	*interface_snmpitem;
	ZBX_DC_MASTERITEM	*master;
	ZBX_DC_PREPROCITEM	*preprocitem;
	ZBX_DC_HTTPITEM		*httpitem;
	ZBX_DC_SCRIPTITEM	*scriptitem;
	ZBX_DC_ITEM_HK		*item_hk, item_hk_local;

	time_t			now;
	unsigned char		status, type, value_type, old_poller_type;
	int			found, update_index, ret, i,  old_nextcheck;
	zbx_uint64_t		itemid, hostid;
	zbx_vector_ptr_t	dep_items;

	zbx_vector_ptr_create(&dep_items);

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

		/* LLD item prototype */
		ZBX_DBROW2UINT64(item->parent_itemid, row[49]);

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
				item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local);

				if (item == item_hk->item_ptr)
				{
					zbx_strpool_release(item_hk->key);
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
		ZBX_DBROW2UINT64(item->interfaceid, row[19]);

		if (SUCCEED != is_time_suffix(row[22], &item->history_sec, ZBX_LENGTH_UNLIMITED))
			item->history_sec = ZBX_HK_PERIOD_MAX;

		if (0 != item->history_sec && ZBX_HK_OPTION_ENABLED == config->config->hk.history_global)
			item->history_sec = config->config->hk.history;

		item->history = (0 != item->history_sec);

		ZBX_STR2UCHAR(item->inventory_link, row[24]);
		ZBX_DBROW2UINT64(item->valuemapid, row[25]);

		if (0 != (ZBX_FLAG_DISCOVERY_RULE & item->flags))
			value_type = ITEM_VALUE_TYPE_TEXT;
		else
			ZBX_STR2UCHAR(value_type, row[4]);

		if (SUCCEED == DCstrpool_replace(found, &item->key, row[5]))
			flags |= ZBX_ITEM_KEY_CHANGED;

		if (0 == found)
		{
			item->triggers = NULL;
			item->update_triggers = 0;
			item->nextcheck = 0;
			item->lastclock = 0;
			item->state = (unsigned char)atoi(row[12]);
			ZBX_STR2UINT64(item->lastlogsize, row[20]);
			item->mtime = atoi(row[21]);
			DCstrpool_replace(found, &item->error, row[27]);
			item->data_expected_from = now;
			item->location = ZBX_LOC_NOWHERE;
			item->poller_type = ZBX_NO_POLLER;
			item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
			item->schedulable = 1;
			
			
			if (ITEM_VALUE_TYPE_STR == value_type || ITEM_VALUE_TYPE_TEXT == value_type)
				DCstrpool_replace(found, (const char **)&item->lastvalue.str, "");
			else 
				item->lastvalue.ui64=0;
			
			item->prevvalue.ui64=0;
			item->lastclock=0;

		}
		else
		{
			if (item->type != type)
				flags |= ZBX_ITEM_TYPE_CHANGED;

			if (ITEM_STATUS_ACTIVE == status && ITEM_STATUS_ACTIVE != item->status)
				item->data_expected_from = now;

			if (ITEM_STATUS_ACTIVE == item->status)
				dc_host_update_agent_stats(host, item->type, -1);
		}

		if (ITEM_STATUS_ACTIVE == status)
			dc_host_update_agent_stats(host, type, 1);

		item->type = type;
		item->status = status;
		item->value_type = value_type;

		/* update items_hk index using new data, if not done already */

		if (1 == update_index)
		{
			item_hk_local.hostid = item->hostid;
			item_hk_local.key = zbx_strpool_acquire(item->key);
			item_hk_local.item_ptr = item;
			zbx_hashset_insert(&config->items_hk, &item_hk_local, sizeof(ZBX_DC_ITEM_HK));
		}

		/* process item intervals and update item nextcheck */

		if (SUCCEED == DCstrpool_replace(found, &item->delay, row[8]))
			flags |= ZBX_ITEM_DELAY_CHANGED;

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			int	trends_sec;

			numitem = (ZBX_DC_NUMITEM *)DCfind_id(&config->numitems, itemid, sizeof(ZBX_DC_NUMITEM), &found);

			if (SUCCEED != is_time_suffix(row[23], &trends_sec, ZBX_LENGTH_UNLIMITED))
				trends_sec = ZBX_HK_PERIOD_MAX;

			if (0 != trends_sec && ZBX_HK_OPTION_ENABLED == config->config->hk.trends_global)
				trends_sec = config->config->hk.trends;

			numitem->trends = (0 != trends_sec);
			numitem->trends_sec = trends_sec;

			DCstrpool_replace(found, &numitem->units, row[26]);
		}
		else if (NULL != (numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&config->numitems, &itemid)))
		{
			/* remove parameters for non-numeric item */

			zbx_strpool_release(numitem->units);

			zbx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (ITEM_TYPE_SNMP == item->type)
		{
			snmpitem = (ZBX_DC_SNMPITEM *)DCfind_id(&config->snmpitems, itemid, sizeof(ZBX_DC_SNMPITEM), &found);

			if (SUCCEED == DCstrpool_replace(found, &snmpitem->snmp_oid, row[6]))
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

			zbx_strpool_release(snmpitem->snmp_oid);
			zbx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (ZBX_DC_IPMIITEM *)DCfind_id(&config->ipmiitems, itemid, sizeof(ZBX_DC_IPMIITEM), &found);

			DCstrpool_replace(found, &ipmiitem->ipmi_sensor, row[7]);
		}
		else if (NULL != (ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems, &itemid)))
		{
			/* remove IPMI parameters for non-IPMI item */
			zbx_strpool_release(ipmiitem->ipmi_sensor);
			zbx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type && '\0' != *row[9])
		{
			trapitem = (ZBX_DC_TRAPITEM *)DCfind_id(&config->trapitems, itemid, sizeof(ZBX_DC_TRAPITEM), &found);
			DCstrpool_replace(found, &trapitem->trapper_hosts, row[9]);
		}
		else if (NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems, &itemid)))
		{
			/* remove trapper_hosts parameter */
			zbx_strpool_release(trapitem->trapper_hosts);
			zbx_hashset_remove_direct(&config->trapitems, trapitem);
		}

		/* dependent items */

		if (ITEM_TYPE_DEPENDENT == item->type && SUCCEED != DBis_null(row[29]))
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
		else if (NULL != (depitem = (ZBX_DC_DEPENDENTITEM *)zbx_hashset_search(&config->dependentitems, &itemid)))
		{
			dc_masteritem_remove_depitem(depitem->master_itemid, itemid);
			zbx_hashset_remove_direct(&config->dependentitems, depitem);
		}

		/* log items */

		if (ITEM_VALUE_TYPE_LOG == item->value_type && '\0' != *row[10])
		{
			logitem = (ZBX_DC_LOGITEM *)DCfind_id(&config->logitems, itemid, sizeof(ZBX_DC_LOGITEM), &found);

			DCstrpool_replace(found, &logitem->logtimefmt, row[10]);
		}
		else if (NULL != (logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&config->logitems, &itemid)))
		{
			/* remove logtimefmt parameter */
			zbx_strpool_release(logitem->logtimefmt);
			zbx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type && '\0' != *row[11])
		{
			dbitem = (ZBX_DC_DBITEM *)DCfind_id(&config->dbitems, itemid, sizeof(ZBX_DC_DBITEM), &found);

			DCstrpool_replace(found, &dbitem->params, row[11]);
			DCstrpool_replace(found, &dbitem->username, row[14]);
			DCstrpool_replace(found, &dbitem->password, row[15]);
		}
		else if (NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems, &itemid)))
		{
			/* remove db item parameters */
			zbx_strpool_release(dbitem->params);
			zbx_strpool_release(dbitem->username);
			zbx_strpool_release(dbitem->password);

			zbx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (ZBX_DC_SSHITEM *)DCfind_id(&config->sshitems, itemid, sizeof(ZBX_DC_SSHITEM), &found);

			sshitem->authtype = (unsigned short)atoi(row[13]);
			DCstrpool_replace(found, &sshitem->username, row[14]);
			DCstrpool_replace(found, &sshitem->password, row[15]);
			DCstrpool_replace(found, &sshitem->publickey, row[16]);
			DCstrpool_replace(found, &sshitem->privatekey, row[17]);
			DCstrpool_replace(found, &sshitem->params, row[11]);
		}
		else if (NULL != (sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems, &itemid)))
		{
			/* remove SSH item parameters */

			zbx_strpool_release(sshitem->username);
			zbx_strpool_release(sshitem->password);
			zbx_strpool_release(sshitem->publickey);
			zbx_strpool_release(sshitem->privatekey);
			zbx_strpool_release(sshitem->params);

			zbx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (ZBX_DC_TELNETITEM *)DCfind_id(&config->telnetitems, itemid, sizeof(ZBX_DC_TELNETITEM), &found);

			DCstrpool_replace(found, &telnetitem->username, row[14]);
			DCstrpool_replace(found, &telnetitem->password, row[15]);
			DCstrpool_replace(found, &telnetitem->params, row[11]);
		}
		else if (NULL != (telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems, &itemid)))
		{
			/* remove TELNET item parameters */

			zbx_strpool_release(telnetitem->username);
			zbx_strpool_release(telnetitem->password);
			zbx_strpool_release(telnetitem->params);

			zbx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (ZBX_DC_SIMPLEITEM *)DCfind_id(&config->simpleitems, itemid, sizeof(ZBX_DC_SIMPLEITEM), &found);

			DCstrpool_replace(found, &simpleitem->username, row[14]);
			DCstrpool_replace(found, &simpleitem->password, row[15]);
		}
		else if (NULL != (simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems, &itemid)))
		{
			/* remove simple item parameters */

			zbx_strpool_release(simpleitem->username);
			zbx_strpool_release(simpleitem->password);

			zbx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (ZBX_DC_JMXITEM *)DCfind_id(&config->jmxitems, itemid, sizeof(ZBX_DC_JMXITEM), &found);

			DCstrpool_replace(found, &jmxitem->username, row[14]);
			DCstrpool_replace(found, &jmxitem->password, row[15]);
			DCstrpool_replace(found, &jmxitem->jmx_endpoint, row[28]);
		}
		else if (NULL != (jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &itemid)))
		{
			/* remove JMX item parameters */

			zbx_strpool_release(jmxitem->username);
			zbx_strpool_release(jmxitem->password);
			zbx_strpool_release(jmxitem->jmx_endpoint);

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
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
			}

			zbx_vector_uint64_append(&interface_snmpitem->itemids, itemid);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (ZBX_DC_CALCITEM *)DCfind_id(&config->calcitems, itemid, sizeof(ZBX_DC_CALCITEM), &found);

			DCstrpool_replace(found, &calcitem->params, row[11]);
		}
		else if (NULL != (calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems, &itemid)))
		{
			/* remove calculated item parameters */

			zbx_strpool_release(calcitem->params);
			zbx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (ZBX_DC_HTTPITEM *)DCfind_id(&config->httpitems, itemid, sizeof(ZBX_DC_HTTPITEM),
					&found);

			DCstrpool_replace(found, &httpitem->timeout, row[30]);
			DCstrpool_replace(found, &httpitem->url, row[31]);
			DCstrpool_replace(found, &httpitem->query_fields, row[32]);
			DCstrpool_replace(found, &httpitem->posts, row[33]);
			DCstrpool_replace(found, &httpitem->status_codes, row[34]);
			httpitem->follow_redirects = (unsigned char)atoi(row[35]);
			httpitem->post_type = (unsigned char)atoi(row[36]);
			DCstrpool_replace(found, &httpitem->http_proxy, row[37]);
			DCstrpool_replace(found, &httpitem->headers, row[38]);
			httpitem->retrieve_mode = (unsigned char)atoi(row[39]);
			httpitem->request_method = (unsigned char)atoi(row[40]);
			httpitem->output_format = (unsigned char)atoi(row[41]);
			DCstrpool_replace(found, &httpitem->ssl_cert_file, row[42]);
			DCstrpool_replace(found, &httpitem->ssl_key_file, row[43]);
			DCstrpool_replace(found, &httpitem->ssl_key_password, row[44]);
			httpitem->verify_peer = (unsigned char)atoi(row[45]);
			httpitem->verify_host = (unsigned char)atoi(row[46]);
			httpitem->allow_traps = (unsigned char)atoi(row[47]);

			httpitem->authtype = (unsigned char)atoi(row[13]);
			DCstrpool_replace(found, &httpitem->username, row[14]);
			DCstrpool_replace(found, &httpitem->password, row[15]);
			DCstrpool_replace(found, &httpitem->trapper_hosts, row[9]);
		}
		else if (NULL != (httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems, &itemid)))
		{
			zbx_strpool_release(httpitem->timeout);
			zbx_strpool_release(httpitem->url);
			zbx_strpool_release(httpitem->query_fields);
			zbx_strpool_release(httpitem->posts);
			zbx_strpool_release(httpitem->status_codes);
			zbx_strpool_release(httpitem->http_proxy);
			zbx_strpool_release(httpitem->headers);
			zbx_strpool_release(httpitem->ssl_cert_file);
			zbx_strpool_release(httpitem->ssl_key_file);
			zbx_strpool_release(httpitem->ssl_key_password);
			zbx_strpool_release(httpitem->username);
			zbx_strpool_release(httpitem->password);
			zbx_strpool_release(httpitem->trapper_hosts);

			zbx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* Script items */

		if (ITEM_TYPE_SCRIPT == item->type)
		{
			scriptitem = (ZBX_DC_SCRIPTITEM *)DCfind_id(&config->scriptitems, itemid,
					sizeof(ZBX_DC_SCRIPTITEM), &found);

			DCstrpool_replace(found, &scriptitem->timeout, row[30]);
			DCstrpool_replace(found, &scriptitem->script, row[11]);

			if (0 == found)
			{
				zbx_vector_ptr_create_ext(&scriptitem->params, __config_mem_malloc_func,
						__config_mem_realloc_func, __config_mem_free_func);
			}
		}
		else if (NULL != (scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems, &itemid)))
		{
			zbx_strpool_release(scriptitem->timeout);
			zbx_strpool_release(scriptitem->script);

			zbx_vector_ptr_destroy(&scriptitem->params);
			zbx_hashset_remove_direct(&config->scriptitems, scriptitem);
		}

		/* it is crucial to update type specific (config->snmpitems, config->ipmiitems, etc.) hashsets before */
		/* attempting to requeue an item because type specific properties are used to arrange items in queues */

		old_poller_type = item->poller_type;
		old_nextcheck = item->nextcheck;

		if (ITEM_STATUS_ACTIVE == item->status && HOST_STATUS_MONITORED == host->status)
		{
			DCitem_poller_type_update(item, host, flags);

			if (SUCCEED == zbx_is_counted_in_item_queue(item->type, item->key))
			{
				char	*error = NULL;

				if (FAIL == DCitem_nextcheck_update(item, host, flags, now, &error))
				{
					zbx_timespec_t	ts = {now, 0};

					/* Usual way for an item to become not supported is to receive an error     */
					/* instead of value. Item state and error will be updated by history syncer */
					/* during history sync following a regular procedure with item update in    */
					/* database and config cache, logging etc. There is no need to set          */
					/* ITEM_STATE_NOTSUPPORTED here.                                            */

					if (0 == host->proxy_hostid)
					{
						dc_add_history(item->itemid, item->value_type, 0, NULL, &ts,
								ITEM_STATE_NOTSUPPORTED, error);
					}
					zbx_free(error);
				}
			}
		}
		else
		{
			item->nextcheck = 0;
			item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
			item->poller_type = ZBX_NO_POLLER;
		}

		DCupdate_item_queue(item, old_poller_type, old_nextcheck);
	}

	/* update dependent item vectors within master items */

	for (i = 0; i < dep_items.values_num; i++)
	{
		zbx_uint64_pair_t	pair;

		depitem = (ZBX_DC_DEPENDENTITEM *)dep_items.values[i];
		dc_masteritem_remove_depitem(depitem->last_master_itemid, depitem->itemid);
		pair.first = depitem->itemid;
		pair.second = depitem->flags;

		/* append item to dependent item vector of master item */
		if (NULL == (master = (ZBX_DC_MASTERITEM *)zbx_hashset_search(&config->masteritems, &depitem->master_itemid)))
		{
			ZBX_DC_MASTERITEM	master_local;

			master_local.itemid = depitem->master_itemid;
			master = (ZBX_DC_MASTERITEM *)zbx_hashset_insert(&config->masteritems, &master_local, sizeof(master_local));

			zbx_vector_uint64_pair_create_ext(&master->dep_itemids, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		zbx_vector_uint64_pair_append(&master->dep_itemids, pair);
	}

	zbx_vector_ptr_destroy(&dep_items);

	/* remove deleted items from buffer */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &rowid)))
			continue;

		if (ITEM_STATUS_ACTIVE == item->status &&
				NULL != (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &item->hostid)))
		{
			dc_host_update_agent_stats(host, item->type, -1);
		}

		itemid = item->itemid;

		if (ITEM_TYPE_SNMPTRAP == item->type)
			dc_interface_snmpitems_remove(item);

		/* numeric items */

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type || ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&config->numitems, &itemid);

			zbx_strpool_release(numitem->units);

			zbx_hashset_remove_direct(&config->numitems, numitem);
		}

		/* SNMP items */

		if (ITEM_TYPE_SNMP == item->type)
		{
			snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &itemid);
			zbx_strpool_release(snmpitem->snmp_oid);
			zbx_hashset_remove_direct(&config->snmpitems, snmpitem);
		}

		/* IPMI items */

		if (ITEM_TYPE_IPMI == item->type)
		{
			ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems, &itemid);
			zbx_strpool_release(ipmiitem->ipmi_sensor);
			zbx_hashset_remove_direct(&config->ipmiitems, ipmiitem);
		}

		/* trapper items */

		if (ITEM_TYPE_TRAPPER == item->type &&
				NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems, &itemid)))
		{
			zbx_strpool_release(trapitem->trapper_hosts);
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
			zbx_strpool_release(logitem->logtimefmt);
			zbx_hashset_remove_direct(&config->logitems, logitem);
		}

		/* db items */

		if (ITEM_TYPE_DB_MONITOR == item->type &&
				NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems, &itemid)))
		{
			zbx_strpool_release(dbitem->params);
			zbx_strpool_release(dbitem->username);
			zbx_strpool_release(dbitem->password);

			zbx_hashset_remove_direct(&config->dbitems, dbitem);
		}

		/* SSH items */

		if (ITEM_TYPE_SSH == item->type)
		{
			sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems, &itemid);

			zbx_strpool_release(sshitem->username);
			zbx_strpool_release(sshitem->password);
			zbx_strpool_release(sshitem->publickey);
			zbx_strpool_release(sshitem->privatekey);
			zbx_strpool_release(sshitem->params);

			zbx_hashset_remove_direct(&config->sshitems, sshitem);
		}

		/* TELNET items */

		if (ITEM_TYPE_TELNET == item->type)
		{
			telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems, &itemid);

			zbx_strpool_release(telnetitem->username);
			zbx_strpool_release(telnetitem->password);
			zbx_strpool_release(telnetitem->params);

			zbx_hashset_remove_direct(&config->telnetitems, telnetitem);
		}

		/* simple items */

		if (ITEM_TYPE_SIMPLE == item->type)
		{
			simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems, &itemid);

			zbx_strpool_release(simpleitem->username);
			zbx_strpool_release(simpleitem->password);

			zbx_hashset_remove_direct(&config->simpleitems, simpleitem);
		}

		/* JMX items */

		if (ITEM_TYPE_JMX == item->type)
		{
			jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &itemid);

			zbx_strpool_release(jmxitem->username);
			zbx_strpool_release(jmxitem->password);
			zbx_strpool_release(jmxitem->jmx_endpoint);

			zbx_hashset_remove_direct(&config->jmxitems, jmxitem);
		}

		/* calculated items */

		if (ITEM_TYPE_CALCULATED == item->type)
		{
			calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems, &itemid);
			zbx_strpool_release(calcitem->params);
			zbx_hashset_remove_direct(&config->calcitems, calcitem);
		}

		/* HTTP agent items */

		if (ITEM_TYPE_HTTPAGENT == item->type)
		{
			httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems, &itemid);

			zbx_strpool_release(httpitem->timeout);
			zbx_strpool_release(httpitem->url);
			zbx_strpool_release(httpitem->query_fields);
			zbx_strpool_release(httpitem->posts);
			zbx_strpool_release(httpitem->status_codes);
			zbx_strpool_release(httpitem->http_proxy);
			zbx_strpool_release(httpitem->headers);
			zbx_strpool_release(httpitem->ssl_cert_file);
			zbx_strpool_release(httpitem->ssl_key_file);
			zbx_strpool_release(httpitem->ssl_key_password);
			zbx_strpool_release(httpitem->username);
			zbx_strpool_release(httpitem->password);
			zbx_strpool_release(httpitem->trapper_hosts);

			zbx_hashset_remove_direct(&config->httpitems, httpitem);
		}

		/* Script items */

		if (ITEM_TYPE_SCRIPT == item->type)
		{
			scriptitem = (ZBX_DC_SCRIPTITEM *)zbx_hashset_search(&config->scriptitems, &itemid);

			zbx_strpool_release(scriptitem->timeout);
			zbx_strpool_release(scriptitem->script);

			zbx_vector_ptr_destroy(&scriptitem->params);
			zbx_hashset_remove_direct(&config->scriptitems, scriptitem);
		}

		/* items */

		item_hk_local.hostid = item->hostid;
		item_hk_local.key = item->key;
		item_hk = (ZBX_DC_ITEM_HK *)zbx_hashset_search(&config->items_hk, &item_hk_local);

		if (item == item_hk->item_ptr)
		{
			zbx_strpool_release(item_hk->key);
			zbx_hashset_remove_direct(&config->items_hk, item_hk);
		}
		if (ZBX_LOC_QUEUE == item->location)
			zbx_binary_heap_remove_direct(&config->queues[item->poller_type], item->itemid);
		
		zbx_strpool_release(item->key);
		zbx_strpool_release(item->error);
		zbx_strpool_release(item->delay);

		if (NULL != item->triggers)
			config->items.mem_free_func(item->triggers);

		if (NULL != (preprocitem = (ZBX_DC_PREPROCITEM *)zbx_hashset_search(&config->preprocitems, &item->itemid)))
		{
			zbx_vector_ptr_destroy(&preprocitem->preproc_ops);
			zbx_hashset_remove_direct(&config->preprocitems, preprocitem);
		}

		zbx_hashset_remove_direct(&config->items, item);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCsync_template_items(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid, itemid;
	unsigned char		tag;
	int			ret, found;
	ZBX_DC_TEMPLATE_ITEM 	*item;

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

static void	DCsync_prototype_items(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid, itemid;
	unsigned char		tag;
	int			ret, found;
	ZBX_DC_PROTOTYPE_ITEM 	*item;

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

static void	DCsync_triggers(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	ZBX_DC_TRIGGER	*trigger;

	int		found, ret;
	zbx_uint64_t	triggerid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(triggerid, row[0]);

		trigger = (ZBX_DC_TRIGGER *)DCfind_id(&config->triggers, triggerid, sizeof(ZBX_DC_TRIGGER), &found);

		/* store new information in trigger structure */

		DCstrpool_replace(found, &trigger->description, row[1]);
		DCstrpool_replace(found, &trigger->expression, row[2]);
		DCstrpool_replace(found, &trigger->recovery_expression, row[11]);
		DCstrpool_replace(found, &trigger->correlation_tag, row[13]);
		DCstrpool_replace(found, &trigger->opdata, row[14]);
		DCstrpool_replace(found, &trigger->event_name, row[15]);
		ZBX_STR2UCHAR(trigger->priority, row[4]);
		ZBX_STR2UCHAR(trigger->type, row[5]);
		ZBX_STR2UCHAR(trigger->status, row[9]);
		ZBX_STR2UCHAR(trigger->recovery_mode, row[10]);
		ZBX_STR2UCHAR(trigger->correlation_mode, row[12]);

		if (0 == found)
		{
			DCstrpool_replace(found, &trigger->error, row[3]);
			ZBX_STR2UCHAR(trigger->value, row[6]);
			ZBX_STR2UCHAR(trigger->state, row[7]);
			trigger->lastchange = atoi(row[8]);
			trigger->locked = 0;

			zbx_vector_ptr_create_ext(&trigger->tags, __config_mem_malloc_func, __config_mem_realloc_func,
					__config_mem_free_func);
			trigger->topoindex = 1;
		}
	}

	/* remove deleted triggers from buffer */
	if (SUCCEED == ret)
	{
		zbx_vector_uint64_t	functionids;
		int			i;
		ZBX_DC_ITEM		*item;
		ZBX_DC_FUNCTION		*function;

		zbx_vector_uint64_create(&functionids);

		for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
		{
			if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &rowid)))
				continue;

			/* force trigger list update for items used in removed trigger */

			get_functionids(&functionids, trigger->expression);

			if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == trigger->recovery_mode)
				get_functionids(&functionids, trigger->recovery_expression);

			for (i = 0; i < functionids.values_num; i++)
			{
				if (NULL == (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionids.values[i])))
					continue;

				if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
					continue;

				item->update_triggers = 1;
				if (NULL != item->triggers)
				{
					config->items.mem_free_func(item->triggers);
					item->triggers = NULL;
				}
			}
			zbx_vector_uint64_clear(&functionids);

			zbx_strpool_release(trigger->description);
			zbx_strpool_release(trigger->expression);
			zbx_strpool_release(trigger->recovery_expression);
			zbx_strpool_release(trigger->error);
			zbx_strpool_release(trigger->correlation_tag);
			zbx_strpool_release(trigger->opdata);
			zbx_strpool_release(trigger->event_name);

			zbx_vector_ptr_destroy(&trigger->tags);

			zbx_hashset_remove_direct(&config->triggers, trigger);
		}
		zbx_vector_uint64_destroy(&functionids);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	DCconfig_sort_triggers_topologically(void);

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_release                                       *
 *                                                                            *
 * Purpose: releases trigger dependency list, removing it if necessary        *
 *                                                                            *
 ******************************************************************************/
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

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_init                                          *
 *                                                                            *
 * Purpose: initializes trigger dependency list                               *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_deplist_init(ZBX_DC_TRIGGER_DEPLIST *trigdep, ZBX_DC_TRIGGER *trigger)
{
	trigdep->refcount = 1;
	trigdep->trigger = trigger;
	zbx_vector_ptr_create_ext(&trigdep->dependencies, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_deplist_reset                                         *
 *                                                                            *
 * Purpose: resets trigger dependency list to release memory allocated by     *
 *          dependencies vector                                               *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_deplist_reset(ZBX_DC_TRIGGER_DEPLIST *trigdep)
{
	zbx_vector_ptr_destroy(&trigdep->dependencies);
	zbx_vector_ptr_create_ext(&trigdep->dependencies, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);
}

static void	DCsync_trigdeps(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;

	ZBX_DC_TRIGGER_DEPLIST	*trigdep_down, *trigdep_up;

	int			found, index, ret;
	zbx_uint64_t		triggerid_down, triggerid_up;
	ZBX_DC_TRIGGER		*trigger_up, *trigger_down;

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

		trigdep_down = (ZBX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_down, sizeof(ZBX_DC_TRIGGER_DEPLIST), &found);
		if (0 == found)
			dc_trigger_deplist_init(trigdep_down, trigger_down);
		else
			trigdep_down->refcount++;

		trigdep_up = (ZBX_DC_TRIGGER_DEPLIST *)DCfind_id(&config->trigdeps, triggerid_up, sizeof(ZBX_DC_TRIGGER_DEPLIST), &found);
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

#define ZBX_TIMER_DELAY		30

static int	dc_function_calculate_trends_nextcheck(time_t from, const char *period_shift, zbx_time_unit_t base,
		time_t *nextcheck, char **error)
{
	time_t		next = from;
	struct tm	tm;

	localtime_r(&next, &tm);

	while (SUCCEED == zbx_trends_parse_nextcheck(next, period_shift, nextcheck, error))
	{
		if (*nextcheck > from)
			return SUCCEED;

		zbx_tm_add(&tm, 1, base);
		if (-1 == (next = mktime(&tm)))
		{
			*error = zbx_strdup(*error, zbx_strerror(errno));
			return FAIL;
		}
	}

	return FAIL;
}

static int	dc_function_calculate_nextcheck(const zbx_trigger_timer_t *timer, time_t from, zbx_uint64_t seed)
{
	if (ZBX_FUNCTION_TYPE_TIMER == timer->type)
	{
		int	nextcheck;

		nextcheck = ZBX_TIMER_DELAY * (int)(from / (time_t)ZBX_TIMER_DELAY) +
				(int)(seed % (zbx_uint64_t)ZBX_TIMER_DELAY);

		while (nextcheck <= from)
			nextcheck += ZBX_TIMER_DELAY;

		return nextcheck;
	}
	else if (ZBX_FUNCTION_TYPE_TRENDS == timer->type)
	{
		struct tm	tm;
		time_t		nextcheck;
		int		offsets[ZBX_TIME_UNIT_COUNT] = {0, SEC_PER_MIN * 10, SEC_PER_HOUR + SEC_PER_MIN * 10,
				SEC_PER_HOUR + SEC_PER_MIN * 10, SEC_PER_HOUR + SEC_PER_MIN * 10,
				SEC_PER_HOUR + SEC_PER_MIN * 10};
		int		periods[ZBX_TIME_UNIT_COUNT] = {0, SEC_PER_MIN * 10, SEC_PER_HOUR, SEC_PER_HOUR * 11,
					SEC_PER_DAY - SEC_PER_HOUR, SEC_PER_DAY - SEC_PER_HOUR};

		if (ZBX_TIME_UNIT_HOUR == timer->trend_base)
		{
			localtime_r(&from, &tm);
			zbx_tm_round_up(&tm, timer->trend_base);

			if (-1 == (nextcheck = mktime(&tm)))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot calculate trend function \"" ZBX_FS_UI64
						"\" schedule: %s", timer->objectid, zbx_strerror(errno));
				THIS_SHOULD_NEVER_HAPPEN;

				return 0;
			}
		}
		else
		{
			int	ret;
			char	*error = NULL, *period_shift;

			period_shift = zbx_function_get_param_dyn(timer->parameter, 2);

			ret = dc_function_calculate_trends_nextcheck(from, period_shift, timer->trend_base, &nextcheck,
					&error);

			zbx_free(period_shift);

			if (FAIL == ret)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot calculate trend function \"" ZBX_FS_UI64
						"\" schedule: %s", timer->objectid, error);
				zbx_free(error);

				return 0;
			}
		}

		return nextcheck + offsets[timer->trend_base] + seed % periods[timer->trend_base];
	}

	THIS_SHOULD_NEVER_HAPPEN;

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_timer_create                                          *
 *                                                                            *
 * Purpose: create trigger timer based on the specified function              *
 *                                                                            *
 * Return value:  Created timer or NULL in the case of error.                 *
 *                                                                            *
 ******************************************************************************/
static zbx_trigger_timer_t	*dc_trigger_timer_create(ZBX_DC_FUNCTION *function)
{
	zbx_trigger_timer_t	*timer;
	zbx_time_unit_t		trend_base;

	if (ZBX_FUNCTION_TYPE_TRENDS == function->type)
	{
		char		*error = NULL;

		if (FAIL == zbx_trends_parse_base(function->parameter, &trend_base, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot parse function " ZBX_FS_UI64 " period base: %s",
					function->functionid, error);
			zbx_free(error);
			return NULL;
		}
	}
	else
		trend_base = ZBX_TIME_UNIT_UNKNOWN;

	timer = (zbx_trigger_timer_t *)__config_mem_malloc_func(NULL, sizeof(zbx_trigger_timer_t));
	timer->type = function->type;
	timer->objectid = function->functionid;
	timer->triggerid = function->triggerid;
	timer->revision = function->revision;
	timer->trend_base = trend_base;
	timer->lock = 0;

	function->timer_revision = function->revision;

	if (ZBX_FUNCTION_TYPE_TRENDS == function->type)
		DCstrpool_replace(0, &timer->parameter, function->parameter);
	else
		timer->parameter = NULL;

	return timer;
}


/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_timer_free                                            *
 *                                                                            *
 * Purpose: free trigger timer                                                *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_timer_free(zbx_trigger_timer_t *timer)
{
	if (NULL != timer->parameter)
		zbx_strpool_release(timer->parameter);

	__config_mem_free_func(timer);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_schedule_trigger_timer                                        *
 *                                                                            *
 * Purpose: schedule trigger timer to be executed at the specified time       *
 *                                                                            *
 * Parameter: timer   - [IN] the timer to schedule                            *
 *            eval_ts - [IN] the history snapshot time, by default (NULL)     *
 *                           execution time will be used.                     *
 *            exec_ts - [IN] the tiemer execution time                        *
 *                                                                            *
 ******************************************************************************/
static void	dc_schedule_trigger_timer(zbx_trigger_timer_t *timer, const zbx_timespec_t *eval_ts,
		const zbx_timespec_t *exec_ts)
{
	zbx_binary_heap_elem_t	elem;

	if (NULL == eval_ts)
		timer->eval_ts = *exec_ts;
	else
		timer->eval_ts = *eval_ts;

	timer->exec_ts = *exec_ts;

	elem.key = 0;
	elem.data = (void *)timer;
	zbx_binary_heap_insert(&config->trigger_queue, &elem);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_schedule_new_trigger_timers                                   *
 *                                                                            *
 * Purpose: set timer schedule and evaluation times based on functions and    *
 *          old trend function queue                                          *
 *                                                                            *
 ******************************************************************************/
static void	dc_schedule_trigger_timers(zbx_hashset_t *trend_queue, int now)
{
	ZBX_DC_FUNCTION		*function;
	ZBX_DC_TRIGGER		*trigger;
	zbx_trigger_timer_t	*timer, *old;
	zbx_timespec_t		ts;
	zbx_hashset_iter_t	iter;

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

		if (TRIGGER_STATUS_ENABLED != trigger->status || TRIGGER_FUNCTIONAL_TRUE != trigger->functional)
			continue;

		if (NULL == (timer = dc_trigger_timer_create(function)))
			continue;

		if (NULL != trend_queue && NULL != (old = (zbx_trigger_timer_t *)zbx_hashset_search(trend_queue,
				&timer->objectid)) && old->eval_ts.sec < now + 10 * SEC_PER_MIN)
		{
			/* if the trigger was scheduled during next 10 minutes         */
			/* schedule its evaluation later to reduce server startup load */
			if (old->eval_ts.sec < now + 10 * SEC_PER_MIN)
				ts.sec = now + 10 * SEC_PER_MIN + timer->triggerid % (10 * SEC_PER_MIN);
			else
				ts.sec = old->eval_ts.sec;

			dc_schedule_trigger_timer(timer, &old->eval_ts, &ts);
		}
		else
		{
			if (0 == (ts.sec = dc_function_calculate_nextcheck(timer, now, timer->triggerid)))
			{
				dc_trigger_timer_free(timer);
				function->timer_revision = 0;
			}
			else
				dc_schedule_trigger_timer(timer, NULL, &ts);
		}
	}
}

static void	DCsync_functions(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	ZBX_DC_ITEM	*item;
	ZBX_DC_FUNCTION	*function;

	int		found, ret;
	zbx_uint64_t	itemid, functionid, triggerid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[0]);
		ZBX_STR2UINT64(functionid, row[1]);
		ZBX_STR2UINT64(triggerid, row[4]);

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		{
			/* Item could have been created after we have selected them in the             */
			/* previous queries. However, we shall avoid the check for functions being the */
			/* same as in the trigger expression, because that is somewhat expensive, not  */
			/* 100% (think functions keeping their functionid, but changing their function */
			/* or parameters), and even if there is an inconsistency, we can live with it. */

			continue;
		}

		/* process function information */

		function = (ZBX_DC_FUNCTION *)DCfind_id(&config->functions, functionid, sizeof(ZBX_DC_FUNCTION), &found);

		if (1 == found)
		{
			if (function->itemid != itemid)
			{
				ZBX_DC_ITEM	*item_last;

				if (NULL != (item_last = zbx_hashset_search(&config->items, &function->itemid)))
				{
					item_last->update_triggers = 1;
					if (NULL != item_last->triggers)
					{
						config->items.mem_free_func(item_last->triggers);
						item_last->triggers = NULL;
					}
				}
			}
		}
		else
			function->timer_revision = 0;

		function->triggerid = triggerid;
		function->itemid = itemid;
		DCstrpool_replace(found, &function->function, row[2]);
		DCstrpool_replace(found, &function->parameter, row[3]);

		function->type = zbx_get_function_type(function->function);
		function->revision = config->sync_start_ts;

		item->update_triggers = 1;
		if (NULL != item->triggers)
			item->triggers[0] = NULL;
	}

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &rowid)))
			continue;

		if (NULL != (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
		{
			item->update_triggers = 1;
			if (NULL != item->triggers)
			{
				config->items.mem_free_func(item->triggers);
				item->triggers = NULL;
			}
		}

		zbx_strpool_release(function->function);
		zbx_strpool_release(function->parameter);

		zbx_hashset_remove_direct(&config->functions, function);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_regexp_remove_expression                                      *
 *                                                                            *
 * Purpose: removes expression from regexp                                    *
 *                                                                            *
 ******************************************************************************/
static ZBX_DC_REGEXP	*dc_regexp_remove_expression(const char *regexp_name, zbx_uint64_t expressionid)
{
	ZBX_DC_REGEXP	*regexp, regexp_local;
	int		index;

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
 * Function: DCsync_expressions                                               *
 *                                                                            *
 * Purpose: Updates expressions configuration cache                           *
 *                                                                            *
 * Parameters: result - [IN] the result of expressions database select        *
 *                                                                            *
 ******************************************************************************/
static void	DCsync_expressions(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_hashset_iter_t	iter;
	ZBX_DC_EXPRESSION	*expression;
	ZBX_DC_REGEXP		*regexp, regexp_local;
	zbx_uint64_t		expressionid;
	int			found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(expressionid, row[1]);
		expression = (ZBX_DC_EXPRESSION *)DCfind_id(&config->expressions, expressionid, sizeof(ZBX_DC_EXPRESSION), &found);

		if (0 != found)
			dc_regexp_remove_expression(expression->regexp, expressionid);

		DCstrpool_replace(found, &expression->regexp, row[0]);
		DCstrpool_replace(found, &expression->expression, row[2]);
		ZBX_STR2UCHAR(expression->type, row[3]);
		ZBX_STR2UCHAR(expression->case_sensitive, row[5]);
		expression->delimiter = *row[4];

		regexp_local.name = row[0];

		if (NULL == (regexp = (ZBX_DC_REGEXP *)zbx_hashset_search(&config->regexps, &regexp_local)))
		{
			DCstrpool_replace(0, &regexp_local.name, row[0]);
			zbx_vector_uint64_create_ext(&regexp_local.expressionids,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

			regexp = (ZBX_DC_REGEXP *)zbx_hashset_insert(&config->regexps, &regexp_local, sizeof(ZBX_DC_REGEXP));
		}

		zbx_vector_uint64_append(&regexp->expressionids, expressionid);
	}

	/* remove regexps with no expressions related to it */
	zbx_hashset_iter_reset(&config->regexps, &iter);

	while (NULL != (regexp = (ZBX_DC_REGEXP *)zbx_hashset_iter_next(&iter)))
	{
		if (0 < regexp->expressionids.values_num)
			continue;

		zbx_strpool_release(regexp->name);
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
				zbx_strpool_release(regexp->name);
				zbx_vector_uint64_destroy(&regexp->expressionids);
				zbx_hashset_remove_direct(&config->regexps, regexp);
			}
		}

		zbx_strpool_release(expression->expression);
		zbx_strpool_release(expression->regexp);
		zbx_hashset_remove_direct(&config->expressions, expression);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_actions                                                   *
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
static void	DCsync_actions(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;
	zbx_uint64_t	actionid;
	zbx_dc_action_t	*action;
	int		found, ret;

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

		DCstrpool_replace(found, &action->formula, row[3]);

		if (0 == found)
		{
			if (EVENT_SOURCE_INTERNAL == action->eventsource)
				config->internal_actions++;

			zbx_vector_ptr_create_ext(&action->conditions, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);

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

		zbx_strpool_release(action->formula);
		zbx_vector_ptr_destroy(&action->conditions);

		zbx_hashset_remove_direct(&config->actions, action);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_action_ops                                                *
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
static void	DCsync_action_ops(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;
	zbx_uint64_t	actionid;
	zbx_dc_action_t	*action;

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
 * Function: dc_compare_action_conditions_by_type                             *
 *                                                                            *
 * Purpose: compare two action conditions by their type                       *
 *                                                                            *
 * Comments: This function is used to sort action conditions by type.         *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_action_conditions_by_type(const void *d1, const void *d2)
{
	zbx_dc_action_condition_t	*c1 = *(zbx_dc_action_condition_t **)d1;
	zbx_dc_action_condition_t	*c2 = *(zbx_dc_action_condition_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(c1->conditiontype, c2->conditiontype);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_action_conditions                                         *
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
static void	DCsync_action_conditions(zbx_dbsync_t *sync)
{
	char				**row;
	zbx_uint64_t			rowid;
	unsigned char			tag;
	zbx_uint64_t			actionid, conditionid;
	zbx_dc_action_t			*action;
	zbx_dc_action_condition_t	*condition;
	int				found, i, index, ret;
	zbx_vector_ptr_t		actions;

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

		condition = (zbx_dc_action_condition_t *)DCfind_id(&config->action_conditions, conditionid, sizeof(zbx_dc_action_condition_t),
				&found);

		ZBX_STR2UCHAR(condition->conditiontype, row[2]);
		ZBX_STR2UCHAR(condition->op, row[3]);

		DCstrpool_replace(found, &condition->value, row[4]);
		DCstrpool_replace(found, &condition->value2, row[5]);

		if (0 == found)
		{
			condition->actionid = actionid;
			zbx_vector_ptr_append(&action->conditions, condition);
		}

		if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			zbx_vector_ptr_append(&actions, action);
	}

	/* remove deleted conditions */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (zbx_dc_action_condition_t *)zbx_hashset_search(&config->action_conditions, &rowid)))
			continue;

		if (NULL != (action = (zbx_dc_action_t *)zbx_hashset_search(&config->actions, &condition->actionid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&action->conditions, condition,
					ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&action->conditions, index);

				if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
					zbx_vector_ptr_append(&actions, action);
			}
		}

		zbx_strpool_release(condition->value);
		zbx_strpool_release(condition->value2);

		zbx_hashset_remove_direct(&config->action_conditions, condition);
	}

	/* sort conditions by type */

	zbx_vector_ptr_sort(&actions, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&actions, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < actions.values_num; i++)
	{
		action = (zbx_dc_action_t *)actions.values[i];

		if (CONDITION_EVAL_TYPE_AND_OR == action->evaltype)
			zbx_vector_ptr_sort(&action->conditions, dc_compare_action_conditions_by_type);
	}

	zbx_vector_ptr_destroy(&actions);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_correlations                                              *
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
static void	DCsync_correlations(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		correlationid;
	zbx_dc_correlation_t	*correlation;
	int			found, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[0]);

		correlation = (zbx_dc_correlation_t *)DCfind_id(&config->correlations, correlationid, sizeof(zbx_dc_correlation_t), &found);

		if (0 == found)
		{
			zbx_vector_ptr_create_ext(&correlation->conditions, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);

			zbx_vector_ptr_create_ext(&correlation->operations, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		DCstrpool_replace(found, &correlation->name, row[1]);
		DCstrpool_replace(found, &correlation->formula, row[3]);

		ZBX_STR2UCHAR(correlation->evaltype, row[2]);
	}

	/* remove deleted correlations */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &rowid)))
			continue;

		zbx_strpool_release(correlation->name);
		zbx_strpool_release(correlation->formula);

		zbx_vector_ptr_destroy(&correlation->conditions);
		zbx_vector_ptr_destroy(&correlation->operations);

		zbx_hashset_remove_direct(&config->correlations, correlation);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_corr_condition_get_size                                       *
 *                                                                            *
 * Purpose: get the actual size of correlation condition data depending on    *
 *          its type                                                          *
 *                                                                            *
 * Parameters: type - [IN] the condition type                                 *
 *                                                                            *
 * Return value: the size                                                     *
 *                                                                            *
 ******************************************************************************/
static size_t	dc_corr_condition_get_size(unsigned char type)
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
 * Function: dc_corr_condition_init_data                                      *
 *                                                                            *
 * Purpose: initializes correlation condition data from database row          *
 *                                                                            *
 * Parameters: condition - [IN] the condition to initialize                   *
 *             found     - [IN] 0 - new condition, 1 - cached condition       *
 *             row       - [IN] the database row containing condition data    *
 *                                                                            *
 ******************************************************************************/
static void	dc_corr_condition_init_data(zbx_dc_corr_condition_t *condition, int found,  DB_ROW row)
{
	if (ZBX_CORR_CONDITION_OLD_EVENT_TAG == condition->type || ZBX_CORR_CONDITION_NEW_EVENT_TAG == condition->type)
	{
		DCstrpool_replace(found, &condition->data.tag.tag, row[0]);
		return;
	}

	row++;

	if (ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE == condition->type ||
			ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE == condition->type)
	{
		DCstrpool_replace(found, &condition->data.tag_value.tag, row[0]);
		DCstrpool_replace(found, &condition->data.tag_value.value, row[1]);
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
		DCstrpool_replace(found, &condition->data.tag_pair.oldtag, row[0]);
		DCstrpool_replace(found, &condition->data.tag_pair.newtag, row[1]);
		return;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: corr_condition_free_data                                         *
 *                                                                            *
 * Purpose: frees correlation condition data                                  *
 *                                                                            *
 * Parameters: condition - [IN] the condition                                 *
 *                                                                            *
 ******************************************************************************/
static void	corr_condition_free_data(zbx_dc_corr_condition_t *condition)
{
	switch (condition->type)
	{
		case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
			/* break; is not missing here */
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
			zbx_strpool_release(condition->data.tag.tag);
			break;
		case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
			zbx_strpool_release(condition->data.tag_pair.oldtag);
			zbx_strpool_release(condition->data.tag_pair.newtag);
			break;
		case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			/* break; is not missing here */
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			zbx_strpool_release(condition->data.tag_value.tag);
			zbx_strpool_release(condition->data.tag_value.value);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_corr_conditions_by_type                               *
 *                                                                            *
 * Purpose: compare two correlation conditions by their type                  *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_corr_conditions_by_type(const void *d1, const void *d2)
{
	zbx_dc_corr_condition_t	*c1 = *(zbx_dc_corr_condition_t **)d1;
	zbx_dc_corr_condition_t	*c2 = *(zbx_dc_corr_condition_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(c1->type, c2->type);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_corr_conditions                                           *
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
static void	DCsync_corr_conditions(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		conditionid, correlationid;
	zbx_dc_corr_condition_t	*condition;
	zbx_dc_correlation_t	*correlation;
	int			found, ret, i, index;
	unsigned char		type;
	size_t			condition_size;
	zbx_vector_ptr_t	correlations;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&correlations);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &correlationid)))
			continue;

		ZBX_STR2UINT64(conditionid, row[0]);
		ZBX_STR2UCHAR(type, row[2]);

		condition_size = dc_corr_condition_get_size(type);
		condition = (zbx_dc_corr_condition_t *)DCfind_id(&config->corr_conditions, conditionid, condition_size, &found);

		condition->correlationid = correlationid;
		condition->type = type;
		dc_corr_condition_init_data(condition, found, row + 3);

		if (0 == found)
			zbx_vector_ptr_append(&correlation->conditions, condition);

		/* sort the conditions later */
		if (CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
			zbx_vector_ptr_append(&correlations, correlation);
	}

	/* remove deleted correlation conditions */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (condition = (zbx_dc_corr_condition_t *)zbx_hashset_search(&config->corr_conditions, &rowid)))
			continue;

		/* remove condition from correlation->conditions vector */
		if (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &condition->correlationid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&correlation->conditions, condition,
					ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				/* sort the conditions later */
				if (CONDITION_EVAL_TYPE_AND_OR == correlation->evaltype)
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
 * Function: DCsync_corr_operations                                           *
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
static void	DCsync_corr_operations(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		operationid, correlationid;
	zbx_dc_corr_operation_t	*operation;
	zbx_dc_correlation_t	*correlation;
	int			found, ret, index;
	unsigned char		type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(correlationid, row[1]);

		if (NULL == (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &correlationid)))
			continue;

		ZBX_STR2UINT64(operationid, row[0]);
		ZBX_STR2UCHAR(type, row[2]);

		operation = (zbx_dc_corr_operation_t *)DCfind_id(&config->corr_operations, operationid, sizeof(zbx_dc_corr_operation_t), &found);

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
		if (NULL == (operation = (zbx_dc_corr_operation_t *)zbx_hashset_search(&config->corr_operations, &rowid)))
			continue;

		/* remove operation from correlation->conditions vector */
		if (NULL != (correlation = (zbx_dc_correlation_t *)zbx_hashset_search(&config->correlations, &operation->correlationid)))
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

static int	dc_compare_hgroups(const void *d1, const void *d2)
{
	const zbx_dc_hostgroup_t	*g1 = *((const zbx_dc_hostgroup_t **)d1);
	const zbx_dc_hostgroup_t	*g2 = *((const zbx_dc_hostgroup_t **)d2);

	return strcmp(g1->name, g2->name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_hostgroups                                                *
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
static void	DCsync_hostgroups(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		groupid;
	zbx_dc_hostgroup_t	*group;
	int			found, ret, index;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(groupid, row[0]);

		group = (zbx_dc_hostgroup_t *)DCfind_id(&config->hostgroups, groupid, sizeof(zbx_dc_hostgroup_t), &found);

		if (0 == found)
		{
			group->flags = ZBX_DC_HOSTGROUP_FLAGS_NONE;
			zbx_vector_ptr_append(&config->hostgroups_name, group);

			zbx_hashset_create_ext(&group->hostids, 0, ZBX_DEFAULT_UINT64_HASH_FUNC,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL, __config_mem_malloc_func,
					__config_mem_realloc_func, __config_mem_free_func);
		}

		DCstrpool_replace(found, &group->name, row[1]);
	}

	/* remove deleted host groups */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups, &rowid)))
			continue;

		if (FAIL != (index = zbx_vector_ptr_search(&config->hostgroups_name, group, ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			zbx_vector_ptr_remove_noorder(&config->hostgroups_name, index);

		if (ZBX_DC_HOSTGROUP_FLAGS_NONE != group->flags)
			zbx_vector_uint64_destroy(&group->nested_groupids);

		zbx_strpool_release(group->name);
		zbx_hashset_destroy(&group->hostids);
		zbx_hashset_remove_direct(&config->hostgroups, group);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_trigger_tags                                              *
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
static void	DCsync_trigger_tags(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	int			found, ret, index;
	zbx_uint64_t		triggerid, triggertagid;
	ZBX_DC_TRIGGER		*trigger;
	zbx_dc_trigger_tag_t	*trigger_tag;

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

		trigger_tag = (zbx_dc_trigger_tag_t *)DCfind_id(&config->trigger_tags, triggertagid, sizeof(zbx_dc_trigger_tag_t), &found);
		DCstrpool_replace(found, &trigger_tag->tag, row[2]);
		DCstrpool_replace(found, &trigger_tag->value, row[3]);

		if (0 == found)
		{
			trigger_tag->triggerid = triggerid;
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
			if (FAIL != (index = zbx_vector_ptr_search(&trigger->tags, trigger_tag,
					ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&trigger->tags, index);

				/* recreate empty tags vector to release used memory */
				if (0 == trigger->tags.values_num)
				{
					zbx_vector_ptr_destroy(&trigger->tags);
					zbx_vector_ptr_create_ext(&trigger->tags, __config_mem_malloc_func,
							__config_mem_realloc_func, __config_mem_free_func);
				}
			}
		}

		zbx_strpool_release(trigger_tag->tag);
		zbx_strpool_release(trigger_tag->value);

		zbx_hashset_remove_direct(&config->trigger_tags, trigger_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_host_tags                                                 *
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
static void	DCsync_host_tags(zbx_dbsync_t *sync)
{
	char		**row;
	zbx_uint64_t	rowid;
	unsigned char	tag;

	zbx_dc_host_tag_t		*host_tag;
	zbx_dc_host_tag_index_t		*host_tag_index_entry;

	int		found, index, ret;
	zbx_uint64_t	hosttagid, hostid;

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
		DCstrpool_replace(found, &host_tag->tag, row[2]);
		DCstrpool_replace(found, &host_tag->value, row[3]);

		/* update host_tags_index*/
		if (tag == ZBX_DBSYNC_ROW_ADD)
		{
			host_tag_index_entry = (zbx_dc_host_tag_index_t *)DCfind_id(&config->host_tags_index, hostid,
					sizeof(zbx_dc_host_tag_index_t), &found);

			if (0 == found)
			{
				zbx_vector_ptr_create_ext(&host_tag_index_entry->tags, __config_mem_malloc_func,
						__config_mem_realloc_func, __config_mem_free_func);
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
		zbx_strpool_release(host_tag->tag);
		zbx_strpool_release(host_tag->value);

		zbx_hashset_remove_direct(&config->host_tags, host_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_itemscript_param                                      *
 *                                                                            *
 * Purpose: compare two item script parameters                                *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_itemscript_param(const void *d1, const void *d2)
{
	zbx_dc_scriptitem_param_t	*p1 = *(zbx_dc_scriptitem_param_t **)d1;
	zbx_dc_scriptitem_param_t	*p2 = *(zbx_dc_scriptitem_param_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->name, p2->name);
	ZBX_RETURN_IF_NOT_EQUAL(p1->value, p2->value);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_item_preproc_by_step                                  *
 *                                                                            *
 * Purpose: compare two item preprocessing operations by step                 *
 *                                                                            *
 * Comments: This function is used to sort correlation conditions by type.    *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_preprocops_by_step(const void *d1, const void *d2)
{
	zbx_dc_preproc_op_t	*p1 = *(zbx_dc_preproc_op_t **)d1;
	zbx_dc_preproc_op_t	*p2 = *(zbx_dc_preproc_op_t **)d2;

	if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED == p1->type)
		return -1;

	if (ZBX_PREPROC_VALIDATE_NOT_SUPPORTED == p2->type)
		return 1;

	ZBX_RETURN_IF_NOT_EQUAL(p1->step, p2->step);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_item_preproc                                              *
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
static void	DCsync_item_preproc(zbx_dbsync_t *sync, int timestamp)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		item_preprocid, itemid;
	int			found, ret, i, index;
	ZBX_DC_PREPROCITEM	*preprocitem = NULL;
	zbx_dc_preproc_op_t	*op;
	zbx_vector_ptr_t	items;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&items);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);

		if (NULL == preprocitem || itemid != preprocitem->itemid)
		{
			if (NULL == (preprocitem = (ZBX_DC_PREPROCITEM *)zbx_hashset_search(&config->preprocitems, &itemid)))
			{
				ZBX_DC_PREPROCITEM	preprocitem_local;

				preprocitem_local.itemid = itemid;

				preprocitem = (ZBX_DC_PREPROCITEM *)zbx_hashset_insert(&config->preprocitems, &preprocitem_local,
						sizeof(preprocitem_local));

				zbx_vector_ptr_create_ext(&preprocitem->preproc_ops, __config_mem_malloc_func,
						__config_mem_realloc_func, __config_mem_free_func);
			}

			preprocitem->update_time = timestamp;
		}

		ZBX_STR2UINT64(item_preprocid, row[0]);

		op = (zbx_dc_preproc_op_t *)DCfind_id(&config->preprocops, item_preprocid, sizeof(zbx_dc_preproc_op_t), &found);

		ZBX_STR2UCHAR(op->type, row[2]);
		DCstrpool_replace(found, &op->params, row[3]);
		op->step = atoi(row[4]);
		op->error_handler = atoi(row[6]);
		DCstrpool_replace(found, &op->error_handler_params, row[7]);

		if (0 == found)
		{
			op->itemid = itemid;
			zbx_vector_ptr_append(&preprocitem->preproc_ops, op);
		}

		zbx_vector_ptr_append(&items, preprocitem);
	}

	/* remove deleted item preprocessing operations */

	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (op = (zbx_dc_preproc_op_t *)zbx_hashset_search(&config->preprocops, &rowid)))
			continue;

		if (NULL != (preprocitem = (ZBX_DC_PREPROCITEM *)zbx_hashset_search(&config->preprocitems, &op->itemid)))
		{
			if (FAIL != (index = zbx_vector_ptr_search(&preprocitem->preproc_ops, op,
					ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				zbx_vector_ptr_remove_noorder(&preprocitem->preproc_ops, index);
				zbx_vector_ptr_append(&items, preprocitem);
			}
		}

		zbx_strpool_release(op->params);
		zbx_strpool_release(op->error_handler_params);
		zbx_hashset_remove_direct(&config->preprocops, op);
	}

	/* sort item preprocessing operations by step */

	zbx_vector_ptr_sort(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < items.values_num; i++)
	{
		preprocitem = (ZBX_DC_PREPROCITEM *)items.values[i];

		if (0 == preprocitem->preproc_ops.values_num)
		{
			zbx_vector_ptr_destroy(&preprocitem->preproc_ops);
			zbx_hashset_remove_direct(&config->preprocitems, preprocitem);
		}
		else
			zbx_vector_ptr_sort(&preprocitem->preproc_ops, dc_compare_preprocops_by_step);
	}

	zbx_vector_ptr_destroy(&items);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_itemscript_param                                          *
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
static void	DCsync_itemscript_param(zbx_dbsync_t *sync)
{
	char				**row;
	zbx_uint64_t			rowid;
	unsigned char			tag;
	zbx_uint64_t			item_script_paramid, itemid;
	int				found, ret, i, index;
	ZBX_DC_SCRIPTITEM		*scriptitem;
	zbx_dc_scriptitem_param_t	*scriptitem_params;
	zbx_vector_ptr_t		items;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_ptr_create(&items);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		ZBX_STR2UINT64(itemid, row[1]);

		if (NULL == (scriptitem = (ZBX_DC_SCRIPTITEM *) zbx_hashset_search(&config->scriptitems, &itemid)))
		{
			zabbix_log(LOG_LEVEL_DEBUG,
					"cannot find parent item for item parameters (itemid=" ZBX_FS_UI64")", itemid);
			continue;
		}

		ZBX_STR2UINT64(item_script_paramid, row[0]);
		scriptitem_params = (zbx_dc_scriptitem_param_t *)DCfind_id(&config->itemscript_params,
				item_script_paramid, sizeof(zbx_dc_scriptitem_param_t), &found);

		DCstrpool_replace(found, &scriptitem_params->name, row[2]);
		DCstrpool_replace(found, &scriptitem_params->value, row[3]);

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

		zbx_strpool_release(scriptitem_params->name);
		zbx_strpool_release(scriptitem_params->value);
		zbx_hashset_remove_direct(&config->itemscript_params, scriptitem_params);
	}

	/* sort item script parameters */

	zbx_vector_ptr_sort(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&items, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < items.values_num; i++)
	{
		scriptitem = (ZBX_DC_SCRIPTITEM *)items.values[i];

		if (0 == scriptitem->params.values_num)
		{
			zbx_vector_ptr_destroy(&scriptitem->params);
			zbx_hashset_remove_direct(&config->scriptitems, scriptitem);
		}
		else
			zbx_vector_ptr_sort(&scriptitem->params, dc_compare_itemscript_param);
	}

	zbx_vector_ptr_destroy(&items);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_hostgroup_hosts                                           *
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
static void	DCsync_hostgroup_hosts(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;

	zbx_dc_hostgroup_t	*group = NULL;

	int			ret;
	zbx_uint64_t		last_groupid = 0, groupid, hostid;

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
 * Function: dc_trigger_update_topology                                       *
 *                                                                            *
 * Purpose: updates trigger topology after trigger dependency changes         *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_update_topology(void)
{
	zbx_hashset_iter_t	iter;
	ZBX_DC_TRIGGER		*trigger;

	zbx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
		trigger->topoindex = 1;

	DCconfig_sort_triggers_topologically();
}

static int	zbx_default_ptr_pair_ptr_compare_func(const void *d1, const void *d2)
{
	const zbx_ptr_pair_t	*p1 = (const zbx_ptr_pair_t *)d1;
	const zbx_ptr_pair_t	*p2 = (const zbx_ptr_pair_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->first, p2->first);
	ZBX_RETURN_IF_NOT_EQUAL(p1->second, p2->second);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_update_cache                                          *
 *                                                                            *
 * Purpose: updates trigger related cache data;                               *
 *              1) time triggers assigned to timer processes                  *
 *              2) trigger functionality (if it uses contain disabled         *
 *                 items/hosts)                                               *
 *              3) list of triggers each item is used by                      *
 *                                                                            *
 ******************************************************************************/
static void	dc_trigger_update_cache(void)
{
	zbx_hashset_iter_t	iter;
	ZBX_DC_TRIGGER		*trigger;
	ZBX_DC_FUNCTION		*function;
	ZBX_DC_ITEM		*item;
	int			i, j, k;
	zbx_ptr_pair_t		itemtrig;
	zbx_vector_ptr_pair_t	itemtrigs;
	ZBX_DC_HOST		*host;

	zbx_hashset_iter_reset(&config->triggers, &iter);
	while (NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
		trigger->functional = TRIGGER_FUNCTIONAL_TRUE;

	zbx_vector_ptr_pair_create(&itemtrigs);
	zbx_hashset_iter_reset(&config->functions, &iter);
	while (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_iter_next(&iter)))
	{

		if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)) ||
				NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &function->triggerid)))
		{
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
		item->triggers = (ZBX_DC_TRIGGER **)config->items.mem_realloc_func(item->triggers, (j - i + 1) * sizeof(ZBX_DC_TRIGGER *));

		for (k = i; k < j; k++)
			item->triggers[k - i] = (ZBX_DC_TRIGGER *)itemtrigs.values[k].second;

		item->triggers[j - i] = NULL;

		i = j - 1;
	}

	zbx_vector_ptr_pair_destroy(&itemtrigs);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_hostgroups_update_cache                                       *
 *                                                                            *
 * Purpose: updates hostgroup name index and resets nested group lists        *
 *                                                                            *
 ******************************************************************************/
static void	dc_hostgroups_update_cache(void)
{
	zbx_hashset_iter_t	iter;
	zbx_dc_hostgroup_t	*group;

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
 * Function: dc_load_trigger_queue                                            *
 *                                                                            *
 * Purpose: load trigger queue from database                                  *
 *                                                                            *
 * Comments: This function is called when syncing configuration cache for the *
 *           first time after server start. After loading trigger queue it    *
 *           will clear the corresponding data in database.                   *
 *                                                                            *
 ******************************************************************************/
static void	dc_load_trigger_queue(zbx_hashset_t *trend_functions)
{
	DB_RESULT	result;
	DB_ROW		row;

	result = DBselect("select objectid,type,clock,ns from trigger_queue");

	while (NULL != (row = DBfetch(result)))
	{
		zbx_trigger_timer_t	timer_local, *timer;

		if (ZBX_FUNCTION_TYPE_TRENDS != atoi(row[1]))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		ZBX_STR2UINT64(timer_local.objectid, row[0]);

		timer_local.eval_ts.sec = atoi(row[2]);
		timer_local.eval_ts.ns =  atoi(row[3]);
		timer = zbx_hashset_insert(trend_functions, &timer_local, sizeof(timer_local));

		/* in the case function was scheduled multiple times use the latest data */
		if (0 > zbx_timespec_compare(&timer->eval_ts, &timer_local.eval_ts))
			timer->eval_ts = timer_local.eval_ts;

	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_configuration                                             *
 *                                                                            *
 * Purpose: Synchronize configuration data from database                      *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCsync_configuration(unsigned char mode, const struct zbx_json_parse *jp_kvs_paths)
{
	int		i, flags;
	double		sec, csec, hsec, hisec, htsec, gmsec, hmsec, ifsec, isec, tsec, dsec, fsec, expr_sec, csec2,
			hsec2, hisec2, htsec2, gmsec2, hmsec2, ifsec2, isec2, tsec2, dsec2, fsec2, expr_sec2,
			action_sec, action_sec2, action_op_sec, action_op_sec2, action_condition_sec,
			action_condition_sec2, trigger_tag_sec, trigger_tag_sec2, host_tag_sec, host_tag_sec2,
			correlation_sec, correlation_sec2, corr_condition_sec, corr_condition_sec2, corr_operation_sec,
			corr_operation_sec2, hgroups_sec, hgroups_sec2, itempp_sec, itempp_sec2, itemscrp_sec,
			itemscrp_sec2, total, total2, update_sec, maintenance_sec, maintenance_sec2;

	zbx_dbsync_t	config_sync, hosts_sync, hi_sync, htmpl_sync, gmacro_sync, hmacro_sync, if_sync, items_sync,
			template_items_sync, prototype_items_sync, triggers_sync, tdep_sync, func_sync, expr_sync,
			action_sync, action_op_sync, action_condition_sync, trigger_tag_sync, host_tag_sync,
			correlation_sync, corr_condition_sync, corr_operation_sync, hgroups_sync, itempp_sync,
			itemscrp_sync, maintenance_sync, maintenance_period_sync, maintenance_tag_sync,
			maintenance_group_sync, maintenance_host_sync, hgroup_host_sync;

	double		autoreg_csec, autoreg_csec2;
	zbx_dbsync_t	autoreg_config_sync;
	zbx_uint64_t	update_flags = 0;
	int k;

	zbx_hashset_t		trend_queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	config->sync_start_ts = time(NULL);

	if (ZBX_SYNC_SECRETS == mode)
	{
		DCsync_kvs_paths(NULL);
		goto skip;
	}

	zbx_dbsync_init_env(config);

	if (ZBX_DBSYNC_INIT == mode)
	{
		zbx_hashset_create(&trend_queue, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		dc_load_trigger_queue(&trend_queue);
	}

	/* global configuration must be synchronized directly with database */
	zbx_dbsync_init(&config_sync, ZBX_DBSYNC_INIT);
	zbx_dbsync_init(&autoreg_config_sync, mode);
	zbx_dbsync_init(&hosts_sync, mode);
	zbx_dbsync_init(&hi_sync, mode);
	zbx_dbsync_init(&htmpl_sync, mode);
	zbx_dbsync_init(&gmacro_sync, mode);
	zbx_dbsync_init(&hmacro_sync, mode);
	zbx_dbsync_init(&if_sync, mode);
	zbx_dbsync_init(&items_sync, mode);
	zbx_dbsync_init(&template_items_sync, mode);
	zbx_dbsync_init(&prototype_items_sync, mode);
	zbx_dbsync_init(&triggers_sync, mode);
	zbx_dbsync_init(&tdep_sync, mode);
	zbx_dbsync_init(&func_sync, mode);
	zbx_dbsync_init(&expr_sync, mode);
	zbx_dbsync_init(&action_sync, mode);

	/* Action operation sync produces virtual rows with two columns - actionid, opflags. */
	/* Because of this it cannot return the original database select and must always be  */
	/* initialized in update mode.                                                       */
	zbx_dbsync_init(&action_op_sync, ZBX_DBSYNC_UPDATE);

	zbx_dbsync_init(&action_condition_sync, mode);
	zbx_dbsync_init(&trigger_tag_sync, mode);
	zbx_dbsync_init(&host_tag_sync, mode);
	zbx_dbsync_init(&correlation_sync, mode);
	zbx_dbsync_init(&corr_condition_sync, mode);
	zbx_dbsync_init(&corr_operation_sync, mode);
	zbx_dbsync_init(&hgroups_sync, mode);
	zbx_dbsync_init(&hgroup_host_sync, mode);
	zbx_dbsync_init(&itempp_sync, mode);
	zbx_dbsync_init(&itemscrp_sync, mode);

	zbx_dbsync_init(&maintenance_sync, mode);
	zbx_dbsync_init(&maintenance_period_sync, mode);
	zbx_dbsync_init(&maintenance_tag_sync, mode);
	zbx_dbsync_init(&maintenance_group_sync, mode);
	zbx_dbsync_init(&maintenance_host_sync, mode);

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_config(&config_sync))
		goto out;
	csec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_autoreg_psk(&autoreg_config_sync))
		goto out;
	autoreg_csec = zbx_time() - sec;

	/* sync global configuration settings */
	START_SYNC;
	sec = zbx_time();
	DCsync_config(&config_sync, &flags);
	csec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_autoreg_config(&autoreg_config_sync);	/* must be done in the same cache locking with config sync */
	autoreg_csec2 = zbx_time() - sec;
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

	START_SYNC;
	sec = zbx_time();
	DCsync_htmpls(&htmpl_sync);
	htsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_gmacros(&gmacro_sync);
	gmsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_hmacros(&hmacro_sync);
	hmsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_host_tags(&host_tag_sync);
	host_tag_sec2 = zbx_time() - sec;
	FINISH_SYNC;

	if (FAIL == DCsync_kvs_paths(jp_kvs_paths))
	{
		START_SYNC;
		goto out;
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

	START_SYNC;
	sec = zbx_time();
	DCsync_hosts(&hosts_sync);
	hsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_host_inventory(&hi_sync);
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

	FINISH_SYNC;

	/* sync item data to support item lookups when resolving macros during configuration sync */

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_interfaces(&if_sync))
		goto out;
	ifsec = zbx_time() - sec;

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_items(&items_sync))
		goto out;

	if (FAIL == zbx_dbsync_compare_template_items(&template_items_sync))
		goto out;

	if (FAIL == zbx_dbsync_compare_prototype_items(&prototype_items_sync))
		goto out;
	isec = zbx_time() - sec;

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
	DCsync_interfaces(&if_sync);
	ifsec2 = zbx_time() - sec;

	/* relies on hosts, proxies and interfaces, must be after DCsync_{hosts,interfaces}() */
	sec = zbx_time();
	DCsync_items(&items_sync, flags);
	DCsync_template_items(&template_items_sync);
	DCsync_prototype_items(&prototype_items_sync);
	isec2 = zbx_time() - sec;

	/* relies on items, must be after DCsync_items() */
	sec = zbx_time();
	DCsync_item_preproc(&itempp_sync, sec);
	itempp_sec2 = zbx_time() - sec;

	/* relies on items, must be after DCsync_items() */
	sec = zbx_time();
	DCsync_itemscript_param(&itemscrp_sync);
	itemscrp_sec2 = zbx_time() - sec;

	config->item_sync_ts = time(NULL);
	FINISH_SYNC;

	dc_flush_history();	/* misconfigured items generate pseudo-historic values to become notsupported */

	/* sync function data to support function lookups when resolving macros during configuration sync */

	sec = zbx_time();
	if (FAIL == zbx_dbsync_compare_functions(&func_sync))
		goto out;
	fsec = zbx_time() - sec;

	START_SYNC;
	sec = zbx_time();
	DCsync_functions(&func_sync);
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
	DCsync_triggers(&triggers_sync);
	tsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_trigdeps(&tdep_sync);
	dsec2 = zbx_time() - sec;

	sec = zbx_time();
	DCsync_expressions(&expr_sync);
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

	/* update trigger topology if trigger dependency was changed */
	if (0 != (update_flags & ZBX_DBSYNC_UPDATE_TRIGGER_DEPENDENCY))
		dc_trigger_update_topology();

	/* update various trigger related links in cache */
	if (0 != (update_flags & (ZBX_DBSYNC_UPDATE_HOSTS | ZBX_DBSYNC_UPDATE_ITEMS | ZBX_DBSYNC_UPDATE_FUNCTIONS |
			ZBX_DBSYNC_UPDATE_TRIGGERS)))
	{
		dc_trigger_update_cache();
		dc_schedule_trigger_timers((ZBX_DBSYNC_INIT == mode ? &trend_queue : NULL), time(NULL));
	}

	update_sec = zbx_time() - sec;

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		total = csec + hsec + hisec + htsec + gmsec + hmsec + ifsec + isec + tsec + dsec + fsec + expr_sec +
				action_sec + action_op_sec + action_condition_sec + trigger_tag_sec + correlation_sec +
				corr_condition_sec + corr_operation_sec + hgroups_sec + itempp_sec + maintenance_sec;
		total2 = csec2 + hsec2 + hisec2 + htsec2 + gmsec2 + hmsec2 + ifsec2 + isec2 + tsec2 + dsec2 + fsec2 +
				expr_sec2 + action_op_sec2 + action_sec2 + action_condition_sec2 + trigger_tag_sec2 +
				correlation_sec2 + corr_condition_sec2 + corr_operation_sec2 + hgroups_sec2 +
				itempp_sec2 + maintenance_sec2 + update_sec;

		zabbix_log(LOG_LEVEL_DEBUG, "%s() config     : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, csec, csec2, config_sync.add_num, config_sync.update_num,
				config_sync.remove_num);

		total += autoreg_csec;
		total2 += autoreg_csec2;
		zabbix_log(LOG_LEVEL_DEBUG, "%s() autoreg    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, autoreg_csec, autoreg_csec2, autoreg_config_sync.add_num,
				autoreg_config_sync.update_num, autoreg_config_sync.remove_num);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() hosts      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, hsec, hsec2, hosts_sync.add_num, hosts_sync.update_num,
				hosts_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host_invent: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, hisec, hisec2, hi_sync.add_num, hi_sync.update_num,
				hi_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() templates  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, htsec, htsec2, htmpl_sync.add_num, htmpl_sync.update_num,
				htmpl_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() globmacros : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, gmsec, gmsec2, gmacro_sync.add_num, gmacro_sync.update_num,
				gmacro_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hostmacros : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, hmsec, hmsec2, hmacro_sync.add_num, hmacro_sync.update_num,
				hmacro_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() interfaces : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, ifsec, ifsec2, if_sync.add_num, if_sync.update_num,
				if_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, isec, isec2, items_sync.add_num, items_sync.update_num,
				items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() template_items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, isec, isec2, template_items_sync.add_num,
				template_items_sync.update_num, template_items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() prototype_items      : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, isec, isec2, prototype_items_sync.add_num,
				prototype_items_sync.update_num, prototype_items_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() triggers   : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, tsec, tsec2, triggers_sync.add_num, triggers_sync.update_num,
				triggers_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trigdeps   : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, dsec, dsec2, tdep_sync.add_num, tdep_sync.update_num,
				tdep_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() trig. tags : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, trigger_tag_sec, trigger_tag_sec2, trigger_tag_sync.add_num,
				trigger_tag_sync.update_num, trigger_tag_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host tags : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, host_tag_sec, host_tag_sec2, host_tag_sync.add_num,
				host_tag_sync.update_num, host_tag_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() functions  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, fsec, fsec2, func_sync.add_num, func_sync.update_num,
				func_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() expressions: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, expr_sec, expr_sec2, expr_sync.add_num, expr_sync.update_num,
				expr_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() actions    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, action_sec, action_sec2, action_sync.add_num, action_sync.update_num,
				action_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() operations : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, action_op_sec, action_op_sec2, action_op_sync.add_num,
				action_op_sync.update_num, action_op_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() conditions : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, action_condition_sec, action_condition_sec2,
				action_condition_sync.add_num, action_condition_sync.update_num,
				action_condition_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr       : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, correlation_sec, correlation_sec2, correlation_sync.add_num,
				correlation_sync.update_num, correlation_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr_cond  : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, corr_condition_sec, corr_condition_sec2, corr_condition_sync.add_num,
				corr_condition_sync.update_num, corr_condition_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() corr_op    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, corr_operation_sec, corr_operation_sec2, corr_operation_sync.add_num,
				corr_operation_sync.update_num, corr_operation_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hgroups    : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, hgroups_sec, hgroups_sec2, hgroups_sync.add_num,
				hgroups_sync.update_num, hgroups_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item pproc : sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, itempp_sec, itempp_sec2, itempp_sync.add_num, itempp_sync.update_num,
				itempp_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() item script param: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, itemscrp_sec, itemscrp_sec2, itemscrp_sync.add_num,
				itemscrp_sync.update_num, itemscrp_sync.remove_num);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() maintenance: sql:" ZBX_FS_DBL " sync:" ZBX_FS_DBL " sec ("
				ZBX_FS_UI64 "/" ZBX_FS_UI64 "/" ZBX_FS_UI64 ").",
				__func__, maintenance_sec, maintenance_sec2, maintenance_sync.add_num,
				maintenance_sync.update_num, maintenance_sync.remove_num);

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
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zabbix_log(LOG_LEVEL_DEBUG, "%s() psks       : %d (%d slots)", __func__,
				config->psks.num_data, config->psks.num_slots);
#endif
		zabbix_log(LOG_LEVEL_DEBUG, "%s() ipmihosts  : %d (%d slots)", __func__,
				config->ipmihosts.num_data, config->ipmihosts.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() host_invent: %d (%d slots)", __func__,
				config->host_inventories.num_data, config->host_inventories.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() htmpls     : %d (%d slots)", __func__,
				config->htmpls.num_data, config->htmpls.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() gmacros    : %d (%d slots)", __func__,
				config->gmacros.num_data, config->gmacros.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() gmacros_m  : %d (%d slots)", __func__,
				config->gmacros_m.num_data, config->gmacros_m.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hmacros    : %d (%d slots)", __func__,
				config->hmacros.num_data, config->hmacros.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() hmacros_hm : %d (%d slots)", __func__,
				config->hmacros_hm.num_data, config->hmacros_hm.num_slots);
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
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items      : %d (%d slots)", __func__,
				config->items.num_data, config->items.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() items_hk   : %d (%d slots)", __func__,
				config->items_hk.num_data, config->items_hk.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() numitems   : %d (%d slots)", __func__,
				config->numitems.num_data, config->numitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() preprocitems: %d (%d slots)", __func__,
				config->preprocitems.num_data, config->preprocitems.num_slots);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() preprocops : %d (%d slots)", __func__,
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

		for (i = 0; ZBX_POLLER_TYPE_COUNT > i; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() queue[%d]   : %d (%d allocated)", __func__,
					i, config->queues[i].elems_num, config->queues[i].elems_alloc);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s() pqueue     : %d (%d allocated)", __func__,
				config->pqueue.elems_num, config->pqueue.elems_alloc);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() timer queue: %d (%d allocated)", __func__,
				config->trigger_queue.elems_num, config->trigger_queue.elems_alloc);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() configfree : " ZBX_FS_DBL "%%", __func__,
				100 * ((double)config_mem->free_size / config_mem->orig_size));

		zabbix_log(LOG_LEVEL_DEBUG, "%s() strings    : %d (%d slots)", __func__,
				config->strpool.num_data, config->strpool.num_slots);

		zbx_mem_dump_stats(LOG_LEVEL_DEBUG, config_mem);
	}
out:
	if (0 == sync_in_progress)
	{
		/* non recoverable database error is encountered */
		THIS_SHOULD_NEVER_HAPPEN;
		START_SYNC;
	}

	config->status->last_update = 0;
	config->sync_ts = time(NULL);

	FINISH_SYNC;

	zbx_dbsync_clear(&config_sync);
	zbx_dbsync_clear(&autoreg_config_sync);
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
	zbx_dbsync_clear(&maintenance_sync);
	zbx_dbsync_clear(&maintenance_period_sync);
	zbx_dbsync_clear(&maintenance_tag_sync);
	zbx_dbsync_clear(&maintenance_group_sync);
	zbx_dbsync_clear(&maintenance_host_sync);
	zbx_dbsync_clear(&hgroup_host_sync);

	if (ZBX_DBSYNC_INIT == mode)
		zbx_hashset_destroy(&trend_queue);

	zbx_dbsync_free_env();
skip:
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
		DCdump_configuration();
	
//	zbx_cluster_set_hosts_recalc();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Helper functions for configuration cache data structure element comparison *
 * and hash value calculation.                                                *
 *                                                                            *
 * The __config_mem_XXX_func(), __config_XXX_hash and __config_XXX_compare    *
 * functions are used only inside init_configuration_cache() function to      *
 * initialize internal data structures.                                       *
 *                                                                            *
 ******************************************************************************/

static zbx_hash_t	__config_ext_worker_hash(const void *data)
{
	const GLB_EXT_WORKER	*ext_worker = (const GLB_EXT_WORKER *)data;

	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_STRING_HASH_FUNC(ext_worker->path);

	return hash;
}

static int	__config_ext_worker_compare(const void *d1, const void *d2)
{
	const GLB_EXT_WORKER	*ext_worker_1 = (const GLB_EXT_WORKER *)d1;
	const GLB_EXT_WORKER	*ext_worker_2 = (const GLB_EXT_WORKER *)d2;

	return strcmp(ext_worker_1->path, ext_worker_2->path);
}

static zbx_hash_t	__config_item_hk_hash(const void *data)
{
	const ZBX_DC_ITEM_HK	*item_hk = (const ZBX_DC_ITEM_HK *)data;

	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&item_hk->hostid);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(item_hk->key, strlen(item_hk->key), hash);

	return hash;
}

static int	__config_item_hk_compare(const void *d1, const void *d2)
{
	const ZBX_DC_ITEM_HK	*item_hk_1 = (const ZBX_DC_ITEM_HK *)d1;
	const ZBX_DC_ITEM_HK	*item_hk_2 = (const ZBX_DC_ITEM_HK *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(item_hk_1->hostid, item_hk_2->hostid);

	return item_hk_1->key == item_hk_2->key ? 0 : strcmp(item_hk_1->key, item_hk_2->key);
}

static zbx_hash_t	__config_host_h_hash(const void *data)
{
	const ZBX_DC_HOST_H	*host_h = (const ZBX_DC_HOST_H *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(host_h->host, strlen(host_h->host), ZBX_DEFAULT_HASH_SEED);
}

static int	__config_host_h_compare(const void *d1, const void *d2)
{
	const ZBX_DC_HOST_H	*host_h_1 = (const ZBX_DC_HOST_H *)d1;
	const ZBX_DC_HOST_H	*host_h_2 = (const ZBX_DC_HOST_H *)d2;

	return host_h_1->host == host_h_2->host ? 0 : strcmp(host_h_1->host, host_h_2->host);
}

static zbx_hash_t	__config_gmacro_m_hash(const void *data)
{
	const ZBX_DC_GMACRO_M	*gmacro_m = (const ZBX_DC_GMACRO_M *)data;

	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_STRING_HASH_FUNC(gmacro_m->macro);

	return hash;
}

static int	__config_gmacro_m_compare(const void *d1, const void *d2)
{
	const ZBX_DC_GMACRO_M	*gmacro_m_1 = (const ZBX_DC_GMACRO_M *)d1;
	const ZBX_DC_GMACRO_M	*gmacro_m_2 = (const ZBX_DC_GMACRO_M *)d2;

	return gmacro_m_1->macro == gmacro_m_2->macro ? 0 : strcmp(gmacro_m_1->macro, gmacro_m_2->macro);
}

static zbx_hash_t	__config_hmacro_hm_hash(const void *data)
{
	const ZBX_DC_HMACRO_HM	*hmacro_hm = (const ZBX_DC_HMACRO_HM *)data;

	zbx_hash_t		hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&hmacro_hm->hostid);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(hmacro_hm->macro, strlen(hmacro_hm->macro), hash);

	return hash;
}

static int	__config_hmacro_hm_compare(const void *d1, const void *d2)
{
	const ZBX_DC_HMACRO_HM	*hmacro_hm_1 = (const ZBX_DC_HMACRO_HM *)d1;
	const ZBX_DC_HMACRO_HM	*hmacro_hm_2 = (const ZBX_DC_HMACRO_HM *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(hmacro_hm_1->hostid, hmacro_hm_2->hostid);

	return hmacro_hm_1->macro == hmacro_hm_2->macro ? 0 : strcmp(hmacro_hm_1->macro, hmacro_hm_2->macro);
}

static zbx_hash_t	__config_interface_ht_hash(const void *data)
{
	const ZBX_DC_INTERFACE_HT	*interface_ht = (const ZBX_DC_INTERFACE_HT *)data;

	zbx_hash_t			hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&interface_ht->hostid);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO((char *)&interface_ht->type, 1, hash);

	return hash;
}

static int	__config_interface_ht_compare(const void *d1, const void *d2)
{
	const ZBX_DC_INTERFACE_HT	*interface_ht_1 = (const ZBX_DC_INTERFACE_HT *)d1;
	const ZBX_DC_INTERFACE_HT	*interface_ht_2 = (const ZBX_DC_INTERFACE_HT *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(interface_ht_1->hostid, interface_ht_2->hostid);
	ZBX_RETURN_IF_NOT_EQUAL(interface_ht_1->type, interface_ht_2->type);

	return 0;
}

static zbx_hash_t	__config_interface_addr_hash(const void *data)
{
	const ZBX_DC_INTERFACE_ADDR	*interface_addr = (const ZBX_DC_INTERFACE_ADDR *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(interface_addr->addr, strlen(interface_addr->addr), ZBX_DEFAULT_HASH_SEED);
}

static int	__config_interface_addr_compare(const void *d1, const void *d2)
{
	const ZBX_DC_INTERFACE_ADDR	*interface_addr_1 = (const ZBX_DC_INTERFACE_ADDR *)d1;
	const ZBX_DC_INTERFACE_ADDR	*interface_addr_2 = (const ZBX_DC_INTERFACE_ADDR *)d2;

	return (interface_addr_1->addr == interface_addr_2->addr ? 0 : strcmp(interface_addr_1->addr, interface_addr_2->addr));
}

static int	__config_snmp_item_compare(const ZBX_DC_ITEM *i1, const ZBX_DC_ITEM *i2)
{
	const ZBX_DC_SNMPITEM	*s1;
	const ZBX_DC_SNMPITEM	*s2;

	unsigned char		f1;
	unsigned char		f2;

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

static int	__config_heap_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM		*i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM		*i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
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

static int	__config_pinger_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM		*i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM		*i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);
	ZBX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	return 0;
}

static int	__config_java_item_compare(const ZBX_DC_ITEM *i1, const ZBX_DC_ITEM *i2)
{
	const ZBX_DC_JMXITEM	*j1;
	const ZBX_DC_JMXITEM	*j2;

	ZBX_RETURN_IF_NOT_EQUAL(i1->interfaceid, i2->interfaceid);

	j1 = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &i1->itemid);
	j2 = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &i2->itemid);

	ZBX_RETURN_IF_NOT_EQUAL(j1->username, j2->username);
	ZBX_RETURN_IF_NOT_EQUAL(j1->password, j2->password);
	ZBX_RETURN_IF_NOT_EQUAL(j1->jmx_endpoint, j2->jmx_endpoint);

	return 0;
}

static int	__config_java_elem_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_ITEM		*i1 = (const ZBX_DC_ITEM *)e1->data;
	const ZBX_DC_ITEM		*i2 = (const ZBX_DC_ITEM *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(i1->nextcheck, i2->nextcheck);
	ZBX_RETURN_IF_NOT_EQUAL(i1->queue_priority, i2->queue_priority);

	return __config_java_item_compare(i1, i2);
}

static int	__config_proxy_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const ZBX_DC_PROXY		*p1 = (const ZBX_DC_PROXY *)e1->data;
	const ZBX_DC_PROXY		*p2 = (const ZBX_DC_PROXY *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(p1->nextcheck, p2->nextcheck);

	return 0;
}

/* hash and compare functions for expressions hashset */

static zbx_hash_t	__config_regexp_hash(const void *data)
{
	const ZBX_DC_REGEXP	*regexp = (const ZBX_DC_REGEXP *)data;

	return ZBX_DEFAULT_STRING_HASH_FUNC(regexp->name);
}

static int	__config_regexp_compare(const void *d1, const void *d2)
{
	const ZBX_DC_REGEXP	*r1 = (const ZBX_DC_REGEXP *)d1;
	const ZBX_DC_REGEXP	*r2 = (const ZBX_DC_REGEXP *)d2;

	return r1->name == r2->name ? 0 : strcmp(r1->name, r2->name);
}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
static zbx_hash_t	__config_psk_hash(const void *data)
{
	const ZBX_DC_PSK	*psk_i = (const ZBX_DC_PSK *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(psk_i->tls_psk_identity, strlen(psk_i->tls_psk_identity),
			ZBX_DEFAULT_HASH_SEED);
}

static int	__config_psk_compare(const void *d1, const void *d2)
{
	const ZBX_DC_PSK	*psk_1 = (const ZBX_DC_PSK *)d1;
	const ZBX_DC_PSK	*psk_2 = (const ZBX_DC_PSK *)d2;

	return psk_1->tls_psk_identity == psk_2->tls_psk_identity ? 0 : strcmp(psk_1->tls_psk_identity,
			psk_2->tls_psk_identity);
}
#endif

static int	__config_timer_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_trigger_timer_t	*t1 = (const zbx_trigger_timer_t *)e1->data;
	const zbx_trigger_timer_t	*t2 = (const zbx_trigger_timer_t *)e2->data;

	int	ret;

	if (0 != (ret = zbx_timespec_compare(&t1->exec_ts, &t2->exec_ts)))
		return ret;

	ZBX_RETURN_IF_NOT_EQUAL(t1->triggerid, t2->triggerid);

	if (0 != (ret = zbx_timespec_compare(&t1->eval_ts, &t2->eval_ts)))
		return ret;

	return 0;
}

static zbx_hash_t	__config_data_session_hash(const void *data)
{
	const zbx_data_session_t	*session = (const zbx_data_session_t *)data;
	zbx_hash_t			hash;

	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&session->hostid);
	return ZBX_DEFAULT_STRING_HASH_ALGO(session->token, strlen(session->token), hash);
}

static int	__config_data_session_compare(const void *d1, const void *d2)
{
	const zbx_data_session_t	*s1 = (const zbx_data_session_t *)d1;
	const zbx_data_session_t	*s2 = (const zbx_data_session_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(s1->hostid, s2->hostid);
	return strcmp(s1->token, s2->token);
}

/******************************************************************************
 *                                                                            *
 * Function: init_configuration_cache                                         *
 *                                                                            *
 * Purpose: Allocate shared memory for configuration cache                    *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	init_configuration_cache(char **error)
{
	int	i, k, ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() size:" ZBX_FS_UI64, __func__, CONFIG_CONF_CACHE_SIZE);

	if (SUCCEED != (ret = zbx_rwlock_create(&config_lock, ZBX_RWLOCK_CONFIG, error)))
		goto out;

	if (SUCCEED != (ret = zbx_mem_create(&config_mem, CONFIG_CONF_CACHE_SIZE, "configuration cache",
			"CacheSize", 0, error)))
	{
		goto out;
	}

	config = (ZBX_DC_CONFIG *)__config_mem_malloc_func(NULL, sizeof(ZBX_DC_CONFIG) +
			CONFIG_TIMER_FORKS * sizeof(zbx_vector_ptr_t));
	
#define CREATE_HASHSET(hashset, hashset_size)									\
														\
	CREATE_HASHSET_EXT(hashset, hashset_size, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC)

#define CREATE_HASHSET_EXT(hashset, hashset_size, hash_func, compare_func)					\
														\
	zbx_hashset_create_ext(&hashset, hashset_size, hash_func, compare_func, NULL,				\
			__config_mem_malloc_func, __config_mem_realloc_func, __config_mem_free_func)

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
	CREATE_HASHSET(config->masteritems, 0);
	CREATE_HASHSET(config->preprocitems, 0);
	CREATE_HASHSET(config->httpitems, 0);
	CREATE_HASHSET(config->scriptitems, 0);
	CREATE_HASHSET(config->itemscript_params, 0);
	CREATE_HASHSET(config->template_items, 0);
	CREATE_HASHSET(config->prototype_items, 0);
	CREATE_HASHSET(config->functions, 100);
	CREATE_HASHSET(config->triggers, 100);
	CREATE_HASHSET(config->trigdeps, 0);
	CREATE_HASHSET(config->hosts, 10);
	CREATE_HASHSET(config->proxies, 0);
	CREATE_HASHSET(config->host_inventories, 0);
	CREATE_HASHSET(config->host_inventories_auto, 0);
	CREATE_HASHSET(config->ipmihosts, 0);
	CREATE_HASHSET(config->htmpls, 0);
	CREATE_HASHSET(config->gmacros, 0);
	CREATE_HASHSET(config->hmacros, 0);
	CREATE_HASHSET(config->interfaces, 10);
	CREATE_HASHSET(config->interfaces_snmp, 0);
	CREATE_HASHSET(config->interface_snmpitems, 0);
	CREATE_HASHSET(config->expressions, 0);
	CREATE_HASHSET(config->actions, 0);
	CREATE_HASHSET(config->action_conditions, 0);
	CREATE_HASHSET(config->trigger_tags, 0);
	CREATE_HASHSET(config->host_tags, 0);
	CREATE_HASHSET(config->host_tags_index, 0);
	CREATE_HASHSET(config->correlations, 0);
	CREATE_HASHSET(config->corr_conditions, 0);
	CREATE_HASHSET(config->corr_operations, 0);
	CREATE_HASHSET(config->hostgroups, 0);
	//CREATE_HASHSET(config->ext_workers, 0);

	zbx_vector_ptr_create_ext(&config->hostgroups_name, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);
	zbx_vector_ptr_create_ext(&config->kvs_paths, __config_mem_malloc_func, __config_mem_realloc_func,
			__config_mem_free_func);

	CREATE_HASHSET(config->preprocops, 0);

	CREATE_HASHSET(config->maintenances, 0);
	CREATE_HASHSET(config->maintenance_periods, 0);
	CREATE_HASHSET(config->maintenance_tags, 0);

	CREATE_HASHSET_EXT(config->items_hk, 100, __config_item_hk_hash, __config_item_hk_compare);
	CREATE_HASHSET_EXT(config->hosts_h, 10, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->hosts_p, 0, __config_host_h_hash, __config_host_h_compare);
	CREATE_HASHSET_EXT(config->gmacros_m, 0, __config_gmacro_m_hash, __config_gmacro_m_compare);
	CREATE_HASHSET_EXT(config->hmacros_hm, 0, __config_hmacro_hm_hash, __config_hmacro_hm_compare);
	CREATE_HASHSET_EXT(config->interfaces_ht, 10, __config_interface_ht_hash, __config_interface_ht_compare);
	CREATE_HASHSET_EXT(config->interface_snmpaddrs, 0, __config_interface_addr_hash, __config_interface_addr_compare);
	CREATE_HASHSET_EXT(config->regexps, 0, __config_regexp_hash, __config_regexp_compare);

	CREATE_HASHSET_EXT(config->strpool, 100, __config_strpool_hash, __config_strpool_compare);

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	CREATE_HASHSET_EXT(config->psks, 0, __config_psk_hash, __config_psk_compare);
#endif

	for (i = 0; i < ZBX_POLLER_TYPE_COUNT; i++)
	{
		switch (i)
		{
			case ZBX_POLLER_TYPE_JAVA:
				zbx_binary_heap_create_ext(&config->queues[i],
						__config_java_elem_compare,
						ZBX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
			case ZBX_POLLER_TYPE_PINGER:
				zbx_binary_heap_create_ext(&config->queues[i],
						__config_pinger_elem_compare,
						ZBX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
			default:
				zbx_binary_heap_create_ext(&config->queues[i],
						__config_heap_elem_compare,
						ZBX_BINARY_HEAP_OPTION_DIRECT,
						__config_mem_malloc_func,
						__config_mem_realloc_func,
						__config_mem_free_func);
				break;
		}
	}

	zbx_binary_heap_create_ext(&config->pqueue,
					__config_proxy_compare,
					ZBX_BINARY_HEAP_OPTION_DIRECT,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

	zbx_binary_heap_create_ext(&config->trigger_queue,
					__config_timer_compare,
					ZBX_BINARY_HEAP_OPTION_EMPTY,
					__config_mem_malloc_func,
					__config_mem_realloc_func,
					__config_mem_free_func);

	CREATE_HASHSET_EXT(config->data_sessions, 0, __config_data_session_hash, __config_data_session_compare);

	config->config = NULL;

	config->status = (ZBX_DC_STATUS *)__config_mem_malloc_func(NULL, sizeof(ZBX_DC_STATUS));
	config->status->last_update = 0;

	config->availability_diff_ts = 0;
	config->sync_ts = 0;
	config->item_sync_ts = 0;
	config->sync_start_ts = 0;

	config->internal_actions = 0;

	/* maintenance data are used only when timers are defined (server) */
	if (0 != CONFIG_TIMER_FORKS)
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_FALSE;
		config->maintenance_update_flags = (zbx_uint64_t *)__config_mem_malloc_func(NULL, sizeof(zbx_uint64_t) *
				ZBX_MAINTENANCE_UPDATE_FLAGS_NUM());
		memset(config->maintenance_update_flags, 0, sizeof(zbx_uint64_t) * ZBX_MAINTENANCE_UPDATE_FLAGS_NUM());
	}

	config->proxy_lastaccess_ts = time(NULL);

	/* create data session token for proxies */
	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
	{
		char	*token;

		token = zbx_create_token(0);
		config->session_token = dc_strdup(token);
		zbx_free(token);
	}
	else
		config->session_token = NULL;
	
	//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: config zeroed");
	
	config->cluster_topology=NULL;
	config->cluster_topology_version=0;
	config->cluster_topology_last_update=time(NULL);
	config->cluster_topology_need_update=1;
	config->cluster_topology_download_host=0;
	
	//this will be set as soon as fisrt server objects will be loaded
	config->cluster_topology_need_update=0;
	config->no_preproc = 0;
	config->local_preproc = 0;

#undef CREATE_HASHSET
#undef CREATE_HASHSET_EXT
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: free_configuration_cache                                         *
 *                                                                            *
 * Purpose: Free memory allocated for configuration cache                     *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
void	free_configuration_cache(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	WRLOCK_CACHE;

	config = NULL;

	UNLOCK_CACHE;

	zbx_rwlock_destroy(&config_lock);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: in_maintenance_without_data_collection                           *
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
int	in_maintenance_without_data_collection(unsigned char maintenance_status, unsigned char maintenance_type,
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

static void	DCget_host(DC_HOST *dst_host, const ZBX_DC_HOST *src_host)
{
	const ZBX_DC_IPMIHOST		*ipmihost;
	const ZBX_DC_HOST_INVENTORY	*host_inventory;

	dst_host->hostid = src_host->hostid;
	dst_host->proxy_hostid = src_host->proxy_hostid;
	strscpy(dst_host->host, src_host->host);
	zbx_strlcpy_utf8(dst_host->name, src_host->name, sizeof(dst_host->name));
	dst_host->maintenance_status = src_host->maintenance_status;
	dst_host->maintenance_type = src_host->maintenance_type;
	dst_host->maintenance_from = src_host->maintenance_from;
	dst_host->errors_from = src_host->errors_from;
	dst_host->available = src_host->available;
	dst_host->disable_until = src_host->disable_until;
	dst_host->snmp_errors_from = src_host->snmp_errors_from;
	dst_host->snmp_available = src_host->snmp_available;
	dst_host->snmp_disable_until = src_host->snmp_disable_until;
	dst_host->ipmi_errors_from = src_host->ipmi_errors_from;
	dst_host->ipmi_available = src_host->ipmi_available;
	dst_host->ipmi_disable_until = src_host->ipmi_disable_until;
	dst_host->jmx_errors_from = src_host->jmx_errors_from;
	dst_host->jmx_available = src_host->jmx_available;
	dst_host->jmx_disable_until = src_host->jmx_disable_until;
	dst_host->status = src_host->status;
	dst_host->cluster_state = src_host->cluster_state;
	strscpy(dst_host->error, src_host->error);
	strscpy(dst_host->snmp_error, src_host->snmp_error);
	strscpy(dst_host->ipmi_error, src_host->ipmi_error);
	strscpy(dst_host->jmx_error, src_host->jmx_error);
	dst_host->tls_connect = src_host->tls_connect;
	dst_host->tls_accept = src_host->tls_accept;
	dst_host->cluster_server_host_id = src_host->cluster_server_host_id;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	strscpy(dst_host->tls_issuer, src_host->tls_issuer);
	strscpy(dst_host->tls_subject, src_host->tls_subject);

	if (NULL == src_host->tls_dc_psk)
	{
		*dst_host->tls_psk_identity = '\0';
		*dst_host->tls_psk = '\0';
	}
	else
	{
		strscpy(dst_host->tls_psk_identity, src_host->tls_dc_psk->tls_psk_identity);
		strscpy(dst_host->tls_psk, src_host->tls_dc_psk->tls_psk);
	}
#endif
	if (NULL != (ipmihost = (ZBX_DC_IPMIHOST *)zbx_hashset_search(&config->ipmihosts, &src_host->hostid)))
	{
		dst_host->ipmi_authtype = ipmihost->ipmi_authtype;
		dst_host->ipmi_privilege = ipmihost->ipmi_privilege;
		strscpy(dst_host->ipmi_username, ipmihost->ipmi_username);
		strscpy(dst_host->ipmi_password, ipmihost->ipmi_password);
	}
	else
	{
		dst_host->ipmi_authtype = ZBX_IPMI_DEFAULT_AUTHTYPE;
		dst_host->ipmi_privilege = ZBX_IPMI_DEFAULT_PRIVILEGE;
		*dst_host->ipmi_username = '\0';
		*dst_host->ipmi_password = '\0';
	}

	if (NULL != (host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories, &src_host->hostid)))
		dst_host->inventory_mode = (char)host_inventory->inventory_mode;
	else
		dst_host->inventory_mode = HOST_INVENTORY_DISABLED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_by_hostid                                             *
 *                                                                            *
 * Purpose: Locate host in configuration cache                                *
 *                                                                            *
 * Parameters: host - [OUT] pointer to DC_HOST structure                      *
 *             hostid - [IN] host ID from database                            *
 *                                                                            *
 * Return value: SUCCEED if record located and FAIL otherwise                 *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_by_hostid(DC_HOST *host, zbx_uint64_t hostid)
{
	int			ret = FAIL;
	const ZBX_DC_HOST	*dc_host;

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
 * Function: DCcheck_proxy_permissions                                        *
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
int	DCcheck_proxy_permissions(const char *host, const zbx_socket_t *sock, zbx_uint64_t *hostid, char **error)
{
	const ZBX_DC_HOST	*dc_host;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_conn_attr_t	attr;

	if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		if (SUCCEED != zbx_tls_get_attr_cert(sock, &attr))
		{
			*error = zbx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#if defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (SUCCEED != zbx_tls_get_attr_psk(sock, &attr))
		{
			*error = zbx_strdup(*error, "internal error: cannot get connection attributes");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
#endif
	else if (ZBX_TCP_SEC_UNENCRYPTED != sock->connection_type)
	{
		*error = zbx_strdup(*error, "internal error: invalid connection type");
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
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
	if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *dc_host->tls_issuer && 0 != strcmp(dc_host->tls_issuer, attr.issuer))
		{
			UNLOCK_CACHE;
			*error = zbx_dsprintf(*error, "proxy \"%s\" certificate issuer does not match", host);
			return FAIL;
		}

		/* simplified match, not compliant with RFC 4517, 4518 */
		if ('\0' != *dc_host->tls_subject && 0 != strcmp(dc_host->tls_subject, attr.subject))
		{
			UNLOCK_CACHE;
			*error = zbx_dsprintf(*error, "proxy \"%s\" certificate subject does not match", host);
			return FAIL;
		}
	}
#if defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		if (NULL != dc_host->tls_dc_psk)
		{
			if (strlen(dc_host->tls_dc_psk->tls_psk_identity) != attr.psk_identity_len ||
					0 != memcmp(dc_host->tls_dc_psk->tls_psk_identity, attr.psk_identity,
					attr.psk_identity_len))
			{
				UNLOCK_CACHE;
				*error = zbx_dsprintf(*error, "proxy \"%s\" is using false PSK identity", host);
				return FAIL;
			}
		}
		else
		{
			UNLOCK_CACHE;
			*error = zbx_dsprintf(*error, "active proxy \"%s\" is connecting with PSK but there is no PSK"
					" in the database for this proxy", host);
			return FAIL;
		}
	}
#endif
#endif
	*hostid = dc_host->hostid;

	UNLOCK_CACHE;

	return SUCCEED;
}

#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: DCget_psk_by_identity                                            *
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
size_t	DCget_psk_by_identity(const unsigned char *psk_identity, unsigned char *psk_buf, unsigned int *psk_usage)
{
	const ZBX_DC_PSK	*psk_i;
	ZBX_DC_PSK		psk_i_local;
	size_t			psk_len = 0;
	unsigned char		autoreg_psk_tmp[HOST_TLS_PSK_LEN_MAX];

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

	if (0 == *psk_usage)	/* only as autoregistration PSK */
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
			" different PSK values, autoregistration will not be allowed", psk_identity);
	return psk_len;
}
#endif
/******************************************************************************
 *                                                                            *
 * Function: dc_preproc_item_init                                             *
 *                                                                            *
 * Purpose: initialize new preprocessor item from configuration cache         *
 *                                                                            *
 * Parameters: item   - [OUT] the item to initialize                          *
 *             itemid - [IN] the item identifier                              *
 *                                                                            *
 * Return value: SUCCEED - the item was initialized successfully              *
 *               FAIL    - item with the specified itemid is not cached or    *
 *                         monitored                                          *
 *                                                                            *
 ******************************************************************************/
int	dc_preproc_item_init(zbx_preproc_item_t *item, zbx_uint64_t itemid)
{
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;

	if (NULL == (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		return FAIL;

	if (ITEM_STATUS_ACTIVE != dc_item->status)
		return FAIL;

	if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		return FAIL;

	if (HOST_STATUS_MONITORED != dc_host->status)
		return FAIL;

	item->itemid = itemid;
	item->type = dc_item->type;
	item->value_type = dc_item->value_type;

	item->dep_itemids = NULL;
	item->dep_itemids_num = 0;

	item->preproc_ops = NULL;
	item->preproc_ops_num = 0;
	item->update_time = 0;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_autoregistration_psk                                       *
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
void	DCget_autoregistration_psk(char *psk_identity_buf, size_t psk_identity_buf_len,
		unsigned char *psk_buf, size_t psk_buf_len)
{
	RDLOCK_CACHE;

	zbx_strlcpy((char *)psk_identity_buf, config->autoreg_psk_identity, psk_identity_buf_len);
	zbx_strlcpy((char *)psk_buf, config->autoreg_psk, psk_buf_len);

	UNLOCK_CACHE;
}

static void	DCget_interface(DC_INTERFACE *dst_interface, const ZBX_DC_INTERFACE *src_interface)
{
	if (NULL != src_interface)
	{
		dst_interface->interfaceid = src_interface->interfaceid;
		strscpy(dst_interface->ip_orig, src_interface->ip);
		strscpy(dst_interface->dns_orig, src_interface->dns);
		strscpy(dst_interface->port_orig, src_interface->port);
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
/*************************************************************************
 * fetches  item's preproc status
 * GLB_NO_PREPROC - no preprocessing needed, item might go to history cache just after polling
 * GLB_PREPROC_LOCAL - independant preprocessing - might be preprocessed in the poller's fork
 * GLB_PREPROC_MANAGER - must go through the preproc manager
 * ****************************************************************/
/*
static char DCget_preproc_type(DC_ITEM* dc_item) {
	
	const ZBX_DC_PREPROCITEM* preproc_item=NULL;
	const zbx_dc_preproc_op_t	*dc_op;
	zbx_preproc_op_t		*op;
	int i;

	//looking if this is a master item

	if (NULL !=  (preproc_item = (const ZBX_DC_PREPROCITEM *)zbx_hashset_search(&config->masteritems,&dc_item->itemid)))
		return dc_item->preproc_type=GLB_PREPROC_MANAGER;
	
	if ( NULL != (preproc_item = (const ZBX_DC_PREPROCITEM *)zbx_hashset_search(&config->preprocitems,&dc_item->itemid))) {
		//todo: figure out how to init preproc items
		zbx_preproc_item_t *item;

	if (NULL == (item=zbx_malloc(NULL, sizeof(zbx_preproc_item_t)))) 
			return dc_item->preproc_type=GLB_PREPROC_MANAGER;

	if (FAIL == dc_preproc_item_init(item, preproc_item->itemid)) 
			return  dc_item->preproc_type=GLB_PREPROC_MANAGER;

	//filling preprocessing steps to do the preproc job right on the poller
	dc_item->preprocitem=item;

	item->preproc_ops_num = preproc_item->preproc_ops.values_num;
	item->preproc_ops = (zbx_preproc_op_t *)zbx_malloc(NULL, sizeof(zbx_preproc_op_t) * item->preproc_ops_num);
	item->update_time = preproc_item->update_time;

	//todo - seek what will clean all this on DC_ITEM cleanup
	for (i = 0; i < preproc_item->preproc_ops.values_num; i++)
	{
		dc_op = (const zbx_dc_preproc_op_t *)preproc_item->preproc_ops.values[i];
		op = &item->preproc_ops[i];
		op->type = dc_op->type;
		op->params = zbx_strdup(NULL, dc_op->params);
		op->error_handler = dc_op->error_handler;
		op->error_handler_params = zbx_strdup(NULL, dc_op->error_handler_params);
	}
	
	return  dc_item->preproc_type=GLB_PREPROC_LOCAL;
}

	//
	return  dc_item->preproc_type=GLB_NO_PREPROC;
}
*/
static void	DCget_item(DC_ITEM *dst_item, const ZBX_DC_ITEM *src_item)
{
	const ZBX_DC_NUMITEM		*numitem;
	const ZBX_DC_LOGITEM		*logitem;
	const ZBX_DC_SNMPITEM		*snmpitem;
	const ZBX_DC_SNMPINTERFACE	*snmp;
	const ZBX_DC_TRAPITEM		*trapitem;
	const ZBX_DC_IPMIITEM		*ipmiitem;
	const ZBX_DC_DBITEM		*dbitem;
	const ZBX_DC_SSHITEM		*sshitem;
	const ZBX_DC_TELNETITEM		*telnetitem;
	const ZBX_DC_SIMPLEITEM		*simpleitem;
	const ZBX_DC_JMXITEM		*jmxitem;
	const ZBX_DC_CALCITEM		*calcitem;
	const ZBX_DC_INTERFACE		*dc_interface;
	const ZBX_DC_HTTPITEM		*httpitem;
	const ZBX_DC_SCRIPTITEM		*scriptitem;

	dst_item->itemid = src_item->itemid;
	dst_item->type = src_item->type;
	dst_item->value_type = src_item->value_type;
	strscpy(dst_item->key_orig, src_item->key);
	dst_item->key = NULL;
	dst_item->delay = zbx_strdup(NULL, src_item->delay);
	dst_item->nextcheck = src_item->nextcheck;
	dst_item->state = src_item->state;
	dst_item->lastclock = src_item->lastclock;
	dst_item->flags = src_item->flags;
	dst_item->lastlogsize = src_item->lastlogsize;
	dst_item->mtime = src_item->mtime;
	dst_item->history = src_item->history;
	dst_item->inventory_link = src_item->inventory_link;
	dst_item->valuemapid = src_item->valuemapid;
	dst_item->status = src_item->status;
	dst_item->history_sec = src_item->history_sec;

	dst_item->error = zbx_strdup(NULL, src_item->error);

	switch (src_item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
		case ITEM_VALUE_TYPE_UINT64:
			numitem = (ZBX_DC_NUMITEM *)zbx_hashset_search(&config->numitems, &src_item->itemid);

			dst_item->trends = numitem->trends;
			dst_item->trends_sec = numitem->trends_sec;
			dst_item->units = zbx_strdup(NULL, numitem->units);
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (NULL != (logitem = (ZBX_DC_LOGITEM *)zbx_hashset_search(&config->logitems, &src_item->itemid)))
				strscpy(dst_item->logtimefmt, logitem->logtimefmt);
			else
				*dst_item->logtimefmt = '\0';
			break;
	}

	switch (src_item->type)
	{
		case ITEM_TYPE_SNMP:
			snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems, &src_item->itemid);
			snmp = (ZBX_DC_SNMPINTERFACE *)zbx_hashset_search(&config->interfaces_snmp, &src_item->interfaceid);

			if (NULL != snmpitem && NULL != snmp)
			{
				strscpy(dst_item->snmp_community_orig, snmp->community);
				strscpy(dst_item->snmp_oid_orig, snmpitem->snmp_oid);
				strscpy(dst_item->snmpv3_securityname_orig, snmp->securityname);
				dst_item->snmpv3_securitylevel = snmp->securitylevel;
				strscpy(dst_item->snmpv3_authpassphrase_orig, snmp->authpassphrase);
				strscpy(dst_item->snmpv3_privpassphrase_orig, snmp->privpassphrase);
				dst_item->snmpv3_authprotocol = snmp->authprotocol;
				dst_item->snmpv3_privprotocol = snmp->privprotocol;
				strscpy(dst_item->snmpv3_contextname_orig, snmp->contextname);
				dst_item->snmp_version = snmp->version;
			}
			else
			{
				*dst_item->snmp_community_orig = '\0';
				*dst_item->snmp_oid_orig = '\0';
				*dst_item->snmpv3_securityname_orig = '\0';
				dst_item->snmpv3_securitylevel = ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV;
				*dst_item->snmpv3_authpassphrase_orig = '\0';
				*dst_item->snmpv3_privpassphrase_orig = '\0';
				dst_item->snmpv3_authprotocol = 0;
				dst_item->snmpv3_privprotocol = 0;
				*dst_item->snmpv3_contextname_orig = '\0';
				dst_item->snmp_version = ZBX_IF_SNMP_VERSION_2;
			}

			dst_item->snmp_community = NULL;
			dst_item->snmp_oid = NULL;
			dst_item->snmpv3_securityname = NULL;
			dst_item->snmpv3_authpassphrase = NULL;
			dst_item->snmpv3_privpassphrase = NULL;
			dst_item->snmpv3_contextname = NULL;
			break;
		case ITEM_TYPE_TRAPPER:
			if (NULL != (trapitem = (ZBX_DC_TRAPITEM *)zbx_hashset_search(&config->trapitems, &src_item->itemid)))
				strscpy(dst_item->trapper_hosts, trapitem->trapper_hosts);
			else
				*dst_item->trapper_hosts = '\0';
			break;
		case ITEM_TYPE_IPMI:
			if (NULL != (ipmiitem = (ZBX_DC_IPMIITEM *)zbx_hashset_search(&config->ipmiitems, &src_item->itemid)))
				strscpy(dst_item->ipmi_sensor, ipmiitem->ipmi_sensor);
			else
				*dst_item->ipmi_sensor = '\0';
			break;
		case ITEM_TYPE_DB_MONITOR:
			if (NULL != (dbitem = (ZBX_DC_DBITEM *)zbx_hashset_search(&config->dbitems, &src_item->itemid)))
			{
				dst_item->params = zbx_strdup(NULL, dbitem->params);
				strscpy(dst_item->username_orig, dbitem->username);
				strscpy(dst_item->password_orig, dbitem->password);
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
			if (NULL != (sshitem = (ZBX_DC_SSHITEM *)zbx_hashset_search(&config->sshitems, &src_item->itemid)))
			{
				dst_item->authtype = sshitem->authtype;
				strscpy(dst_item->username_orig, sshitem->username);
				strscpy(dst_item->publickey_orig, sshitem->publickey);
				strscpy(dst_item->privatekey_orig, sshitem->privatekey);
				strscpy(dst_item->password_orig, sshitem->password);
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
			if (NULL != (httpitem = (ZBX_DC_HTTPITEM *)zbx_hashset_search(&config->httpitems, &src_item->itemid)))
			{
				strscpy(dst_item->timeout_orig, httpitem->timeout);
				strscpy(dst_item->url_orig, httpitem->url);
				strscpy(dst_item->query_fields_orig, httpitem->query_fields);
				strscpy(dst_item->status_codes_orig, httpitem->status_codes);
				dst_item->follow_redirects = httpitem->follow_redirects;
				dst_item->post_type = httpitem->post_type;
				strscpy(dst_item->http_proxy_orig, httpitem->http_proxy);
				dst_item->headers = zbx_strdup(NULL, httpitem->headers);
				dst_item->retrieve_mode = httpitem->retrieve_mode;
				dst_item->request_method = httpitem->request_method;
				dst_item->output_format = httpitem->output_format;
				strscpy(dst_item->ssl_cert_file_orig, httpitem->ssl_cert_file);
				strscpy(dst_item->ssl_key_file_orig, httpitem->ssl_key_file);
				strscpy(dst_item->ssl_key_password_orig, httpitem->ssl_key_password);
				dst_item->verify_peer = httpitem->verify_peer;
				dst_item->verify_host = httpitem->verify_host;
				dst_item->authtype = httpitem->authtype;
				strscpy(dst_item->username_orig, httpitem->username);
				strscpy(dst_item->password_orig, httpitem->password);
				dst_item->posts = zbx_strdup(NULL, httpitem->posts);
				dst_item->allow_traps = httpitem->allow_traps;
				strscpy(dst_item->trapper_hosts, httpitem->trapper_hosts);
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
				int		i;
				struct zbx_json	json;

				zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

				strscpy(dst_item->timeout_orig, scriptitem->timeout);
				dst_item->params = zbx_strdup(NULL, scriptitem->script);

				for (i = 0; i < scriptitem->params.values_num; i++)
				{
					zbx_dc_scriptitem_param_t	*params =
							(zbx_dc_scriptitem_param_t*)(scriptitem->params.values[i]);

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
			if (NULL != (telnetitem = (ZBX_DC_TELNETITEM *)zbx_hashset_search(&config->telnetitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, telnetitem->username);
				strscpy(dst_item->password_orig, telnetitem->password);
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
			if (NULL != (simpleitem = (ZBX_DC_SIMPLEITEM *)zbx_hashset_search(&config->simpleitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, simpleitem->username);
				strscpy(dst_item->password_orig, simpleitem->password);
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
			if (NULL != (jmxitem = (ZBX_DC_JMXITEM *)zbx_hashset_search(&config->jmxitems, &src_item->itemid)))
			{
				strscpy(dst_item->username_orig, jmxitem->username);
				strscpy(dst_item->password_orig, jmxitem->password);
				strscpy(dst_item->jmx_endpoint_orig, jmxitem->jmx_endpoint);
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
			calcitem = (ZBX_DC_CALCITEM *)zbx_hashset_search(&config->calcitems, &src_item->itemid);
			dst_item->params = zbx_strdup(NULL, NULL != calcitem ? calcitem->params : "");
			break;
		default:
			/* nothing to do */;
	}

	dc_interface = (ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &src_item->interfaceid);

	DCget_interface(&dst_item->interface, dc_interface);
	
	//dst_item->preprocitem = NULL;
	//dst_item->preproc_type = GLB_PREPROC_MANAGER;
}

void	DCconfig_clean_items(DC_ITEM *items, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (NULL != errcodes && SUCCEED != errcodes[i])
			continue;

		if (ITEM_VALUE_TYPE_FLOAT == items[i].value_type || ITEM_VALUE_TYPE_UINT64 == items[i].value_type)
		{
			zbx_free(items[i].units);
		}

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
			case ITEM_TYPE_CALCULATED:
				zbx_free(items[i].params);
				break;
		}

		zbx_free(items[i].delay);
		zbx_free(items[i].error);
	}
}

static void	DCget_function(DC_FUNCTION *dst_function, const ZBX_DC_FUNCTION *src_function)
{
	size_t	sz_function, sz_parameter;

	dst_function->functionid = src_function->functionid;
	dst_function->triggerid = src_function->triggerid;
	dst_function->itemid = src_function->itemid;

	sz_function = strlen(src_function->function) + 1;
	sz_parameter = strlen(src_function->parameter) + 1;
	dst_function->function = (char *)zbx_malloc(NULL, sz_function + sz_parameter);
	dst_function->parameter = dst_function->function + sz_function;
	memcpy(dst_function->function, src_function->function, sz_function);
	memcpy(dst_function->parameter, src_function->parameter, sz_parameter);
}

static void	DCget_trigger(DC_TRIGGER *dst_trigger, const ZBX_DC_TRIGGER *src_trigger)
{
	int	i;

	dst_trigger->triggerid = src_trigger->triggerid;
	dst_trigger->description = zbx_strdup(NULL, src_trigger->description);
	dst_trigger->expression_orig = zbx_strdup(NULL, src_trigger->expression);
	dst_trigger->recovery_expression_orig = zbx_strdup(NULL, src_trigger->recovery_expression);
	dst_trigger->error = zbx_strdup(NULL, src_trigger->error);
	dst_trigger->timespec.sec = 0;
	dst_trigger->timespec.ns = 0;
	dst_trigger->priority = src_trigger->priority;
	dst_trigger->type = src_trigger->type;
	dst_trigger->value = src_trigger->value;
	dst_trigger->state = src_trigger->state;
	dst_trigger->new_value = TRIGGER_VALUE_UNKNOWN;
	dst_trigger->lastchange = src_trigger->lastchange;
	dst_trigger->topoindex = src_trigger->topoindex;
	dst_trigger->status = src_trigger->status;
	dst_trigger->recovery_mode = src_trigger->recovery_mode;
	dst_trigger->correlation_mode = src_trigger->correlation_mode;
	dst_trigger->correlation_tag = zbx_strdup(NULL, src_trigger->correlation_tag);
	dst_trigger->opdata = zbx_strdup(NULL, src_trigger->opdata);
	dst_trigger->event_name = ('\0' != *src_trigger->event_name ? zbx_strdup(NULL, src_trigger->event_name) : NULL);
	dst_trigger->flags = 0;

	dst_trigger->expression = NULL;
	dst_trigger->recovery_expression = NULL;
	dst_trigger->new_error = NULL;

	dst_trigger->expression = zbx_strdup(NULL, src_trigger->expression);
	dst_trigger->recovery_expression = zbx_strdup(NULL, src_trigger->recovery_expression);

	zbx_vector_ptr_create(&dst_trigger->tags);

	if (0 != src_trigger->tags.values_num)
	{
		zbx_vector_ptr_reserve(&dst_trigger->tags, src_trigger->tags.values_num);

		for (i = 0; i < src_trigger->tags.values_num; i++)
		{
			const zbx_dc_trigger_tag_t	*dc_trigger_tag = (const zbx_dc_trigger_tag_t *)src_trigger->tags.values[i];
			zbx_tag_t			*tag;

			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
			tag->tag = zbx_strdup(NULL, dc_trigger_tag->tag);
			tag->value = zbx_strdup(NULL, dc_trigger_tag->value);

			zbx_vector_ptr_append(&dst_trigger->tags, tag);
		}
	}
	//dst_item->preprocitem = NULL;
	//dst_item->preproc_type = GLB_PREPROC_MANAGER;
}

void	zbx_free_tag(zbx_tag_t *tag)
{
	zbx_free(tag->tag);
	zbx_free(tag->value);
	zbx_free(tag);
}

void	zbx_free_item_tag(zbx_item_tag_t *item_tag)
{
	zbx_free(item_tag->tag.tag);
	zbx_free(item_tag->tag.value);
	zbx_free(item_tag);
}

static void	DCclean_trigger(DC_TRIGGER *trigger)
{
	zbx_free(trigger->new_error);
	zbx_free(trigger->error);
	zbx_free(trigger->expression_orig);
	zbx_free(trigger->recovery_expression_orig);
	zbx_free(trigger->expression);
	zbx_free(trigger->recovery_expression);
	zbx_free(trigger->description);
	zbx_free(trigger->correlation_tag);
	zbx_free(trigger->opdata);
	zbx_free(trigger->event_name);

	zbx_vector_ptr_clear_ext(&trigger->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&trigger->tags);
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_items_by_keys                                       *
 *                                                                            *
 * Purpose: locate item in configuration cache by host and key                *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to array of DC_ITEM structures        *
 *             keys     - [IN] list of item keys with host names              *
 *             errcodes - [OUT] SUCCEED if record located and FAIL otherwise  *
 *             num      - [IN] number of elements in items, keys, errcodes    *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_items_by_keys(DC_ITEM *items, zbx_host_key_t *keys, int *errcodes, size_t num)
{
	size_t			i;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_host = DCfind_host(keys[i].host)) ||
				NULL == (dc_item = DCfind_item(dc_host->hostid, keys[i].key)))
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

int	DCconfig_get_hostid_by_name(const char *host, zbx_uint64_t *hostid)
{
	const ZBX_DC_HOST	*dc_host;
	int			ret;

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
 * Function: DCconfig_get_items_by_itemids                                    *
 *                                                                            *
 * Purpose: Get item with specified ID                                        *
 *                                                                            *
 * Parameters: items    - [OUT] pointer to DC_ITEM structures                 *
 *             itemids  - [IN] array of item IDs                              *
 *             errcodes - [OUT] SUCCEED if item found, otherwise FAIL         *
 *             num      - [IN] number of elements                             *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_items_by_itemids(DC_ITEM *items, const zbx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t			i;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;

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



/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_preprocessable_items                                *
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
void	DCconfig_get_preprocessable_items(zbx_hashset_t *items, int *timestamp, int manager_num)
{
	const ZBX_DC_PREPROCITEM	*dc_preprocitem;
	const ZBX_DC_MASTERITEM		*dc_masteritem;
	const ZBX_DC_ITEM		*dc_item;
	const zbx_dc_preproc_op_t	*dc_op;
	zbx_preproc_item_t		*item, item_local;
	zbx_hashset_iter_t		iter;
	zbx_preproc_op_t		*op;
	int				i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* no changes */
	if (0 != *timestamp && *timestamp == config->item_sync_ts)
		goto out;

	zbx_hashset_clear(items);
	*timestamp = config->item_sync_ts;

	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->preprocitems, &iter);
	while (NULL != (dc_preprocitem = (const ZBX_DC_PREPROCITEM *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == (dc_item = zbx_hashset_search(&config->items,&dc_preprocitem->itemid))) {
			continue;
		};
		
		//when distributed preprocessing is on, preproc manager only needs 
		//config for master itemds and items of dependent type
		if (0 == CONFIG_DISABLE_INPOLLER_PREPROC && 
			NULL == (dc_masteritem = zbx_hashset_search(&config->masteritems,&dc_preprocitem->itemid)) && 
			ITEM_TYPE_DEPENDENT != dc_item->type ) {
			//	zabbix_log(LOG_LEVEL_INFORMATION,"Skipping item %ld from loading preproc config",dc_preprocitem->itemid);
				continue;	
			}
		if ((dc_item->hostid % CONFIG_PREPROCMAN_FORKS) != manager_num  )	
			continue;
		if (FAIL == dc_preproc_item_init(&item_local, dc_preprocitem->itemid))
			continue;
		//zabbix_log(LOG_LEVEL_INFORMATION,"Loading %ld item to preproc config",dc_preprocitem->itemid);

		item = (zbx_preproc_item_t *)zbx_hashset_insert(items, &item_local, sizeof(item_local));

		item->preproc_ops_num = dc_preprocitem->preproc_ops.values_num;
		item->preproc_ops = (zbx_preproc_op_t *)zbx_malloc(NULL, sizeof(zbx_preproc_op_t) * item->preproc_ops_num);
		item->update_time = dc_preprocitem->update_time;

		for (i = 0; i < dc_preprocitem->preproc_ops.values_num; i++)
		{
			dc_op = (const zbx_dc_preproc_op_t *)dc_preprocitem->preproc_ops.values[i];
			op = &item->preproc_ops[i];
			op->type = dc_op->type;
			//todo: strings interning here!!!!!
			op->params = zbx_strdup(NULL, dc_op->params);
			op->error_handler = dc_op->error_handler;
			//todo: strings interning here
			op->error_handler_params = zbx_strdup(NULL, dc_op->error_handler_params);
		}
	}

	zbx_hashset_iter_reset(&config->masteritems, &iter);
	while (NULL != (dc_masteritem = (const ZBX_DC_MASTERITEM *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL == (item = (zbx_preproc_item_t *)zbx_hashset_search(items, &dc_masteritem->itemid)))
		{
			if (FAIL == dc_preproc_item_init(&item_local, dc_masteritem->itemid))
				continue;

			item = (zbx_preproc_item_t *)zbx_hashset_insert(items, &item_local, sizeof(item_local));
		}

		item->dep_itemids_num = 0;
		item->dep_itemids = (zbx_uint64_pair_t *)zbx_malloc(NULL, sizeof(zbx_uint64_pair_t) *
				dc_masteritem->dep_itemids.values_num);

		for (i = 0; i < dc_masteritem->dep_itemids.values_num; i++)
		{
			if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items,
					&dc_masteritem->dep_itemids.values[i].first)) ||
					ITEM_STATUS_ACTIVE != dc_item->status)
			{
				continue;
			}
			item->dep_itemids[item->dep_itemids_num++] = dc_masteritem->dep_itemids.values[i];
		}
	}

	zbx_hashset_iter_reset(&config->items, &iter);
	while (NULL != (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
	{
		if (ITEM_TYPE_INTERNAL != dc_item->type)
			continue;

		if (NULL == (item = (zbx_preproc_item_t *)zbx_hashset_search(items, &dc_item->itemid)))
		{
			if (FAIL == dc_preproc_item_init(&item_local, dc_item->itemid))
				continue;

			zbx_hashset_insert(items, &item_local, sizeof(item_local));
		}
	}

	UNLOCK_CACHE;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() items:%d", __func__, items->num_data);
}

void	DCconfig_get_hosts_by_itemids(DC_HOST *hosts, const zbx_uint64_t *itemids, int *errcodes, size_t num)
{
	size_t			i;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;

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

void	DCconfig_get_triggers_by_triggerids(DC_TRIGGER *triggers, const zbx_uint64_t *triggerids, int *errcode,
		size_t num)
{
	size_t			i;
	const ZBX_DC_TRIGGER	*dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < num; i++)
	{
		if (NULL == (dc_trigger = (const ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerids[i])))
		{
			errcode[i] = FAIL;
			continue;
		}

		DCget_trigger(&triggers[i], dc_trigger);
		errcode[i] = SUCCEED;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_functions_by_functionids                            *
 *                                                                            *
 * Purpose: Get functions by IDs                                              *
 *                                                                            *
 * Parameters: functions   - [OUT] pointer to DC_FUNCTION structures          *
 *             functionids - [IN] array of function IDs                       *
 *             errcodes    - [OUT] SUCCEED if item found, otherwise FAIL      *
 *             num         - [IN] number of elements                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs, Alexander Vladishev                          *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_functions_by_functionids(DC_FUNCTION *functions, zbx_uint64_t *functionids, int *errcodes,
		size_t num)
{
	size_t			i;
	const ZBX_DC_FUNCTION	*dc_function;

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

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_clean_functions                                         *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_clean_functions(DC_FUNCTION *functions, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		zbx_free(functions[i].function);
	}
}

void	DCconfig_clean_triggers(DC_TRIGGER *triggers, int *errcodes, size_t num)
{
	size_t	i;

	for (i = 0; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		DCclean_trigger(&triggers[i]);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_lock_triggers_by_history_items                          *
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
 * Author: Aleksandrs Saveljevs                                               *
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
int	DCconfig_lock_triggers_by_history_items(zbx_vector_ptr_t *history_items, zbx_vector_uint64_t *triggerids)
{
	int			i, j, locked_num = 0;
	const ZBX_DC_ITEM	*dc_item;
	ZBX_DC_TRIGGER		*dc_trigger;
	zbx_hc_item_t		*history_item;

	WRLOCK_CACHE;

	for (i = 0; i < history_items->values_num; i++)
	{
		history_item = (zbx_hc_item_t *)history_items->values[i];

		if (0 != (ZBX_DC_FLAG_NOVALUE & history_item->tail->flags))
			continue;

		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &history_item->itemid)))
			continue;

		if (NULL == dc_item->triggers)
			continue;

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			if (1 == dc_trigger->locked)
			{
				locked_num++;
				history_item->status = ZBX_HC_ITEM_STATUS_BUSY;
				goto next;
			}
		}

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			dc_trigger->locked = 1;
			zbx_vector_uint64_append(triggerids, dc_trigger->triggerid);
		}
next:;
	}

	UNLOCK_CACHE;

	return history_items->values_num - locked_num;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_lock_triggers_by_triggerids                             *
 *                                                                            *
 * Purpose: Lock triggers so that multiple processes do not process one       *
 *          trigger simultaneously.                                           *
 *                                                                            *
 * Parameters: triggerids_in  - [IN] ids of triggers to lock                  *
 *             triggerids_out - [OUT] ids of locked triggers                  *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_lock_triggers_by_triggerids(zbx_vector_uint64_t *triggerids_in, zbx_vector_uint64_t *triggerids_out)
{
	int		i;
	ZBX_DC_TRIGGER	*dc_trigger;

	if (0 == triggerids_in->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < triggerids_in->values_num; i++)
	{
		if (NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerids_in->values[i])))
			continue;

		if (1 == dc_trigger->locked)
			continue;

		dc_trigger->locked = 1;
		zbx_vector_uint64_append(triggerids_out, dc_trigger->triggerid);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_unlock_triggers                                         *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_unlock_triggers(const zbx_vector_uint64_t *triggerids)
{
	int		i;
	ZBX_DC_TRIGGER	*dc_trigger;

	WRLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerids->values[i])))
			continue;

		dc_trigger->locked = 0;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_unlock_all_triggers                                     *
 *                                                                            *
 * Purpose: Unlocks all locked triggers before doing full history sync at     *
 *          program exit                                                      *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_unlock_all_triggers(void)
{
	ZBX_DC_TRIGGER		*dc_trigger;
	zbx_hashset_iter_t	iter;

	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
		dc_trigger->locked = 0;

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_triggers_by_itemids                                 *
 *                                                                            *
 * Purpose: get enabled triggers for specified items                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_get_triggers_by_itemids(zbx_hashset_t *trigger_info, zbx_vector_ptr_t *trigger_order,
		const zbx_uint64_t *itemids, const zbx_timespec_t *timespecs, int itemids_num)
{
	int			i, j, found;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_TRIGGER	*dc_trigger;
	DC_TRIGGER		*trigger;

	RDLOCK_CACHE;

	for (i = 0; i < itemids_num; i++)
	{
		/* skip items which are not in configuration cache and items without triggers */

		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids[i])) || NULL == dc_item->triggers)
			continue;

		/* process all triggers for the specified item */

		for (j = 0; NULL != (dc_trigger = dc_item->triggers[j]); j++)
		{
			if (TRIGGER_STATUS_ENABLED != dc_trigger->status)
				continue;

			/* find trigger by id or create a new record in hashset if not found */
			trigger = (DC_TRIGGER *)DCfind_id(trigger_info, dc_trigger->triggerid, sizeof(DC_TRIGGER), &found);

			if (0 == found)
			{
				DCget_trigger(trigger, dc_trigger);
				zbx_vector_ptr_append(trigger_order, trigger);
			}

			/* copy latest change timestamp */

			if (trigger->timespec.sec < timespecs[i].sec ||
					(trigger->timespec.sec == timespecs[i].sec &&
					trigger->timespec.ns < timespecs[i].ns))
			{
				/* DCconfig_get_triggers_by_itemids() function is called during trigger processing */
				/* when syncing history cache. A trigger cannot be processed by two syncers at the */
				/* same time, so its safe to update trigger timespec within read lock.             */
				trigger->timespec = timespecs[i];
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_find_active_time_function                               *
 *                                                                            *
 * Purpose: checks if the expression contains time based functions            *
 *                                                                            *
 ******************************************************************************/
static int	DCconfig_find_active_time_function(const char *expression)
{
	zbx_uint64_t		functionid;
	const ZBX_DC_FUNCTION	*dc_function;
	const ZBX_DC_HOST	*dc_host;
	const ZBX_DC_ITEM	*dc_item;

	while (SUCCEED == get_N_functionid(expression, 1, &functionid, &expression))
	{
		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionid)))
			continue;

		if (ZBX_FUNCTION_TYPE_TIMER == dc_function->type || ZBX_FUNCTION_TYPE_TRENDS == dc_function->type)
		{
			if (NULL == (dc_item = zbx_hashset_search(&config->items, &dc_function->itemid)))
				continue;

			if (NULL == (dc_host = zbx_hashset_search(&config->hosts, &dc_item->hostid)))
				continue;

			if (SUCCEED != DCin_maintenance_without_data_collection(dc_host, dc_item))
				return SUCCEED;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_triggers_by_timers                                    *
 *                                                                            *
 * Purpose: gets timer triggers from cache                                    *
 *                                                                            *
 * Parameters: trigger_info  - [IN/OUT] triggers                              *
 *             trigger_order - [IN/OUT] triggers in processing order          *
 *             timers        - [IN] timers of triggers to retrieve            *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_get_triggers_by_timers(zbx_hashset_t *trigger_info, zbx_vector_ptr_t *trigger_order,
		const zbx_vector_ptr_t *timers)
{
	int		i;
	ZBX_DC_TRIGGER	*dc_trigger;

	RDLOCK_CACHE;

	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t	*timer = (zbx_trigger_timer_t *)timers->values[i];

		/* skip timers of 'busy' (being processed) triggers */
		if (0 == timer->lock)
			continue;

		if (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &timer->triggerid)))
		{
			DC_TRIGGER	*trigger, trigger_local;
			unsigned char	flags;

			if (SUCCEED == DCconfig_find_active_time_function(dc_trigger->expression))
			{
				flags = ZBX_DC_TRIGGER_PROBLEM_EXPRESSION;
			}
			else
			{
				if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION != dc_trigger->recovery_mode)
					continue;

				if (TRIGGER_VALUE_PROBLEM != dc_trigger->value)
					continue;

				if (SUCCEED != DCconfig_find_active_time_function(dc_trigger->recovery_expression))
					continue;

				flags = 0;
			}

			trigger_local.triggerid = dc_trigger->triggerid;
			trigger = (DC_TRIGGER *)zbx_hashset_insert(trigger_info, &trigger_local, sizeof(trigger_local));
			DCget_trigger(trigger, dc_trigger);

			trigger->timespec = timer->eval_ts;
			trigger->flags = flags;

			zbx_vector_ptr_append(trigger_order, trigger);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_trigger_timers                                        *
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
void	zbx_dc_get_trigger_timers(zbx_vector_ptr_t *timers, int now, int soft_limit, int hard_limit)
{
	zbx_trigger_timer_t	*first_timer = NULL;

	WRLOCK_CACHE;

	while (SUCCEED != zbx_binary_heap_empty(&config->trigger_queue) && timers->values_num < hard_limit)
	{
		zbx_binary_heap_elem_t	*elem;
		zbx_trigger_timer_t	*timer;
		ZBX_DC_TRIGGER		*dc_trigger;
		ZBX_DC_FUNCTION		*dc_function;

		elem = zbx_binary_heap_find_min(&config->trigger_queue);
		timer = (zbx_trigger_timer_t *)elem->data;

		if (timer->exec_ts.sec > now)
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

		/* check if function exists and trigger should be calculated */
		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &timer->objectid)) ||
				dc_function->revision > timer->revision ||
				NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers,
						&timer->triggerid)) ||
				TRIGGER_STATUS_ENABLED != dc_trigger->status ||
				TRIGGER_FUNCTIONAL_TRUE != dc_trigger->functional)
		{
			if (NULL != dc_function && dc_function->revision == timer->revision)
				dc_function->timer_revision = 0;

			dc_trigger_timer_free(timer);
			continue;
		}

		zbx_vector_ptr_append(timers, timer);

		/* Trigger expression must be calculated using function evaluation time. If a trigger is locked   */
		/* keep rescheduling its timer until trigger is unlocked and can be calculated using the required */
		/* evaluation time. However there are exceptions when evaluation time of a locked trigger is      */
		/* acceptable to evaluate other functions:                                                        */
		/*  1) time functions uses current time, so trigger evaluation time does not affect their results */
		/*  2) trend function of the same trigger with the same evaluation timestamp is being             */
		/*     evaluated by the same process                                                              */
		if (0 == dc_trigger->locked || ZBX_FUNCTION_TYPE_TRENDS != timer->type ||
				(NULL != first_timer && 1 == first_timer->lock))
		{
			/* resetting execution timer will cause a new execution time to be set */
			/* when timer is put back into queue                                   */
			timer->exec_ts.sec = 0;
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
 * Function: dc_reschedule_trigger_timers                                     *
 *                                                                            *
 * Purpose: reschedule trigger timers                                         *
 *                                                                            *
 * Comments: Triggers are unlocked by DCconfig_unlock_triggers()              *
 *                                                                            *
 ******************************************************************************/
static void	dc_reschedule_trigger_timers(zbx_vector_ptr_t *timers)
{
	int	i;

	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t	*timer = (zbx_trigger_timer_t *)timers->values[i];

		timer->lock = 0;

		/* schedule calculation error can result in 0 execution time */
		if (0 == timer->exec_ts.sec)
		{
			ZBX_DC_FUNCTION	*function;

			if (NULL != (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
					&timer->objectid)) && function->timer_revision == timer->revision)
			{
				function->timer_revision = 0;
			}
			dc_trigger_timer_free(timer);
		}
		else
			dc_schedule_trigger_timer(timer, &timer->eval_ts, &timer->exec_ts);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_reschedule_trigger_timers                                 *
 *                                                                            *
 * Purpose: reschedule trigger timers while locking configuration cache       *
 *                                                                            *
 * Comments: Triggers are unlocked by DCconfig_unlock_triggers()              *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_reschedule_trigger_timers(zbx_vector_ptr_t *timers, int now)
{
	int	i;

	/* calculate new execution/evaluation time for the evaluated triggers */
	/* (timers with reseted execution time)                               */
	for (i = 0; i < timers->values_num; i++)
	{
		zbx_trigger_timer_t	*timer = (zbx_trigger_timer_t *)timers->values[i];

		if (0 == timer->exec_ts.sec)
		{
			if (0 != (timer->exec_ts.sec = dc_function_calculate_nextcheck(timer, now, timer->triggerid)))
				timer->eval_ts = timer->exec_ts;
		}
	}

	WRLOCK_CACHE;
	dc_reschedule_trigger_timers(timers);
	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_timer_queue                                           *
 *                                                                            *
 * Purpose: clears timer trigger queue                                        *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_clear_timer_queue(zbx_vector_ptr_t *timers)
{
	ZBX_DC_FUNCTION	*function;
	int		i;

	zbx_vector_ptr_reserve(timers, config->trigger_queue.elems_num);

	WRLOCK_CACHE;

	for (i = 0; i < config->trigger_queue.elems_num; i++)
	{
		zbx_trigger_timer_t	*timer = (zbx_trigger_timer_t *)config->trigger_queue.elems[i].data;

		if (ZBX_FUNCTION_TYPE_TRENDS == timer->type &&
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

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_free_timers                                               *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_free_timers(zbx_vector_ptr_t *timers)
{
	int	i;

	WRLOCK_CACHE;

	for (i = 0; i < timers->values_num; i++)
		dc_trigger_timer_free(timers->values[i]);

	UNLOCK_CACHE;
}

void	DCfree_triggers(zbx_vector_ptr_t *triggers)
{
	int	i;

	for (i = 0; i < triggers->values_num; i++)
		DCclean_trigger((DC_TRIGGER *)triggers->values[i]);

	zbx_vector_ptr_clear(triggers);
}

void	DCconfig_update_interface_snmp_stats(zbx_uint64_t interfaceid, int max_snmp_succeed, int min_snmp_fail)
{
	ZBX_DC_SNMPINTERFACE	*dc_snmp;

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

static int	DCconfig_get_suggested_snmp_vars_nolock(zbx_uint64_t interfaceid, int *bulk)
{
	int				num;
	const ZBX_DC_SNMPINTERFACE	*dc_snmp;

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

int	DCconfig_get_suggested_snmp_vars(zbx_uint64_t interfaceid, int *bulk)
{
	int	ret;

	RDLOCK_CACHE;

	ret = DCconfig_get_suggested_snmp_vars_nolock(interfaceid, bulk);

	UNLOCK_CACHE;

	return ret;
}

static int	dc_get_interface_by_type(DC_INTERFACE *interface, zbx_uint64_t hostid, unsigned char type)
{
	int				res = FAIL;
	const ZBX_DC_INTERFACE		*dc_interface;
	const ZBX_DC_INTERFACE_HT	*interface_ht;
	ZBX_DC_INTERFACE_HT		interface_ht_local;

	interface_ht_local.hostid = hostid;
	interface_ht_local.type = type;

	if (NULL != (interface_ht = (const ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht, &interface_ht_local)))
	{
		dc_interface = interface_ht->interface_ptr;
		DCget_interface(interface, dc_interface);
		res = SUCCEED;
	}

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_interface_by_type                                   *
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
int	DCconfig_get_interface_by_type(DC_INTERFACE *interface, zbx_uint64_t hostid, unsigned char type)
{
	int	res;

	RDLOCK_CACHE;

	res = dc_get_interface_by_type(interface, hostid, type);

	UNLOCK_CACHE;

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_interface                                           *
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
int	DCconfig_get_interface(DC_INTERFACE *interface, zbx_uint64_t hostid, zbx_uint64_t itemid)
{
	int			res = FAIL, i;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_INTERFACE	*dc_interface;

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
 * Function: dc_config_get_queue_nextcheck                                    *
 *                                                                            *
 * Purpose: Get nextcheck for selected queue                                  *
 *                                                                            *
 * Parameters: queue - [IN] the queue                                         *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for the specified queue        *
 *                                                                            *
 ******************************************************************************/
static int	dc_config_get_queue_nextcheck(zbx_binary_heap_t *queue)
{
	int				nextcheck;
	const zbx_binary_heap_elem_t	*min;
	const ZBX_DC_ITEM		*dc_item;

	if (FAIL == zbx_binary_heap_empty(queue))
	{
		min = zbx_binary_heap_find_min(queue);
		dc_item = (const ZBX_DC_ITEM *)min->data;

		nextcheck = dc_item->nextcheck;
	}
	else
		nextcheck = FAIL;

	return nextcheck;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_poller_nextcheck                                    *
 *                                                                            *
 * Purpose: Get nextcheck for selected poller                                 *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *                                                                            *
 * Return value: nextcheck or FAIL if no items for selected poller            *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_poller_nextcheck(unsigned char poller_type)
{
	int			nextcheck;
	zbx_binary_heap_t	*queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() poller_type:%d", __func__, (int)poller_type);

	queue = &config->queues[poller_type];

	RDLOCK_CACHE;

	nextcheck = dc_config_get_queue_nextcheck(queue);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, nextcheck);

	return nextcheck;
}

static void	dc_requeue_item(ZBX_DC_ITEM *dc_item, const ZBX_DC_HOST *dc_host, int flags,
		int lastclock)
{
	unsigned char	old_poller_type;
	int		old_nextcheck;
	
	old_nextcheck = dc_item->nextcheck;
	DCitem_nextcheck_update(dc_item, dc_host, flags, lastclock, NULL);

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, flags);

	if (CONFIG_DEBUG_ITEM == dc_item->itemid) 
		zabbix_log(LOG_LEVEL_INFORMATION, "Debug item: %ld sheduled in %d sec, to poller type %d -> %d", 
			dc_item->itemid, dc_item->nextcheck - time(NULL),old_poller_type, dc_item->poller_type );

	DCupdate_item_queue(dc_item, old_poller_type, old_nextcheck);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_requeue_item_at                                               *
 *                                                                            *
 * Purpose: requeues items at the specified time                              *
 *                                                                            *
 * Parameters: dc_item   - [IN] the item to reque                             *
 *             dc_host   - [IN] item's host                                   *
 *             nextcheck - [IN] the scheduled time                            *
 *                                                                            *
 ******************************************************************************/
static void	dc_requeue_item_at(ZBX_DC_ITEM *dc_item, ZBX_DC_HOST *dc_host, int nextcheck)
{
	unsigned char	old_poller_type;
	int		old_nextcheck;

	dc_item->queue_priority = ZBX_QUEUE_PRIORITY_HIGH;

	old_nextcheck = dc_item->nextcheck;
	dc_item->nextcheck = nextcheck;

	old_poller_type = dc_item->poller_type;
	DCitem_poller_type_update(dc_item, dc_host, ZBX_ITEM_COLLECTED);

	DCupdate_item_queue(dc_item, old_poller_type, old_nextcheck);
}

static int glb_might_be_async_polled ( ZBX_DC_ITEM *zbx_dc_item, ZBX_DC_HOST *zbx_dc_host ) {
	ZBX_DC_SNMPITEM *snmpitem;

	if ( CONFIG_GLB_SNMP_FORKS == 0 ) return FAIL;
#ifdef HAVE_NETSNMP	
	if ( zbx_dc_item->type == ITEM_TYPE_SNMP ) {
			snmpitem = (ZBX_DC_SNMPITEM *)zbx_hashset_search(&config->snmpitems,&zbx_dc_item->itemid);
			//avoiding dynamic and discovery items from being processed by async glb pollers
			if  ( NULL != snmpitem &&  (ZBX_FLAG_DISCOVERY_RULE & zbx_dc_item->flags) || (ZBX_SNMP_OID_TYPE_DYNAMIC == snmpitem->snmp_oid_type) )  
			  	  return FAIL;
			else {
			  //zabbix_log(LOG_LEVEL_INFORMATION, "Item %ld host %s oid %s might be asyn pooled, skipping from the normal poll",zbx_dc_item->itemid, zbx_dc_host->host, snmpitem->snmp_oid);
			  return SUCCEED;
			}
	
	}
 #endif
 	return FAIL;
}
/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_poller_items                                        *
 *                                                                            *
 * Purpose: Get array of items for selected poller                            *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *             items       - [OUT] array of items                             *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
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
int	DCconfig_get_poller_items(unsigned char poller_type, DC_ITEM **items)
{
	int			now, num = 0, max_items;
	zbx_binary_heap_t	*queue;

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
		int				disable_until;
		const zbx_binary_heap_elem_t	*min;
		ZBX_DC_HOST			*dc_host;
		ZBX_DC_ITEM			*dc_item;
		static const ZBX_DC_ITEM	*dc_item_prev = NULL;

		min = zbx_binary_heap_find_min(queue);
		dc_item = (ZBX_DC_ITEM *)min->data;

		if (dc_item->nextcheck > now)
			break;

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

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, now);
			continue;
		}

		/* don't apply unreachable item/host throttling for prioritized items */
		if (ZBX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
			if (0 == (disable_until = DCget_disable_until(dc_item, dc_host)))
			{
				/* move reachable items on reachable hosts to normal pollers */
				if (ZBX_POLLER_TYPE_UNREACHABLE == poller_type &&
						ZBX_QUEUE_PRIORITY_LOW != dc_item->queue_priority)
				{
					dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, now);
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
					dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE,
							now);
					continue;
				}

				DCincrease_disable_until(dc_item, dc_host, now);
			}
		}
		
		/***** Glaber fix: do not poll items that might be polled by async methods */
		if (SUCCEED == glb_might_be_async_polled(dc_item,dc_host)) {
			
			dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, now);
			continue;			
		
		}

		if (0 == num)
		{
			if (ZBX_POLLER_TYPE_NORMAL == poller_type && ITEM_TYPE_SNMP == dc_item->type &&
					0 == (ZBX_FLAG_DISCOVERY_RULE & dc_item->flags))
			{
				ZBX_DC_SNMPITEM	*snmpitem;

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

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_glb_poller_items                                    *
 *                                                                            *
 * Purpose: Get array of items for selected poller to use in own queue        *
 *                                                                            *
 * Parameters: poller_type - [IN] poller type (ZBX_POLLER_TYPE_...)           *
 *             items       - [OUT] array of items                             *
 *                                                                            *
 * Return value: number of items in items array                               *
 *																			  *
 *                                                                            *
 * Comments: Items do not leave the queue through this function. They are 	  *
 * 			 immediately requeued to closest point of tome 					  *
 * 			 CONFIG_GLB_REQUEUE_TIME 										  *								
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_glb_poller_items(zbx_binary_heap_t *events, zbx_hashset_t *hosts, zbx_hashset_t *items, unsigned char item_type, unsigned int  process_num, void *poll_engine)
{
	int			now, num = 0, queue_num;
	int forks;
	//zbx_binary_heap_t	*queue;
	zbx_hashset_iter_t iter;
	void *item;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() process num %d" , __func__, process_num);

	now = time(NULL);
	
	RDLOCK_CACHE;

	//this might change when i rollback queues change 
	switch (item_type) {
#ifdef HAVE_NETSNMP			
		case ITEM_TYPE_SNMP:
			zbx_hashset_iter_reset(&config->snmpitems,&iter);
			forks = CONFIG_GLB_SNMP_FORKS;

			queue_num = ZBX_POLLER_TYPE_NORMAL;
#endif
		break;

		case ITEM_TYPE_SIMPLE:
			zbx_hashset_iter_reset(&config->simpleitems,&iter);
			forks = CONFIG_GLB_PINGER_FORKS;

			queue_num = ZBX_POLLER_TYPE_PINGER;
		default:
			zabbix_log(LOG_LEVEL_WARNING,"Glaber poller doesn't support item type %d yet, this is programming BUG",item_type);
			THIS_SHOULD_NEVER_HAPPEN;
			exit(-1);
	}

	while (NULL != (item =(void *) zbx_hashset_iter_next(&iter) ) ) {
		
		ZBX_DC_ITEM *zbx_dc_item;
		ZBX_DC_HOST *zbx_dc_host;
		
		//so we know the item, looking for the dc item
		//as all type specifi types has itemid first (for a reason), we can safely do this
		if (NULL == ( zbx_dc_item = zbx_hashset_search(&config->items, item))) 
			continue;

		if (NULL == (zbx_dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &zbx_dc_item->hostid))) 
			continue;

		if (zbx_dc_host->hostid % forks != (process_num - 1) ) {
			//zabbix_log(LOG_LEVEL_INFORMATION, "Skipping item, it belongs to other process %d %d",zbx_dc_host->hostid % forks, process_num-1);
			continue;
		}

		if (HOST_STATUS_MONITORED != zbx_dc_host->status) 			
			continue;
		
		if (SUCCEED == DCin_maintenance_without_data_collection(zbx_dc_host, zbx_dc_item))
		{	//dc_requeue_item(zbx_dc_item, zbx_dc_host, ZBX_ITEM_COLLECTED, now);
			continue;
		}
		
		if (ZBX_CLUSTER_HOST_STATE_ACTIVE != zbx_dc_host->cluster_state && CONFIG_CLUSTER_SERVER_ID > 0) {
			//dc_requeue_item(zbx_dc_item, zbx_dc_host, ZBX_ITEM_COLLECTED, now + CONFIG_TIMEOUT);
			continue;
		}

		//doing all kind of checks here
		if ( zbx_dc_item->type == ITEM_TYPE_SNMP ) {
			
			ZBX_DC_SNMPITEM *snmpitem = (ZBX_DC_SNMPITEM *)item;
			//avoiding dynamic and discovery items from being processed by async glb pollers
			if  ( (ZBX_FLAG_DISCOVERY_RULE & zbx_dc_item->flags) || (ZBX_SNMP_OID_TYPE_DYNAMIC == snmpitem->snmp_oid_type) )  
					continue;
		}

		DEBUG_ITEM(zbx_dc_item->itemid,"Item fetched for placement to the local queue");
		
		//creating/updating the item in the local hash
		//this will also add events to poll the new items
		
		//convering the item to the dc_item
		//todo: this might be avoided if dc_item, interface and type specific data are used together
		
		DC_ITEM dc_item;
		int errcode=SUCCEED;
		AGENT_RESULT result;

		DCget_host(&dc_item.host, zbx_dc_host);
		DCget_item(&dc_item, zbx_dc_item);
		
		zbx_prepare_items(&dc_item, &errcode, 1, &result, MACRO_EXPAND_YES);

		glb_create_item(events, hosts, items, &dc_item, poll_engine);
		
		init_result(&result);
		zbx_clean_items(&dc_item, 1, &result);
		DCconfig_clean_items(&dc_item, &errcode, 1);

		num++;
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}


/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_ipmi_poller_items                                   *
 *                                                                            *
 * Purpose: Get array of items for IPMI poller                                *
 *                                                                            *
 * Parameters: now       - [IN] current timestamp                             *
 *             items     - [OUT] array of items                               *
 *             items_num - [IN] the number of items to get                    *
 *             nextcheck - [OUT] the next scheduled check                     *
 *                                                                            *
 * Return value: number of items in items array                               *
 *                                                                            *
 * Comments: IPMI items leave the queue only through this function. IPMI      *
 *           manager must always return the items they have taken using       *
 *           DCrequeue_items() or DCpoller_requeue_items().                   *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_ipmi_poller_items(int now, DC_ITEM *items, int items_num, int *nextcheck, unsigned int process_num)
{
	int			num = 0;
	zbx_binary_heap_t	*queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->queues[ZBX_POLLER_TYPE_IPMI];

	WRLOCK_CACHE;

	while (num < items_num && FAIL == zbx_binary_heap_empty(queue))
	{
		int				disable_until;
		const zbx_binary_heap_elem_t	*min;
		ZBX_DC_HOST			*dc_host;
		ZBX_DC_ITEM			*dc_item;

		min = zbx_binary_heap_find_min(queue);
		dc_item = (ZBX_DC_ITEM *)min->data;

		if (dc_item->nextcheck > now)
			break;

		zbx_binary_heap_remove_min(queue);
		dc_item->location = ZBX_LOC_NOWHERE;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
		{
			dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, now);
			continue;
		}

		/* don't apply unreachable item/host throttling for prioritized items */
		if (ZBX_QUEUE_PRIORITY_HIGH != dc_item->queue_priority)
		{
			if (0 != (disable_until = DCget_disable_until(dc_item, dc_host)))
			{
				if (disable_until > now)
				{
					dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE,
							now);
					continue;
				}

				DCincrease_disable_until(dc_item, dc_host, now);
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


/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_snmp_interfaceids_by_addr                           *
 *                                                                            *
 * Purpose: get array of interface IDs for the specified address              *
 *                                                                            *
 * Return value: number of interface IDs returned                             *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_snmp_interfaceids_by_addr(const char *addr, zbx_uint64_t **interfaceids)
{
	int				count = 0, i;
	const ZBX_DC_INTERFACE_ADDR	*dc_interface_snmpaddr;
	ZBX_DC_INTERFACE_ADDR		dc_interface_snmpaddr_local;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() addr:'%s'", __func__, addr);

	dc_interface_snmpaddr_local.addr = addr;

	RDLOCK_CACHE;

	if (NULL == (dc_interface_snmpaddr = (const ZBX_DC_INTERFACE_ADDR *)zbx_hashset_search(&config->interface_snmpaddrs, &dc_interface_snmpaddr_local)))
		goto unlock;

	*interfaceids = (zbx_uint64_t *)zbx_malloc(*interfaceids, dc_interface_snmpaddr->interfaceids.values_num * sizeof(zbx_uint64_t));

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
 * Function: DCconfig_get_snmp_items_by_interfaceid                           *
 *                                                                            *
 * Purpose: get array of snmp trap items for the specified interfaceid        *
 *                                                                            *
 * Return value: number of items returned                                     *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
size_t	DCconfig_get_snmp_items_by_interfaceid(zbx_uint64_t interfaceid, DC_ITEM **items)
{
	size_t				items_num = 0, items_alloc = 8;
	int				i;
	const ZBX_DC_ITEM		*dc_item;
	const ZBX_DC_INTERFACE_ITEM	*dc_interface_snmpitem;
	const ZBX_DC_INTERFACE		*dc_interface;
	const ZBX_DC_HOST		*dc_host;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() interfaceid:" ZBX_FS_UI64, __func__, interfaceid);

	RDLOCK_CACHE;

	if (NULL == (dc_interface = (const ZBX_DC_INTERFACE *)zbx_hashset_search(&config->interfaces, &interfaceid)))
		goto unlock;

	if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_interface->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	if (NULL == (dc_interface_snmpitem = (const ZBX_DC_INTERFACE_ITEM *)zbx_hashset_search(&config->interface_snmpitems, &interfaceid)))
		goto unlock;

	*items = (DC_ITEM *)zbx_malloc(*items, items_alloc * sizeof(DC_ITEM));

	for (i = 0; i < dc_interface_snmpitem->itemids.values_num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &dc_interface_snmpitem->itemids.values[i])))
			continue;

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

static void	dc_requeue_items(const zbx_uint64_t *itemids, const int *lastclocks, const int *errcodes, size_t num)
{
	size_t		i;
	ZBX_DC_ITEM	*dc_item;
	ZBX_DC_HOST	*dc_host;

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

		switch (errcodes[i])
		{
			case SUCCEED:
			case NOTSUPPORTED:
			case AGENT_ERROR:
			case CONFIG_ERROR:
				dc_item->queue_priority = ZBX_QUEUE_PRIORITY_NORMAL;
				dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, lastclocks[i]);
				break;
			case NETWORK_ERROR:
			//case PROCESSED:
			
			case GATEWAY_ERROR:
			case TIMEOUT_ERROR:
				dc_item->queue_priority = ZBX_QUEUE_PRIORITY_LOW;
				dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE,
						time(NULL));
				break;
			default:
				zabbix_log(LOG_LEVEL_INFORMATION,"Unknown errcode: %d", errcodes[i]);
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}
/******************************************************************************
* The major differnce is that async poller have their own queues to reduce 
* overall write locks (theу still need read locks to access items data)
* unlike traditional queues, item's queues hold item id's and not direct
* memory pointers, this requires additional searches
******************************************************************************/
static void	dc_requeue_async_items(const zbx_uint64_t *itemids, const unsigned char *states, const int *lastclocks,
		const int *errcodes, size_t num)
{
	size_t		i;
	ZBX_DC_ITEM	*dc_item;
	ZBX_DC_HOST	*dc_host;

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

		//whatever the other return codes, we want the item to be polled again
		dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED, lastclocks[i]);
	}
}


void	DCrequeue_items(const zbx_uint64_t *itemids, const int *lastclocks,
		const int *errcodes, size_t num)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, lastclocks, errcodes, num);

	UNLOCK_CACHE;
}

void	DCpoller_requeue_items(const zbx_uint64_t *itemids, const int *lastclocks,
		const int *errcodes, size_t num, unsigned char poller_type, int *nextcheck)
{
	WRLOCK_CACHE;

	dc_requeue_items(itemids, lastclocks, errcodes, num);
	*nextcheck = dc_config_get_queue_nextcheck(&config->queues[poller_type]);

	UNLOCK_CACHE;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_requeue_unreachable_items                                 *
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
void	zbx_dc_requeue_unreachable_items(zbx_uint64_t *itemids, size_t itemids_num)
{
	size_t		i;
	ZBX_DC_ITEM	*dc_item;
	ZBX_DC_HOST	*dc_host;

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

		dc_requeue_item(dc_item, dc_host, ZBX_ITEM_COLLECTED | ZBX_HOST_UNREACHABLE,
				time(NULL));
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_get_agent_availability                                    *
 *                                                                            *
 * Purpose: get host availability data for the specified agent                *
 *                                                                            *
 * Parameters: dc_host      - [IN] the host                                   *
 *             agent        - [IN] the agent (see ZBX_FLAGS_AGENT_STATUS_*    *
 *                                 defines                                    *
 *             availability - [OUT] the host availability data                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 ******************************************************************************/
static void	DChost_get_agent_availability(const ZBX_DC_HOST *dc_host, unsigned char agent_type,
		zbx_agent_availability_t *agent)
{

	agent->flags = ZBX_FLAGS_AGENT_STATUS;

	switch (agent_type)
	{
		case ZBX_AGENT_ZABBIX:
			agent->available = dc_host->available;
			agent->error = zbx_strdup(agent->error, dc_host->error);
			agent->errors_from = dc_host->errors_from;
			agent->disable_until = dc_host->disable_until;
			break;
		case ZBX_AGENT_SNMP:
			agent->available = dc_host->snmp_available;
			agent->error = zbx_strdup(agent->error, dc_host->snmp_error);
			agent->errors_from = dc_host->snmp_errors_from;
			agent->disable_until = dc_host->snmp_disable_until;
			break;
		case ZBX_AGENT_IPMI:
			agent->available = dc_host->ipmi_available;
			agent->error = zbx_strdup(agent->error, dc_host->ipmi_error);
			agent->errors_from = dc_host->ipmi_errors_from;
			agent->disable_until = dc_host->ipmi_disable_until;
			break;
		case ZBX_AGENT_JMX:
			agent->available = dc_host->jmx_available;
			agent->error = zbx_strdup(agent->error, dc_host->jmx_error);
			agent->errors_from = dc_host->jmx_errors_from;
			agent->disable_until = dc_host->jmx_disable_until;
			break;
	}
}

static void	DCagent_set_availability(zbx_agent_availability_t *av,  unsigned char *available, const char **error,
		int *errors_from, int *disable_until)
{
#define AGENT_AVAILABILITY_ASSIGN(flags, mask, dst, src)	\
	if (0 != (flags & mask))				\
	{							\
		if (dst != src)					\
			dst = src;				\
		else						\
			flags &= (~(mask));			\
	}

#define AGENT_AVAILABILITY_ASSIGN_STR(flags, mask, dst, src)	\
	if (0 != (flags & mask))				\
	{							\
		if (0 != strcmp(dst, src))			\
			DCstrpool_replace(1, &dst, src);	\
		else						\
			flags &= (~(mask));			\
	}

	AGENT_AVAILABILITY_ASSIGN(av->flags, ZBX_FLAGS_AGENT_STATUS_AVAILABLE, *available, av->available);
	AGENT_AVAILABILITY_ASSIGN_STR(av->flags, ZBX_FLAGS_AGENT_STATUS_ERROR, *error, av->error);
	AGENT_AVAILABILITY_ASSIGN(av->flags, ZBX_FLAGS_AGENT_STATUS_ERRORS_FROM, *errors_from, av->errors_from);
	AGENT_AVAILABILITY_ASSIGN(av->flags, ZBX_FLAGS_AGENT_STATUS_DISABLE_UNTIL, *disable_until, av->disable_until);

#undef AGENT_AVAILABILITY_ASSIGN_STR
#undef AGENT_AVAILABILITY_ASSIGN
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_set_agent_availability                                    *
 *                                                                            *
 * Purpose: set host availability data in configuration cache                 *
 *                                                                            *
 * Parameters: dc_host      - [OUT] the host                                  *
 *             availability - [IN/OUT] the host availability data             *
 *                                                                            *
 * Return value: SUCCEED - at least one availability field was updated        *
 *               FAIL    - no availability fields were updated                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 *           This function clears availability flags of non updated fields    *
 *           updated leaving only flags identifying changed fields.           *
 *                                                                            *
 ******************************************************************************/
static int	DChost_set_agent_availability(ZBX_DC_HOST *dc_host, int now, unsigned char agent_type,
		zbx_agent_availability_t *agent)
{
	switch (agent_type)
	{
		case ZBX_AGENT_ZABBIX:
			DCagent_set_availability(agent, &dc_host->available,
					&dc_host->error, &dc_host->errors_from, &dc_host->disable_until);
			break;
		case ZBX_AGENT_SNMP:
			DCagent_set_availability(agent, &dc_host->snmp_available,
					&dc_host->snmp_error, &dc_host->snmp_errors_from, &dc_host->snmp_disable_until);
			break;
		case ZBX_AGENT_IPMI:
			DCagent_set_availability(agent, &dc_host->ipmi_available,
					&dc_host->ipmi_error, &dc_host->ipmi_errors_from, &dc_host->ipmi_disable_until);
			break;
		case ZBX_AGENT_JMX:
			DCagent_set_availability(agent, &dc_host->jmx_available,
					&dc_host->jmx_error, &dc_host->jmx_errors_from, &dc_host->jmx_disable_until);
			break;
	}

	if (ZBX_FLAGS_AGENT_STATUS_NONE == agent->flags)
		return FAIL;

	if (0 != (agent->flags & (ZBX_FLAGS_AGENT_STATUS_AVAILABLE | ZBX_FLAGS_AGENT_STATUS_ERROR)))
		dc_host->availability_ts = now;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_set_availability                                          *
 *                                                                            *
 * Purpose: set host availability data in configuration cache                 *
 *                                                                            *
 * Parameters: dc_host      - [OUT] the host                                  *
 *             availability - [IN/OUT] the host availability data             *
 *                                                                            *
 * Return value: SUCCEED - at least one availability field was updated        *
 *               FAIL    - no availability fields were updated                *
 *                                                                            *
 * Comments: The configuration cache must be locked already.                  *
 *                                                                            *
 *           This function clears availability flags of non updated fields    *
 *           updated leaving only flags identifying changed fields.           *
 *                                                                            *
 ******************************************************************************/
static int	DChost_set_availability(ZBX_DC_HOST *dc_host, int now, zbx_host_availability_t *ha)
{
	int		i;
	unsigned char	flags = ZBX_FLAGS_AGENT_STATUS_NONE;

	DCagent_set_availability(&ha->agents[ZBX_AGENT_ZABBIX], &dc_host->available, &dc_host->error,
			&dc_host->errors_from, &dc_host->disable_until);
	DCagent_set_availability(&ha->agents[ZBX_AGENT_SNMP], &dc_host->snmp_available, &dc_host->snmp_error,
			&dc_host->snmp_errors_from, &dc_host->snmp_disable_until);
	DCagent_set_availability(&ha->agents[ZBX_AGENT_IPMI], &dc_host->ipmi_available, &dc_host->ipmi_error,
			&dc_host->ipmi_errors_from, &dc_host->ipmi_disable_until);
	DCagent_set_availability(&ha->agents[ZBX_AGENT_JMX], &dc_host->jmx_available, &dc_host->jmx_error,
			&dc_host->jmx_errors_from, &dc_host->jmx_disable_until);

	for (i = 0; i < ZBX_AGENT_MAX; i++)
		flags |= ha->agents[i].flags;

	if (ZBX_FLAGS_AGENT_STATUS_NONE == flags)
		return FAIL;

	if (0 != (flags & (ZBX_FLAGS_AGENT_STATUS_AVAILABLE | ZBX_FLAGS_AGENT_STATUS_ERROR)))
		dc_host->availability_ts = now;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_availability_init                                       *
 *                                                                            *
 * Purpose: initializes host availability data                                *
 *                                                                            *
 * Parameters: availability - [IN/OUT] host availability data                 *
 *                                                                            *
 ******************************************************************************/
void	zbx_host_availability_init(zbx_host_availability_t *availability, zbx_uint64_t hostid)
{
	memset(availability, 0, sizeof(zbx_host_availability_t));
	availability->hostid = hostid;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_availability_clean                                      *
 *                                                                            *
 * Purpose: releases resources allocated to store host availability data      *
 *                                                                            *
 * Parameters: ha - [IN] host availability data                               *
 *                                                                            *
 ******************************************************************************/
void	zbx_host_availability_clean(zbx_host_availability_t *ha)
{
	int	i;

	for (i = 0; i < ZBX_AGENT_MAX; i++)
		zbx_free(ha->agents[i].error);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_availability_free                                       *
 *                                                                            *
 * Purpose: frees host availability data                                      *
 *                                                                            *
 * Parameters: availability - [IN] host availability data                     *
 *                                                                            *
 ******************************************************************************/
void	zbx_host_availability_free(zbx_host_availability_t *availability)
{
	zbx_host_availability_clean(availability);
	zbx_free(availability);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_agent_availability_init                                      *
 *                                                                            *
 * Purpose: initializes agent availability with the specified data            *
 *                                                                            *
 * Parameters: availability  - [IN/OUT] agent availability data               *
 *             hostid        - [IN] the host identifier                       *
 *             flags         - [IN] the availability flags indicating which   *
 *                                  availability fields to set                *
 *             available     - [IN] the availability data                     *
 *             error         - [IN]                                           *
 *             errors_from   - [IN]                                           *
 *             disable_until - [IN]                                           *
 *                                                                            *
 ******************************************************************************/
static void	zbx_agent_availability_init(zbx_agent_availability_t *agent, unsigned char available, const char *error,
		int errors_from, int disable_until)
{
	agent->flags = ZBX_FLAGS_AGENT_STATUS;
	agent->available = available;
	agent->error = zbx_strdup(agent->error, error);
	agent->errors_from = errors_from;
	agent->disable_until = disable_until;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_availability_is_set                                     *
 *                                                                            *
 * Purpose: checks host availability if any agent availability field is set   *
 *                                                                            *
 * Parameters: availability - [IN] host availability data                     *
 *                                                                            *
 * Return value: SUCCEED - an agent availability field is set                 *
 *               FAIL - no agent availability fields are set                  *
 *                                                                            *
 ******************************************************************************/
int	zbx_host_availability_is_set(const zbx_host_availability_t *ha)
{
	int	i;

	for (i = 0; i < ZBX_AGENT_MAX; i++)
	{
		if (ZBX_FLAGS_AGENT_STATUS_NONE != ha->agents[i].flags)
			return SUCCEED;
	}

	return FAIL;
}

/**************************************************************************************
 *                                                                                    *
 * Host availability update example                                                   *
 *                                                                                    *
 *                                                                                    *
 *               |            UnreachablePeriod                                       *
 *               |               (conf file)                                          *
 *               |              ______________                                        *
 *               |             /              \                                       *
 *               |             p     p     p     p       p       p                    *
 *               |             o     o     o     o       o       o                    *
 *               |             l     l     l     l       l       l                    *
 *               |             l     l     l     l       l       l                    *
 *               | n                                                                  *
 *               | e           e     e     e     e       e       e                    *
 *     agent     | w   p   p   r     r     r     r       r       r       p   p   p    *
 *       polls   |     o   o   r     r     r     r       r       r       o   o   o    *
 *               | h   l   l   o     o     o     o       o       o       l   l   l    *
 *               | o   l   l   r     r     r     r       r       r       l   l   l    *
 *               | s                                                                  *
 *               | t   ok  ok  E1    E1    E2    E1      E1      E2      ok  ok  ok   *
 *  --------------------------------------------------------------------------------  *
 *  available    | 0   1   1   1     1     1     2       2       2       0   0   0    *
 *               |                                                                    *
 *  error        | ""  ""  ""  ""    ""    ""    E1      E1      E2      ""  ""  ""   *
 *               |                                                                    *
 *  errors_from  | 0   0   0   T4    T4    T4    T4      T4      T4      0   0   0    *
 *               |                                                                    *
 *  disable_until| 0   0   0   T5    T6    T7    T8      T9      T10     0   0   0    *
 *  --------------------------------------------------------------------------------  *
 *   timestamps  | T1  T2  T3  T4    T5    T6    T7      T8      T9     T10 T11 T12   *
 *               |  \_/ \_/ \_/ \___/ \___/ \___/ \_____/ \_____/ \_____/ \_/ \_/     *
 *               |   |   |   |    |     |     |      |       |       |     |   |      *
 *  polling      |  item delay   UnreachableDelay    UnavailableDelay     item |      *
 *      periods  |                 (conf file)         (conf file)         delay      *
 *                                                                                    *
 *                                                                                    *
 **************************************************************************************/

/******************************************************************************
 *                                                                            *
 * Function: DChost_activate                                                  *
 *                                                                            *
 * Purpose: set host as available based on the agent availability data        *
 *                                                                            *
 * Parameters: hostid     - [IN] the host identifier                          *
 *             agent_type - [IN] the agent type (see ZBX_AGENT_* defines)     *
 *             ts         - [IN] the last timestamp                           *
 *             in         - [IN/OUT] IN: the caller's agent availability data *
 *                                  OUT: the agent availability data in cache *
 *                                       before changes                       *
 *             out        - [OUT] the agent availability data after changes   *
 *                                                                            *
 * Return value: SUCCEED - the host was activated successfully                *
 *               FAIL    - the host was already activated or activation       *
 *                         failed                                             *
 *                                                                            *
 * Comments: The host availability fields are updated according to the above  *
 *           schema.                                                          *
 *                                                                            *
 ******************************************************************************/
int	DChost_activate(zbx_uint64_t hostid, unsigned char agent_type, const zbx_timespec_t *ts,
		zbx_agent_availability_t *in, zbx_agent_availability_t *out)
{
	int		ret = FAIL;
	ZBX_DC_HOST	*dc_host;

	/* don't try activating host if there were no errors detected */
	if (0 == in->errors_from && HOST_AVAILABLE_TRUE == in->available)
		goto out;

	WRLOCK_CACHE;

	if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* Don't try activating host if:                  */
	/* - (server, proxy) it's not monitored any more; */
	/* - (server) it's monitored by proxy.            */
	if ((0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && 0 != dc_host->proxy_hostid) ||
			HOST_STATUS_MONITORED != dc_host->status)
	{
		goto unlock;
	}

	DChost_get_agent_availability(dc_host, agent_type, in);
	zbx_agent_availability_init(out, HOST_AVAILABLE_TRUE, "", 0, 0);
	DChost_set_agent_availability(dc_host, ts->sec, agent_type, out);

	if (ZBX_FLAGS_AGENT_STATUS_NONE != out->flags)
		ret = SUCCEED;
unlock:
	UNLOCK_CACHE;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DChost_deactivate                                                *
 *                                                                            *
 * Purpose: attempt to set host as unavailable based on agent availability    *
 *                                                                            *
 * Parameters: hostid     - [IN] the host identifier                          *
 *             agent_type - [IN] the agent type (see ZBX_AGENT_* defines)     *
 *             ts         - [IN] the last timestamp                           *
 *             in         - [IN/OUT] IN: the caller's host availability data  *
 *                                  OUT: the host availability data in cache  *
 *                                       before changes                       *
 *             out        - [OUT] the host availability data after changes    *
 *             error_msg  - [IN] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the host was deactivated successfully              *
 *               FAIL    - the host was already deactivated or deactivation   *
 *                         failed                                             *
 *                                                                            *
 * Comments: The host availability fields are updated according to the above  *
 *           schema.                                                          *
 *                                                                            *
 ******************************************************************************/
int	DChost_deactivate(zbx_uint64_t hostid, unsigned char agent_type, const zbx_timespec_t *ts,
		zbx_agent_availability_t *in, zbx_agent_availability_t *out, const char *error_msg)
{
	int		ret = FAIL, errors_from,disable_until;
	const char	*error;
	unsigned char	available;
	ZBX_DC_HOST	*dc_host;


	/* don't try deactivating host if the unreachable delay has not passed since the first error */
	if (CONFIG_UNREACHABLE_DELAY > ts->sec - in->errors_from)
		goto out;

	WRLOCK_CACHE;

	if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &hostid)))
		goto unlock;

	/* Don't try deactivating host if:                */
	/* - (server, proxy) it's not monitored any more; */
	/* - (server) it's monitored by proxy.            */
	if ((0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && 0 != dc_host->proxy_hostid) ||
			HOST_STATUS_MONITORED != dc_host->status)
	{
		goto unlock;
	}

	DChost_get_agent_availability(dc_host, agent_type, in);

	available = in->available;
	error = in->error;

	if (0 == in->errors_from)
	{
		/* first error, schedule next unreachable check */
		errors_from = ts->sec;
		disable_until = ts->sec + CONFIG_UNREACHABLE_DELAY;
	}
	else
	{
		errors_from = in->errors_from;
		disable_until = in->disable_until;

		/* Check if other pollers haven't already attempted deactivating host. */
		/* In that case should wait the initial unreachable delay before       */
		/* trying to make it unavailable.                                      */
		if (CONFIG_UNREACHABLE_DELAY <= ts->sec - errors_from)
		{
			/* repeating error */
			if (CONFIG_UNREACHABLE_PERIOD > ts->sec - errors_from)
			{
				/* leave host available, schedule next unreachable check */
				disable_until = ts->sec + CONFIG_UNREACHABLE_DELAY;
			}
			else
			{
				/* make host unavailable, schedule next unavailable check */
				disable_until = ts->sec + CONFIG_UNAVAILABLE_DELAY;
				available = HOST_AVAILABLE_FALSE;
				error = error_msg;
			}
		}
	}

	zbx_agent_availability_init(out, available, error, errors_from, disable_until);
	DChost_set_agent_availability(dc_host, ts->sec, agent_type, out);

	if (ZBX_FLAGS_AGENT_STATUS_NONE != out->flags)
		ret = SUCCEED;
unlock:
	UNLOCK_CACHE;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCset_hosts_availability                                         *
 *                                                                            *
 * Purpose: update availability of hosts in configuration cache and return    *
 *          the updated field flags                                           *
 *                                                                            *
 * Parameters: availabilities - [IN/OUT] the hosts availability data          *
 *                                                                            *
 * Return value: SUCCEED - at least one host availability data was updated    *
 *               FAIL    - no hosts were updated                              *
 *                                                                            *
 ******************************************************************************/
int	DCset_hosts_availability(zbx_vector_ptr_t *availabilities)
{
	int			i;
	ZBX_DC_HOST		*dc_host;
	zbx_host_availability_t	*ha;
	int			ret = FAIL, now;

	now = time(NULL);

	WRLOCK_CACHE;

	for (i = 0; i < availabilities->values_num; i++)
	{
		ha = (zbx_host_availability_t *)availabilities->values[i];

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &ha->hostid)))
		{
			int	j;

			/* reset availability flags so this host is ignored when saving availability diff to DB */
			for (j = 0; j < ZBX_AGENT_MAX; j++)
				ha->agents[j].flags = ZBX_FLAGS_AGENT_STATUS_NONE;

			continue;
		}

		if (SUCCEED == DChost_set_availability(dc_host, now, ha))
			ret = SUCCEED;
	}

	UNLOCK_CACHE;

	return ret;
}

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
static int	DCconfig_check_trigger_dependencies_rec(const ZBX_DC_TRIGGER_DEPLIST *trigdep, int level,
		const zbx_vector_uint64_t *triggerids, zbx_vector_uint64_t *master_triggerids)
{
	int				i;
	const ZBX_DC_TRIGGER		*next_trigger;
	const ZBX_DC_TRIGGER_DEPLIST	*next_trigdep;

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
					if (TRIGGER_VALUE_PROBLEM == next_trigger->value)
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
 * Function: DCconfig_check_trigger_dependencies                              *
 *                                                                            *
 * Purpose: check whether any of trigger dependencies have value PROBLEM      *
 *                                                                            *
 * Return value: SUCCEED - trigger can change its value                       *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_check_trigger_dependencies(zbx_uint64_t triggerid)
{
	int				ret = SUCCEED;
	const ZBX_DC_TRIGGER_DEPLIST	*trigdep;

	RDLOCK_CACHE;

	if (NULL != (trigdep = (const ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps, &triggerid)))
		ret = DCconfig_check_trigger_dependencies_rec(trigdep, 0, NULL, NULL);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Comments: helper function for DCconfig_sort_triggers_topologically()       *
 *                                                                            *
 ******************************************************************************/
static unsigned char	DCconfig_sort_triggers_topologically_rec(const ZBX_DC_TRIGGER_DEPLIST *trigdep, int level)
{
	int				i;
	unsigned char			topoindex = 2, next_topoindex;
	const ZBX_DC_TRIGGER_DEPLIST	*next_trigdep;

	if (32 < level)
	{
		zabbix_log(LOG_LEVEL_CRIT, "recursive trigger dependency is too deep (triggerid:" ZBX_FS_UI64 ")",
				trigdep->triggerid);
		goto exit;
	}

	if (0 == trigdep->trigger->topoindex)
	{
		zabbix_log(LOG_LEVEL_CRIT, "trigger dependencies contain a cycle (triggerid:" ZBX_FS_UI64 ")",
				trigdep->triggerid);
		goto exit;
	}

	trigdep->trigger->topoindex = 0;

	for (i = 0; i < trigdep->dependencies.values_num; i++)
	{
		next_trigdep = (const ZBX_DC_TRIGGER_DEPLIST *)trigdep->dependencies.values[i];

		if (1 < (next_topoindex = next_trigdep->trigger->topoindex))
			goto next;

		if (0 == next_trigdep->dependencies.values_num)
			continue;

		next_topoindex = DCconfig_sort_triggers_topologically_rec(next_trigdep, level + 1);
next:
		if (topoindex < next_topoindex + 1)
			topoindex = next_topoindex + 1;
	}

	trigdep->trigger->topoindex = topoindex;
exit:
	return topoindex;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_sort_triggers_topologically                             *
 *                                                                            *
 * Purpose: assign each trigger an index based on trigger dependency topology *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
static void	DCconfig_sort_triggers_topologically(void)
{
	zbx_hashset_iter_t		iter;
	ZBX_DC_TRIGGER			*trigger;
	const ZBX_DC_TRIGGER_DEPLIST	*trigdep;

	zbx_hashset_iter_reset(&config->trigdeps, &iter);

	while (NULL != (trigdep = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_iter_next(&iter)))
	{
		trigger = trigdep->trigger;

		if (NULL == trigger || 1 < trigger->topoindex || 0 == trigdep->dependencies.values_num)
			continue;

		DCconfig_sort_triggers_topologically_rec(trigdep, 0);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_triggers_apply_changes                                  *
 *                                                                            *
 * Purpose: apply trigger value,state,lastchange or error changes to          *
 *          configuration cache after committed to database                   *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_triggers_apply_changes(zbx_vector_ptr_t *trigger_diff)
{
	int			i;
	zbx_trigger_diff_t	*diff;
	ZBX_DC_TRIGGER		*dc_trigger;

	if (0 == trigger_diff->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (zbx_trigger_diff_t *)trigger_diff->values[i];

		if (NULL == (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &diff->triggerid)))
			continue;

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE))
			dc_trigger->lastchange = diff->lastchange;

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
			dc_trigger->value = diff->value;

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_STATE))
			dc_trigger->state = diff->state;

		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
			DCstrpool_replace(1, &dc_trigger->error, diff->error);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_stats                                               *
 *                                                                            *
 * Purpose: get statistics of the database cache                              *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
void	*DCconfig_get_stats(int request)
{
	static zbx_uint64_t	value_uint;
	static double		value_double;

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

static void	DCget_proxy(DC_PROXY *dst_proxy, const ZBX_DC_PROXY *src_proxy)
{
	const ZBX_DC_HOST	*host;
	ZBX_DC_INTERFACE_HT	*interface_ht, interface_ht_local;

	dst_proxy->hostid = src_proxy->hostid;
	dst_proxy->proxy_config_nextcheck = src_proxy->proxy_config_nextcheck;
	dst_proxy->proxy_data_nextcheck = src_proxy->proxy_data_nextcheck;
	dst_proxy->proxy_tasks_nextcheck = src_proxy->proxy_tasks_nextcheck;
	dst_proxy->last_cfg_error_time = src_proxy->last_cfg_error_time;
	dst_proxy->version = src_proxy->version;
	dst_proxy->lastaccess = src_proxy->lastaccess;
	dst_proxy->auto_compress = src_proxy->auto_compress;
	dst_proxy->last_version_error_time = src_proxy->last_version_error_time;

	dst_proxy->cluster_id = src_proxy->cluster_id;
	dst_proxy->cluster_topology_version = src_proxy->cluster_topology_version;
	dst_proxy->cluster_state = src_proxy->cluster_state;
	dst_proxy->cluster_lastheard = src_proxy->cluster_lastheard;
	dst_proxy->cluster_failed_hello_count = src_proxy->cluster_failed_hello_count;
	dst_proxy->cluster_rerouted_data = src_proxy->cluster_rerouted_data.values_num;

	if (NULL != (host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &src_proxy->hostid)))
	{
		strscpy(dst_proxy->host, host->host);
		strscpy(dst_proxy->proxy_address, src_proxy->proxy_address);

		dst_proxy->tls_connect = host->tls_connect;
		dst_proxy->tls_accept = host->tls_accept;
		dst_proxy->proxy_type = host->status;
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		strscpy(dst_proxy->tls_issuer, host->tls_issuer);
		strscpy(dst_proxy->tls_subject, host->tls_subject);

		if (NULL == host->tls_dc_psk)
		{
			*dst_proxy->tls_psk_identity = '\0';
			*dst_proxy->tls_psk = '\0';
		}
		else
		{
			strscpy(dst_proxy->tls_psk_identity, host->tls_dc_psk->tls_psk_identity);
			strscpy(dst_proxy->tls_psk, host->tls_dc_psk->tls_psk);
		}
#endif
	}
	else
	{
		/* DCget_proxy() is called only from DCconfig_get_proxypoller_hosts(), which is called only from */
		/* process_proxy(). So, this branch should never happen. */
		*dst_proxy->host = '\0';
		*dst_proxy->proxy_address = '\0';
		dst_proxy->tls_connect = ZBX_TCP_SEC_TLS_PSK;	/* set PSK to deliberately fail in this case */
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		*dst_proxy->tls_psk_identity = '\0';
		*dst_proxy->tls_psk = '\0';
#endif
		THIS_SHOULD_NEVER_HAPPEN;
	}

	interface_ht_local.hostid = src_proxy->hostid;
	interface_ht_local.type = INTERFACE_TYPE_UNKNOWN;

	if (NULL != (interface_ht = (ZBX_DC_INTERFACE_HT *)zbx_hashset_search(&config->interfaces_ht, &interface_ht_local)))
	{
		const ZBX_DC_INTERFACE	*interface = interface_ht->interface_ptr;

		strscpy(dst_proxy->addr_orig, interface->useip ? interface->ip : interface->dns);
		strscpy(dst_proxy->port_orig, interface->port);
	}
	else
	{
		*dst_proxy->addr_orig = '\0';
		*dst_proxy->port_orig = '\0';
	}

	dst_proxy->addr = NULL;
	dst_proxy->port = 0;
}

int	DCconfig_get_last_sync_time(void)
{
	return config->sync_ts;
}

void	DCconfig_wait_sync(void)
{
	struct timespec	ts = {0, 1e8};

	while (0 == config->sync_ts)
		nanosleep(&ts, NULL);
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_proxypoller_hosts                                   *
 *                                                                            *
 * Purpose: Get array of proxies for proxy poller                             *
 *                                                                            *
 * Parameters: hosts - [OUT] array of hosts                                   *
 *             max_hosts - [IN] elements in hosts array                       *
 *                                                                            *
 * Return value: number of proxies in hosts array                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: Proxies leave the queue only through this function. Pollers must *
 *           always return the proxies they have taken using DCrequeue_proxy. *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_proxypoller_hosts(DC_PROXY *proxies, int max_hosts)
{
	int			now, num = 0;
	zbx_binary_heap_t	*queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	queue = &config->pqueue;

	WRLOCK_CACHE;

	while (num < max_hosts && FAIL == zbx_binary_heap_empty(queue))
	{
		const zbx_binary_heap_elem_t	*min;
		ZBX_DC_PROXY			*dc_proxy;

		min = zbx_binary_heap_find_min(queue);
		dc_proxy = (ZBX_DC_PROXY *)min->data;

		if (dc_proxy->nextcheck > now)
			break;

		zbx_binary_heap_remove_min(queue);
		dc_proxy->location = ZBX_LOC_POLLER;

		DCget_proxy(&proxies[num], dc_proxy);
		num++;
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_get_proxypoller_nextcheck                               *
 *                                                                            *
 * Purpose: Get nextcheck for passive proxies                                 *
 *                                                                            *
 * Return value: nextcheck or FAIL if no passive proxies in queue             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	DCconfig_get_proxypoller_nextcheck(void)
{
	int			nextcheck;
	zbx_binary_heap_t	*queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	queue = &config->pqueue;

	RDLOCK_CACHE;

	if (FAIL == zbx_binary_heap_empty(queue))
	{
		const zbx_binary_heap_elem_t	*min;
		const ZBX_DC_PROXY		*dc_proxy;

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

void	DCrequeue_proxy(zbx_uint64_t hostid, unsigned char update_nextcheck, int proxy_conn_err)
{
	time_t		now;
	ZBX_DC_HOST	*dc_host;
	ZBX_DC_PROXY	*dc_proxy;

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

		if (HOST_STATUS_PROXY_PASSIVE == dc_host->status || 
			HOST_STATUS_SERVER == dc_host->status )
		{
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
			if (0 != (update_nextcheck & ZBX_PROXY_HELLO_NEXTSEND))
			{
				int interval = ZBX_CLUSTER_HELLO_FREQUENCY;
		
				dc_proxy->server_hello_nextsend = (int)calculate_proxy_nextcheck(
						hostid, interval, now);

			}

			DCupdate_proxy_queue(dc_proxy);
		}
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	dc_get_host_macro_value(const ZBX_DC_HMACRO *macro, char **value)
{
	if (ZBX_MACRO_ENV_NONSECURE == macro_env && (ZBX_MACRO_VALUE_SECRET == macro->type ||
			ZBX_MACRO_VALUE_VAULT == macro->type))
	{
		*value = zbx_strdup(*value, ZBX_MACRO_SECRET_MASK);
	}
	else if (ZBX_MACRO_VALUE_VAULT == macro->type)
	{
		if (NULL == macro->kv->value)
			*value = zbx_strdup(*value, ZBX_MACRO_SECRET_MASK);
		else
			*value = zbx_strdup(*value, macro->kv->value);
	}
	else
		*value = zbx_strdup(*value, macro->value);
}

static int	dc_match_macro_context(const char *context, const char *pattern, unsigned char op)
{
	switch (op)
	{
		case CONDITION_OPERATOR_EQUAL:
			return 0 == zbx_strcmp_null(context, pattern) ? SUCCEED : FAIL;
		case CONDITION_OPERATOR_REGEXP:
			if (NULL == context)
				return FAIL;
			return NULL != zbx_regexp_match(context, pattern, NULL) ? SUCCEED : FAIL;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
}

static void	dc_get_host_macro(const zbx_uint64_t *hostids, int host_num, const char *macro, const char *context,
		char **value, char **value_default)
{
	int			i, j;
	const ZBX_DC_HMACRO_HM	*hmacro_hm;
	ZBX_DC_HMACRO_HM	hmacro_hm_local;
	const ZBX_DC_HTMPL	*htmpl;
	zbx_vector_uint64_t	templateids;
	const ZBX_DC_HMACRO	*hmacro;

	if (0 == host_num)
		return;

	hmacro_hm_local.macro = macro;

	for (i = 0; i < host_num; i++)
	{
		hmacro_hm_local.hostid = hostids[i];

		if (NULL != (hmacro_hm = (const ZBX_DC_HMACRO_HM *)zbx_hashset_search(&config->hmacros_hm, &hmacro_hm_local)))
		{
			for (j = 0; j < hmacro_hm->hmacros.values_num; j++)
			{
				hmacro = (const ZBX_DC_HMACRO *)hmacro_hm->hmacros.values[j];

				if (SUCCEED == dc_match_macro_context(context, hmacro->context, hmacro->context_op))
				{
					dc_get_host_macro_value(hmacro, value);
					return;
				}
			}
			/* Check for the default (without context) macro value. If macro has a value without */
			/* context it will be the first element in the macro index vector.                   */
			hmacro = (const ZBX_DC_HMACRO *)hmacro_hm->hmacros.values[0];
			if (NULL == *value_default && NULL != context && NULL == hmacro->context)
				dc_get_host_macro_value(hmacro, value_default);
		}
	}

	zbx_vector_uint64_create(&templateids);
	zbx_vector_uint64_reserve(&templateids, 32);

	for (i = 0; i < host_num; i++)
	{
		if (NULL != (htmpl = (const ZBX_DC_HTMPL *)zbx_hashset_search(&config->htmpls, &hostids[i])))
		{
			for (j = 0; j < htmpl->templateids.values_num; j++)
				zbx_vector_uint64_append(&templateids, htmpl->templateids.values[j]);
		}
	}

	if (0 != templateids.values_num)
	{
		zbx_vector_uint64_sort(&templateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		dc_get_host_macro(templateids.values, templateids.values_num, macro, context, value, value_default);
	}

	zbx_vector_uint64_destroy(&templateids);
}

static void	dc_get_global_macro_value(const ZBX_DC_GMACRO *macro, char **value)
{
	if (ZBX_MACRO_ENV_NONSECURE == macro_env && (ZBX_MACRO_VALUE_SECRET == macro->type ||
			ZBX_MACRO_VALUE_VAULT == macro->type))
	{
		*value = zbx_strdup(*value, ZBX_MACRO_SECRET_MASK);
	}
	else if (ZBX_MACRO_VALUE_VAULT == macro->type)
	{
		if (NULL == macro->kv->value)
			*value = zbx_strdup(*value, ZBX_MACRO_SECRET_MASK);
		else
			*value = zbx_strdup(*value, macro->kv->value);
	}
	else
		*value = zbx_strdup(*value, macro->value);
}

static void	dc_get_global_macro(const char *macro, const char *context, char **value, char **value_default)
{
	int			i;
	const ZBX_DC_GMACRO_M	*gmacro_m;
	ZBX_DC_GMACRO_M		gmacro_m_local;
	const ZBX_DC_GMACRO	*gmacro;

	gmacro_m_local.macro = macro;

	if (NULL != (gmacro_m = (const ZBX_DC_GMACRO_M *)zbx_hashset_search(&config->gmacros_m, &gmacro_m_local)))
	{
		for (i = 0; i < gmacro_m->gmacros.values_num; i++)
		{
			gmacro = (const ZBX_DC_GMACRO *)gmacro_m->gmacros.values[i];

			if (SUCCEED == dc_match_macro_context(context, gmacro->context, gmacro->context_op))
			{
				dc_get_global_macro_value(gmacro, value);
				break;
			}
		}

		/* Check for the default (without context) macro value. If macro has a value without */
		/* context it will be the first element in the macro index vector.                   */
		gmacro = (const ZBX_DC_GMACRO *)gmacro_m->gmacros.values[0];
		if (NULL == *value_default && NULL != context && NULL == gmacro->context)
			dc_get_global_macro_value(gmacro, value_default);
	}
}

static void	dc_get_user_macro(const zbx_uint64_t *hostids, int hostids_num, const char *macro, const char *context,
		char **replace_to)
{
	char	*value = NULL, *value_default = NULL;

	/* User macros should be expanded according to the following priority: */
	/*                                                                     */
	/*  1) host context macro                                              */
	/*  2) global context macro                                            */
	/*  3) host base (default) macro                                       */
	/*  4) global base (default) macro                                     */
	/*                                                                     */
	/* We try to expand host macros first. If there is no perfect match on */
	/* the host level, we try to expand global macros, passing the default */
	/* macro value found on the host level, if any.                        */

	dc_get_host_macro(hostids, hostids_num, macro, context, &value, &value_default);

	if (NULL == value)
		dc_get_global_macro(macro, context, &value, &value_default);

	if (NULL != value)
	{
		zbx_free(*replace_to);
		*replace_to = value;

		zbx_free(value_default);
	}
	else if (NULL != value_default)
	{
		zbx_free(*replace_to);
		*replace_to = value_default;
	}
}

void	DCget_user_macro(const zbx_uint64_t *hostids, int hostids_num, const char *macro, char **replace_to)
{
	char	*name = NULL, *context = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() macro:'%s'", __func__, macro);

	if (SUCCEED != zbx_user_macro_parse_dyn(macro, &name, &context, NULL, NULL))
		goto out;

	RDLOCK_CACHE;

	dc_get_user_macro(hostids, hostids_num, name, context, replace_to);

	UNLOCK_CACHE;

	zbx_free(context);
	zbx_free(name);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expand_user_macros                                            *
 *                                                                            *
 * Purpose: expand user macros in the specified text value                    *
 * WARNING - DO NOT USE FOR TRIGGERS, for triggers use the dedicated function *
 *                                                                            *
 * Parameters: text           - [IN] the text value to expand                 *
 *             hostids        - [IN] an array of related hostids              *
 *             hostids_num    - [IN] the number of hostids                    *
 *                                                                            *
 * Return value: The text value with expanded user macros. Unknown or invalid *
 *               macros will be left unresolved.                              *
 *                                                                            *
 * Comments: The returned value must be freed by the caller.                  *
 *           This function must be used only by configuration syncer          *
 *                                                                            *
 ******************************************************************************/
char	*dc_expand_user_macros(const char *text, zbx_uint64_t *hostids, int hostids_num)
{
	zbx_token_t	token;
	int		pos = 0, len, last_pos = 0;
	char		*str = NULL, *name = NULL, *context = NULL, *value = NULL;
	size_t		str_alloc = 0, str_offset = 0;

	if ('\0' == *text)
		return zbx_strdup(NULL, text);

	for (; SUCCEED == zbx_token_find(text, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		if (ZBX_TOKEN_USER_MACRO != token.type)
			continue;

		if (SUCCEED != zbx_user_macro_parse_dyn(text + token.loc.l, &name, &context, &len, NULL))
			continue;

		zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + last_pos, token.loc.l - last_pos);
		dc_get_user_macro(hostids, hostids_num, name, context, &value);

		if (NULL != value)
		{
			zbx_strcpy_alloc(&str, &str_alloc, &str_offset, value);
			zbx_free(value);

		}
		else
		{
			zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + token.loc.l,
					token.loc.r - token.loc.l + 1);
		}

		zbx_free(name);
		zbx_free(context);

		pos = token.loc.r;
		last_pos = pos + 1;
	}

	zbx_strcpy_alloc(&str, &str_alloc, &str_offset, text + last_pos);

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_expand_user_macros                                        *
 *                                                                            *
 * Purpose: expand user macros in the specified text value                    *
 *                                                                            *
 * Parameters: text           - [IN] the text value to expand                 *
 *             hostid         - [IN] related hostid                           *
 *                                                                            *
 * Return value: The text value with expanded user macros. Unknown or invalid *
 *               macros will be left unresolved.                              *
 *                                                                            *
 * Comments: The returned value must be freed by the caller.                  *
 *                                                                            *
 ******************************************************************************/
char	*zbx_dc_expand_user_macros(const char *text, zbx_uint64_t hostid)
{
	char	*resolved_text;

	RDLOCK_CACHE;
	resolved_text = dc_expand_user_macros(text, &hostid, 1);
	UNLOCK_CACHE;

	return resolved_text;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expand_user_macros_in_expression                              *
 *                                                                            *
 * Purpose: expand user macros for triggers and calculated items in the       *
 *          specified text value and autoquote macros that are not already    *
 *          quoted that cannot be casted to a double                          *
 *                                                                            *
 * Parameters: text           - [IN] the text value to expand                 *
 *             hostids        - [IN] an array of related hostids              *
 *             hostids_num    - [IN] the number of hostids                    *
 *                                                                            *
 * Return value: The text value with expanded user macros. Unknown or invalid *
 *               macros will be left unresolved.                              *
 *                                                                            *
 * Comments: The returned value must be freed by the caller.                  *
 *                                                                            *
 ******************************************************************************/
char	*dc_expand_user_macros_in_expression(const char *text, zbx_uint64_t *hostids, int hostids_num)
{
	zbx_token_t	token;
	int		pos = 0, last_pos = 0, cur_token_inside_quote = 0, prev_token_loc_r = -1, len;
	char		*str = NULL, *name = NULL, *context = NULL, *value = NULL;
	size_t		str_alloc = 0, str_offset = 0, i;

	if ('\0' == *text)
		return zbx_strdup(NULL, text);

	for (; SUCCEED == zbx_token_find(text, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		for (i = prev_token_loc_r + 1; i < token.loc.l; i++)
		{
			switch (text[i])
			{
				case '\\':
					if (0 != cur_token_inside_quote)
						i++;
					break;
				case '"':
					cur_token_inside_quote = !cur_token_inside_quote;
					break;
			}
		}

		if (ZBX_TOKEN_USER_MACRO != token.type)
		{
			prev_token_loc_r = token.loc.r;
			continue;
		}

		if (SUCCEED != zbx_user_macro_parse_dyn(text + token.loc.l, &name, &context, &len, NULL))
		{
			prev_token_loc_r = token.loc.r;
			continue;
		}

		zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + last_pos, token.loc.l - last_pos);
		dc_get_user_macro(hostids, hostids_num, name, context, &value);

		if (NULL != value)
		{
			size_t	sz;
			char	*tmp;

			sz = zbx_get_escape_string_len(value, "\"\\");

			if (0 == cur_token_inside_quote && ZBX_INFINITY == evaluate_string_to_double(value))
			{
				/* autoquote */
				tmp = zbx_malloc(NULL, sz + 3);
				tmp[0] = '\"';
				zbx_escape_string(tmp + 1, sz + 1, value, "\"\\");
				tmp[sz + 1] = '\"';
				tmp[sz + 2] = '\0';
				zbx_free(value);
				value = tmp;
			}
			else
			{
				if (sz != strlen(value))
				{
					tmp = zbx_malloc(NULL, sz + 1);
					zbx_escape_string(tmp, sz + 1, value, "\"\\");
					tmp[sz] = '\0';
					zbx_free(value);
					value = tmp;
				}
			}
		}

		if (NULL != value)
		{
			zbx_strcpy_alloc(&str, &str_alloc, &str_offset, value);
			zbx_free(value);
		}
		else
		{
			zbx_strncpy_alloc(&str, &str_alloc, &str_offset, text + token.loc.l,
					token.loc.r - token.loc.l + 1);
		}

		zbx_free(name);
		zbx_free(context);

		pos = token.loc.r;
		last_pos = pos + 1;
		prev_token_loc_r = token.loc.r;
	}

	zbx_strcpy_alloc(&str, &str_alloc, &str_offset, text + last_pos);

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expression_expand_user_macros                                 *
 *                                                                            *
 * Purpose: expand user macros in trigger expression                          *
 *                                                                            *
 * Parameters: expression - [IN] the expression to expand                     *
 *             error      - [OUT] the error message                           *
 *                                                                            *
 * Return value: The expanded expression or NULL in the case of error.        *
 *               If NULL is returned the error message is set.                *
 *                                                                            *
 * Comments: The returned expression must be freed by the caller.             *
 *                                                                            *
 ******************************************************************************/
static char	*dc_expression_expand_user_macros(const char *expression)
{
	zbx_vector_uint64_t	functionids, hostids;
	char			*out;

	zbx_vector_uint64_create(&functionids);
	zbx_vector_uint64_create(&hostids);

	get_functionids(&functionids, expression);
	dc_get_hostids_by_functionids(functionids.values, functionids.values_num, &hostids);

	out = dc_expand_user_macros_in_expression(expression, hostids.values, hostids.values_num);

	if (NULL != strstr(out, "{$"))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot evaluate expression: invalid macro value");
		zbx_free(out);
	}

	zbx_vector_uint64_destroy(&hostids);
	zbx_vector_uint64_destroy(&functionids);

	return out;
}

/******************************************************************************
 *                                                                            *
 * Function: DCexpression_expand_user_macros                                  *
 *                                                                            *
 * Purpose: expand user macros in trigger expression                          *
 *                                                                            *
 * Parameters: expression - [IN] the expression to expand                     *
 *                                                                            *
 * Return value: The expanded expression or NULL in the case of error.        *
 *               If NULL is returned the error message is set.                *
 *                                                                            *
 * Comments: The returned expression must be freed by the caller.             *
 *           This function is a locking wrapper of                            *
 *           dc_expression_expand_user_macros() function for external usage.  *
 *                                                                            *
 ******************************************************************************/
char	*DCexpression_expand_user_macros(const char *expression)
{
	char	*expression_ex;

	RDLOCK_CACHE;

	expression_ex = dc_expression_expand_user_macros(expression);

	UNLOCK_CACHE;

	return expression_ex;
}

/******************************************************************************
 *                                                                            *
 * Function: DCfree_item_queue                                                *
 *                                                                            *
 * Purpose: frees the item queue data vector created by DCget_item_queue()    *
 *                                                                            *
 * Parameters: queue - [IN] the item queue data vector to free                *
 *                                                                            *
 ******************************************************************************/
void	DCfree_item_queue(zbx_vector_ptr_t *queue)
{
	int	i;

	for (i = 0; i < queue->values_num; i++)
		zbx_free(queue->values[i]);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_item_queue                                                 *
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
int	DCget_item_queue(zbx_vector_ptr_t *queue, int from, int to)
{
	zbx_hashset_iter_t	iter;
	const ZBX_DC_ITEM	*dc_item;
	int			now, nitems = 0, data_expected_from, delay;
	zbx_queue_item_t	*queue_item;

	now = time(NULL);

	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
	{
		const ZBX_DC_HOST	*dc_host;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (SUCCEED != zbx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			continue;

		if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (SUCCEED == DCin_maintenance_without_data_collection(dc_host, dc_item))
			continue;

		switch (dc_item->type)
		{
			case ITEM_TYPE_ZABBIX:
				if (HOST_AVAILABLE_TRUE != dc_host->available)
					continue;
				break;
			case ITEM_TYPE_ZABBIX_ACTIVE:
				if (dc_host->data_expected_from > (data_expected_from = dc_item->data_expected_from))
					data_expected_from = dc_host->data_expected_from;
				if (SUCCEED != zbx_interval_preproc(dc_item->delay, &delay, NULL, NULL))
					continue;
				if (data_expected_from + delay > now)
					continue;
				break;
			case ITEM_TYPE_SNMP:
				if (HOST_AVAILABLE_TRUE != dc_host->snmp_available)
					continue;
				break;
			case ITEM_TYPE_IPMI:
				if (HOST_AVAILABLE_TRUE != dc_host->ipmi_available)
					continue;
				break;
			case ITEM_TYPE_JMX:
				if (HOST_AVAILABLE_TRUE != dc_host->jmx_available)
					continue;
				break;
		}

		if (now - dc_item->nextcheck < from || (ZBX_QUEUE_TO_INFINITY != to && now - dc_item->nextcheck >= to))
			continue;

		if (NULL != queue)
		{
			queue_item = (zbx_queue_item_t *)zbx_malloc(NULL, sizeof(zbx_queue_item_t));
			queue_item->itemid = dc_item->itemid;
			queue_item->type = dc_item->type;
			queue_item->nextcheck = dc_item->nextcheck;
			queue_item->proxy_hostid = dc_host->proxy_hostid;
			
			zbx_vector_ptr_append(queue, queue_item);

		}
		nitems++;
	}

	UNLOCK_CACHE;

	return nitems;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trigger_items_hosts_enabled                                   *
 *                                                                            *
 * Purpose: check that functionids in trigger (recovery) expression           *
 *          correspond to enabled items and hosts                             *
 *                                                                            *
 * Parameters: expression - [IN] trigger (recovery) expression                *
 *                                                                            *
 * Return value: SUCCEED - all functionids correspond to enabled items and    *
 *                           enabled hosts                                    *
 *               FAIL    - at least one item or host is disabled              *
 *                                                                            *
 ******************************************************************************/
static int	dc_trigger_items_hosts_enabled(const char *expression)
{
	zbx_uint64_t		functionid;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_FUNCTION	*dc_function;
	const ZBX_DC_HOST	*dc_host;
	const char		*p, *q;

	for (p = expression; '\0' != *p; p++)
	{
		if ('{' != *p)
			continue;

		if ('$' == p[1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == zbx_user_macro_parse(p, &macro_r, &context_l, &context_r, NULL))
				p += macro_r;
			else
				p++;

			continue;
		}

		if (NULL == (q = strchr(p + 1, '}')))
			return FAIL;

		if (SUCCEED != is_uint64_n(p + 1, q - p - 1, &functionid))
			continue;

		if (NULL == (dc_function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionid)) ||
				NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &dc_function->itemid)) ||
				ITEM_STATUS_ACTIVE != dc_item->status ||
				NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)) ||
				HOST_STATUS_MONITORED != dc_host->status)
		{
			return FAIL;
		}

		p = q;
	}

	return SUCCEED;
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
static void	dc_status_update(void)
{
#define ZBX_STATUS_LIFETIME	SEC_PER_MIN

	zbx_hashset_iter_t	iter;
	ZBX_DC_PROXY		*dc_proxy;
	ZBX_DC_HOST		*dc_host, *dc_proxy_host;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_TRIGGER	*dc_trigger;

	if (0 != config->status->last_update && config->status->last_update + ZBX_STATUS_LIFETIME > time(NULL))
		return;

	/* reset global counters */

	config->status->hosts_monitored = 0;
	config->status->hosts_not_monitored = 0;
	config->status->items_active_normal = 0;
	config->status->items_active_notsupported = 0;
	config->status->items_disabled = 0;
	config->status->triggers_enabled_ok = 0;
	config->status->triggers_enabled_problem = 0;
	config->status->triggers_disabled = 0;
	config->status->required_performance = 0.0;

	/* loop over proxies to reset per-proxy host and required performance counters */

	zbx_hashset_iter_reset(&config->proxies, &iter);

	while (NULL != (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
	{
		dc_proxy->hosts_monitored = 0;
		dc_proxy->hosts_not_monitored = 0;
		dc_proxy->required_performance = 0.0;
	}

	/* loop over hosts */

	zbx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (dc_host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		/* reset per-host/per-proxy item counters */

		dc_host->items_active_normal = 0;
		dc_host->items_active_notsupported = 0;
		dc_host->items_disabled = 0;

		/* gather per-proxy statistics of enabled and disabled hosts */
		switch (dc_host->status)
		{
			case HOST_STATUS_MONITORED:
				config->status->hosts_monitored++;
				if (0 == dc_host->proxy_hostid)
					break;
				if (NULL == (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->proxy_hostid)))
					break;
				dc_proxy->hosts_monitored++;
				break;
			case HOST_STATUS_NOT_MONITORED:
				config->status->hosts_not_monitored++;
				if (0 == dc_host->proxy_hostid)
					break;
				if (NULL == (dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->proxy_hostid)))
					break;
				dc_proxy->hosts_not_monitored++;
				break;
		}
	}

	/* loop over items to gather per-host and per-proxy statistics */

	zbx_hashset_iter_reset(&config->items, &iter);

	while (NULL != (dc_item = (ZBX_DC_ITEM *)zbx_hashset_iter_next(&iter)))
	{
		dc_proxy = NULL;
		dc_proxy_host = NULL;

		if (ZBX_FLAG_DISCOVERY_NORMAL != dc_item->flags && ZBX_FLAG_DISCOVERY_CREATED != dc_item->flags)
			continue;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (0 != dc_host->proxy_hostid)
		{
			dc_proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &dc_host->proxy_hostid);
			dc_proxy_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_host->proxy_hostid);
		}

		switch (dc_item->status)
		{
			case ITEM_STATUS_ACTIVE:
				if (HOST_STATUS_MONITORED == dc_host->status)
				{
					int	delay;

					if (SUCCEED == zbx_interval_preproc(dc_item->delay, &delay, NULL, NULL) &&
							0 != delay)
					{
						config->status->required_performance += 1.0 / delay;

						if (NULL != dc_proxy)
							dc_proxy->required_performance += 1.0 / delay;
					}

					switch (dc_item->state)
					{
						case ITEM_STATE_NORMAL:
							config->status->items_active_normal++;
							dc_host->items_active_normal++;
							if (NULL != dc_proxy_host)
								dc_proxy_host->items_active_normal++;
							break;
						case ITEM_STATE_NOTSUPPORTED:
							config->status->items_active_notsupported++;
							dc_host->items_active_notsupported++;
							if (NULL != dc_proxy_host)
								dc_proxy_host->items_active_notsupported++;
							break;
						default:
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

	/* loop over triggers to gather enabled and disabled trigger statistics */

	zbx_hashset_iter_reset(&config->triggers, &iter);

	while (NULL != (dc_trigger = (ZBX_DC_TRIGGER *)zbx_hashset_iter_next(&iter)))
	{
		switch (dc_trigger->status)
		{
			case TRIGGER_STATUS_ENABLED:
				if (SUCCEED == dc_trigger_items_hosts_enabled(dc_trigger->expression) &&
						(TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION != dc_trigger->recovery_mode ||
						SUCCEED == dc_trigger_items_hosts_enabled(dc_trigger->recovery_expression)))
				{
					switch (dc_trigger->value)
					{
						case TRIGGER_VALUE_OK:
							config->status->triggers_enabled_ok++;
							break;
						case TRIGGER_VALUE_PROBLEM:
							config->status->triggers_enabled_problem++;
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

	config->status->last_update = time(NULL);

#undef ZBX_STATUS_LIFETIME
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_item_count                                                 *
 *                                                                            *
 * Purpose: return the number of active items                                 *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active items                                   *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	DCget_item_count(zbx_uint64_t hostid)
{
	zbx_uint64_t		count;
	const ZBX_DC_HOST	*dc_host;

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
 * Function: DCget_item_unsupported_count                                     *
 *                                                                            *
 * Purpose: return the number of active unsupported items                     *
 *                                                                            *
 * Parameters: hostid - [IN] the host id, pass 0 to specify all hosts         *
 *                                                                            *
 * Return value: the number of active unsupported items                       *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	DCget_item_unsupported_count(zbx_uint64_t hostid)
{
	zbx_uint64_t		count;
	const ZBX_DC_HOST	*dc_host;

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
 * Function: DCget_trigger_count                                              *
 *                                                                            *
 * Purpose: count active triggers                                             *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	DCget_trigger_count(void)
{
	zbx_uint64_t	count;

	WRLOCK_CACHE;

	dc_status_update();

	count = config->status->triggers_enabled_ok + config->status->triggers_enabled_problem;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_count                                                 *
 *                                                                            *
 * Purpose: count monitored and not monitored hosts                           *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	DCget_host_count(void)
{
	zbx_uint64_t	nhosts;

	WRLOCK_CACHE;

	dc_status_update();

	nhosts = config->status->hosts_monitored;

	UNLOCK_CACHE;

	return nhosts;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_required_performance                                       *
 *                                                                            *
 * Return value: the required nvps number                                     *
 *                                                                            *
 ******************************************************************************/
double	DCget_required_performance(void)
{
	double	nvps;

	WRLOCK_CACHE;

	dc_status_update();

	nvps = config->status->required_performance;

	UNLOCK_CACHE;

	return nvps;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_count_stats_all                                            *
 *                                                                            *
 * Purpose: retrieves all internal metrics of the configuration cache         *
 *                                                                            *
 * Parameters: stats - [OUT] the configuration cache statistics               *
 *                                                                            *
 ******************************************************************************/
void	DCget_count_stats_all(zbx_config_cache_info_t *stats)
{
	WRLOCK_CACHE;

	dc_status_update();

	stats->hosts = config->status->hosts_monitored;
	stats->items = config->status->items_active_normal + config->status->items_active_notsupported;
	stats->items_unsupported = config->status->items_active_notsupported;
	stats->requiredperformance = config->status->required_performance;

	UNLOCK_CACHE;
}

static void	proxy_counter_ui64_push(zbx_vector_ptr_t *vector, zbx_uint64_t proxyid, zbx_uint64_t counter)
{
	zbx_proxy_counter_t	*proxy_counter;

	proxy_counter = (zbx_proxy_counter_t *)zbx_malloc(NULL, sizeof(zbx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.ui64 = counter;
	zbx_vector_ptr_append(vector, proxy_counter);
}

static void	proxy_counter_dbl_push(zbx_vector_ptr_t *vector, zbx_uint64_t proxyid, double counter)
{
	zbx_proxy_counter_t	*proxy_counter;

	proxy_counter = (zbx_proxy_counter_t *)zbx_malloc(NULL, sizeof(zbx_proxy_counter_t));
	proxy_counter->proxyid = proxyid;
	proxy_counter->counter_value.dbl = counter;
	zbx_vector_ptr_append(vector, proxy_counter);
}

void	DCget_status(zbx_vector_ptr_t *hosts_monitored, zbx_vector_ptr_t *hosts_not_monitored,
		zbx_vector_ptr_t *items_active_normal, zbx_vector_ptr_t *items_active_notsupported,
		zbx_vector_ptr_t *items_disabled, zbx_uint64_t *triggers_enabled_ok,
		zbx_uint64_t *triggers_enabled_problem, zbx_uint64_t *triggers_disabled,
		zbx_vector_ptr_t *required_performance)
{
	zbx_hashset_iter_t	iter;
	const ZBX_DC_PROXY	*dc_proxy;
	const ZBX_DC_HOST	*dc_proxy_host;

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
 * Function: DCget_expressions_by_names                                       *
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
void	DCget_expressions_by_names(zbx_vector_ptr_t *expressions, const char * const *names, int names_num)
{
	int			i, iname;
	const ZBX_DC_EXPRESSION	*expression;
	const ZBX_DC_REGEXP	*regexp;
	ZBX_DC_REGEXP		search_regexp;

	RDLOCK_CACHE;

	for (iname = 0; iname < names_num; iname++)
	{
		search_regexp.name = names[iname];

		if (NULL != (regexp = (const ZBX_DC_REGEXP *)zbx_hashset_search(&config->regexps, &search_regexp)))
		{
			for (i = 0; i < regexp->expressionids.values_num; i++)
			{
				zbx_uint64_t		expressionid = regexp->expressionids.values[i];
				zbx_expression_t	*rxp;

				if (NULL == (expression = (const ZBX_DC_EXPRESSION *)zbx_hashset_search(&config->expressions, &expressionid)))
					continue;

				rxp = (zbx_expression_t *)zbx_malloc(NULL, sizeof(zbx_expression_t));
				rxp->name = zbx_strdup(NULL, regexp->name);
				rxp->expression = zbx_strdup(NULL, expression->expression);
				rxp->exp_delimiter = expression->delimiter;
				rxp->case_sensitive = expression->case_sensitive;
				rxp->expression_type = expression->type;

				zbx_vector_ptr_append(expressions, rxp);
			}
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_expression                                                 *
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
void	DCget_expressions_by_name(zbx_vector_ptr_t *expressions, const char *name)
{
	DCget_expressions_by_names(expressions, &name, 1);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_data_expected_from                                         *
 *                                                                            *
 * Purpose: Returns time since which data is expected for the given item. We  *
 *          would not mind not having data for the item before that time, but *
 *          since that time we expect data to be coming.                      *
 *                                                                            *
 * Parameters: itemid  - [IN] the item id                                     *
 *             seconds - [OUT] the time data is expected as a Unix timestamp  *
 *                                                                            *
 ******************************************************************************/
int	DCget_data_expected_from(zbx_uint64_t itemid, int *seconds)
{
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;
	int			ret = FAIL;

	RDLOCK_CACHE;

	if (NULL == (dc_item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		goto unlock;

	if (ITEM_STATUS_ACTIVE != dc_item->status)
		goto unlock;

	if (NULL == (dc_host = (const ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		goto unlock;

	if (HOST_STATUS_MONITORED != dc_host->status)
		goto unlock;

	if (ZBX_CLUSTER_HOST_STATE_ACTIVE != dc_host->cluster_state)
		goto unlock;

	*seconds = MAX(dc_item->data_expected_from, dc_host->data_expected_from);

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_hostids_by_functionids                                    *
 *                                                                            *
 * Purpose: get host identifiers for the specified list of functions          *
 *                                                                            *
 * Parameters: functionids     - [IN] the function ids                        *
 *             functionids_num - [IN] the number of function ids              *
 *             hostids         - [OUT] the host ids                           *
 *                                                                            *
 * Comments: this function must be used only by configuration syncer          *
 *                                                                            *
 ******************************************************************************/
void	dc_get_hostids_by_functionids(const zbx_uint64_t *functionids, int functionids_num,
		zbx_vector_uint64_t *hostids)
{
	const ZBX_DC_FUNCTION	*function;
	const ZBX_DC_ITEM	*item;
	int			i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (function = (const ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionids[i])))
				continue;

		if (NULL != (item = (const ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
			zbx_vector_uint64_append(hostids, item->hostid);
	}

	zbx_vector_uint64_sort(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_hostids_by_functionids                                     *
 *                                                                            *
 * Purpose: get function host ids grouped by an object (trigger) id           *
 *                                                                            *
 * Parameters: functionids - [IN] the function ids                            *
 *             hostids     - [OUT] the host ids                               *
 *                                                                            *
 ******************************************************************************/
void	DCget_hostids_by_functionids(zbx_vector_uint64_t *functionids, zbx_vector_uint64_t *hostids)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	dc_get_hostids_by_functionids(functionids->values, functionids->values_num, hostids);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hostids->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_hosts_by_functionids                                      *
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids     - [IN] the function ids                        *
 *             functionids_num - [IN] the number of function ids              *
 *             hosts           - [OUT] hosts                                  *
 *                                                                            *
 ******************************************************************************/
static void	dc_get_hosts_by_functionids(const zbx_uint64_t *functionids, int functionids_num, zbx_hashset_t *hosts)
{
	const ZBX_DC_FUNCTION	*dc_function;
	const ZBX_DC_ITEM	*dc_item;
	const ZBX_DC_HOST	*dc_host;
	DC_HOST			host;
	int			i;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (dc_function = (const ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions, &functionids[i])))
			continue;

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
 * Function: DCget_hosts_by_functionids                                       *
 *                                                                            *
 * Purpose: get hosts for the specified list of functions                     *
 *                                                                            *
 * Parameters: functionids - [IN] the function ids                            *
 *             hosts       - [OUT] hosts                                      *
 *                                                                            *
 ******************************************************************************/
void	DCget_hosts_by_functionids(const zbx_vector_uint64_t *functionids, zbx_hashset_t *hosts)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	dc_get_hosts_by_functionids(functionids->values, functionids->values_num, hosts);

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): found %d hosts", __func__, hosts->num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_internal_action_count                                      *
 *                                                                            *
 * Purpose: get number of enabled internal actions                            *
 *                                                                            *
 * Return value: number of enabled internal actions                           *
 *                                                                            *
 ******************************************************************************/
unsigned int	DCget_internal_action_count(void)
{
	unsigned int count;

	RDLOCK_CACHE;

	count = config->internal_actions;

	UNLOCK_CACHE;

	return count;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_config_get                                                   *
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
void	zbx_config_get(zbx_config_t *cfg, zbx_uint64_t flags)
{
	RDLOCK_CACHE;

	if (0 != (flags & ZBX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int	i;

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
		cfg->db.history_compression_status = config->config->db.history_compression_status;
		cfg->db.history_compression_availability = config->config->db.history_compression_availability;
		cfg->db.history_compress_older = config->config->db.history_compress_older;
	}

	if (0 != (flags & ZBX_CONFIG_FLAGS_AUTOREG_TLS_ACCEPT))
		cfg->autoreg_tls_accept = config->config->autoreg_tls_accept;

	if (0 != (flags & ZBX_CONFIG_FLAGS_DEFAULT_TIMEZONE))
		cfg->default_timezone = zbx_strdup(NULL, config->config->default_timezone);

	UNLOCK_CACHE;

	cfg->flags = flags;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_config_clean                                                 *
 *                                                                            *
 * Purpose: cleans global configuration data structure filled                 *
 *          by zbx_config_get() function                                      *
 *                                                                            *
 * Parameters: cfg   - [IN] the global configuration data                     *
 *                                                                            *
 ******************************************************************************/
void	zbx_config_clean(zbx_config_t *cfg)
{
	if (0 != (cfg->flags & ZBX_CONFIG_FLAGS_SEVERITY_NAME))
	{
		int	i;

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
 * Function: DCreset_hosts_availability                                       *
 *                                                                            *
 * Purpose: resets host availability for disabled hosts and hosts without     *
 *          enabled items for the corresponding interface                     *
 *                                                                            *
 * Parameters: hosts - [OUT] changed host availability data                   *
 *                                                                            *
 * Return value: SUCCEED - host availability was reset for at least one host  *
 *               FAIL    - no hosts required availability reset               *
 *                                                                            *
 * Comments: This function resets host availability in configuration cache.   *
 *           The caller must perform corresponding database updates based     *
 *           on returned host availability reset data. On server the function *
 *           skips hosts handled by proxies.                                  *
 *                                                                            *
 ******************************************************************************/
int	DCreset_hosts_availability(zbx_vector_ptr_t *hosts)
{
	ZBX_DC_HOST		*host;
	zbx_hashset_iter_t	iter;
	zbx_host_availability_t	*ha = NULL;
	int			now;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	now = time(NULL);

	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		int	items_num = 0, snmp_items_num = 0, ipmi_items_num = 0, jmx_items_num = 0;

		/* On server skip hosts handled by proxies. They are handled directly */
		/* when receiving hosts' availability data from proxies.              */
		/* Unless a host was just (re)assigned to a proxy or the proxy has    */
		/* not updated its status during the maximum proxy heartbeat period.  */
		/* In this case reset all interfaces to unknown status.               */
		if (0 == host->reset_availability &&
				0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && 0 != host->proxy_hostid)
		{
			ZBX_DC_PROXY	*proxy;

			if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &host->proxy_hostid)))
			{
				/* SEC_PER_MIN is a tolerance interval, it was chosen arbitrarily */
				if (ZBX_PROXY_HEARTBEAT_FREQUENCY_MAX + SEC_PER_MIN >= now - proxy->lastaccess)
					continue;
			}

			host->reset_availability = 1;
		}

		if (NULL == ha)
			ha = (zbx_host_availability_t *)zbx_malloc(NULL, sizeof(zbx_host_availability_t));

		zbx_host_availability_init(ha, host->hostid);

		if (0 == host->reset_availability)
		{
			items_num = host->items_num;
			snmp_items_num = host->snmp_items_num;
			ipmi_items_num = host->ipmi_items_num;
			jmx_items_num = host->jmx_items_num;
		}

		if (0 == items_num && HOST_AVAILABLE_UNKNOWN != host->available)
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_ZABBIX], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == snmp_items_num && HOST_AVAILABLE_UNKNOWN != host->snmp_available)
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_SNMP], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == ipmi_items_num && HOST_AVAILABLE_UNKNOWN != host->ipmi_available)
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_IPMI], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (0 == jmx_items_num && HOST_AVAILABLE_UNKNOWN != host->jmx_available)
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_JMX], HOST_AVAILABLE_UNKNOWN, "", 0, 0);

		if (SUCCEED == zbx_host_availability_is_set(ha))
		{
			if (SUCCEED == DChost_set_availability(host, now, ha))
			{
				zbx_vector_ptr_append(hosts, ha);
				ha = NULL;
			}
			else
				zbx_host_availability_clean(ha);
		}

		host->reset_availability = 0;
	}
	UNLOCK_CACHE;

	zbx_free(ha);

	zbx_vector_ptr_sort(hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() hosts:%d", __func__, hosts->values_num);

	return 0 == hosts->values_num ? FAIL : SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_hosts_availability                                         *
 *                                                                            *
 * Purpose: gets availability data for hosts with availability data changed   *
 *          in period from last availability update to the specified          *
 *          timestamp                                                         *
 *                                                                            *
 * Parameters: hosts - [OUT] changed host availability data                   *
 *             ts    - [OUT] the availability diff timestamp                  *
 *                                                                            *
 * Return value: SUCCEED - availability was changed for at least one host     *
 *               FAIL    - no host availability was changed                   *
 *                                                                            *
 ******************************************************************************/
int	DCget_hosts_availability(zbx_vector_ptr_t *hosts, int *ts)
{
	const ZBX_DC_HOST	*host;
	zbx_hashset_iter_t	iter;
	zbx_host_availability_t	*ha = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	*ts = time(NULL);

	zbx_hashset_iter_reset(&config->hosts, &iter);

	while (NULL != (host = (const ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		if (config->availability_diff_ts <= host->availability_ts && host->availability_ts < *ts)
		{
			ha = (zbx_host_availability_t *)zbx_malloc(NULL, sizeof(zbx_host_availability_t));
			zbx_host_availability_init(ha, host->hostid);

			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_ZABBIX], host->available, host->error,
					host->errors_from, host->disable_until);
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_SNMP], host->snmp_available, host->snmp_error,
					host->snmp_errors_from, host->snmp_disable_until);
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_IPMI], host->ipmi_available, host->ipmi_error,
					host->ipmi_errors_from, host->ipmi_disable_until);
			zbx_agent_availability_init(&ha->agents[ZBX_AGENT_JMX], host->jmx_available, host->jmx_error,
					host->jmx_errors_from, host->jmx_disable_until);

			zbx_vector_ptr_append(hosts, ha);
		}
	}

	UNLOCK_CACHE;

	zbx_vector_ptr_sort(hosts, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() hosts:%d", __func__, hosts->values_num);

	return 0 == hosts->values_num ? FAIL : SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DCtouch_hosts_availability                                       *
 *                                                                            *
 * Purpose: sets availability timestamp to current time for the specified     *
 *          hosts                                                             *
 *                                                                            *
 * Parameters: hostids - [IN] the host identifiers                            *
 *                                                                            *
 ******************************************************************************/
void	DCtouch_hosts_availability(const zbx_vector_uint64_t *hostids)
{
	ZBX_DC_HOST	*dc_host;
	int		i, now;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() hostids:%d", __func__, hostids->values_num);

	now = time(NULL);

	WRLOCK_CACHE;

	for (i = 0; i < hostids->values_num; i++)
	{
		if (NULL != (dc_host = zbx_hashset_search(&config->hosts, &hostids->values[i])))
			dc_host->availability_ts = now;
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_action_copy_conditions                                        *
 *                                                                            *
 * Purpose: copies configuration cache action conditions to the specified     *
 *          vector                                                            *
 *                                                                            *
 * Parameters: dc_action  - [IN] the source action                            *
 *             conditions - [OUT] the conditions vector                       *
 *                                                                            *
 ******************************************************************************/
static void	dc_action_copy_conditions(const zbx_dc_action_t *dc_action, zbx_vector_ptr_t *conditions)
{
	int				i;
	zbx_condition_t			*condition;
	zbx_dc_action_condition_t	*dc_condition;

	zbx_vector_ptr_reserve(conditions, dc_action->conditions.values_num);

	for (i = 0; i < dc_action->conditions.values_num; i++)
	{
		dc_condition = (zbx_dc_action_condition_t *)dc_action->conditions.values[i];

		condition = (zbx_condition_t *)zbx_malloc(NULL, sizeof(zbx_condition_t));

		condition->conditionid = dc_condition->conditionid;
		condition->actionid = dc_action->actionid;
		condition->conditiontype = dc_condition->conditiontype;
		condition->op = dc_condition->op;
		condition->value = zbx_strdup(NULL, dc_condition->value);
		condition->value2 = zbx_strdup(NULL, dc_condition->value2);
		zbx_vector_uint64_create(&condition->eventids);

		zbx_vector_ptr_append(conditions, condition);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_action_eval_create                                            *
 *                                                                            *
 * Purpose: creates action evaluation data from configuration cache action    *
 *                                                                            *
 * Parameters: dc_action - [IN] the source action                             *
 *                                                                            *
 * Return value: the action evaluation data                                   *
 *                                                                            *
 * Comments: The returned value must be freed with zbx_action_eval_free()     *
 *           function later.                                                  *
 *                                                                            *
 ******************************************************************************/
static zbx_action_eval_t	*dc_action_eval_create(const zbx_dc_action_t *dc_action)
{
	zbx_action_eval_t		*action;

	action = (zbx_action_eval_t *)zbx_malloc(NULL, sizeof(zbx_action_eval_t));

	action->actionid = dc_action->actionid;
	action->eventsource = dc_action->eventsource;
	action->evaltype = dc_action->evaltype;
	action->opflags = dc_action->opflags;
	action->formula = zbx_strdup(NULL, dc_action->formula);
	zbx_vector_ptr_create(&action->conditions);

	dc_action_copy_conditions(dc_action, &action->conditions);

	return action;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_actions_eval                                          *
 *                                                                            *
 * Purpose: gets action evaluation data                                       *
 *                                                                            *
 * Parameters: actions         - [OUT] the action evaluation data             *
 *             uniq_conditions - [OUT] unique conditions that actions         *
 *                                     point to (several sources)             *
 *             opflags         - [IN] flags specifying which actions to get   *
 *                                    based on their operation classes        *
 *                                    (see ZBX_ACTION_OPCLASS_* defines)      *
 *                                                                            *
 * Comments: The returned actions and conditions must be freed with           *
 *           zbx_action_eval_free() function later.                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_get_actions_eval(zbx_vector_ptr_t *actions, unsigned char opflags)
{
	const zbx_dc_action_t		*dc_action;
	zbx_hashset_iter_t		iter;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->actions, &iter);

	while (NULL != (dc_action = (const zbx_dc_action_t *)zbx_hashset_iter_next(&iter)))
	{
		if (0 != (opflags & dc_action->opflags))
			zbx_vector_ptr_append(actions, dc_action_eval_create(dc_action));
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() actions:%d", __func__, actions->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_set_availability_update_ts                                   *
 *                                                                            *
 * Purpose: sets timestamp of the last availability update                    *
 *                                                                            *
 * Parameter: ts - [IN] the last availability update timestamp                *
 *                                                                            *
 * Comments: This function is used only by proxies when preparing host        *
 *           availability data to be sent to server.                          *
 *                                                                            *
 ******************************************************************************/
void	zbx_set_availability_diff_ts(int ts)
{
	/* this data can't be accessed simultaneously from multiple processes - locking is not necessary */
	config->availability_diff_ts = ts;
}

/******************************************************************************
 *                                                                            *
 * Function: corr_condition_clean                                             *
 *                                                                            *
 * Purpose: frees correlation condition                                       *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void	corr_condition_clean(zbx_corr_condition_t *condition)
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
 * Function: dc_correlation_free                                              *
 *                                                                            *
 * Purpose: frees global correlation rule                                     *
 *                                                                            *
 * Parameter: condition - [IN] the condition to free                          *
 *                                                                            *
 ******************************************************************************/
static void	dc_correlation_free(zbx_correlation_t *correlation)
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
 * Function: dc_corr_condition_copy                                           *
 *                                                                            *
 * Purpose: copies cached correlation condition to memory                     *
 *                                                                            *
 * Parameter: dc_condition - [IN] the condition to copy                       *
 *            condition    - [OUT] the destination condition                  *
 *                                                                            *
 * Return value: The cloned correlation condition.                            *
 *                                                                            *
 ******************************************************************************/
static void	dc_corr_condition_copy(const zbx_dc_corr_condition_t *dc_condition, zbx_corr_condition_t *condition)
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
 * Function: zbx_dc_corr_operation_dup                                        *
 *                                                                            *
 * Purpose: clones cached correlation operation to memory                     *
 *                                                                            *
 * Parameter: operation - [IN] the operation to clone                         *
 *                                                                            *
 * Return value: The cloned correlation operation.                            *
 *                                                                            *
 ******************************************************************************/
static zbx_corr_operation_t	*zbx_dc_corr_operation_dup(const zbx_dc_corr_operation_t *dc_operation)
{
	zbx_corr_operation_t	*operation;

	operation = (zbx_corr_operation_t *)zbx_malloc(NULL, sizeof(zbx_corr_operation_t));
	operation->type = dc_operation->type;

	return operation;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_correlation_formula_dup                                       *
 *                                                                            *
 * Purpose: clones cached correlation formula, generating it if necessary     *
 *                                                                            *
 * Parameter: correlation - [IN] the correlation                              *
 *                                                                            *
 * Return value: The cloned correlation formula.                              *
 *                                                                            *
 ******************************************************************************/
static char	*dc_correlation_formula_dup(const zbx_dc_correlation_t *dc_correlation)
{
#define ZBX_OPERATION_TYPE_UNKNOWN	0
#define ZBX_OPERATION_TYPE_OR		1
#define ZBX_OPERATION_TYPE_AND		2

	char				*formula = NULL;
	const char			*op = NULL;
	size_t				formula_alloc = 0, formula_offset = 0;
	int				i, last_type = -1, last_op = ZBX_OPERATION_TYPE_UNKNOWN;
	const zbx_dc_corr_condition_t	*dc_condition;
	zbx_uint64_t			last_id;

	if (CONDITION_EVAL_TYPE_EXPRESSION == dc_correlation->evaltype || 0 == dc_correlation->conditions.values_num)
		return zbx_strdup(NULL, dc_correlation->formula);

	dc_condition = (const zbx_dc_corr_condition_t *)dc_correlation->conditions.values[0];

	switch (dc_correlation->evaltype)
	{
		case CONDITION_EVAL_TYPE_OR:
			op = " or";
			break;
		case CONDITION_EVAL_TYPE_AND:
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

void	zbx_dc_correlation_rules_init(zbx_correlation_rules_t *rules)
{
	zbx_vector_ptr_create(&rules->correlations);
	zbx_hashset_create_ext(&rules->conditions, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
			(zbx_clean_func_t)corr_condition_clean, ZBX_DEFAULT_MEM_MALLOC_FUNC,
			ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

	rules->sync_ts = 0;
}

void	zbx_dc_correlation_rules_clean(zbx_correlation_rules_t *rules)
{
	zbx_vector_ptr_clear_ext(&rules->correlations, (zbx_clean_func_t)dc_correlation_free);
	zbx_hashset_clear(&rules->conditions);
}

void	zbx_dc_correlation_rules_free(zbx_correlation_rules_t *rules)
{
	zbx_dc_correlation_rules_clean(rules);
	zbx_vector_ptr_destroy(&rules->correlations);
	zbx_hashset_destroy(&rules->conditions);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_correlation_get_rules                                     *
 *                                                                            *
 * Purpose: gets correlation rules from configuration cache                   *
 *                                                                            *
 * Parameter: rules   - [IN/OUT] the correlation rules                        *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_correlation_rules_get(zbx_correlation_rules_t *rules)
{
	int				i;
	zbx_hashset_iter_t		iter;
	const zbx_dc_correlation_t	*dc_correlation;
	const zbx_dc_corr_condition_t	*dc_condition;
	zbx_correlation_t		*correlation;
	zbx_corr_condition_t		*condition, condition_local;

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
			condition = (zbx_corr_condition_t *)zbx_hashset_insert(&rules->conditions, &condition_local, sizeof(condition_local));
			dc_corr_condition_copy(dc_condition, condition);
			zbx_vector_ptr_append(&correlation->conditions, condition);
		}

		for (i = 0; i < dc_correlation->operations.values_num; i++)
		{
			zbx_vector_ptr_append(&correlation->operations,
					zbx_dc_corr_operation_dup((const zbx_dc_corr_operation_t *)dc_correlation->operations.values[i]));
		}

		zbx_vector_ptr_append(&rules->correlations, correlation);
	}

	rules->sync_ts = config->sync_ts;

	UNLOCK_CACHE;

	zbx_vector_ptr_sort(&rules->correlations, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_hostgroup_cache_nested_groupids                               *
 *                                                                            *
 * Purpose: cache nested group identifiers                                    *
 *                                                                            *
 ******************************************************************************/
void	dc_hostgroup_cache_nested_groupids(zbx_dc_hostgroup_t *parent_group)
{
	zbx_dc_hostgroup_t	*group;

	if (0 == (parent_group->flags & ZBX_DC_HOSTGROUP_FLAGS_NESTED_GROUPIDS))
	{
		int	index, len;

		zbx_vector_uint64_create_ext(&parent_group->nested_groupids, __config_mem_malloc_func,
				__config_mem_realloc_func, __config_mem_free_func);

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
 * Function: dc_maintenance_precache_nested_groups                            *
 *                                                                            *
 * Purpose: pre-caches nested groups for groups used in running maintenances  *
 *                                                                            *
 ******************************************************************************/
static void	dc_maintenance_precache_nested_groups(void)
{
	zbx_hashset_iter_t	iter;
	zbx_dc_maintenance_t	*maintenance;
	zbx_vector_uint64_t	groupids;
	int			i;
	zbx_dc_hostgroup_t	*group;

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
 * Function: dc_get_nested_hostgroupids                                       *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host group                *
 *          (including the target group id)                                   *
 *                                                                            *
 * Parameter: groupid         - [IN] the parent group identifier              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	dc_get_nested_hostgroupids(zbx_uint64_t groupid, zbx_vector_uint64_t *nested_groupids)
{
	zbx_dc_hostgroup_t	*parent_group;

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
 * Function: zbx_dc_get_nested_hostgroupids                                   *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host groups               *
 *                                                                            *
 * Parameter: groupids        - [IN] the parent group identifiers             *
 *            groupids_num    - [IN] the number of parent groups              *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_get_nested_hostgroupids(zbx_uint64_t *groupids, int groupids_num, zbx_vector_uint64_t *nested_groupids)
{
	int	i;

	WRLOCK_CACHE;

	for (i = 0; i < groupids_num; i++)
		dc_get_nested_hostgroupids(groupids[i], nested_groupids);

	UNLOCK_CACHE;

	zbx_vector_uint64_sort(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_nested_hostgroupids_by_names                          *
 *                                                                            *
 * Purpose: gets nested group ids for the specified host groups               *
 *                                                                            *
 * Parameter: groups          - [IN] the parent group names                   *
 *            nested_groupids - [OUT] the nested + parent group ids           *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_get_nested_hostgroupids_by_names(zbx_vector_str_t *groups, zbx_vector_uint64_t *nested_groupids)
{
	int	i, index;

	WRLOCK_CACHE;

	for (i = 0; i < groups->values_num; i++)
	{
		zbx_dc_hostgroup_t	group_local, *group;

		group_local.name = groups->values[i];

		if (FAIL != (index = zbx_vector_ptr_bsearch(&config->hostgroups_name, &group_local,
				dc_compare_hgroups)))
		{
			group = (zbx_dc_hostgroup_t *)config->hostgroups_name.values[index];
			dc_get_nested_hostgroupids(group->groupid, nested_groupids);
		}
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_sort(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(nested_groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_active_proxy_by_name                                  *
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
int	zbx_dc_get_active_proxy_by_name(const char *name, DC_PROXY *proxy, char **error)
{
	int			ret = FAIL;
	const ZBX_DC_HOST	*dc_host;
	const ZBX_DC_PROXY	*dc_proxy;

	RDLOCK_CACHE;

	if (NULL == (dc_host = DCfind_proxy(name)))
	{
		*error = zbx_dsprintf(*error, "proxy \"%s\" not found", name);
		goto out;
	}

	if (HOST_STATUS_PROXY_ACTIVE != dc_host->status && HOST_STATUS_SERVER != dc_host->status )
	{
		*error = zbx_dsprintf(*error, "recieved hello/heartbeat \"%s\" not from server or active proxy", name);
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
 * Function: zbx_dc_items_update_nextcheck                                    *
 *                                                                            *
 * Purpose: updates item nextcheck values in configuration cache              *
 *                                                                            *
 * Parameters: items      - [IN] the items to update                          *
 *             values     - [IN] the items values containing new properties   *
 *             errcodes   - [IN] item error codes. Update only items with     *
 *                               SUCCEED code                                 *
 *             values_num - [IN] the number of elements in items,values and   *
 *                               errcodes arrays                              *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_items_update_nextcheck(DC_ITEM *items, zbx_agent_value_t *values, int *errcodes, size_t values_num)
{
	size_t		i;
	ZBX_DC_ITEM	*dc_item;
	ZBX_DC_HOST	*dc_host;

	WRLOCK_CACHE;

	for (i = 0; i < values_num; i++)
	{
		if (FAIL == errcodes[i])
			continue;

		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &items[i].itemid)))
			continue;

		if (ITEM_STATUS_ACTIVE != dc_item->status)
			continue;

		if (NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
			continue;

		if (HOST_STATUS_MONITORED != dc_host->status)
			continue;

		if (ZBX_LOC_NOWHERE != dc_item->location)
			continue;

		/* update nextcheck for items that are counted in queue for monitoring purposes */
		if (SUCCEED == zbx_is_counted_in_item_queue(dc_item->type, dc_item->key))
			DCitem_nextcheck_update(dc_item, dc_host, ZBX_ITEM_COLLECTED, values[i].ts.sec,
					NULL);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_host_interfaces                                       *
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
int	zbx_dc_get_host_interfaces(zbx_uint64_t hostid, DC_INTERFACE2 **interfaces, int *n)
{
	const ZBX_DC_HOST	*host;
	int			i, ret = FAIL;

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
		const ZBX_DC_INTERFACE	*src = (const ZBX_DC_INTERFACE *)host->interfaces_v.values[i];
		DC_INTERFACE2		*dst = *interfaces + i;

		dst->interfaceid = src->interfaceid;
		dst->type = src->type;
		dst->main = src->main;
		dst->useip = src->useip;
		strscpy(dst->ip_orig, src->ip);
		strscpy(dst->dns_orig, src->dns);
		strscpy(dst->port_orig, src->port);
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
			dst->snmp_version= snmp->version;
		}
	}

	ret = SUCCEED;
unlock:
	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_items_apply_changes                                     *
 *                                                                            *
 * Purpose: apply item state, error, mtime, lastlogsize changes to            *
 *          configuration cache                                               *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_items_apply_changes(const zbx_vector_ptr_t *item_diff)
{
	int			i;
	const zbx_item_diff_t	*diff;
	ZBX_DC_ITEM		*dc_item;

	if (0 == item_diff->values_num)
		return;

	WRLOCK_CACHE;

	for (i = 0; i < item_diff->values_num; i++)
	{
		diff = (const zbx_item_diff_t *)item_diff->values[i];

		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &diff->itemid)))
			continue;

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & diff->flags))
			dc_item->lastlogsize = diff->lastlogsize;

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_MTIME & diff->flags))
			dc_item->mtime = diff->mtime;
		
		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_ERROR & diff->flags))
			DCstrpool_replace(1, &dc_item->error, diff->error);

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE & diff->flags))
			dc_item->state = diff->state;

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK & diff->flags))
			dc_item->lastclock = diff->lastclock;
		
		switch (dc_item->value_type){
			case ITEM_VALUE_TYPE_FLOAT:
			case ITEM_VALUE_TYPE_UINT64:
				dc_item->prevvalue=dc_item->lastvalue;
				dc_item->lastvalue=diff->value;
				break;

			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				DCstrpool_replace(1, (const char **)&dc_item->lastvalue.str, diff->value.str);
				break;
			
		}
		
	}
	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCconfig_update_inventory_values                                 *
 *                                                                            *
 * Purpose: update automatic inventory in configuration cache                 *
 *                                                                            *
 ******************************************************************************/
void	DCconfig_update_inventory_values(const zbx_vector_ptr_t *inventory_values)
{
	ZBX_DC_HOST_INVENTORY	*host_inventory = NULL;
	int			i;

	WRLOCK_CACHE;

	for (i = 0; i < inventory_values->values_num; i++)
	{
		const zbx_inventory_value_t	*inventory_value = (zbx_inventory_value_t *)inventory_values->values[i];
		const char			**value;

		if (NULL == host_inventory || inventory_value->hostid != host_inventory->hostid)
		{
			host_inventory = (ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories_auto, &inventory_value->hostid);

			if (NULL == host_inventory)
				continue;
		}

		value = &host_inventory->values[inventory_value->idx];

		DCstrpool_replace((NULL != *value ? 1 : 0), value, inventory_value->value);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_host_inventory_value_by_hostid                            *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 * Comments: This function must be called inside configuration cache read     *
 *           (or write) lock.                                                 *
 *                                                                            *
 ******************************************************************************/
static int	dc_get_host_inventory_value_by_hostid(zbx_uint64_t hostid, char **replace_to, int value_idx)
{
	const ZBX_DC_HOST_INVENTORY	*dc_inventory;

	if (NULL != (dc_inventory = (const ZBX_DC_HOST_INVENTORY *)zbx_hashset_search(&config->host_inventories_auto,
			&hostid)) && NULL != dc_inventory->values[value_idx])
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
 * Function: DCget_host_inventory_value_by_itemid                             *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_inventory_value_by_itemid(zbx_uint64_t itemid, char **replace_to, int value_idx)
{
	const ZBX_DC_ITEM	*dc_item;
	int			ret = FAIL;

	RDLOCK_CACHE;

	if (NULL != (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		ret = dc_get_host_inventory_value_by_hostid(dc_item->hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_host_inventory_value_by_hostid                             *
 *                                                                            *
 * Purpose: find inventory value in automatically populated cache, if not     *
 *          found then look in main inventory cache                           *
 *                                                                            *
 ******************************************************************************/
int	DCget_host_inventory_value_by_hostid(zbx_uint64_t hostid, char **replace_to, int value_idx)
{
	int	ret;

	RDLOCK_CACHE;

	ret = dc_get_host_inventory_value_by_hostid(hostid, replace_to, value_idx);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_trigger_dependencies                                  *
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
void	zbx_dc_get_trigger_dependencies(const zbx_vector_uint64_t *triggerids, zbx_vector_ptr_t *deps)
{
	int				i, ret;
	const ZBX_DC_TRIGGER_DEPLIST	*trigdep;
	zbx_vector_uint64_t		masterids;
	zbx_trigger_dep_t		*dep;

	zbx_vector_uint64_create(&masterids);
	zbx_vector_uint64_reserve(&masterids, 64);

	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		if (NULL == (trigdep = (ZBX_DC_TRIGGER_DEPLIST *)zbx_hashset_search(&config->trigdeps, &triggerids->values[i])))
			continue;

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
 * Function: zbx_dc_reschedule_items                                          *
 *                                                                            *
 * Purpose: reschedules items that are processed by the target daemon         *
 *                                                                            *
 * Parameter: itemids       - [IN] the item identifiers                       *
 *            nextcheck     - [IN] the schedueld time                         *
 *            proxy_hostids - [OUT] the proxy_hostids of the given itemids    *
 *                                  (optional, can be NULL)                   *
 *                                                                            *
 * Comments: On server this function reschedules items monitored by server.   *
 *           On proxy only items monitored by the proxy is accessible, so     *
 *           all items can be safely rescheduled.                             *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_reschedule_items(const zbx_vector_uint64_t *itemids, int nextcheck, zbx_uint64_t *proxy_hostids)
{
	int		i;
	ZBX_DC_ITEM	*dc_item;
	ZBX_DC_HOST	*dc_host;
	zbx_uint64_t	proxy_hostid;

	WRLOCK_CACHE;

	for (i = 0; i < itemids->values_num; i++)
	{
		if (NULL == (dc_item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemids->values[i])) ||
				NULL == (dc_host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &dc_item->hostid)))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot perform check now for itemid [" ZBX_FS_UI64 "]"
					": item is not in cache", itemids->values[i]);

			proxy_hostid = 0;
		}
		else if (0 == dc_item->schedulable)
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot perform check now for item \"%s\" on host \"%s\""
					": item configuration error", dc_item->key, dc_host->host);

			proxy_hostid = 0;
		}
		else if (0 == (proxy_hostid = dc_host->proxy_hostid) ||
				SUCCEED == is_item_processed_by_server(dc_item->type, dc_item->key))
		{
			dc_requeue_item_at(dc_item, dc_host, nextcheck);
			proxy_hostid = 0;
		}

		if (NULL != proxy_hostids)
			proxy_hostids[i] = proxy_hostid;
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_proxy_update_nodata                                       *
 *                                                                            *
 * Purpose: stop suppress mode of the nodata() trigger                        *
 *                                                                            *
 * Parameter: subscriptions - [IN] the array of trigger id and time of values *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_proxy_update_nodata(zbx_vector_uint64_pair_t *subscriptions)
{
	ZBX_DC_PROXY		*proxy = NULL;
	int			i;
	zbx_uint64_pair_t	p;

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

		proxy->nodata_win.values_num --;

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
 * Function: zbx_dc_update_proxy                                              *
 *                                                                            *
 * Purpose: updates changed proxy data in configuration cache and updates     *
 *          diff flags to reflect the updated data                            *
 *                                                                            *
 * Parameter: diff - [IN/OUT] the properties to update                        *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_update_proxy(zbx_proxy_diff_t *diff)
{
	ZBX_DC_PROXY	*proxy;

	WRLOCK_CACHE;

	if (diff->lastaccess < config->proxy_lastaccess_ts)
		diff->lastaccess = config->proxy_lastaccess_ts;

	if (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &diff->hostid)))
	{
		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTACCESS))
		{
			int	lost = 0;	/* communication lost */

			if (0 != (diff->flags &
					(ZBX_FLAGS_PROXY_DIFF_UPDATE_HEARTBEAT | ZBX_FLAGS_PROXY_DIFF_UPDATE_CONFIG)))
			{
				int	delay = diff->lastaccess - proxy->lastaccess;

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
			if (proxy->version != diff->version)
				proxy->version = diff->version;
			else
				diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
		{
			if (proxy->auto_compress != diff->compress)
				proxy->auto_compress = diff->compress;
			else
				diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS);
		}
		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR))
		{
			if (proxy->last_version_error_time != diff->last_version_error_time)
				proxy->last_version_error_time = diff->last_version_error_time;
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_PROXYDELAY))
		{
			if (proxy->proxy_delay != diff->proxy_delay)
				proxy->proxy_delay = diff->proxy_delay;

			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_PROXYDELAY);
		}

		if (0 != (diff->flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_SUPPRESS_WIN))
		{
			zbx_proxy_suppress_t	*ps_win = &proxy->nodata_win, *ds_win = &diff->nodata_win;

			if ((ps_win->flags & ZBX_PROXY_SUPPRESS_ACTIVE) != (ds_win->flags & ZBX_PROXY_SUPPRESS_ACTIVE))
			{
				ps_win->period_end = ds_win->period_end;
			}

			if (ps_win->flags != ds_win->flags)
				ps_win->flags = ds_win->flags;

			if (0 > ps_win->values_num)	/* some new values was processed faster than old */
				ps_win->values_num = 0;	/* we will suppress more                         */

			ps_win->values_num += ds_win->values_num;
			diff->flags &= (~ZBX_FLAGS_PROXY_DIFF_UPDATE_SUPPRESS_WIN);
		}
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_proxy_lastaccess                                      *
 *                                                                            *
 * Purpose: returns proxy lastaccess changes since last lastaccess request    *
 *                                                                            *
 * Parameter: lastaccess - [OUT] last access updates for proxies that need    *
 *                               to be synced with database, sorted by        *
 *                               hostid                                       *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_get_proxy_lastaccess(zbx_vector_uint64_pair_t *lastaccess)
{
	ZBX_DC_PROXY	*proxy;
	int		now;

	if (ZBX_PROXY_LASTACCESS_UPDATE_FREQUENCY < (now = time(NULL)) - config->proxy_lastaccess_ts)
	{
		zbx_hashset_iter_t	iter;

		WRLOCK_CACHE;

		zbx_hashset_iter_reset(&config->proxies, &iter);

		while (NULL != (proxy = (ZBX_DC_PROXY *)zbx_hashset_iter_next(&iter)))
		{
			if (proxy->lastaccess >= config->proxy_lastaccess_ts)
			{
				zbx_uint64_pair_t	pair = {proxy->hostid, proxy->lastaccess};

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
 * Function: zbx_dc_get_session_token                                         *
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
const char	*zbx_dc_get_session_token(void)
{
	return config->session_token;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_or_create_data_session                                *
 *                                                                            *
 * Purpose: returns data session, creates a new session if none found         *
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
zbx_data_session_t	*zbx_dc_get_or_create_data_session(zbx_uint64_t hostid, const char *token)
{
	zbx_data_session_t	*session, session_local;
	time_t			now;

	now = time(NULL);
	session_local.hostid = hostid;
	session_local.token = token;

	RDLOCK_CACHE;
	session = (zbx_data_session_t *)zbx_hashset_search(&config->data_sessions, &session_local);
	UNLOCK_CACHE;

	if (NULL == session)
	{
		WRLOCK_CACHE;
		session = (zbx_data_session_t *)zbx_hashset_insert(&config->data_sessions, &session_local,
				sizeof(session_local));
		session->token = dc_strdup(token);
		UNLOCK_CACHE;

		session->last_valueid = 0;
	}

	session->lastaccess = now;

	return session;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_cleanup_data_sessions                                     *
 *                                                                            *
 * Purpose: removes data sessions not accessed for 24 hours                   *
 *                                                                            *
 ******************************************************************************/
void	zbx_dc_cleanup_data_sessions(void)
{
	zbx_data_session_t	*session;
	zbx_hashset_iter_t	iter;
	time_t			now;

	now = time(NULL);

	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->data_sessions, &iter);
	while (NULL != (session = (zbx_data_session_t *)zbx_hashset_iter_next(&iter)))
	{
		if (session->lastaccess + SEC_PER_DAY <= now)
		{
			__config_mem_free_func((char *)session->token);
			zbx_hashset_iter_remove(&iter);
		}
	}

	UNLOCK_CACHE;
}

static void	zbx_gather_tags_from_host(zbx_uint64_t hostid, zbx_vector_ptr_t *item_tags)
{
	zbx_dc_host_tag_index_t 	*dc_tag_index;
	zbx_dc_host_tag_t		*dc_tag;
	zbx_item_tag_t			*tag;
	int				i;

	if (NULL != (dc_tag_index = zbx_hashset_search(&config->host_tags_index, &hostid)))
	{
		for (i = 0; i < dc_tag_index->tags.values_num; i++)
		{
			dc_tag = (zbx_dc_host_tag_t *)dc_tag_index->tags.values[i];
			tag = (zbx_item_tag_t *) zbx_malloc(NULL, sizeof(zbx_item_tag_t));
			tag->tag.tag = zbx_strdup(NULL, dc_tag->tag);
			tag->tag.value = zbx_strdup(NULL, dc_tag->value);
			zbx_vector_ptr_append(item_tags, tag);
		}
	}
}

static void	zbx_gather_tags_from_template_chain(zbx_uint64_t itemid, zbx_vector_ptr_t *item_tags)
{
	ZBX_DC_TEMPLATE_ITEM	*item;

	if (NULL != (item = (ZBX_DC_TEMPLATE_ITEM *)zbx_hashset_search(&config->template_items, &itemid)))
	{
		zbx_gather_tags_from_host(item->hostid, item_tags);

		if (0 != item->templateid)
			zbx_gather_tags_from_template_chain(item->templateid, item_tags);
	}
}

static void	zbx_get_item_tags(zbx_uint64_t itemid, zbx_vector_ptr_t *item_tags)
{
	ZBX_DC_ITEM		*item;
	ZBX_DC_PROTOTYPE_ITEM	*lld_item;
	zbx_item_tag_t		*tag;
	int			n, i;

	if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &itemid)))
		return;

	n = item_tags->values_num;

	zbx_gather_tags_from_host(item->hostid, item_tags);

	if (0 != item->templateid)
		zbx_gather_tags_from_template_chain(item->templateid, item_tags);

	/* check for discovered item */
	if (0 != item->parent_itemid && 4 == item->flags)
	{
		if (NULL != (lld_item = (ZBX_DC_PROTOTYPE_ITEM *)zbx_hashset_search(&config->prototype_items,
				&item->parent_itemid)))
		{
			if (0 != lld_item->templateid)
				zbx_gather_tags_from_template_chain(lld_item->templateid, item_tags);
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

void	zbx_dc_get_item_tags_by_functionids(const zbx_uint64_t *functionids, size_t functionids_num,
		zbx_vector_ptr_t *item_tags)
{
	const ZBX_DC_FUNCTION	*dc_function;
	size_t			i;

	RDLOCK_CACHE;

	for (i = 0; i < functionids_num; i++)
	{
		if (NULL == (dc_function = (const ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
				&functionids[i])))
		{
			continue;
		}

		zbx_get_item_tags(dc_function->itemid, item_tags);
	}

	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_proxy_nodata_win                                           *
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
int	DCget_proxy_nodata_win(zbx_uint64_t hostid, zbx_proxy_suppress_t *nodata_win, int *lastaccess)
{
	const ZBX_DC_PROXY	*dc_proxy;
	int			ret;

	RDLOCK_CACHE;

	if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
	{
		const zbx_proxy_suppress_t	*proxy_nodata_win = &dc_proxy->nodata_win;

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
 * Function: DCget_proxy_delay                                                *
 *                                                                            *
 * Purpose: retrieves proxy delay from the cache                              *
 *                                                                            *
 * Parameters: hostid - [IN] proxy host id                                    *
 *             delay  - [OUT] proxy delay                                     *
 *                                                                            *
 * Return value: SUCCEED - the delay is retrieved                             *
 *               FAIL    - the delay cannot be retrieved, proxy not found in  *
 *                         configuration cache                                *
 *                                                                            *
 ******************************************************************************/
int	DCget_proxy_delay(zbx_uint64_t hostid, int *delay)
{
	const ZBX_DC_PROXY	*dc_proxy;
	int			ret;

	RDLOCK_CACHE;

	if (NULL != (dc_proxy = (const ZBX_DC_PROXY *)zbx_hashset_search(&config->proxies, &hostid)))
	{
		*delay = dc_proxy->proxy_delay;
		ret = SUCCEED;
	}
	else
		ret = FAIL;

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_proxy_delay_by_name                                        *
 *                                                                            *
 * Purpose: retrieves proxy delay from the cache                              *
 *                                                                            *
 * Parameters: name  - [IN] proxy host name                                   *
 *             delay - [OUT] proxy delay                                      *
 *             error - [OUT] error                                            *
 *                                                                            *
 * Return value: SUCCEED - proxy delay is retrieved                           *
 *               FAIL    - proxy delay cannot be retrieved                    *
 *                                                                            *
 ******************************************************************************/
int	DCget_proxy_delay_by_name(const char *name, int *delay, char **error)
{
	const ZBX_DC_HOST	*dc_host;

	RDLOCK_CACHE;
	dc_host = DCfind_proxy(name);
	UNLOCK_CACHE;

	if (NULL == dc_host)
	{
		*error = zbx_dsprintf(*error, "Proxy \"%s\" not found.", name);
		return FAIL;
	}

	if (SUCCEED != DCget_proxy_delay(dc_host->hostid, delay))
	{
		*error = zbx_dsprintf(*error, "Proxy \"%s\" not found in configuration cache.", name);
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_set_macro_env                                             *
 *                                                                            *
 * Purpose: sets user macro environment security level                        *
 *                                                                            *
 * Parameter: env - [IN] the security level (see ZBX_MACRO_ENV_* defines)     *
 *                                                                            *
 * Comments: The security level affects how the secret macros are resolved -  *
 *           as they values or '******'.                                      *
 *                                                                            *
 ******************************************************************************/
unsigned char	zbx_dc_set_macro_env(unsigned char env)
{
	unsigned char	old_env = macro_env;
	macro_env = env;
	return old_env;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_instanceid                                            *
 *                                                                            *
 * Purpose: returns server/proxy instance id                                  *
 *                                                                            *
 * Return value: the instance id                                              *
 *                                                                            *
 ******************************************************************************/
const char	*zbx_dc_get_instanceid(void)
{
	/* instanceid is initialized during the first configuration cache synchronization */
	/* and is never updated - so it can be accessed without locking cache             */
	return config->config->instanceid;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_expand_user_macros_in_func_params                             *
 *                                                                            *
 * Purpose: expand user macros in trigger function parameters                 *
 *                                                                            *
 * Parameters: params - [IN] the function parameters                          *
 *             hostid - [IN] host of the item used in function                *
 *                                                                            *
 * Return value: The function parameters with expanded user macros.           *
 *                                                                            *
 ******************************************************************************/
char	*dc_expand_user_macros_in_func_params(const char *params, zbx_uint64_t hostid)
{
	const char	*ptr;
	size_t		params_len;
	char		*buf;
	size_t		buf_alloc, buf_offset = 0, sep_pos;

	if ('\0' == *params)
		return zbx_strdup(NULL, params);

	buf_alloc = params_len = strlen(params);
	buf = zbx_malloc(NULL, buf_alloc);

	for (ptr = params; ptr < params + params_len; ptr += sep_pos + 1)
	{
		size_t	param_pos, param_len;
		int	quoted;
		char	*param, *resolved_param;

		zbx_function_param_parse(ptr, &param_pos, &param_len, &sep_pos);

		param = zbx_function_param_unquote_dyn(ptr + param_pos, param_len, &quoted);
		resolved_param = dc_expand_user_macros(param, &hostid, 1);

		if (SUCCEED == zbx_function_param_quote(&resolved_param, quoted))
			zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, resolved_param);
		else
			zbx_strncpy_alloc(&buf, &buf_alloc, &buf_offset, ptr + param_pos, param_len);

		if (',' == ptr[sep_pos])
			zbx_chrcpy_alloc(&buf, &buf_alloc, &buf_offset, ',');

		zbx_free(resolved_param);
		zbx_free(param);
	}

	return buf;
}

char	*zbx_dc_expand_user_macros_in_func_params(const char *params, zbx_uint64_t hostid)
{
	char	*resolved_params;

	RDLOCK_CACHE;
	resolved_params = dc_expand_user_macros_in_func_params(params, hostid);
	UNLOCK_CACHE;

	return resolved_params;
}

char	*dc_expand_user_macros_in_calcitem(const char *formula, zbx_uint64_t hostid)
{
	char		*exp, *tmp,*expanded, error[128];
	const char	*e;
	size_t		exp_alloc = 128, exp_offset = 0, tmp_alloc = 128, tmp_offset = 0, f_pos, par_l, par_r;
	ZBX_DC_HOST	*dc_host;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() formula:%s", __func__, formula);

	exp = (char *)zbx_malloc(NULL, exp_alloc);
	tmp = (char *)zbx_malloc(NULL, tmp_alloc);

	for (e = formula; SUCCEED == zbx_function_find(e, &f_pos, &par_l, &par_r, error, sizeof(error)); e += par_r + 1)
	{
		size_t		param_pos, param_len, sep_pos;
		int		quoted;
		char		*hostkey, *host = NULL, *key = NULL;
		zbx_uint64_t	func_hostid = 0;

		/* substitute user macros in the part of the string preceding function parameters */

		zbx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e, par_l + 1);
		expanded = dc_expand_user_macros_in_expression(tmp, &hostid, 1);
		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, expanded);
		zbx_free(expanded);
		tmp_offset = 0;

		/* substitute user macros in function parameters */

		zbx_function_param_parse(e + par_l + 1, &param_pos, &param_len, &sep_pos);

		/* convert relative offset to absolute */
		param_pos += par_l + 1;
		sep_pos += par_l + 1;

		zbx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, e + par_l + 1, sep_pos - par_l);

		/* calculated function has only fist parameter - host:key */
		if (sep_pos == par_r)
			continue;

		/* extract host:key parameter to find the function hostid */
		hostkey = zbx_function_param_unquote_dyn(e + param_pos, param_len, &quoted);
		if (SUCCEED == parse_host_key(hostkey, &host, &key))
		{
			if (NULL != host)
			{
				if (NULL != (dc_host = DCfind_host(host)))
					func_hostid = dc_host->hostid;
			}
			else
				func_hostid = hostid;
		}
		zbx_free(host);
		zbx_free(key);
		zbx_free(hostkey);

		if (0 == func_hostid)
		{
			/* couldn't obtain target host, copy the rest of the function as it is */
			zbx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, e + sep_pos + 1, par_r - sep_pos);
			continue;
		}

		/* extract remaining parameters and expand user macros */
		zbx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e + sep_pos + 1, par_r - sep_pos - 1);
		expanded = dc_expand_user_macros_in_func_params(tmp, func_hostid);
		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, expanded);
		zbx_free(expanded);
		tmp_offset = 0;

		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, ")");
	}

	if (par_l <= par_r)
	{
		/* substitute user macros in the remaining part */
		zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e);
		expanded = dc_expand_user_macros_in_expression(tmp, &hostid, 1);
		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, expanded);
		zbx_free(expanded);
	}

	zbx_free(tmp);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() formula:%s", __func__, exp);

	return exp;
}

#ifdef HAVE_TESTS
#	include "../../../tests/libs/zbxdbcache/dc_item_poller_type_update_test.c"
#	include "../../../tests/libs/zbxdbcache/dc_function_calculate_nextcheck_test.c"
#endif

size_t	DCconfig_get_trigger_itemids_by_valuetype( int value_type, zbx_vector_uint64_t *vector_itemids)
{
	size_t			count=0;
	const ZBX_DC_ITEM	*item;
	

	zbx_hashset_iter_t iter;
	
	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->items,&iter);

	while (NULL != (item = zbx_hashset_iter_next(&iter))) {
		//if (item->value_type == value_type && NULL != item->triggers) {
		//TODO: FIXXXXX
		if (item->value_type == value_type){//} && item->itemid ==  2793701 ) {
			if (2793701 == item->itemid)
				zabbix_log(LOG_LEVEL_INFORMATION,"Preload: Added atem %ld to the list to fetch to value cache",item->itemid);
			zbx_vector_uint64_append(vector_itemids,item->itemid);
			count ++;
		}
	}

	UNLOCK_CACHE;
	return count;
	
}

zbx_uint64_t zbx_dc_current_topology_version() {
	zbx_uint64_t version = 0;
	
	RDLOCK_CACHE;
	
	version = config->cluster_topology_version;
	
	UNLOCK_CACHE;

	return version;
}


/********************************************************************/
/*creates json object to send as a hello message to a server		*/
int zbx_dc_create_hello_json(struct zbx_json *j){

	char *proxy_ids = NULL;
	size_t alloc=0,offset=0;
	zbx_hashset_iter_t proxy_iter;
	ZBX_DC_PROXY *proxy;
	zbx_uint64_t cluster_topology_version;

	
	zbx_json_adduint64(j,ZBX_PROTO_SERVER_ID,CONFIG_CLUSTER_SERVER_ID);
	zbx_json_addstring(j,ZBX_PROTO_TAG_HOST,CONFIG_HOSTNAME,ZBX_JSON_TYPE_STRING);
	
	RDLOCK_CACHE;

	zbx_hashset_iter_reset(&config->proxies,&proxy_iter);
	

	//todo: filter only active proxies here (lastaccess+proxy_timeout>now + only proxy_active hosts)
	while (NULL!=(proxy=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&proxy_iter))){
		if (proxy->cluster_lastheard + ZBX_PROXY_TIMEOUT > time(NULL) ) {
			ZBX_DC_HOST *host;
			//checking if this is actually a proxy
			if ( (NULL != (host=zbx_hashset_search(&config->hosts,&proxy->hostid))) &&
				 ( HOST_STATUS_PROXY_ACTIVE == host->status ||
				   HOST_STATUS_PROXY_PASSIVE == host->status ) ) {
				zbx_snprintf_alloc(&proxy_ids,&alloc,&offset,"%ld,",proxy->hostid);
				
			}
			
		}
		//	if (ZB proxy->state )

		//todo: proper proxy reg filtering
		//	zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Adding domains from proxy %ld (%s) to the server announcement",proxy->hostid,proxy->domains);
			
	}
	
	UNLOCK_CACHE;

	if (offset>0) {
		proxy_ids[offset-1]=0;
	}
	zbx_json_addstring(j,ZBX_PROTO_CLUSTER_PROXYLIST,proxy_ids,ZBX_JSON_TYPE_STRING);
	//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Adding list of active proxies to hello: %s",proxy_ids);
	
	
	zbx_free(proxy_ids);

	cluster_topology_version=zbx_dc_current_topology_version();
	zbx_json_adduint64(j,ZBX_PROTO_CLUSTER_TOPOLOGY_VERSION,cluster_topology_version);
	
}

/**************************************************************/
/* parces json objects recieved from a server and updates object accordingly */
int zbx_dc_parce_hello_json(DC_PROXY *proxy,struct zbx_json_parse *jp, int timediff) {
	
	char server_id[MAX_STRING_LEN],
		 proxy_list[MAX_STRING_LEN],
		 cluster_topology_version[MAX_STRING_LEN];
	
	int ret = FAIL;	 
	int need_recalc_topology = 0, new_cluster_id;
	zbx_uint64_t proxy_hostid, new_cluster_topology_version;

	zbx_hashset_iter_t proxy_iter;
	zbx_json_type_t type;

	ZBX_DC_PROXY *update_proxy;

	char *proxy_id = NULL;
	const char s[2] = ",";

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_SERVER_ID, server_id, sizeof(server_id),&type) ||
		SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_CLUSTER_PROXYLIST, proxy_list, sizeof(proxy_list), &type) ||
		SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_CLUSTER_TOPOLOGY_VERSION, cluster_topology_version,
		 sizeof(cluster_topology_version), &type) ) 
	{
		zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: missing some important params in the hello");
		goto out;
	}
	
	new_cluster_id=strtol(server_id,NULL,10);	
	new_cluster_topology_version=strtol(cluster_topology_version,NULL,10);	
	
	
	if (proxy->cluster_id != new_cluster_id) {
			//todo: seems that on init there is no zbx_dc_proxy cleanup
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Server (id: %ld) cluster id differs, setting new one ( %d -> %d )",
					proxy->hostid,proxy->cluster_id,new_cluster_id);
			proxy->cluster_id = new_cluster_id;
			need_recalc_topology = 1;
	}

	if (proxy->cluster_topology_version != new_cluster_topology_version) {
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Server %ld cluster topology version differs, setting new one ( %ld -> %ld )",
					proxy->hostid,proxy->cluster_topology_version,new_cluster_topology_version);
			proxy->cluster_topology_version= new_cluster_topology_version;
			//we better recalc topology in case there are topology changes upstream
			if ( proxy->cluster_id > CONFIG_CLUSTER_SERVER_ID) {
				zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: Server %ld with higher id %d reports topology change to %ld, setting recalc topology flag",
								proxy->hostid,proxy->cluster_id,proxy->cluster_topology_version);
				need_recalc_topology = 1;
			}
			
		
	}
	
	if (timediff) {
			//setting new timediff for the server
			proxy->cluster_rtt = timediff;
	}

	
	//handling the state
	//succeed answer
	//proxy->cluster_lastheard=time(NULL);
	
	switch (proxy->cluster_state) {
		case ZBX_CLUSTER_SERVER_STATE_DOWN:
				//getting hello in DOWN state leads switching to HOLD state to wait for stability

			zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: server %ld, changed state DOWN -> HOLD",proxy->hostid);
			
			proxy->cluster_state = ZBX_CLUSTER_SERVER_STATE_HOLD;				
			proxy->cluster_lastheard = time(NULL);
			proxy->cluster_failed_hello_count = 0 ;
			
			need_recalc_topology=0;
			break;						
		case ZBX_CLUSTER_SERVER_STATE_HOLD:
				
			if (proxy->cluster_lastheard + ZBX_CLUSTER_SERVER_HOLD_TIME < time(NULL)) {
						zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: server %ld changed state HOLD -> ALIVE",proxy->hostid);
				proxy->cluster_state = ZBX_CLUSTER_SERVER_STATE_ALIVE;	
				proxy->cluster_failed_hello_count=0;

				zbx_dc_set_topology_recalc();
			} 
			break;
	}


	
	WRLOCK_CACHE;
	if ( NULL != ( update_proxy = (ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&proxy->hostid))) {
			char *proxy_list_str=NULL;
			
			update_proxy->cluster_id=new_cluster_id;
			update_proxy->cluster_topology_version=new_cluster_topology_version;
			update_proxy->cluster_state = proxy->cluster_state;
			update_proxy->cluster_lastheard = proxy->cluster_lastheard;

	//		if (timediff) update_proxy->cluster_rtt=timediff;


			zbx_vector_uint64_clear(&update_proxy->connected_servers);
			proxy_list_str=strtok(proxy_list,s);

			while (proxy_list_str) {
				zbx_uint64_t proxy_id=strtol(proxy_list_str,NULL,10);
				zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: avail proxy id %ld ",proxy_id);
				zbx_vector_uint64_append(&update_proxy->connected_servers ,proxy_id);
				proxy_list_str=strtok(NULL,s);
				
				
				


			} 
			
		
	}
    UNLOCK_CACHE;


	if (need_recalc_topology) {
		zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: there has been changes in the cluster, calling recalc topology");
		zbx_dc_set_topology_recalc();
	}

	ret = SUCCEED;

out:

	return ret;
}

char *zbx_dc_get_topology() {
	char *topology = NULL;
	
	RDLOCK_CACHE;
	if (NULL != config->cluster_topology) {
	
		topology=zbx_strdup(NULL,config->cluster_topology);
			
	}
	UNLOCK_CACHE;

	return topology;

}
void DC_replace_topology(char *new_topology) {

	if ( NULL != config->cluster_topology ) {
		zbx_strpool_release((const char *)config->cluster_topology);
	}

	config->cluster_topology = (char *) zbx_strpool_intern(new_topology);
}

int DC_apply_topology(void) {
	char *topology = NULL;
	struct zbx_json_parse	jp, jp_servers, jp_server, jp_clusterid, jp_hosts;
	unsigned int server_id_int, activated_hosts=0, total_hosts=0, ret = FAIL;
	zbx_uint64_t cluster_server_id;
	const char *p = NULL;
	zbx_hashset_iter_t hosts_iter;
	ZBX_DC_HOST *dc_host;
	u_int32_t local_proxy_hosts=0, remote_server_proxy_hosts=0;
	zbx_json_type_t type;

	zabbix_log(LOG_LEVEL_INFORMATION,"%s CLUSTER: starting to process the topology", __func__);
	
	topology=zbx_strdup(NULL,config->cluster_topology);

	if ( SUCCEED != zbx_json_open(topology, &jp)) {
		zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: couldn't parse topology to set");
		return FAIL;
	}

	if (0 == CONFIG_CLUSTER_SERVER_ID) return SUCCEED;

	//just before applying topology params, cleaning all the hosts in case if topology is different from db config
	zbx_hashset_iter_reset(&config->hosts,&hosts_iter);
	
	while (NULL!=(dc_host=(ZBX_DC_HOST*)zbx_hashset_iter_next(&hosts_iter))){
		dc_host->cluster_state = ZBX_CLUSTER_HOST_STATE_DISABLED;
		dc_host->cluster_server_host_id = 0;
	}

	zabbix_log(LOG_LEVEL_DEBUG,"Will porcess %s",topology);
	if (SUCCEED == zbx_json_brackets_by_name(&jp, "servers", &jp_servers))
	{
		while (NULL != (p = zbx_json_next(&jp_servers, p))) {
			char server_id[MAX_ID_LEN], host[MAX_ZBX_HOSTNAME_LEN], host_id[MAX_ID_LEN];
			char mark_hosts_cluster_state;
			u_int32_t processed_hosts;

			if ((SUCCEED != zbx_json_brackets_open(p, &jp_server) ) || 	
				(SUCCEED != zbx_json_value_by_name(&jp_server,ZBX_PROTO_SERVER_ID, server_id, MAX_ID_LEN, &type)) || 
				(SUCCEED != zbx_json_value_by_name(&jp_server,ZBX_PROTO_SERVER_HOST_ID, host_id, MAX_ID_LEN, &type)) || 
				(SUCCEED != zbx_json_value_by_name(&jp_server, ZBX_PROTO_TAG_HOST, host, MAX_ZBX_HOSTNAME_LEN, &type)) )	
			{
				ret = FAIL;
				zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: couldn't parse server entry in the topology file");	
				continue;	
			}
			cluster_server_id = strtol(host_id,0,10);
					
			//mark hosts belonging to this server as ACTIVE
			if  ( strtol(server_id,0,10) == CONFIG_CLUSTER_SERVER_ID &&  0 == strcmp(CONFIG_HOSTNAME,host)) 
				mark_hosts_cluster_state=ZBX_CLUSTER_HOST_STATE_ACTIVE; 
			else
				mark_hosts_cluster_state=ZBX_CLUSTER_HOST_STATE_DISABLED;

			/* iterating on all the hosts of the server to apply server name for metrics_rerouting and mark this server hosts */
			if (SUCCEED == zbx_json_brackets_by_name (&jp_server,ZBX_PROTO_TAG_HOSTS,&jp_hosts)) {
				char hostid[MAX_ID_LEN];
				int is_null = 0;
				const char *hp = NULL;
		
				while  ( NULL != ( hp = zbx_json_next(&jp_hosts, hp ) )) {
					zbx_uint64_t hostid_int;

					hostid_int=strtol(hp,NULL,10);
		
					if (NULL == (dc_host = zbx_hashset_search(&config->hosts,&hostid_int))) continue;
						
						dc_host->cluster_state = mark_hosts_cluster_state;
						dc_host->cluster_server_host_id = cluster_server_id; 
						zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: APPLY_TOPOLOGY: Host %ld: cluster server is %ld, state is %d",dc_host->hostid, dc_host->cluster_server_host_id, dc_host->cluster_state);
						processed_hosts++;
				}
			}			
			//if (ZBX_CLUSTER_HOST_STATE_ACTIVE == mark_hosts_cluster_state) {
			activated_hosts +=processed_hosts;
			struct zbx_json_parse	jp_proxies;

			//processing proxy assignement topology
			if (SUCCEED == zbx_json_brackets_by_name (&jp_server,ZBX_PROTO_TAG_PROXIES,&jp_proxies)) {
				//iterating over proxies
				const char *pp=NULL;
				struct zbx_json_parse jp_proxy_entry, jp_proxy_hosts;
					
				u_int64_t proxyid_int;
				//iterating over proxy entries
				while (NULL != (pp = zbx_json_next(&jp_proxies, pp))) {
					//parsing proxy id and array of hosts assigned to the proxy
					char proxy_id[MAX_ID_LEN];
					zabbix_log(LOG_LEVEL_DEBUG,"Processing proxy entry %s",pp);

					if ( SUCCEED == zbx_json_brackets_open(pp, &jp_proxy_entry) &&
					     SUCCEED == zbx_json_value_by_name(&jp_proxy_entry,ZBX_PROTO_PROXY_ID, proxy_id, MAX_ID_LEN,&type)) {
							
						//parsing proxy id and array of hosts assigned to the proxy
						proxyid_int=strtol(proxy_id,NULL,10);
						zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: processing proxy entry for proxy id %ld: %s",proxyid_int,jp_proxy_entry.start);

						//opening the array of hosts assigned to the proxy:			
						const char *hp = NULL;
						/* iterating on all the hosts of the server to apply server name for metrics_rerouting and mark this server hosts */
						if (SUCCEED == zbx_json_brackets_by_name (&jp_proxy_entry,ZBX_PROTO_TAG_HOSTS,&jp_proxy_hosts) ){
							//&& 	SUCCEED == zbx_json_brackets_open(hp, &jp_proxy_entry)) {
								
							char pr_hostid[MAX_ID_LEN];
							//int is_null = 0;
							zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: processing hosts proxy entry for proxy id %ld (%s)",proxyid_int, jp_proxy_hosts.start);
							//iterating over proxy hosts
							//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: processing proxy's host entry: %s",hp);
							while  ( NULL != ( hp = zbx_json_next(&jp_proxy_hosts, hp ) )) {
								zbx_uint64_t hostid_int;

								hostid_int=strtol(hp,NULL,10);
								zabbix_log(LOG_LEVEL_DEBUG,"Found proxy host %ld",hostid_int);
								if (NULL != (dc_host = zbx_hashset_search(&config->hosts,&hostid_int))) {
										
									//marking the host's server id as proxy id 
									if (mark_hosts_cluster_state=ZBX_CLUSTER_HOST_STATE_ACTIVE) {
										//if proxy is under the current sever, remembering the proxy_id in 
										//the cluster_server_host_id field, to filter proxy's tasks
										//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: marking host %ld as polled by locally connected proxy %ld",dc_host->hostid, proxyid_int);
										 dc_host->cluster_server_host_id=proxyid_int;
										 dc_host->cluster_state=ZBX_CLUSTER_HOST_STATE_ACTIVE_PROXY;
										// zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: APPLY_TOPOLOGY: Host %ld: assigned to local proxy %ld", dc_host->hostid, proxyid_int);
										 local_proxy_hosts++;
									}	else {
										//if the host assigned to different server / proxy pair, remembering 
										// server id in the cluster_server_host_id field so we will reroute data 
										// to the server responsible for the host's operations
										dc_host->cluster_server_host_id=cluster_server_id;
										//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: APPLY_TOPOLOGY: Host %ld: assigned to remote server %ld", dc_host->hostid, cluster_server_id);
										remote_server_proxy_hosts++;
									}							
								}
							}			
						}
					} 
				}
			}
			total_hosts += processed_hosts;
		}
		zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Applied cluster config,  %d hosts activated, total is %d in config",activated_hosts,total_hosts);
		zabbix_log(LOG_LEVEL_INFORMATION,"									%d hosts assigned to local proxies, %d to remote",local_proxy_hosts,remote_server_proxy_hosts);
		
		//todo : invoke config reloading here, triggers update and  make it intelectual - only load hosts
		//that has been added to the cluster monitoring lately
				
	} else {
		zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: couldn;t process topology JSON, apply config failed ");
	}
	
	zbx_free(topology);
	return ret;
}

/**********************************************************************/
/* based on accessible peer servers and proxies calculates hosts 	  */
/* assignement and sets the new topology for the server 			  */
/* todo: setting topology must be removed from this func to separate  */
/* functionality 													  */
/* 																	  */
/**********************************************************************/
long int DC_generate_topology() {
	
	ZBX_DC_HOST *server_host, *domain_host, *proxy_host;
	ZBX_DC_PROXY *server, *domain, *proxy;
	zbx_uint64_t default_domain_id = 0, domain_id, server_id;
	zbx_hashset_iter_t server_iter,hosts_iter,proxy_iter, domain_iter;
	struct zbx_json	j;
	char *tmp_str, *domain_id_str=NULL;
	u_int64_t hosts_server=0;
	
	char s[2]=",";

	if (0 == CONFIG_CLUSTER_SERVER_ID) return SUCCEED;
	
	zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Generating new own topology");
	zbx_hashset_iter_reset(&config->proxies,&server_iter);
	
	//step1: prepare: looking for the 'default' domain + cleaning 

	//todo: filter only active proxies here (lastaccess+proxy_timeout>now + only proxy_active hosts)
	while (NULL!=(server=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&server_iter))){
		if ( NULL!=(server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server->hostid))) {
			if (HOST_STATUS_DOMAIN == server_host->status && !strcmp("default",server_host->host)) {
				zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: found 'defaut' domain: %ld", server_host->hostid);
				default_domain_id=server_host->hostid;
			}
			if (HOST_STATUS_PROXY_ACTIVE == server_host->status || 
			    HOST_STATUS_PROXY_PASSIVE == server_host->status ) {
				//clearing links from proxies to servers as we fill them from scrath a bit later
				zbx_vector_uint64_clear(&server->connected_servers);
			}
		}
		zbx_vector_uint64_clear(&server->cluster_domains);
		zbx_vector_uint64_pair_clear(&server->proxy_hosts);
		
	}
		//zbx_vector_uint64_pair_append()
	
	//step2: going through all the servers and saving their ids to the arrays of the domains they belong
	zbx_hashset_iter_reset(&config->proxies,&server_iter);
	//todo: fix ZBX_CLUSTER_SERVER_STATE setting for proxies
	while (NULL!=(server=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&server_iter))){
		if ( NULL!=(server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server->hostid)) && 
						(HOST_STATUS_SERVER == server_host->status && ZBX_CLUSTER_SERVER_STATE_ALIVE == server->cluster_state) || 
						(HOST_STATUS_PROXY_ACTIVE ==server_host->status 
							|| HOST_STATUS_PROXY_PASSIVE == server_host->status) && server->cluster_lastheard+ZBX_PROXY_TIMEOUT > time(NULL))
						 {
					
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: found server/proxy %s, with domain ids '%s'", server_host->host, server_host->error);
			tmp_str=zbx_strdup(NULL,server_host->error);

			domain_id_str=strtok(tmp_str,s);

			while (domain_id_str) {
				ZBX_DC_PROXY *domain;
				zbx_uint64_t domain_int_id=strtol(domain_id_str,NULL,10);
				zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Fetched id %ld of the domain, will add %ld server to it",domain_int_id, server->hostid);
				
				if ( NULL!=(domain=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&domain_int_id))) {
					zbx_vector_uint64_append(&domain->cluster_domains,server->hostid);
				} 
				domain_id_str=strtok(NULL,s);
			} 
			zbx_free(tmp_str);
		} else {
			zabbix_log(LOG_LEVEL_INFORMATION,"Slipping server %ld last seen %ld",server->hostid,server->cluster_lastheard);
		}
	}
	zabbix_log(LOG_LEVEL_INFORMATION,"Dumping domains");
	//dummping domain->server assignement
	/* //todo: perhaps must be in LOG_LEVEL_TRACE debug mode */
	//dumping domains
	zbx_hashset_iter_reset(&config->proxies,&domain_iter);
	while (NULL!=(domain=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&domain_iter))){
		if ( NULL!=(domain_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&domain->hostid)) && 
					HOST_STATUS_DOMAIN == domain_host->status) {
						
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: DOMAIN_DUMP: found domain %ld '%s'",domain_host->hostid, domain_host->host);		
			
			int i;
			for ( i = 0 ; i < domain->cluster_domains.values_num; i++ ) {
				zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER:			  	active server#%d %ld",i, domain->cluster_domains.values[i]);
			}
		
		}
	}

	zabbix_log(LOG_LEVEL_INFORMATION,"Dumping servers");
	//dumping servers and proxies
	zbx_hashset_iter_reset(&config->proxies,&server_iter);
	while (NULL!=(server=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&server_iter))){
		if ( NULL!=(server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server->hostid)) && 
					HOST_STATUS_SERVER == server_host->status || 
					HOST_STATUS_PROXY_ACTIVE == server_host->status || 
					HOST_STATUS_PROXY_PASSIVE == server_host->status) {
						
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: (SERVER/PROXY)_DUMP: server %ld '%s' in state %d",server_host->hostid, server_host->host, 
				server->cluster_state);		
			
			int i;
			for (i=0; i< server->connected_servers.values_num; i++) {
				zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER:			  	connected proxy#%d %ld",i, server->connected_servers.values[i]);
				

				hosts_server=server->connected_servers.values[i];
				//this will fill prox->server hash
				if( NULL != (proxy = (ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&hosts_server))) {
					if (FAIL ==  zbx_vector_uint64_search(&proxy->connected_servers, server->hostid,
									ZBX_DEFAULT_UINT64_COMPARE_FUNC)) {
						zbx_vector_uint64_append(&proxy->connected_servers,server->hostid);
					}
				}
			}	
		}
	}
	//dumping proxies
	//todo: fix timing and set cluster state instead if lastheard
	zbx_hashset_iter_reset(&config->proxies,&proxy_iter);
	while (NULL!=(proxy=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&proxy_iter))){
		if ( NULL!=(proxy_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&proxy->hostid)) && 
					(HOST_STATUS_PROXY_ACTIVE == proxy_host->status ||HOST_STATUS_PROXY_PASSIVE == proxy_host->status ) &&
					proxy->cluster_lastheard + ZBX_PROXY_TIMEOUT > time(NULL))
					 {
						
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: PROXY_DUMP: proxy %ld '%s' ",proxy_host->hostid, proxy_host->host);		
			int i;
			for (i=0; i< proxy->connected_servers.values_num; i++) {
				zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER:			  	connected server#%d %ld",i, proxy->connected_servers.values[i]);
			}	
		}
	}


	//checking if the default domain has any hosts in it 
	if ( default_domain_id	) {
		if ( NULL!=(proxy=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&default_domain_id))) {
			if (!proxy->cluster_domains.values_num) {
				zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: WARNING: default domain exists but has no servers in it");
			}
		} 
	} else  zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: WARNING: no default domain set, some hosts might be unmonitored");
	

	ZBX_DC_HOST *host;
	u_int64_t resolve_domain=0;
	//step3, final: calculating the topology
	
	zbx_hashset_iter_reset(&config->hosts,&hosts_iter);
	while (NULL!=(host=(ZBX_DC_HOST*)zbx_hashset_iter_next(&hosts_iter))) {
		if ( HOST_STATUS_MONITORED == host->status || HOST_STATUS_NOT_MONITORED == host->status )  {
			zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: resolving topology for host %ld",host->hostid);
			if (host->proxy_hostid)  {
		
				domain_id=host->proxy_hostid;
				zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: Host %ld has monitoring domain %ld",host->hostid, domain_id);
				
				//host has a domain but 
				//checking if there are any servers in the host's domain

				//todo1: checking what it the host's proxy is
				//in case it's a server or proxy which has been assigne directly,
				//then checking if  - server is alive
				//					- or an announce from a proxy is present on one of the severs in the topology
				//if not, then assigning default domain id 
			
				ZBX_DC_HOST *proxy_host;
				hosts_server=0;
				
				if ( NULL != (server=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&domain_id)) && 
				 	 NULL != (server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server->hostid)) ) {
				    
					switch (server_host->status) {
						case HOST_STATUS_PROXY_PASSIVE:
						case HOST_STATUS_PROXY_ACTIVE:
							zabbix_log(LOG_LEVEL_INFORMATION,"Host originally monitored by a proxy");
							zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: found host %ld with proxy set to %ld",host->hostid,server->hostid);
							//this host is processed by a proxy, collecting which server's are connected to the proxy
							//u_int64_t proxy_servers;

							//hosts_proxy = host->proxy_hostid;
							//todo: check and set by cluster status instead of redundand timeout check
							
							//checking if the proxy is still alive
							if (server->cluster_lastheard + ZBX_PROXY_TIMEOUT> time(NULL)) {
								u_int64_t server_id = 0;
								//getting the server from the list
								if (server->connected_servers.values_num) {
									server_id=server->connected_servers.values[host->hostid % server->connected_servers.values_num];
									//getting link to the server 
									ZBX_DC_PROXY *tmp_server=NULL;
									if (NULL != (tmp_server=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&server_id))) {
										//adding pair to the server: proxy, host
										zbx_uint64_pair_t	pair;
										pair.first = server->hostid;
										pair.second = host->hostid;
										zbx_vector_uint64_pair_append(&tmp_server->proxy_hosts,pair);
										zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: host %ld will be monitored server->proxy (%ld)->(%ld)",
												host->hostid,server_id,server->hostid );
									} else {
										THIS_SHOULD_NEVER_HAPPEN;
										resolve_domain=default_domain_id;
									}

								} else {
									//there is no servers handling this proxy
									zabbix_log(LOG_LEVEL_INFORMATION,"Proxy %d seems to be down, last heard of it %ld seconds ago",server->hostid, 
												time(NULL)- server->cluster_lastheard );
									//THIS_SHOULD_NEVER_HAPPEN;
									resolve_domain=default_domain_id;
								}
							} else {
								resolve_domain=default_domain_id;
							}
							break;
						case HOST_STATUS_SERVER:
							zabbix_log(LOG_LEVEL_INFORMATION,"Host originally monitored by a server");
							//direct server assignement
							if (ZBX_CLUSTER_SERVER_STATE_ALIVE == server->cluster_state) {
								zbx_vector_uint64_append(&server->cluster_domains,host->hostid);
							} else {
								resolve_domain=default_domain_id;
							}; 
							break;
						case HOST_STATUS_DOMAIN:
							resolve_domain = host->proxy_hostid;
							zabbix_log(LOG_LEVEL_DEBUG,"Host originally monitored by a domain %ld",resolve_domain);
							
							break;
						default:
							THIS_SHOULD_NEVER_HAPPEN;
							resolve_domain =  default_domain_id;
						
					}
					
				} else THIS_SHOULD_NEVER_HAPPEN;

			} else {
				//if host had no server specified, setting default domain
				resolve_domain=default_domain_id;
			}
			
			zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: finished pre-resolve, selected domain is %ld, default is %ld ",resolve_domain,default_domain_id);
			//the final step - we'd like to resolve the default domain if it's set for the host
			//othrewize the host is already calculated to a server/proxy
			if (resolve_domain ) {
				zabbix_log(LOG_LEVEL_DEBUG,"Resolving domain %ld",resolve_domain);
				//now we've got a domain id can distribute host to the domain's servers
				if ( NULL != (domain=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&resolve_domain))) {
					int i;
								
					if (domain->cluster_domains.values_num) {
	
						server_id=domain->cluster_domains.values[host->hostid % domain->cluster_domains.values_num];
						zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: Selected server %ld for host  %ld",server_id, host->hostid);

						//adding the host id to the server's cluster_domains array
						if ( (NULL != (server=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&server_id))) && 
							 (NULL != (server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server_id)))) {
							//if we've found a server, adding the host to the server's array of hosts
							if ( HOST_STATUS_SERVER == server_host->status ) {
								zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: Selected server %ld for host  %ld",server_id, host->hostid);
								zbx_vector_uint64_append(&server->cluster_domains,host->hostid);
							} else {
								//for a proxy we need to select one of the servers
								//we don't support domains of domains yet, but it might be 
								//a cool idea .... may be ... too complicated yet
								zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: Selected proxy %ld for host  %ld",server_id, host->hostid);

								if (server->connected_servers.values_num) {
									server_id=server->connected_servers.values[host->hostid % server->connected_servers.values_num];
									//getting link to the server 
									ZBX_DC_PROXY *tmp_server=NULL;

									if (NULL != (tmp_server=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&server_id))) {
										//adding pair to the server: proxy, host
										zbx_uint64_pair_t	pair;
										pair.first = server->hostid;
										pair.second = host->hostid;
										zbx_vector_uint64_pair_append(&tmp_server->proxy_hosts,pair);
										zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: host %ld will be monitored server->proxy (%ld)->(%ld)",
												host->hostid,server_id,server->hostid );
									} 
								}
							}		
						}
					} else {
						zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: No servers in the domain, host %ld will not be actively monitored",host->hostid);
					}
				} else {
					//zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't find server id %ld",host_server_id);
					THIS_SHOULD_NEVER_HAPPEN;
				}
			} else {
				zabbix_log(LOG_LEVEL_DEBUG,"Host %ld will not be actively monitored",host->hostid);
			}
		
		} 
	}
	//end of wile hosts iteration;
	//step4: generating nice and shiny configuration json as well as new config id
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
	
	zbx_json_addint64(&j,ZBX_PROTO_CLUSTER_TOPOLOGY_VERSION,config->cluster_topology_version);
	zbx_json_addarray(&j,"servers");
	
	zbx_hashset_iter_reset(&config->proxies,&server_iter);
	
	while (NULL!=(server=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&server_iter))){
		//iterate only on servers due to all proxies will be depended on them
		if ( NULL!=(server_host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&server->hostid)) && 
						(HOST_STATUS_SERVER == server_host->status) ) {
			
							
			zbx_json_addobject(&j,NULL);//starting server object

			zbx_json_addstring(&j,ZBX_PROTO_TAG_HOST,server_host->host,ZBX_JSON_TYPE_STRING);
			zbx_json_addint64(&j,ZBX_PROTO_SERVER_HOST_ID,server->hostid);
			zbx_json_addint64(&j,ZBX_PROTO_SERVER_ID,server->cluster_id);

		
		
			//iterating over the server's proxies
			if ( 0 <  server->cluster_domains.values_num ) {
									
				//hosts directly processed by the server
				zbx_json_addarray(&j,ZBX_PROTO_TAG_HOSTS);
				
			
				int i;
				for ( i=0; i< server->cluster_domains.values_num; i++) {
						zbx_json_adduint64(&j,NULL,server->cluster_domains.values[i]);
			
				}
				
				zbx_json_close(&j);//closing hosts array
				
				
				//iterating on all the proxies known on the server
			
			}
		
			//iterating over the server connectedproxies
			//for server connected_servers holds connected proxy ids
			zbx_json_addarray(&j,ZBX_PROTO_TAG_PROXIES);
			
			int i;
			for ( i=0; i< server->connected_servers.values_num; i++) {
					
				zbx_json_addobject(&j,NULL);
				zbx_json_addint64(&j,ZBX_PROTO_PROXY_ID, server->connected_servers.values[i]);
				
				
				zbx_json_addarray(&j,ZBX_PROTO_TAG_HOSTS);
								
				int k;
				//zabbix_log(LOG_LEVEL_INFORMATION,"Proxy has %ld hosts",server->proxy_hosts.values_num);
				for ( k = 0; k < server->proxy_hosts.values_num ; k++ ) {
						//zabbix_log(LOG_LEVEL_INFORMATION,"Pchecking entry (%ld) -> (%ld)",server->proxy_hosts.values[k].first,
						//server->proxy_hosts.values[k].second);
							if ( server->proxy_hosts.values[k].first == server->connected_servers.values[i]) 
						zbx_json_adduint64(&j,NULL,server->proxy_hosts.values[k].second);
						
				}
				
				zbx_json_close(&j);//closing proxy hosts array
				
				zbx_json_close(&j);//closing the proxy object
			}
			
			
			
			
			
			zbx_json_close(&j);//closing proxies array	
			zbx_json_close(&j);//closing server object
		}
		
	}
	zbx_json_close(&j);//closing servers array
	zbx_json_close(&j);//closing json
	
	zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: Topology: %s",
		j.buffer);
	DC_replace_topology(j.buffer);
	DC_apply_topology();
	
	config->cluster_topology_version = CONFIG_CLUSTER_SERVER_ID * 10000000000 + time(NULL);
	
	
	zbx_json_free(&j);
	
	zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Calculated own topology version %ld",
		config->cluster_topology_version);
	
	
	return SUCCEED;

}

int zbx_dc_set_topology(const char *topology) {
	
	const zbx_uint64_t topology_version;
	struct zbx_json_parse	jp, jp_topology;
	const char *cluster_topology_version = NULL;//[MAX_STRING_LEN];
	char *buffer = NULL;
	//const char *buffer2=NULL;
	size_t alloc = 0;

	if (NULL == topology) return FAIL;

	zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: setting topology: %s",topology);
	
	/* validating and setting jp */
	if ( SUCCEED != zbx_json_open(topology, &jp)) {
		zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: couldn't parse topology to set");
		return FAIL;
	}
	
	/* fetching topology object from the response */
	//zbx_json_value_by_name_dyn(&jp,ZBX_PROTO_VALUE_CLUSTER_TOPOLGY,&buffer,&alloc);
		
	if (SUCCEED != zbx_json_brackets_by_name (&jp,ZBX_PROTO_TAG_DATA,&jp_topology) ) {
		zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: couldn't parse topology in responce");
		return FAIL;
	};

	//zbx_json_value_dyn(&jp_topology,&buffer, &alloc);
	jp_topology.end='\0';
	buffer=(char *)jp_topology.start;

	zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: parced toplogy object is %s",buffer);
	/* validating and setting jp for topology */
	if ( SUCCEED != zbx_json_open(buffer,&jp)) {
		zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: couldn't open '%s' object in the responce buffer '%s' : %s",ZBX_PROTO_TAG_DATA, buffer, zbx_json_strerror());
		return FAIL;
	}
	/* looking for start of topology version property */
	if ( NULL == ( cluster_topology_version = zbx_json_pair_by_name(&jp,ZBX_PROTO_CLUSTER_TOPOLOGY_VERSION))) {
		zabbix_log(LOG_LEVEL_WARNING, "CLUSTER: couldn't find or parse '%s' object in the responce",ZBX_PROTO_CLUSTER_TOPOLOGY_VERSION);
		return FAIL;
	}	

	
	WRLOCK_CACHE;

	DC_replace_topology(buffer);
	
	config->cluster_topology_version = strtol(cluster_topology_version,NULL,10);;	
	zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: set new topology version %ld", config->cluster_topology_version);
	
	DC_apply_topology();

	UNLOCK_CACHE;
	
	zbx_free(buffer);
	
	return SUCCEED;
}


/*************************************************************************/
/* Purpose: calculates topology tree - finds a server with highest ID    */
/* if none found (means this server is the msater or no peer servers are */
/* active), generates own topology 										 */
/* returns 0 if no servers has been found OR hostid of the master 		 */
/* if no servres has been found then does own topology calc				 */
/*************************************************************************/ 
zbx_uint64_t zbx_dc_recalc_topology(void) {

	char *new_topology = NULL;
	int topology_version;
	ZBX_DC_PROXY *proxy, *server=NULL;
	ZBX_DC_HOST  *host;
	DC_PROXY dc_server;
	zbx_uint64_t max_version,master_server_id = 0;
	zbx_hashset_iter_t proxy_iter;
	
	
	if (!CONFIG_CLUSTER_SERVER_ID) {
		return 0;
	}

	WRLOCK_CACHE;	
	max_version=0;//config->cluster_config_version;

	if (config->cluster_topology_last_update + ZBX_CLUSTER_TOPOLOGY_RECALC_INTERVAL < time(NULL) ) {
//	&& config->cluster_topology_need_update ) {
		
		config->cluster_topology_last_update=time(NULL);
		config->cluster_topology_need_update=0;
		
		zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Starting topology recalc");
	
		//iterating on all the servers and checking if they have higher topology number
		zbx_hashset_iter_reset(&config->proxies,&proxy_iter);
		
		while (NULL!=(proxy=(ZBX_DC_PROXY*)zbx_hashset_iter_next(&proxy_iter))){
			if ( NULL!=(host=(ZBX_DC_HOST*)zbx_hashset_search(&config->hosts,&proxy->hostid)) && 
						 HOST_STATUS_SERVER == host->status  ) {
					
				if ( ZBX_CLUSTER_SERVER_STATE_ALIVE == proxy->cluster_state && 
						proxy->cluster_topology_version > max_version &&
						proxy->cluster_id > CONFIG_CLUSTER_SERVER_ID ) 
				{
					zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Found server %s with higher ID and topology:%ld -> %ld", 
							host->host,max_version, proxy->cluster_topology_version);
					max_version = proxy->cluster_topology_version;
					master_server_id = proxy->hostid;
					server=proxy;
				}
			}
		}
	
		if ( 0 == master_server_id ) {
			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: I am a MASTER, generating new topology");
			DC_generate_topology();
		} else {
			//char * topology = NULL;

			zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: setting topology download task from server %ld", master_server_id);
			//downloading new topology from the new server
		 	server->nextcheck=time(NULL);
			
			config->cluster_topology_download_host=master_server_id;
			
		}
		
		//resetting topology updates and counters
	
		//todo:
		//the last thing to do is to calculate hosts difference 
		//and reload new hosts from the database

		zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: new topology is %s",config->cluster_topology);
		
	} 
	UNLOCK_CACHE;
	
	return master_server_id;
}

int zbx_dc_set_topology_recalc(void) {
	
	WRLOCK_CACHE;
	config->cluster_topology_need_update=1;
	UNLOCK_CACHE;

}

void	zbx_dc_register_server_na(zbx_uint64_t hostid, char *reason){
	
	ZBX_DC_PROXY *update_proxy;
	
	WRLOCK_CACHE;
	if ( NULL != ( update_proxy = (ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&hostid))) {
	
			//increasing fail count whatever the fail reason, this is a misscommunication
		update_proxy->cluster_failed_hello_count++;

		switch (update_proxy->cluster_state) {
			case ZBX_CLUSTER_SERVER_STATE_ALIVE:
				if (ZBX_CLUSTER_MAX_FAIL_HELLOS <= update_proxy->cluster_failed_hello_count) {
					zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: server %ld changed state ALIVE -> DOWN %s",update_proxy->hostid, reason);
					update_proxy->cluster_state = ZBX_CLUSTER_SERVER_STATE_DOWN;
					
					//need to recalc topology 
					config->cluster_topology_need_update =1;
				} 
				break;
			case ZBX_CLUSTER_SERVER_STATE_HOLD:
				//loosing a hello in hold stage resets to DOWN to zero counters to wait for stable peer server state
				zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: server %ld changed state to HOLD -> DOWN %s", update_proxy->hostid,reason);
				update_proxy->cluster_state=ZBX_CLUSTER_SERVER_STATE_DOWN;
				update_proxy->cluster_lastheard = time(NULL);
				break;
			case ZBX_CLUSTER_SERVER_STATE_DOWN:
				zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: DOWN: server %ld already in DOWN state",update_proxy->hostid);
				break;	
		}

	}
    UNLOCK_CACHE;
	
};

void zbx_dc_set_download_server_topology(zbx_uint64_t hostid) {
	WRLOCK_CACHE;
	config->cluster_topology_download_host=hostid;
	UNLOCK_CACHE;
};

zbx_uint64_t zbx_dc_get_download_server_topology(void) {
	zbx_uint64_t hostid;
	RDLOCK_CACHE;
	hostid=config->cluster_topology_download_host;
	UNLOCK_CACHE;
	return hostid;
};


int zbx_dc_forward_item_to_server(DC_ITEM *item, zbx_agent_value_t *value) {
	
	int ret = SUCCEED, now;
	struct zbx_json j;
	const char *ptr;
	ZBX_DC_PROXY *server;
	zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: REROUTE will forward a metric from item id %ld to server %ld", item->itemid, item->host.cluster_server_host_id);
	
	zbx_json_init(&j,ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addint64(&j,ZBX_PROTO_TAG_ITEMID,item->itemid);
	zbx_json_addstring(&j,ZBX_PROTO_TAG_VALUE,value->value,ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(&j,ZBX_PROTO_TAG_LOGTIMESTAMP,value->ts.sec);
	zbx_json_addstring(&j,ZBX_PROTO_TAG_LOGSOURCE,value->source,ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(&j,ZBX_PROTO_TAG_LASTLOGSIZE,value->lastlogsize);
	zbx_json_adduint64(&j,ZBX_PROTO_TAG_ID,value->id);
	zbx_json_adduint64(&j,ZBX_PROTO_TAG_LOGSEVERITY,value->severity);
	zbx_json_addint64(&j, ZBX_PROTO_TAG_MTIME,value->mtime);
	zbx_json_adduint64(&j,ZBX_PROTO_TAG_LOGEVENTID,value->logeventid);
	zbx_json_addint64(&j,ZBX_PROTO_TAG_STATE,value->state);
	zbx_json_addint64(&j,"meta",value->meta);

	zabbix_log(LOG_LEVEL_DEBUG,"CLUSTER: REROUTE: generated JSON : %s",j.buffer);
	now=time(NULL);

	WRLOCK_CACHE;
	//saving to config strpool
	if (NULL != (server = (ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&item->host.cluster_server_host_id))) {
	
		ptr = zbx_strpool_intern(j.buffer);
    	
		zbx_vector_ptr_append(&server->cluster_rerouted_data,(void *)ptr);
		
		//reducing next server check so that data will be flushed to the server
		if (server->nextcheck > now + 1 ) {
				server->nextcheck = now+1;
		} 
		//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: Added reroute data to buffer, total %d record",
		//	server->cluster_rerouted_data.values_num);

	} else {
		ret = FAIL; 
		zabbix_log(LOG_LEVEL_WARNING, "Couldn't find server id %ld in config cache: skipping rerouted data",
					item->host.cluster_server_host_id);
		
	}
	UNLOCK_CACHE;

	zbx_json_free(&j);
	
	return ret;
}

int zbx_dc_create_rerouted_json(struct zbx_json *j, zbx_uint64_t serverid) {
	ZBX_DC_PROXY *server=NULL;
	zbx_json_addarray(j,ZBX_PROTO_CLUSTER_REROUTED_DATA);
	WRLOCK_CACHE;
	if (NULL != (server = (ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&serverid))) {
		int i;
		for (i=0; i < server->cluster_rerouted_data.values_num; i++) {
			zbx_json_addraw(j, NULL, (const char *)server->cluster_rerouted_data.values[i]);
			zbx_strpool_release((const char *)&server->cluster_rerouted_data.values[i]);
		}
		zbx_vector_ptr_clear(&server->cluster_rerouted_data);
	}

	UNLOCK_CACHE;
}

int zbx_dc_parce_rerouted_data(DC_PROXY *server, struct zbx_json_parse *jp) {
	const char *p = NULL;
	int ret=SUCCEED;
	struct zbx_json_parse jp_data,jp_record;
	zbx_json_type_t type;

	zabbix_log(LOG_LEVEL_DEBUG, "CLUSTER: REROUTED: parsing data from server %ld",server->hostid);

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_CLUSTER_REROUTED_DATA, &jp_data))
	{
		while (NULL != (p = zbx_json_next(&jp_data, p))) {
			zabbix_log(LOG_LEVEL_DEBUG, "CLUSTER: REROUTED: parsing data :%s",p);

			char itemid[MAX_ID_LEN],item_value[MAX_STRING_LEN],timestamp[MAX_ID_LEN],
			source[MAX_STRING_LEN],lastlogsize[MAX_ID_LEN],id[MAX_ID_LEN],mtime[MAX_ID_LEN],
			logeventid[MAX_ID_LEN],state[MAX_ID_LEN],meta[MAX_ID_LEN],severity[MAX_ID_LEN];
			
			DC_ITEM dc_item;
			ZBX_DC_ITEM *zbx_dc_item = NULL;
			int processed_items=0, errcode=0;


			if (SUCCEED == zbx_json_brackets_open(p, &jp_record)  && 	
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_ITEMID , itemid, MAX_ID_LEN, &type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_VALUE, item_value, MAX_STRING_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LOGTIMESTAMP , timestamp, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LOGSOURCE, source, MAX_STRING_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LASTLOGSIZE , lastlogsize, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_ID, id, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LOGSEVERITY, severity, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LOGSEVERITY, mtime, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_LOGEVENTID , logeventid, MAX_ID_LEN,&type) && 
				SUCCEED == zbx_json_value_by_name(&jp_record,ZBX_PROTO_TAG_STATE, state, MAX_ID_LEN,&type) &&
				SUCCEED == zbx_json_value_by_name(&jp_record,"meta",meta, MAX_ID_LEN,&type) )  {

				zbx_agent_value_t value;
				zbx_uint64_t itemid_int;

				itemid_int=strtol(itemid,0,10);
				value.ts.sec=strtol(timestamp,0,10);
				value.ts.ns=0;
				value.source=source;
				value.value=item_value;
				value.lastlogsize=strtol(lastlogsize,0,10);
				value.id=strtol(id,0,10);
				value.mtime=strtol(mtime,0,10);
				value.timestamp=value.ts.sec;
				value.severity=strtol(severity,0,10);
				value.logeventid=strtol(logeventid,0,10);
				value.state=strtol(state,0,10);
				value.meta=strtol(meta,0,10);
		



				zabbix_log(LOG_LEVEL_INFORMATION, "CLUSTER: REROUTED: parsing data: got itemid %ld value %s ",itemid_int,item_value);

				//looking for the item 
				WRLOCK_CACHE;
				
				if (NULL != (zbx_dc_item = (ZBX_DC_ITEM*)zbx_hashset_search(&config->items, &itemid_int))) {
				   	DCget_item(&dc_item, zbx_dc_item);
					zabbix_log(LOG_LEVEL_INFORMATION,"found and parced DC item");
					UNLOCK_CACHE;
				

				
					if(  0 == process_history_data(&dc_item,&value,&errcode,1,NULL) ) {
							zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: REROUTED_DATA: couldn't process item %ld",itemid_int);
						} else {
							processed_items++;
							zabbix_log(LOG_LEVEL_INFORMATION, "CLUSTER: REROUTED: SUCCEED processed history data for item %ld ",itemid_int);
						}
					} else UNLOCK_CACHE;

			} else 
			{
				ret = FAIL;
				zabbix_log(LOG_LEVEL_WARNING,"CLUSTER: couldn't parse reroute entry in the topology file");	
				continue;	
			}
			
		}
	}
	return ret;
}

//registers proxy's availability
void zbx_dc_register_proxy_availability(u_int64_t hostid) {
	ZBX_DC_PROXY *proxy;
	WRLOCK_CACHE;
	if ( NULL!=(proxy=(ZBX_DC_PROXY*)zbx_hashset_search(&config->proxies,&hostid)) ) {
		proxy->cluster_lastheard=time(NULL);
	} 
	UNLOCK_CACHE;

};


int zbx_dc_get_proxy_hosts(u_int64_t proxyid,zbx_vector_uint64_t *hosts) {
	ZBX_DC_HOST *host;
	zbx_hashset_iter_t hosts_iter;
	u_int32_t cnt = 0;


	RDLOCK_CACHE;
	zbx_hashset_iter_reset(&config->hosts, &hosts_iter);
	
	while ( NULL!=(host=(ZBX_DC_HOST*)zbx_hashset_iter_next(&hosts_iter) )) {

		//zabbix_log(LOG_LEVEL_INFORMATION,"CLUSTER: checking host %ld cluster server id is %ld, cluster state is %d", host->hostid,
		//host->cluster_server_host_id, host->cluster_state );

		if ( (host->proxy_hostid == proxyid && 0 == CONFIG_CLUSTER_SERVER_ID ) || //non cluster mode - all monitored hosts
			 (host->cluster_server_host_id == proxyid && ZBX_CLUSTER_HOST_STATE_ACTIVE_PROXY ==host->cluster_state && 
			 			CONFIG_CLUSTER_SERVER_ID > 0) ||  //cluster mode - all monitored hosts active for a proxy on this server
				HOST_STATUS_NOT_MONITORED == host->status || HOST_STATUS_TEMPLATE == host->status) { //all non-monitored things which are probably templates
				 zbx_vector_uint64_append(hosts,host->hostid);
				 cnt++;
			 } 
	} 
	UNLOCK_CACHE;
	return cnt;
}
 
/****************************************************************************
 * Creates struct for a worker and saves it to config cache
 * 
 * 
 * **************************************************************************/
/*
int zbx_dc_add_ext_worker(char *path, char *params, char* socket_file, int max_calls, int timeout, int mode_to_writer, int mode_from_writer) {
	
	const char *str=NULL;
	GLB_EXT_WORKER worker;

	bzero(&worker,sizeof(GLB_EXT_WORKER));

	worker.max_calls=max_calls;
//	worker.to_runner_mode = mode_to_writer;
//	worker.from_runner_mode = mode_from_writer;
	worker.timeout=timeout;
	//zbx_strlcpy(worker.path,path,strlen(path));
	
	
	WRLOCK_CACHE;
	//adding the worker to the workers hashset
	worker.path=zbx_strpool_intern(path);
//	worker.params=zbx_strpool_intern(params);
//	worker.path_to_socket=zbx_strpool_intern(socket_file);
	//generating socket file name:
	//

	worker.workerid=config->ext_workers.num_data;

	zabbix_log(LOG_LEVEL_INFORMATION,"Added worker id %ld", worker.workerid);	
	zbx_hashset_insert(&config->ext_workers,&worker,sizeof(GLB_EXT_WORKER));
	UNLOCK_CACHE;

	
}
*/
/*
zbx_uint64_t zbx_dc_get_ext_worker(GLB_EXT_WORKER *worker, char *path ) {
	GLB_EXT_WORKER *free_worker;

	zbx_hashset_iter_t iter;

	
	WRLOCK_CACHE;

	zbx_hashset_iter_reset(&config->ext_workers,&iter);
	//finding a free one
	while (NULL != (free_worker = zbx_hashset_iter_next(&iter) ) ) { 
			//zabbix_log(LOG_LEVEL_INFORMATION,"cmp %s to %s",free_worker->path,path);
			if (!strcmp(free_worker->path,path) && !free_worker->used ) break;
	}

	if (NULL != free_worker) {
		free_worker->used=1;
		memcpy(worker,free_worker,sizeof(GLB_EXT_WORKER));
	}	
	
	UNLOCK_CACHE;

	if (free_worker) return worker->workerid;
	return FAIL;
}

zbx_uint64_t zbx_dc_return_ext_worker(GLB_EXT_WORKER *worker) {
	
	GLB_EXT_WORKER *dc_worker;
	int ret=FAIL;
	WRLOCK_CACHE;

	if (NULL != (dc_worker = zbx_hashset_search(&config->ext_workers,worker) )) { 
			//zabbix_log(LOG_LEVEL_INFORMATION,"Retruning worker %ld",dc_worker->workerid);
			//there might be pipe nums and counter changes, saving then
			if (! dc_worker->used ) THIS_SHOULD_NEVER_HAPPEN; 

			dc_worker->calls=worker->calls;
			dc_worker->pid=worker->pid;
			//dc_worker->from_runner=worker->from_runner;
			//dc_worker->to_runner=worker->to_runner;
			//and marking the worker as free
			dc_worker->used=0;
			ret = SUCCEED; 
						
	}
	
	UNLOCK_CACHE;
	return ret;

}
*/
int zbx_dc_get_item_type(zbx_uint64_t itemid, int *value_type) {
	ZBX_DC_ITEM *item;
	int ret=FAIL;

	RDLOCK_CACHE;
	
	if (NULL != (item=zbx_hashset_search(&config->items,&itemid))) {
		*value_type=item->value_type;
		ret = SUCCEED;
	}

	UNLOCK_CACHE;
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_lastvalues_json                                       *
 *                                                                            *
 * Purpose: generates json object holding all lastvalues from the requested   *
 * 			itemids															  *
 * 		data[{"itemid":1234,"lastclock":2342343,"change":45.3,"value":234},   *
 * 			{....}, ]														  *
 *                                                                            *
 ******************************************************************************/
int glb_dc_get_lastvalues_json(zbx_vector_uint64_t *itemids, struct zbx_json *json) {
	
	ZBX_DC_ITEM *item;
	int i;
	
	ZBX_DC_HISTORY	hr;

	RDLOCK_CACHE;
	
	zbx_json_addarray(json,ZBX_PROTO_TAG_DATA);

	for (i=0; i<itemids->values_num; i++) {
		if ( NULL != (item=zbx_hashset_search(&config->items,&itemids->values[i])) )
		 {
			zbx_json_addobject(json,NULL);

			zbx_json_adduint64(json,"itemid",item->itemid);
			zbx_json_adduint64(json,"lastclock",item->lastclock);
			zbx_json_adduint64(json,"nextcheck",item->nextcheck);
			zbx_json_addstring(json,"error",item->error,ZBX_JSON_TYPE_STRING);

			switch	( item->value_type ) {
				case ITEM_VALUE_TYPE_UINT64:
					zbx_json_adduint64(json,"value",item->lastvalue.ui64);
					zbx_json_addint64(json,"prev_value",item->prevvalue.ui64);
					break;
				case ITEM_VALUE_TYPE_TEXT:
				case ITEM_VALUE_TYPE_STR:
					zbx_json_addstring(json,"value",item->lastvalue.str,ZBX_JSON_TYPE_STRING);
					break;
				case ITEM_VALUE_TYPE_FLOAT:
					zbx_json_addfloat(json,"value",item->lastvalue.dbl);
					zbx_json_addfloat(json,"prev_value",item->prevvalue.dbl);
					break;
			}	
			
			zbx_json_close(json);
		}
	}	
	zbx_json_close(json);
	UNLOCK_CACHE;

	return SUCCEED;
}
/************************************************
* fetches items host names and keys to send to 	*
* trends storage
* when i come back this func i should reconsider
* implementing something like go contexts for
* pass-through item processing
************************************************/
void DC_get_trends_items_keys(ZBX_DC_TREND *trends, int trends_num) {
	int i;

	ZBX_DC_ITEM *item;
	ZBX_DC_HOST *host;

	RDLOCK_CACHE;

	for (i =0 ; i < trends_num; i++) {
		if ( (NULL!=(item=zbx_hashset_search(&config->items,&trends[i].itemid))) && 
			(NULL!=(host=zbx_hashset_search(&config->hosts,&item->hostid)))) {
				trends[i].host_name=zbx_strdup(NULL,host->host);
				trends[i].item_key=zbx_strdup(NULL,item->key);
		} else {
				trends[i].host_name=zbx_strdup(NULL,"");
				trends[i].item_key=zbx_strdup(NULL,"");
		}
	}

	UNLOCK_CACHE;
}

/*******************************************************
 * 
 * 
 * 
 * 
 * *****************************************************/
void DC_UpdatePreprocStat(u_int64_t no_preproc, u_int64_t local_preproc) {


	WRLOCK_CACHE;
	config->no_preproc += no_preproc;
	config->local_preproc += local_preproc;
	UNLOCK_CACHE;

}

void DC_GetPreprocStat(u_int64_t *no_prpeproc, u_int64_t *local_preproc) {
	RDLOCK_CACHE;
	
	*no_prpeproc=config->no_preproc;
	*local_preproc=config->local_preproc;
	
	//config->no_preproc = 0;
	//config->local_preproc = 0;
	
	UNLOCK_CACHE;
}