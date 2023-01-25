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

#include "trapper_request.h"

#include "zbxcommshigh.h"
#include "proxyconfigwrite/proxyconfig_write.h"

extern int		CONFIG_TIMEOUT;

static void	active_passive_misconfig(zbx_socket_t *sock)
{
	char	*msg = NULL;

	msg = zbx_dsprintf(msg, "misconfiguration error: the proxy is running in the active mode but server at \"%s\""
			" sends requests to it as to proxy in passive mode", sock->peer);

	zabbix_log(LOG_LEVEL_WARNING, "%s", msg);
	zbx_send_proxy_response(sock, FAIL, msg, CONFIG_TIMEOUT);
	zbx_free(msg);
}

int	trapper_process_request(const char *request, zbx_socket_t *sock, const struct zbx_json_parse *jp,
		const zbx_config_tls_t *zbx_config_tls, zbx_get_program_type_f get_program_type_cb)
{
	if (0 == strcmp(request, ZBX_PROTO_VALUE_PROXY_CONFIG))
	{
		if (0 != (get_program_type_cb() & ZBX_PROGRAM_TYPE_PROXY_PASSIVE))
		{
			zbx_recv_proxyconfig(sock, zbx_config_tls);
			return SUCCEED;
		}
		else if (0 != (get_program_type_cb() & ZBX_PROGRAM_TYPE_PROXY_ACTIVE))
		{
			/* This is a misconfiguration: the proxy is configured in active mode */
			/* but server sends requests to it as to a proxy in passive mode. To  */
			/* prevent logging of this problem for every request we report it     */
			/* only when the server sends configuration to the proxy and ignore   */
			/* it for other requests.                                             */
			active_passive_misconfig(sock);
			return SUCCEED;
		}
	}
	ZBX_UNUSED(request);
	ZBX_UNUSED(sock);
	ZBX_UNUSED(jp);

	return FAIL;
}
