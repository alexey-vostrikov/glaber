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

#include "zbxcommon.h"
#include "log.h"
#include "zbxvariant.h"
#include "metric.h"
#include "zbxtime.h"

void metric_set_time(metric_t *metric, u_int64_t *mstime) {
    if (NULL == mstime) {
        metric->mstime = glb_ms_time();
        return;
    }

    metric->mstime = *mstime;
}
