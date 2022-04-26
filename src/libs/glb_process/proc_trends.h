/*
** Copyright Glaber
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

#include "common.h"

#include "zbxalgo.h"
#include "dbcache.h"
#include "log.h"
#include "process.h"

typedef union
{
	double		dbl;
	zbx_uint64_t	ui64;
} trend_value_t;

typedef struct
{
	zbx_uint64_t	itemid;
    trend_value_t	value_min;
	trend_value_t	value_avg; //until exported we keep the sum, calc avg on export
	trend_value_t	value_max;
	int		account_hour;
	int		num;
	unsigned char	value_type; //inherited from variant
} trend_t;


int trends_account_metric(metric_t *metric , metric_processing_data_t *proc_data);

int trends_init_cache();
int trends_destroy_cache();

#endif