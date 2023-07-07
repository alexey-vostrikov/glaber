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

#include "zabbix_stats.h"

#include "zbxcacheconfig.h"
#include "zbxlld.h"
#include "log.h"
#include "zbxtrends.h"
#include "zbxha.h"
#include "zbxconnector.h"

/******************************************************************************
 *                                                                            *
 * Purpose: get program type (server) specific internal statistics            *
 *                                                                            *
 * Parameters: json         - [IN/OUT]                                        *
 *             arg          - [IN] anonymous argument provided by register    *
 *                                                                            *
 * Comments: This function is used to gather server specific internal         *
 *           statistics.                                                      *
 *                                                                            *
 ******************************************************************************/
void	zbx_server_stats_ext_get(struct zbx_json *json, const void *arg)
{
	zbx_uint64_t			queue_size, connector_queue_size;
	char				*value, *error = NULL;
	zbx_tfc_stats_t			tcache_stats;

	ZBX_UNUSED(arg);

	/* zabbix[lld_queue] */
	if (SUCCEED == zbx_lld_get_queue_size(&queue_size, &error))
	{
		zbx_json_adduint64(json, "lld_queue", queue_size);
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get LLD queue size: %s", error);
		zbx_free(error);
	}

	/* zabbix[connector_queue] */
	if (SUCCEED == zbx_connector_get_queue_size(&connector_queue_size, &error))
	{
		zbx_json_adduint64(json, "connector_queue", queue_size);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot get connector queue size: %s", error);
		zbx_free(error);
	}


	/* zabbix[triggers] */
	zbx_json_adduint64(json, "triggers", DCget_trigger_count());



	if (SUCCEED == zbx_ha_get_nodes(&value, &error))
	{
		zbx_json_addraw(json, "ha", value);
		zbx_free(value);
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get HA node data: %s", error);
		zbx_free(error);
	}

	if (SUCCEED == zbx_proxy_discovery_get(&value, &error))
	{
		zbx_json_addraw(json, "proxy", value);
		zbx_free(value);
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get proxy data: %s", error);
		zbx_free(error);
	}
}
