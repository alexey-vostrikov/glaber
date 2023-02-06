/*
** Glaber
** Copyright (C) 2018-2042 Glaber
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
#include "zbxshmem.h"
#include "zbxalgo.h"
#include "elems_hash_tests.h"
#include "obj_index_tests.h"

#include "elems_hash_tests.c"
#include "obj_index_tests.c"

void tests_algo_run() {
    LOG_INF("Running algo tests");
    sleep(1);
    tests_elems_hash_run();
    tests_obj_index_run();
    LOG_INF("Finished algo tests");
}

