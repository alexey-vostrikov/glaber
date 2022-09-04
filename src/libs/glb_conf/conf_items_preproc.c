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
#include "zbxvariant.h"
#include "log.h"
#include "memalloc.h"

typedef struct
{
    zbx_vector_ptr_t	preproc_ops;
} preproc_ops_t;


preproc_ops_t conf_item_init_preproc_ops_json(char *json) {

}

void conf_item_free_preproc_ops(preproc_ops_t *ops) {

}

//void conf_item_process_preproc_ops(preproc_ops_t *ops, preproc_cb_t proc_func) {
//
//}