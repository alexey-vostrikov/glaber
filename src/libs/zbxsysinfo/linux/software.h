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

#ifndef ZABBIX_SOFTWARE_H
#define ZABBIX_SOFTWARE_H

#include "zbxtypes.h"
#include "zbxjson.h"

typedef struct
{
	/* package manager name */
	const char	*name;
	/* if this shell command has stdout output, package manager is present */
	const char	*test_cmd;
	/* this command lists the installed packages */
	const char	*list_cmd;
	/* this command lists the installed packages with details */
	const char	*details_cmd;
	/* for non-standard list (not just package name per line) specify a parser function to get the package name */
	int		(*list_parser)(const char *line, char *package, size_t max_package_len);
	/* specify a parser function to add package details to JSON */
	void		(*details_parser)(const char *manager, const char *line, const char *regex, struct zbx_json *json);
}
ZBX_PACKAGE_MANAGER;

#endif	/* ZABBIX_SOFTWARE_H */
