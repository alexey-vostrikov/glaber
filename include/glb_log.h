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

#ifndef GLABER_LOG_H
#define GLABER_LOG_H

#include "log.h"

#define LOG_DBG(...) zabbix_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INF(...) zabbix_log(LOG_LEVEL_INFORMATION, __VA_ARGS__)
#define LOG_WRN(...) zabbix_log(LOG_LEVEL_WARNING, __VA_ARGS__)


#ifndef DEBUG_ITEM
u_int64_t DC_get_debug_item();
#define DEBUG_ITEM(id, message,...) {if ( DC_get_debug_item() == id && id > 0 )\
		zabbix_log(LOG_LEVEL_INFORMATION,  "In %s:%d, debug_item:%ld, " message, __FILE__, __LINE__, id, ##__VA_ARGS__);}
#endif

#ifndef DEBUG_TRIGGER
u_int64_t DC_get_debug_trigger();
#define DEBUG_TRIGGER(id, message,...) if ( DC_get_debug_trigger() == id && id > 0 )\
		zabbix_log(LOG_LEVEL_INFORMATION,  "In %s:%d, debug_trigger:%ld, " message, __FILE__, __LINE__, id, ##__VA_ARGS__);
#endif

#ifndef HALT_HERE
#define HALT_HERE(message,...) { zabbix_log(LOG_LEVEL_WARNING, "In %s:%d, intentional halt: " message, __FILE__, __LINE__, ##__VA_ARGS__); zbx_backtrace();  if (time(NULL)) exit(-1); }
#endif

#ifndef RUN_ONCE_IN

#define RUN_ONCE_IN(freq) { \
        static int __lastcall= 0; \
        int __now = time(NULL); \
        if (__lastcall + freq > __now) \
           return; \
        __lastcall = __now; \
        }

#define RUN_ONCE_IN_WITH_RET(freq, ret) { \
        static int __lastcall= 0; \
        int __now = time(NULL); \
        if (__lastcall + freq > __now) \
            return ret; \
        __lastcall = __now; \
        }
#endif

#ifndef TIME_MEASURE
#define TIME_MEASURE
/*usage:
 { //something that happens frequently, might be module-wise
	INIT_MEASURE(some_usefull_name); // vars will be static

    START_MEASURE(some_usefull_name);
	some_long_procedure;
	STOP_MEASURE(some_usefull_name);
 }
*/
#define INIT_MEASURE(name) double __time_start_##name; \
	static double __time_summ_##name = 0;\
	static u_int64_t __time_count_##name = 0;\
	static int __time_report_##name = 0; \
	if (time(NULL) > __time_report_##name + 10) {\
		LOG_INF("Time spent in "#name": %f sec, calls: %ld", __time_summ_##name, __time_count_##name);\
		__time_report_##name = time(NULL);\
	}
#define START_MEASURE(name) __time_start_##name = zbx_time(); 
#define STOP_MEASURE(name) __time_summ_##name += zbx_time() - __time_start_##name;__time_count_##name++;
#endif
#endif

