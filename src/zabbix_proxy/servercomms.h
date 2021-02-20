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

#ifndef ZABBIX_SERVERCOMMS_H
#define ZABBIX_SERVERCOMMS_H

extern char	*CONFIG_SOURCE_IP;
extern char	*CONFIG_SERVER;
extern int	CONFIG_SERVER_PORT;
extern char	*CONFIG_HOSTNAME;

#include "comms.h"

int	connect_to_server(zbx_socket_t *sock, int timeout, int retry_interval);
int	connect_to_a_server(zbx_socket_t *sock, char *SERVER_NAME, int timeout, int retry_interval);
void	disconnect_server(zbx_socket_t *sock);

int	get_data_from_server(zbx_socket_t *sock, const char *request, char **error);
int	put_data_to_server(zbx_socket_t *sock, struct zbx_json *j, char **error);

#endif
