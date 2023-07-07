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

#ifndef ZABBIX_ZBXPROMETHEUS_H
#define ZABBIX_ZBXPROMETHEUS_H

#include "zbxalgo.h"

typedef struct
{
	char	*name;
	char	*value;
}
zbx_prometheus_label_t;

ZBX_PTR_VECTOR_DECL(prometheus_label, zbx_prometheus_label_t *)

typedef struct
{
	char				*metric;
	char				*value;
	zbx_vector_prometheus_label_t	labels;
	char				*raw;
}
zbx_prometheus_row_t;

ZBX_PTR_VECTOR_DECL(prometheus_row, zbx_prometheus_row_t *)

typedef struct
{
	char		*label;
	zbx_hashset_t	index;
}
zbx_prometheus_label_index_t;

ZBX_PTR_VECTOR_DECL(prometheus_label_index, zbx_prometheus_label_index_t *)

typedef struct
{
	zbx_vector_prometheus_row_t		rows;
	zbx_vector_prometheus_label_index_t	indexes;
	pthread_rwlock_t			index_lock;
}
zbx_prometheus_t;

int	zbx_prometheus_init(zbx_prometheus_t *prom, const char *data, char **error);
void	zbx_prometheus_clear(zbx_prometheus_t *prom);
int	zbx_prometheus_pattern_ex(zbx_prometheus_t *prom, const char *filter_data, const char *request,
		const char *output, char **value, char **error);

int	zbx_prometheus_pattern(const char *data, const char *filter_data, const char *request, const char *output,
		char **value, char **error);
int	zbx_prometheus_to_json(const char *data, const char *filter_data, char **value, char **error);

int	zbx_prometheus_validate_filter(const char *pattern, char **error);
int	zbx_prometheus_validate_label(const char *label);

#endif
