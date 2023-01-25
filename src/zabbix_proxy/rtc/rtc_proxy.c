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

#include "rtc_proxy.h"

#include "zbxdbwrap.h"
#include "zbx_rtc_constants.h"

extern int	CONFIG_PROXYMODE;

int	rtc_process_request_ex(zbx_rtc_t *rtc, int code, const unsigned char *data, char **result)
{
	ZBX_UNUSED(data);
	ZBX_UNUSED(result);

	switch (code)
	{
		case ZBX_RTC_CONFIG_CACHE_RELOAD:
			if (ZBX_PROXYMODE_PASSIVE == CONFIG_PROXYMODE)
			{
				zbx_rtc_notify(rtc, ZBX_PROCESS_TYPE_TASKMANAGER, 0, ZBX_RTC_CONFIG_CACHE_RELOAD, NULL,
						0);
				return SUCCEED;
			}
			return FAIL;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: process runtime control option and print result                   *
 *                                                                            *
 * Parameters: option   - [IN] the runtime control option                     *
 *             error    - [OUT] error message                                 *
 *                                                                            *
 * Return value: SUCCEED - the runtime control option was processed           *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	rtc_process(const char *option, char **error)
{
	zbx_uint32_t	code = ZBX_RTC_UNKNOWN;
	char			*data = NULL;

	if (SUCCEED != zbx_rtc_parse_options(option, &code, &data, error))
		return FAIL;

	if (ZBX_RTC_UNKNOWN == code)
	{
		*error = zbx_dsprintf(NULL, "unknown option \"%s\"", option);
		return FAIL;
	}

	return zbx_rtc_async_exchange(&data, code, error);
}
