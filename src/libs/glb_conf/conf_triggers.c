

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

#include "common.h"
#include "zbxalgo.h"
#include "conf_triggers.h"
#include "../glb_state/state_triggers.h"
#include "../zbxdbcache/changeset.h"
#include "conf_db_sync.h"
#include "db.h"
#include "log.h"
#include "conf_index.h"

typedef struct {
    elems_hash_t *triggers;
    mem_funcs_t memf;
    strpool_t strpool;
} 
conf_t;

static conf_t *conf = NULL;

/*this is non-locking version of the DCget_trigger 
but only fetches configuration, not state for state - metric operations
better use callbacks with lambda - like processing
*/
int DCget_conf_trigger(u_int64_t triggerid, trigger_conf_t *conf);

/* based on code from dbconfig.c */
static void	prepare_trigger_conf(trigger_conf_t *tr)
{
	tr->eval_ctx = zbx_eval_deserialize_dyn(tr->expression_bin, tr->expression, ZBX_EVAL_EXCTRACT_ALL);
	DEBUG_TRIGGER(tr->triggerid,"Extracted trigger expression to binary");
	
	if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
	{
		tr->eval_ctx_r = zbx_eval_deserialize_dyn(tr->recovery_expression_bin, tr->recovery_expression,
					ZBX_EVAL_EXCTRACT_ALL);
		DEBUG_TRIGGER(tr->triggerid,"Extracted trigger recovery expression to binary");
	}
}

int conf_trigger_get_trigger_conf_data(u_int64_t triggerid, trigger_conf_t *conf) {
	
 	DCget_conf_trigger(triggerid, conf);
	prepare_trigger_conf(conf);
	return SUCCEED;
	
}

void	conf_trigger_get_functionids(zbx_vector_uint64_t *functionids, trigger_conf_t *tr)
{
	int		i;

	LOG_DBG("In %s() ", __func__);

	zbx_vector_uint64_reserve(functionids, 10);
	zbx_eval_get_functionids(tr->eval_ctx, functionids);

	if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
		zbx_eval_get_functionids(tr->eval_ctx_r, functionids);
	

	zbx_vector_uint64_sort(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(functionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	LOG_DBG("End of %s() functionids_num:%d", __func__, functionids->values_num);
}


void conf_trigger_free_trigger( trigger_conf_t *trigger) {
	
	zbx_free(trigger->expression);
	zbx_free(trigger->recovery_expression);
	zbx_free(trigger->description);
	zbx_free(trigger->correlation_tag);
	zbx_free(trigger->opdata);
	zbx_free(trigger->event_name);
	zbx_free(trigger->expression_bin);
	zbx_free(trigger->recovery_expression_bin);

	zbx_vector_ptr_clear_ext(&trigger->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&trigger->tags);

	if (NULL != trigger->eval_ctx)
	{
		zbx_eval_clear(trigger->eval_ctx);
		zbx_free(trigger->eval_ctx);
	}

	if (NULL != trigger->eval_ctx_r)
	{
		zbx_eval_clear(trigger->eval_ctx_r);
		zbx_free(trigger->eval_ctx_r);
	}
}