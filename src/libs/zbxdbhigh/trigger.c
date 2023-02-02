/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "zbxdbhigh.h"

#include "log.h"
#include "glb_events.h"
#include "zbxnum.h"
#include "../glb_objects/glb_trigger.h"



/******************************************************************************
 *                                                                            *
 * Purpose: frees trigger changeset                                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_trigger_diff_free(zbx_trigger_diff_t *diff)
{
	zbx_free(diff->error);
	zbx_free(diff);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Adds a new trigger diff to trigger changeset vector               *
 *                                                                            *
 ******************************************************************************/
void	zbx_append_trigger_diff(zbx_vector_ptr_t *trigger_diff, zbx_uint64_t triggerid, unsigned char priority,
		zbx_uint64_t flags, unsigned char value, int lastchange, const char *error)
{
	zbx_trigger_diff_t	*diff;

	diff = (zbx_trigger_diff_t *)zbx_malloc(NULL, sizeof(zbx_trigger_diff_t));
	diff->triggerid = triggerid;
	diff->priority = priority;
	diff->flags = flags;
	diff->value = value;
	diff->lastchange = lastchange;
	diff->error = (NULL != error ? zbx_strdup(NULL, error) : NULL);
	diff->problem_count = 0;

	zbx_vector_ptr_append(trigger_diff, diff);
}
