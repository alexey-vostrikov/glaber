/*
** Glaber
** Copyright (C) 2001-2028 Glaber JSC
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
#ifndef GLABER_POLLER_AGENT_H
#define GLABER_POLLER_AGENT_H

#include "glb_poller.h"
#define DEFAULT_TCP_HOST_CONTENTION 4


#define ASYNC_IO_TCP_PROC_FINISH	1
#define ASYNC_IO_TCP_PROC_CONTINUE	2

int     glb_poller_agent_init();

#endif