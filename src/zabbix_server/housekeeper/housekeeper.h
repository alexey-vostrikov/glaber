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

#ifndef ZABBIX_HOUSEKEEPER_H
#define ZABBIX_HOUSEKEEPER_H

#include "zbxthreads.h"

extern int	CONFIG_HOUSEKEEPING_FREQUENCY;
extern int	CONFIG_MAX_HOUSEKEEPER_DELETE;

typedef struct
{
	zbx_get_program_type_f		zbx_get_program_type_cb_arg;
	struct zbx_db_version_info_t	*db_version_info;
}
zbx_thread_housekeeper_args;


ZBX_THREAD_ENTRY(housekeeper_thread, args);

#endif
