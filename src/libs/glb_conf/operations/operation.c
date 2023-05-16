
/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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

#include "operations.h"
#include "zbxcommon.h"
#include "log.h"
#include "zbxalgo.h"
#include "operation.h"

typedef struct glb_operation_t {
    char *name;
};

void glb_operation_free(mem_funcs_t *memf, glb_operation_t *operation) {
    HALT_HERE("Unimplemeted");
}

glb_operation_t *glb_operation_create_from_json(mem_funcs_t *memf, struct zbx_json_parse *jp) {
    HALT_HERE("Unimplemeted");
}