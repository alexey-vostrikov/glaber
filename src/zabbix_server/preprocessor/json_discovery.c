/*
** Glaber
** Copyright (C) 2001-2100 Glaber
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
#include "zbxjson.h"
#include "zbxalgo.h"

typedef struct {
	zbx_hashset_t all_data;
	zbx_hashset_t new_data;
	int last_new_dump;
	int last_full_dump;
} json_discovery_t;

static json_discovery_t conf = {0};

void json_discovery_add_data(u_int64_t itemid, const char *json) {

}

const char * json_discovery_get_data(u_int64_t itemid, int timeout){

}

