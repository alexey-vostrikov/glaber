
/*
** Copyright (C) 2001-2023 Glaber
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
#ifndef GLB_HISTORY_H
#define GLB_HISTORY_H

#define GLB_HISTORY_GET_NON_INTERACTIVE 2
#define GLB_HISTORY_GET_INTERACTIVE 3

#include "zbxcommon.h"
#include "zbxtime.h"
#include "zbxjson.h"
/* value_avg_t structure is used for item average value trend calculations. */
/*                                                                          */
/* For double values the average value is calculated on the fly with the    */
/* following formula: avg = (dbl * count + value) / (count + 1) and stored  */
/* into dbl member.                                                         */
/* For uint64 values the item values are summed into ui64 member and the    */
/* average value is calculated before flushing trends to database:          */
/* avg = ui64 / count                                                       */
typedef union
{
	double		dbl;
	zbx_uint128_t	ui64;
}
value_avg_t;


typedef union
{
	double		dbl;
	zbx_uint64_t	ui64;
	char		*str;
	char		*err;
	zbx_log_value_t	*log;
}
zbx_history_value_t;

typedef struct
{
	zbx_timespec_t		timestamp;
	zbx_history_value_t	value;
}
zbx_history_record_t;

ZBX_VECTOR_DECL(history_record, zbx_history_record_t)

#define zbx_history_record_vector_create(vector)	zbx_vector_history_record_create(vector)
void	zbx_history_record_vector_destroy(zbx_vector_history_record_t *vector, int value_type);
int		zbx_history_record_compare_desc_func(const zbx_history_record_t *d1, const zbx_history_record_t *d2);
void	zbx_history_record_vector_clean(zbx_vector_history_record_t *vector, int value_type);
void	zbx_history_record_clear(zbx_history_record_t *value, int value_type);
void	zbx_history_value2variant(const zbx_history_value_t *value, unsigned char value_type, zbx_variant_t *var);
int     history_record_float_compare(const zbx_history_record_t *d1, const zbx_history_record_t *d2);
void	zbx_history_value_print(char *buffer, size_t size, const zbx_history_value_t *value, int value_type);




typedef struct
{
	zbx_uint64_t	itemid;
	zbx_uint64_t	hostid;
	
	unsigned char	value_type;
	zbx_history_value_t	value;
	
	unsigned char	state;
	zbx_uint64_t	lastlogsize;
	zbx_timespec_t	ts;
	int		mtime;
		
	unsigned char	flags;		/* see ZBX_DC_FLAG_* */
	

	char *host_name; /*hostname to log to history */
	char *item_key; /* name of metric*/
}
ZBX_DC_HISTORY;

typedef union
{
	double		dbl;
	zbx_uint64_t	ui64;
} trend_value_t;

typedef struct 
{
	zbx_uint64_t	itemid;
    zbx_uint64_t	hostid;
    trend_value_t	value_min;
	trend_value_t	value_avg; //until exported we keep the sum, calc avg on export
	trend_value_t	value_max;
	int		account_hour;
	int		num;
	unsigned char	value_type; //inherited from variant
	char *host_name; /*hostname to log to history */
	char *item_key; /* name of metric*/
} trend_t;


int glb_history_init(char **history_modules, char **error);

int glb_history_add_history(ZBX_DC_HISTORY *history, int history_num);
int glb_history_get_history(zbx_uint64_t itemid, int value_type, int start, int count, int end, unsigned char interactive, zbx_vector_history_record_t *values);
int glb_history_add_trend(trend_t *trend);

int glb_history_get_trends_json(zbx_uint64_t itemid, int value_type, int start, int end, struct zbx_json *json);
 
int glb_history_get_trends_aggregates_json(zbx_uint64_t itemid, int value_type, int start, int end, int aggregates, struct zbx_json *json);
int glb_history_get_history_aggregates_json(zbx_uint64_t itemid, int value_type, int start, int end, int aggregates, struct zbx_json *json);

int glb_history_history_record_to_json(u_int64_t itemid, int value_type, zbx_history_record_t *record, struct zbx_json *json);
//int	glb_history_json_to_history_record(struct zbx_json_parse *jp, char value_type, zbx_history_record_t * value);
 
int	history_update_log_enty_severity(ZBX_DC_HISTORY *h, int severity, u_int64_t eventid, u_int64_t triggerid, int value);

#endif