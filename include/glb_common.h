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
#ifndef GLB_COMMON_H
#define GLB_COMMON_H

#define GLB_ICMP 20
#define ZBX_ICMP 21

/* severity */
#define SEVERITY_NOT_CLASSIFIED	0
#define SEVERITY_INFORMATION	1
#define SEVERITY_WARNING	2
#define SEVERITY_AVERAGE	3
#define SEVERITY_HIGH		4
#define SEVERITY_DISASTER	5
#define SEVERITY_COUNT		6	/* number of trigger severities */

#define TRIGGER_SEVERITY_UNDEFINED	255

#define HAVE_GLB_TESTS 1

#endif