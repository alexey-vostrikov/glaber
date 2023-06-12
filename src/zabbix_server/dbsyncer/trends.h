/*
** Copyright Glaber 2018-2023
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

#ifndef PROC_TRENDS_H
#define PROC_TRENDS_H

#include "zbxcommon.h"
#include "glb_history.h"


int trends_account_metric(const ZBX_DC_HISTORY *h);

int trends_init_cache();
int trends_destroy_cache();

void trend_set_hostname(trend_t *trend, const char *hostname);
void trend_set_itemkey(trend_t *trend, const char *itemkey);

char *trend_get_hostname(trend_t *trend);
char *trend_get_itemkey(trend_t *trend);

#endif