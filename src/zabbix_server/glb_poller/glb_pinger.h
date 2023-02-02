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

#ifndef GLABER_PINGER_H
#define GLABER_PINGER_H
#include "zbxicmpping.h"
#include "glb_poller.h"

#define GLB_ICMP_NAME "glbmap"
#define ZBX_ICMP_NAME "fping"

#define GLB_DEFAULT_ICMP_TIMEOUT 500
#define GLB_DEFAULT_ICMP_INTERVAL 1000
#define GLB_DEFAULT_ICMP_SIZE 68
#define GLB_PINGER_DEFAULT_RTT 20

void glb_pinger_init();

#endif
