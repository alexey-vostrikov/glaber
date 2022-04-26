
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

#ifndef CONF_ITEMS_H
#define CONF_ITEMS_H

#include "common.h"

typedef struct {
    u_int64_t itemid;
    u_int64_t hostid;
    u_int64_t host_proxy_hostid;
    unsigned char value_type;
} item_func_eval_conf_t;


int conf_items_get_func_eval_conf(u_int64_t itemid, item_func_eval_conf_t *item_conf, char **error);

#endif