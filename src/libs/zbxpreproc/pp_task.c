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

#include "pp_task.h"
#include "pp_error.h"
#include "zbxcommon.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxsysinc.h"
#include "zbxipcservice.h"

#define PP_TASK_QUEUE_INIT_NONE		0x00
#define PP_TASK_QUEUE_INIT_LOCK		0x01
#define PP_TASK_QUEUE_INIT_EVENT	0x02

ZBX_PTR_VECTOR_IMPL(pp_task_ptr, zbx_pp_task_t *)

void	pp_task_free(zbx_pp_task_t *task);

/******************************************************************************
 *                                                                            *
 * Purpose: create task                                                       *
 *                                                                            *
 * Parameters: size - [IN] the task data size                                 *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
static zbx_pp_task_t	*pp_task_create(size_t size)
{
	zbx_pp_task_t	*task;

	task = (zbx_pp_task_t *)zbx_malloc(NULL, offsetof(zbx_pp_task_t, data) + size);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: create preprocessing test task                                    *
 *                                                                            *
 * Parameters: preproc   - [IN] the item preprocessing data                   *
 *             value     - [IN] the value to preprocess, its contents will be *
 *                              directly copied over and cleared by the task  *
 *                              (optional)                                    *
 *             ts        - [IN] the value timestamp                           *
 *             client    - [IN] the request source                            *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
zbx_pp_task_t	*pp_task_test_create(zbx_pp_item_preproc_t *preproc, zbx_variant_t *value, zbx_timespec_t ts,
		zbx_ipc_client_t *client)
{
	zbx_pp_task_t		*task = pp_task_create(sizeof(zbx_pp_task_test_t));
	zbx_pp_task_test_t	*d = (zbx_pp_task_test_t *)PP_TASK_DATA(task);

	task->itemid = 0;
	task->type = ZBX_PP_TASK_TEST;
	d->value = *value;
	d->ts = ts;

	d->results = NULL;
	d->results_num = 0;
	zbx_variant_set_none(&d->result);

	d->preproc = pp_item_preproc_copy(preproc);

	d->client = client;
	zbx_ipc_client_addref(client);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clear test task                                                   *
 *                                                                            *
 * Parameters: task - [IN] the task to clear                                  *
 *                                                                            *
 ******************************************************************************/
static void	pp_task_test_clear(zbx_pp_task_test_t *task)
{
	zbx_variant_clear(&task->value);

	zbx_variant_clear(&task->result);
	pp_free_results(task->results, task->results_num);

	zbx_pp_item_preproc_release(task->preproc);
	zbx_ipc_client_release(task->client);
}

/******************************************************************************
 *                                                                            *
 * Purpose: create value preprocesing task                                    *
 *                                                                            *
 * Parameters: itemid    - [IN] the item identifier                           *
 *             preproc   - [IN] the item preprocessing data                   *
 *             value     - [IN] the value to preprocess, its contents will be *
 *                              directly copied over and cleared by the task  *
 *                              (optional)                                    *
 *             ts        - [IN] the value timestamp                           *
 *             value_opt - [IN] the optional value data (optional)            *
 *             cache     - [IN] the preprocessing cache (optional)            *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
zbx_pp_task_t	*pp_task_value_create(zbx_uint64_t itemid, zbx_pp_item_preproc_t *preproc, zbx_variant_t *value,
		zbx_timespec_t ts, const zbx_pp_value_opt_t *value_opt, zbx_pp_cache_t *cache)
{
	zbx_pp_task_t		*task = pp_task_create(sizeof(zbx_pp_task_value_t));
	zbx_pp_task_value_t	*d = (zbx_pp_task_value_t *)PP_TASK_DATA(task);

	task->itemid = itemid;
	task->type = ZBX_PP_TASK_VALUE;

	if (NULL != value)
		d->value = *value;
	else
		zbx_variant_set_none(&d->value);

	zbx_variant_set_none(&d->result);
	d->cache = pp_cache_copy(cache);
	d->ts = ts;
	if (NULL != value_opt)
		d->opt = *value_opt;
	else
		d->opt.flags = ZBX_PP_VALUE_OPT_NONE;

	d->preproc = pp_item_preproc_copy(preproc);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clear value preprocessing task                                    *
 *                                                                            *
 * Parameters: task - [IN] the task to clear                                  *
 *                                                                            *
 ******************************************************************************/
static void	pp_task_value_clear(zbx_pp_task_value_t *task)
{
	zbx_pp_value_opt_clear(&task->opt);

	zbx_variant_clear(&task->value);
	zbx_variant_clear(&task->result);
	zbx_pp_item_preproc_release(task->preproc);
	pp_cache_release(task->cache);
}

/******************************************************************************
 *                                                                            *
 * Purpose: create serial value preprocesing task                             *
 *                                                                            *
 * Parameters: itemid    - [IN] the item identifier                           *
 *             preproc   - [IN] the item preprocessing data                   *
 *             value     - [IN] the value to preprocess, its contents will be *
 *                              directly copied over and cleared by the task  *
 *                              (optional)                                    *
 *             ts        - [IN] the value timestamp                           *
 *             value_opt - [IN] the optional value data (optional)            *
 *             cache     - [IN] the preprocessing cache (optional)            *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
zbx_pp_task_t	*pp_task_value_seq_create(zbx_uint64_t itemid, zbx_pp_item_preproc_t *preproc, zbx_variant_t *value,
		zbx_timespec_t ts, const zbx_pp_value_opt_t *value_opt, zbx_pp_cache_t *cache)
{
	zbx_pp_task_t	*task = pp_task_value_create(itemid, preproc, value, ts, value_opt, cache);

	task->type = ZBX_PP_TASK_VALUE_SEQ;

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: create dependent item preprocessing task                          *
 *                                                                            *
 * Parameters: itemid    - [IN] the item identifier                           *
 *             preproc   - [IN] the item preprocessing data                   *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
zbx_pp_task_t	*pp_task_dependent_create(zbx_uint64_t itemid, zbx_pp_item_preproc_t *preproc)
{
	zbx_pp_task_t		*task = pp_task_create(sizeof(zbx_pp_task_dependent_t));
	zbx_pp_task_dependent_t	*d = (zbx_pp_task_dependent_t *)PP_TASK_DATA(task);

	task->itemid = itemid;
	task->type = ZBX_PP_TASK_DEPENDENT;

	d->primary = NULL;
	d->cache = NULL;

	d->preproc = pp_item_preproc_copy(preproc);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clear dependent item preprocessing task                           *
 *                                                                            *
 * Parameters: task - [IN] the task to clear                                  *
 *                                                                            *
 ******************************************************************************/
static void	pp_task_dependent_clear(zbx_pp_task_dependent_t *task)
{
	zbx_pp_item_preproc_release(task->preproc);
	pp_cache_release(task->cache);

	if (NULL != task->primary)
		pp_task_free(task->primary);

}

/******************************************************************************
 *                                                                            *
 * Purpose: create sequence task                                              *
 *                                                                            *
 * Parameters: itemid    - [IN] the item identifier                           *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
zbx_pp_task_t	*pp_task_sequence_create(zbx_uint64_t itemid)
{
	zbx_pp_task_t		*task = pp_task_create(sizeof(zbx_pp_task_sequence_t));
	zbx_pp_task_sequence_t	*d = (zbx_pp_task_sequence_t *)PP_TASK_DATA(task);

	task->itemid = itemid;
	task->type = ZBX_PP_TASK_SEQUENCE;
	zbx_list_create(&d->tasks);

	return task;
}

/******************************************************************************
 *                                                                            *
 * Purpose: clear sequence task task                                          *
 *                                                                            *
 * Parameters: task - [IN] the task to clear                                  *
 *                                                                            *
 ******************************************************************************/
static void	pp_task_sequence_clear(zbx_pp_task_sequence_t *seq)
{
	zbx_pp_task_t	*task;

	while (SUCCEED == zbx_list_pop(&seq->tasks, (void **)&task))
		pp_task_free(task);

	zbx_list_destroy(&seq->tasks);
}

/******************************************************************************
 *                                                                            *
 * Purpose: free task                                                         *
 *                                                                            *
 ******************************************************************************/
void	pp_task_free(zbx_pp_task_t *task)
{
	if (NULL == task)
		return;

	switch (task->type)
	{
		case ZBX_PP_TASK_TEST:
			pp_task_test_clear((zbx_pp_task_test_t *)PP_TASK_DATA(task));
			break;
		case ZBX_PP_TASK_VALUE:
		case ZBX_PP_TASK_VALUE_SEQ:
			pp_task_value_clear((zbx_pp_task_value_t *)PP_TASK_DATA(task));
			break;
		case ZBX_PP_TASK_DEPENDENT:
			pp_task_dependent_clear((zbx_pp_task_dependent_t *)PP_TASK_DATA(task));
			break;
		case ZBX_PP_TASK_SEQUENCE:
			pp_task_sequence_clear((zbx_pp_task_sequence_t *)PP_TASK_DATA(task));
			break;
	}

	zbx_free(task);
}

/******************************************************************************
 *                                                                            *
 * Purpose: clear tasks                                                       *
 *                                                                            *
 ******************************************************************************/
void	zbx_pp_tasks_clear(zbx_vector_pp_task_ptr_t *tasks)
{
	zbx_vector_pp_task_ptr_clear_ext(tasks, pp_task_free);
}

/******************************************************************************
 *                                                                            *
 * Purpose: extract value task data                                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_pp_value_task_get_data(zbx_pp_task_t *task, unsigned char *value_type, unsigned char *flags,
		zbx_variant_t **value, zbx_timespec_t *ts, zbx_pp_value_opt_t **value_opt)
{
	zbx_pp_task_value_t	*d = (zbx_pp_task_value_t *)PP_TASK_DATA(task);

	*value_type = d->preproc->value_type;
	*flags = d->preproc->flags;
	*value = &d->result;
	*ts = d->ts;
	*value_opt = &d->opt;
}

/******************************************************************************
 *                                                                            *
 * Purpose: extract test task data                                            *
 *                                                                            *
 ******************************************************************************/
void	zbx_pp_test_task_get_data(zbx_pp_task_t *task, zbx_ipc_client_t **client, zbx_variant_t **value,
		zbx_pp_result_t **results, int *results_num, zbx_pp_history_t **history)
{
	zbx_pp_task_test_t	*d = (zbx_pp_task_test_t *)PP_TASK_DATA(task);

	*client = d->client;
	*value = &d->result;
	*results = d->results;
	*results_num = d->results_num;
	*history = d->preproc->history;
}
