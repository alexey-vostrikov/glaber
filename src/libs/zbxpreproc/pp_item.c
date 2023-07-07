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

#include "pp_item.h"
#include "pp_history.h"
#include "zbxvariant.h"

ZBX_PTR_VECTOR_IMPL(pp_step_ptr, zbx_pp_step_t *)

/******************************************************************************
 *                                                                            *
 * Purpose: create item preprocessing data                                    *
 *                                                                            *
 * Parameters: type       - [IN] the item type                                *
 *             value_type - [IN] the item value type                          *
 *             flags      - [IN] the item flags                               *
 *                                                                            *
 * Return value: The created item preprocessing data.                         *
 *                                                                            *
 ******************************************************************************/
zbx_pp_item_preproc_t	*zbx_pp_item_preproc_create(unsigned char type, unsigned char value_type, unsigned char flags)
{
	zbx_pp_item_preproc_t	*preproc = zbx_malloc(NULL, sizeof(zbx_pp_item_preproc_t));

	preproc->refcount = 1;
	preproc->steps_num = 0;
	preproc->steps = NULL;
	preproc->dep_itemids_num = 0;
	preproc->dep_itemids = NULL;

	preproc->type = type;
	preproc->value_type = value_type;
	preproc->flags = flags;

	preproc->history = NULL;
	preproc->history_num = 0;

	preproc->mode = ZBX_PP_PROCESS_PARALLEL;

	return preproc;
}

void	zbx_pp_step_free(zbx_pp_step_t *step)
{
	zbx_free(step->params);
	zbx_free(step->error_handler_params);
	zbx_free(step);
}

/******************************************************************************
 *                                                                            *
 * Purpose: free item preprocessing data                                      *
 *                                                                            *
 * Parameters: preproc - [IN] the item preprocessing data                     *
 *                                                                            *
 ******************************************************************************/
static void	pp_item_preproc_free(zbx_pp_item_preproc_t *preproc)
{
	int	i;

	for (i = 0; i < preproc->steps_num; i++)
	{
		zbx_free(preproc->steps[i].params);
		zbx_free(preproc->steps[i].error_handler_params);
	}

	zbx_free(preproc->steps);
	zbx_free(preproc->dep_itemids);

	if (NULL != preproc->history)
		pp_history_free(preproc->history);

	zbx_free(preproc);
}

/******************************************************************************
 *                                                                            *
 * Purpose: copy item preprocessing data                                      *
 *                                                                            *
 * Parameters: preproc - [IN] the item preprocessing data                     *
 *                                                                            *
 * Return value: The copied preprocessing data.                               *
 *                                                                            *
 ******************************************************************************/
zbx_pp_item_preproc_t	*pp_item_preproc_copy(zbx_pp_item_preproc_t *preproc)
{
	if (NULL == preproc)
		return NULL;

	preproc->refcount++;

	return preproc;
}

/******************************************************************************
 *                                                                            *
 * Purpose: release item preprocessing data                                   *
 *                                                                            *
 * Parameters: preproc - [IN] the item preprocessing data                     *
 *                                                                            *
 ******************************************************************************/
void	zbx_pp_item_preproc_release(zbx_pp_item_preproc_t *preproc)
{
	if (NULL == preproc || 0 != --preproc->refcount)
		return;

	pp_item_preproc_free(preproc);
}

/******************************************************************************
 *                                                                            *
 * Purpose: check if preprocessing step requires history                      *
 *                                                                            *
 * Parameters: preproc - [IN] the item preprocessing data                     *
 *                                                                            *
 * Return value: SUCCEED - the step requires history                          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_pp_preproc_has_history(int type)
{
	switch (type)
	{
		case ZBX_PREPROC_DELTA_VALUE:
		case ZBX_PREPROC_DELTA_SPEED:
		case ZBX_PREPROC_THROTTLE_VALUE:
		case ZBX_PREPROC_THROTTLE_TIMED_VALUE:
		case ZBX_PREPROC_SCRIPT:
		case GLB_PREPROC_DISCOVERY_PREPARE:
		case GLB_PREPROC_THROTTLE_TIMED_VALUE_AGG:
			return SUCCEED;
		default:
			return FAIL;

	}
}

void	pp_item_clear(zbx_pp_item_t *item)
{
	zbx_pp_item_preproc_release(item->preproc);
}

void	zbx_pp_value_opt_clear(zbx_pp_value_opt_t *opt)
{
	if (0 != (opt->flags & ZBX_PP_VALUE_OPT_LOG))
		zbx_free(opt->source);
}
