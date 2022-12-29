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

/* this is simple monitoring engine to export internal metrics
via prometheus protocol via standart monitoring url

now it's NOT follows standards as it doesn't support HELP and TYPE keywords
*/

//TODO idea for improvement - implement a kind of a buffer pool to avoid alloc cluttering

#include "zbxcommon.h"
#include "zbxalgo.h"

void test_worker_steps();