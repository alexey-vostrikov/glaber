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

#ifndef ZABBIX_PINGER_H
#define ZABBIX_PINGER_H

#include "zbxthreads.h"
#include "zbxicmpping.h"

typedef struct
{
	int			config_timeout;
}
zbx_thread_pinger_args;

ZBX_THREAD_ENTRY(pinger_thread, args);
int	zbx_parse_key_params(const char *key, const char *host_addr, icmpping_t *icmpping, char **addr, int *count,
		int *interval, int *size, int *timeout, icmppingsec_type_t *type, char *error, int max_error_len, int *use_item_addr);

#endif
