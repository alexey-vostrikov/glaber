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
#include "common.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "log.h"

/* this mostly copy-paste of the original triggers queue with some fetaures:
   each queue has it's own lock queues are created per - syncer process 

   also, all timers are placed in the syncer's heap

   to notify the syncer about changes of triggers a separate trigger_update ipc queue is used
*/ 

typedef struct
{
	zbx_uint64_t		objectid;
	zbx_uint64_t		triggerid;
	zbx_uint32_t		type;
	zbx_time_unit_t		trend_base;
	unsigned char		lock;		/* 1 if the timer has locked trigger, 0 otherwise */
	int			revision;	/* revision */
	zbx_timespec_t		eval_ts;	/* the history time for which trigger must be recalculated */
	zbx_timespec_t		exec_ts;	/* real time when the timer must be executed */
	const char		*parameter;	/* function parameters (for trend functions) */
}
trigger_timer_t;

typedef struct { //notification message, for now it's only used to inform that timer trigger needs to be reshceduled
	u_int64_t id;
} notify_t;

typedef enum {
    PROCESS_TRIGGER = 1
} events_types_t;

/* specifies if trigger expression/recovery expression has timer functions */
/* (date, time, now, dayofweek or dayofmonth)                              */
#define TRIGGER_TIMER_DEFAULT		0x00
#define TRIGGER_TIMER_EXPRESSION		0x01
#define TRIGGER_TIMER_RECOVERY_EXPRESSION	0x02


#define TRIGGER_TIMER_NONE			0x0000
#define TRIGGER_TIMER_TRIGGER		0x0001
#define TRIGGER_TIMER_FUNCTION_TIME		0x0002
#define TRIGGER_TIMER_FUNCTION_TREND	0x0004
#define TRIGGER_TIMER_FUNCTION		(TRIGGER_TIMER_FUNCTION_TIME | TRIGGER_TIMER_FUNCTION_TREND)


int processing_notify_changed_trigger(uint64_t new_triggerid);
int processing_notify_flush();

int processing_trigger_timers_init();

int process_time_triggers(int *processed_triggers, int max_triggers, int process_num);