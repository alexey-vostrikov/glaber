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
#include "glb_common.h"
#ifdef HAVE_GLB_TESTS
#include "zbxcommon.h"
#include "log.h"
#include "../glb_macro.h"

void glb_macro_run_tests() {
    sleep(1);
    assert(SUCCEED == glb_macro_translate_string(" {#TOKEN2} test {TOKEN1} {$TOKEN2} some trailing data", 0, NULL, 0) );
    HALT_HERE("Macro test succesifully finished");
}
#endif