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

#include "postinit.h"
#include "zbxserver.h"

#include "db_lengths.h"
#include "zbxtasks.h"
#include "log.h"
#include "zbxnum.h"
#include "zbxdbwrap.h"

#define ZBX_HIST_MACRO_NONE		(-1)
#define ZBX_HIST_MACRO_ITEM_VALUE	0
#define ZBX_HIST_MACRO_ITEM_LASTVALUE	1

/******************************************************************************
 *                                                                            *
 * Purpose: gets the total number of triggers on system                       *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: The total number of triggers on system.                      *
 *                                                                            *
 ******************************************************************************/
static int	get_trigger_count(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		triggers_num;

	result = DBselect("select count(*) from triggers");
	if (NULL != (row = DBfetch(result)))
		triggers_num = atoi(row[0]);
	else
		triggers_num = 0;
	zbx_db_free_result(result);

	return triggers_num;
}

/******************************************************************************
 *                                                                            *
 * Purpose: checks if this is historical macro that cannot be expanded for    *
 *          bulk event name update                                            *
 *                                                                            *
 * Parameters: macro      - [IN] the macro name                               *
 *                                                                            *
 * Return value: ZBX_HIST_MACRO_* defines                                     *
 *                                                                            *
 ******************************************************************************/
static int	is_historical_macro(const char *macro)
{
	if (0 == strncmp(macro, "ITEM.VALUE", ZBX_CONST_STRLEN("ITEM.VALUE")))
		return ZBX_HIST_MACRO_ITEM_VALUE;

	if (0 == strncmp(macro, "ITEM.LASTVALUE", ZBX_CONST_STRLEN("ITEM.LASTVALUE")))
		return ZBX_HIST_MACRO_ITEM_LASTVALUE;

	return ZBX_HIST_MACRO_NONE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: translates historical macro to lld macro format                   *
 *                                                                            *
 * Parameters: macro - [IN] the macro type (see ZBX_HIST_MACRO_* defines)     *
 *                                                                            *
 * Return value: the macro                                                    *
 *                                                                            *
 * Comments: Some of the macros can be converted to different name.           *
 *                                                                            *
 ******************************************************************************/
static const char	*convert_historical_macro(int macro)
{
	/* When expanding macros for old events ITEM.LASTVALUE macro would */
	/* always expand to one (latest) value. Expanding it as ITEM.VALUE */
	/* makes more sense in this case.                                  */
	const char	*macros[] = {"#ITEM.VALUE", "#ITEM.VALUE"};

	return macros[macro];
}

