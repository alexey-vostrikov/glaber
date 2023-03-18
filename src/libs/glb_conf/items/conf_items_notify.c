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

#include "log.h"
#include "zbxalgo.h"

void DC_notify_changed_items(zbx_vector_uint64_t *items);

void conf_items_notify_changes(zbx_vector_uint64_t *changed_items) {
    int i;
    
//    for (i = 0; i < changed_items->values_num; i++) {
//        DEBUG_ITEM(changed_items->values[i],"Sending item change notification to the item");
//    }

    DC_notify_changed_items(changed_items);
}