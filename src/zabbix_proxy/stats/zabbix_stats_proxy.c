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

#include "zabbix_stats.h"
#include "zbxdbwrap.h"
#include "zbxcomms.h"
#include "zbxjson.h"
#include "zbxstr.h"

/******************************************************************************
 *                                                                            *
 * Purpose: get program type (proxy) specific internal statistics             *
 *                                                                            *
 * Parameters: json       - [OUT] the json data                               *
 *             zbx_config - [IN] proxy config                                 *
 *                                                                            *
 * Comments: This function is used to gather proxy specific internal          *
 *           statistics.                                                      *
 *                                                                            *
 ******************************************************************************/
void	zbx_zabbix_stats_ext_get(struct zbx_json *json, const zbx_config_comms_args_t *zbx_config)
{
	unsigned int	encryption;

	zbx_json_addstring(json, "name", ZBX_NULL2EMPTY_STR(zbx_config->hostname), ZBX_JSON_TYPE_STRING);

	if (ZBX_PROXYMODE_PASSIVE == zbx_config->proxymode)
	{
		zbx_json_addstring(json, "passive", "true", ZBX_JSON_TYPE_INT);
		encryption = zbx_config->zbx_config_tls->accept_modes;
	}
	else
	{
		zbx_json_addstring(json, "passive", "false", ZBX_JSON_TYPE_INT);
		encryption = zbx_config->zbx_config_tls->connect_mode;
	}

	zbx_json_addstring(json, ZBX_TCP_SEC_UNENCRYPTED_TXT,
			0 < (encryption & ZBX_TCP_SEC_UNENCRYPTED) ? "true" : "false", ZBX_JSON_TYPE_INT);

	zbx_json_addstring(json, ZBX_TCP_SEC_TLS_PSK_TXT,
			0 < (encryption & ZBX_TCP_SEC_TLS_PSK) ? "true" : "false", ZBX_JSON_TYPE_INT);

	zbx_json_addstring(json, ZBX_TCP_SEC_TLS_CERT_TXT,
			0 < (encryption & ZBX_TCP_SEC_TLS_CERT) ? "true" : "false", ZBX_JSON_TYPE_INT);

	return;
}
