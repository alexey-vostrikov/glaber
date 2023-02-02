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

#ifndef ZABBIX_ZBXICMPPING_H
#define ZABBIX_ZBXICMPPING_H

#include "zbxcommon.h"

typedef struct
{
	zbx_get_config_str_f	get_source_ip;
	zbx_get_config_str_f	get_fping_location;
#ifdef HAVE_IPV6
	zbx_get_config_str_f	get_fping6_location;
#endif
	zbx_get_config_str_f	get_tmpdir;
}
zbx_config_icmpping_t;

typedef struct
{
	char	*addr;
	double	min;
	double	sum;
	double	max;
	int	rcv;
	int	cnt;
	char	*status;	/* array of individual response statuses: 1 - valid, 0 - timeout */
}
ZBX_FPING_HOST;

typedef enum
{
	ICMPPING = 0,
	ICMPPINGSEC,
	ICMPPINGLOSS
}
icmpping_t;

typedef enum
{
	ICMPPINGSEC_MIN = 0,
	ICMPPINGSEC_AVG,
	ICMPPINGSEC_MAX
}
icmppingsec_type_t;

typedef struct
{
	int			count;
	int			interval;
	int			size;
	int			timeout;
	zbx_uint64_t		itemid;
	char			*addr;
	icmpping_t		icmpping;
	icmppingsec_type_t	type;
}
icmpitem_t;

void	zbx_init_library_icmpping(const zbx_config_icmpping_t *config);

int	zbx_ping(ZBX_FPING_HOST *hosts, int hosts_count, int count, int period, int size, int timeout,
		char *error, size_t max_error_len);

#endif
