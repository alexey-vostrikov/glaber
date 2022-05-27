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


#ifndef GLB_PROCESS_H
#define GLB_PROCESS_H

#include "common.h"
#include "zbxalgo.h"
#include "module.h"
#include "zbxvariant.h"
#include "db.h"


//#define ZBX_DC_FLAG_META	0x01	/* contains meta information (lastlogsize and mtime) */
#define PROCESS_FLAG_NOVALUE	0x02	/* entry contains no value */
#define PROCESS_FLAG_LLD		0x04	/* discovery value */
#define PROCESS_FLAG_UNDEF	0x08	/* unsupported or undefined (delta calculation failed) value */
//#define PROCESS_FLAG_NOHISTORY	0x10	/* values should not be kept in history */
//#define PROCESS_FLAG_NOTRENDS	0x20	/* values should not be kept in trends */
#define PROCESS_METRIC_FLAG_EVENTLOG 0x40 /* log is event sourced */
#define METRIC_BUF_LEN 256

typedef struct
{
	zbx_uint64_t	itemid;
	zbx_uint64_t	hostid;
		
	int 		sec; 

	variant_t	value;

	unsigned char	flags;		/* see ZBX_DC_FLAG_* */
	unsigned char	state;

	char str_buffer[METRIC_BUF_LEN];
	//if str ptr == str_buffer then it doesn't need 
	//to be freed
} metric_t;


//data required to process metric
typedef struct {
	u_int64_t hostid;
	u_int64_t itmeid;

	char hostname[MAX_ZBX_HOSTNAME_LEN + 1];
	char key[ITEM_KEY_LEN * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];
	
	unsigned char value_type;

} metric_processing_data_t;


int processing_ipc_init(size_t ipc_mem_size);

//metric processing loop entry point 
int process_metric_values(int max_values, int process_num) ;


//funcs for temporary transition from zabbix structs (agent result, timespec)
void	create_metric_from_agent_result(u_int64_t hostid, u_int64_t itemid, int sec, unsigned char state, AGENT_RESULT *result, metric_t * metric, char *error);
u_int64_t ts_to_msec(zbx_timespec_t *ts);

//sends a value to processing
void    send_metric_to_processing(metric_t *metric);
//gets metric metadata required for the processing
int 	fetch_metric_processing_data(u_int64_t itemid, metric_processing_data_t *proc_data);

#endif