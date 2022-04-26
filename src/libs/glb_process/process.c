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
#include "dbcache.h"
#include "log.h"
#include "../zbxipcservice/glb_ipc.h"
#include "../glb_state/state_items.h"
#include "zbxhistory.h"
#include "proc_triggers.h"
#include "proc_trigger_timers.h"
#include "process.h"
#include "proc_trends.h"

#define PROCESS_ITEMS_PER_RUN	100

extern unsigned char	program_type;

static zbx_mem_info_t	*proc_ipc_mem = NULL;
ZBX_MEM_FUNC_IMPL(__proc_ipc, proc_ipc_mem)
static mem_funcs_t memf = {.free_func = __proc_ipc_mem_free_func, .malloc_func = __proc_ipc_mem_malloc_func, .realloc_func = __proc_ipc_mem_realloc_func};

extern u_int64_t CONFIG_PROCESSING_IPC_SIZE;

extern void *ipc_processing;
extern void *ipc_processing_notify;

int DC_get_processing_data(u_int64_t itemid, metric_processing_data_t *proc_data);

//this fetches parts of configuration and state to normilize and history-process
//data. Trigger operations are performed inside trigger callback so this 
//naturally provides locking and access to all the trigger configs needed
//todo: fetch configuration should happen in non-locaking manner (at least non-global locking)
int fetch_metric_processing_data(u_int64_t itemid, metric_processing_data_t *proc_data) {
	return DC_get_processing_data(itemid, proc_data);
}

//convert metric to ITEM_VALUE_TYPE* compatible value type
int convert_metric_value(metric_t *metric, unsigned char new_value_type, mem_funcs_t *memf) {
	double dbl_val;
	u_int64_t uint64_val;
	//LOG_INF("Metric %ld convering from variant: %d to value_type: %d",metric->itemid, metric->value.type, new_value_type);
	DEBUG_ITEM(metric->itemid, "Metric %ld convering from variant: %d to value_type: %d",metric->itemid, metric->value.type, new_value_type);
	switch (metric->value.type) {
		case VARIANT_DBL:
			switch (new_value_type) {
				case ITEM_VALUE_TYPE_UINT64:
					if (0 > metric->value.data.dbl) {

						zbx_snprintf(metric->str_buffer,MAX_STRING_LEN, "Cannot convert negative double '%f' to uint format", metric->value.data.dbl);
						metric->value.type = VARIANT_ERR;
						metric->state = ITEM_STATE_NOTSUPPORTED;
						glb_state_item_set_error(metric->itemid, metric->str_buffer);

						return FAIL;
					}
					metric->value.data.ui64 = (u_int64_t) metric->value.data.dbl;
					metric->value.type = VARIANT_UI64;
					break;

				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
				case ITEM_VALUE_TYPE_LOG:
					zbx_snprintf(metric->str_buffer, MAX_STRING_LEN, "%f" , metric->value.data.dbl);
					metric->value.type = VARIANT_STR;
					break;			
			}
		break;

		case VARIANT_STR:
			DEBUG_ITEM(metric->itemid,"In str coversion, new_value_type is %d",(int)new_value_type);
			DEBUG_ITEM(metric->itemid,"metric's str is set to %s", metric->value.data.str);

			if (NULL == metric->value.data.str ) {
				return FAIL;
			}

			switch (new_value_type) {
			case ITEM_VALUE_TYPE_FLOAT: 
				
				if (SUCCEED == is_double(metric->value.data.str, &dbl_val)) {
					if (metric->value.data.str != metric->str_buffer) {
						memf->free_func(metric->value.data.str);
					}
					metric->value.data.dbl = dbl_val;
					metric->value.type = VARIANT_DBL;
				} else {
					zbx_snprintf(metric->str_buffer,MAX_STRING_LEN, "Cannot convert value '%s' to double format", metric->value.data.str);
					metric->value.type = VARIANT_ERR;
					metric->state = ITEM_STATE_NOTSUPPORTED;

					glb_state_item_set_error(metric->itemid, metric->str_buffer);
					return FAIL;
				}
				break;
			case ITEM_VALUE_TYPE_UINT64: 
				DEBUG_ITEM(metric->itemid,"In _to uint64 coversion");
				
				if (SUCCEED == is_uint64( metric->value.data.str, &uint64_val)) {
					if (metric->value.data.str != metric->str_buffer) {
						memf->free_func(metric->value.data.str);
					}
					DEBUG_ITEM(metric->itemid, "Converted metric %s to %ld",  metric->value.data.str, uint64_val );
					metric->value.data.ui64 = uint64_val;
					metric->value.type = VARIANT_UI64;
				}
			}
		
		case VARIANT_UI64:
			switch (new_value_type) {
				case ITEM_VALUE_TYPE_FLOAT:
					metric->value.data.dbl = (double) metric->value.data.ui64;
					metric->value.type = VARIANT_DBL;
					break;
				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
				case ITEM_VALUE_TYPE_LOG:
					zbx_snprintf(metric->str_buffer, MAX_STRING_LEN, "%ld" , metric->value.data.ui64);
					metric->value.data.str = metric->str_buffer;
					metric->value.type = VARIANT_STR;
					break;			
			}
		break;
	}

	return SUCCEED;
}


//a temporary func till original sync_server_history is used //cpaste from the hsit cache
static void copy_metric_to_dc_hist(metric_t *metric, ZBX_DC_HISTORY *history, unsigned char value_type) 
{
	history->itemid = metric->itemid;
	history->ts.sec = metric->sec;
	history->ts.ns = 0;
	history->state = metric->state;
	history->flags = metric->flags;
	
	history->lastlogsize = 0;
	history->mtime = 0;

	if (ITEM_STATE_NOTSUPPORTED == metric->state)
	{
		DEBUG_ITEM(history->itemid,"Copied unsupported item for history syncer");
		history->value.err = zbx_strdup(NULL, metric->value.data.err);
		history->flags |= ZBX_DC_FLAG_UNDEF;
		return;
	}

	history->value_type = value_type;

	if (0 == (ZBX_DC_FLAG_NOVALUE & metric->flags))
	{
		switch (value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				history->value.dbl = metric->value.data.dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				history->value.ui64 = metric->value.data.ui64;
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				history->value.str = zbx_strdup(NULL, metric->value.data.str);
				break;
			case ITEM_VALUE_TYPE_LOG:
				history->value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
				history->value.log->value = zbx_strdup(NULL, metric->value.data.str);

				//if (NULL != data->value.log->source)
				//	history->value.log->source = zbx_strdup(NULL, data->value.log->source);
				//else
				history->value.log->source = NULL;

				history->value.log->timestamp = 0;
				history->value.log->severity = 0; 
				history->value.log->logeventid = 0;

				break;
		}
	}
}

static void free_metric(metric_t *metric, zbx_mem_free_func_t free_func) {
	//metric needs to be freed if there is a dynamic allocation
	//it might be for dynamic-length types: str, text, log
	//to prevent lots of dynamic allocations (and ease copying and 
	//handling). 
	if ( (  VARIANT_STR == metric->value.type ||
		 	VARIANT_ERR == metric->value.type ) && 
		 	NULL != metric->value.data.str && 
			metric->value.data.str != metric->str_buffer ) 
	{
		free_func(metric->value.data.str);
		metric->value.type = ITEM_VALUE_TYPE_NONE;
		metric->value.data. str = NULL;
	}
}

IPC_PROCESS_CB(process_metric_cb) {
	metric_t *metric = ipc_data;
	metric_processing_data_t proc_data;
	ZBX_DC_HISTORY *history = cb_data;

	if ( FAIL == fetch_metric_processing_data(metric->itemid, &proc_data) ) {
		LOG_INF("Failed to fetch processing data for the metric, skipping from processing");
		return;
	}

	if ( SUCCEED == convert_metric_value(metric, proc_data.value_type, memf) )
	{
		//LOG_INF("Metric converted, sending to the history");
		glb_history_add_metric(metric, &proc_data);
		DEBUG_ITEM(metric->itemid, "Accounting in the trends");
		trends_account_metric(metric, &proc_data);
		DEBUG_ITEM(metric->itemid, "Saving to the values memory cache");
		glb_state_item_add_value(metric, proc_data.value_type);
		
	//	LOG_INF("Saved, converting to dc_hist");
	//	copy_metric_to_dc_hist(&metric, &history[i], proc_data.value_type);
	} else {
		LOG_INF("Failed to convert the metric");
	}
	DEBUG_ITEM(metric->itemid, "Processing item's triggers");
	process_metric_triggers(metric->itemid);
	
}


int process_metric_values(int max_values, int process_num) {
	return glb_ipc_process(ipc_processing, process_num-1, process_metric_cb, NULL, max_values);
}

IPC_CREATE_CB(metric_ipc_create_cb){
	//LOG_INF("Creating new metric in the IPC0");
	metric_t *local_metric = local_data, *ipc_metric = ipc_data;
	
	//LOG_INF("Creating new metric %ld in the IPC1", local_metric->itemid);
	//LOG_INF("Copy size is %ld", sizeof(metric_t));
	//LOG_INF("Copy metric itemid %ld, hostid %ld", local_metric->itemid, local_metric->hostid);

	memcpy(ipc_metric, local_metric, sizeof(metric_t));
	//LOG_INF("Creating new metric in the IPC2");
//	LOG_INF("Metric %ld value.type is %d, status is %d , str ptr is %p", local_metric->itemid, (int)local_metric->value.type, (int) local_metric->state, 
	//		local_metric->value.data.str);
	if (( VARIANT_STR == local_metric->value.type  || VARIANT_ERR == local_metric->value.type ) && //this will be if error is set in failed metric
		 	NULL != local_metric->value.data.str ) 
	{
	//	LOG_INF("Moving text to the IPC: %s", local_metric->value.data.str );
		size_t len = strlen( local_metric->value.data.str);
		
		if (len > MAX_STRING_LEN) {
		//	LOG_INF("Metric %ld size %d sending as dynamic buffer", local_metric->itemid, len);
			ipc_metric->value.data.str = memf->malloc_func(NULL, len + 1);
			memcpy(ipc_metric->value.data.str, local_metric->value.data.str, len + 1);
		} else {
		//	LOG_INF("Metric %ld size %d sending as static buffer", local_metric->itemid, len);	
			memcpy(ipc_metric->str_buffer, local_metric->value.data.str, len + 1);
			ipc_metric->value.data.str = ipc_metric->str_buffer;
		}
	} else {
		//LOG_INF("Metric %ld is non string, doesn't need conversion or allocation", local_metric->itemid);
	}

}

IPC_FREE_CB(metric_ipc_free_cb) {
	
	metric_t  *ipc_metric = ipc_data;

//	LOG_INF("Releasing metric %ld str addr is %p", ipc_metric->itemid, ipc_metric->value.data.str);
//	memcpy(local_metric, ipc_metric, sizeof(metric_t));
	
	if (( VARIANT_STR == ipc_metric->value.type || VARIANT_ERR == ipc_metric->value.type ) && //this will be if error is set in failed metric
		 	NULL != ipc_metric->value.data.str ) 
	{
		if (ipc_metric->value.data.str != ipc_metric->str_buffer) 
		{	
			memf->free_func(ipc_metric->value.data.str);
		}
		ipc_metric->value.data.str = NULL;
	}
	ipc_metric->value.type = VARIANT_NONE;
}


void	create_metric_from_agent_result(u_int64_t hostid, u_int64_t itemid, int sec, unsigned char state, AGENT_RESULT *result, metric_t * metric, char *error) {

//	LOG_INF("Convering metric %ld addr ", itemid);	
	bzero(metric, sizeof(metric_t));

//	if (NULL != result ) && 

	metric->itemid = itemid;
	metric->hostid = hostid;
	metric->sec = sec;
	metric->value.type = ITEM_VALUE_TYPE_NONE;
	
	if (NULL == result || ITEM_STATE_NOTSUPPORTED == state)  {
	//	LOG_INF("NULL agent result metric arrived");
		metric->state = ITEM_STATE_NOTSUPPORTED;
		
		if (NULL != error ) {
		//	LOG_INF("Setting error value");
			metric->value.data.err = error;
			metric->value.type = VARIANT_ERR;
		} else 
			metric->value.data.err = NULL;
		return;
	}
	
	metric->state = ITEM_STATE_NORMAL;

	if ( ISSET_STR(result) )  {
		//LOG_INF("Convering str metric");
		metric->value.type = VARIANT_STR;
		metric->value.data.str = result->str;
		if (NULL == result->str ) {
		//	LOG_INF("Metric %ld has string variant but null pointer", itemid);
		}
		DEBUG_ITEM(itemid,"Metric converted from agent result with str value %s",  result->str);
		return;
	}
	
	if ( ISSET_TEXT(result) )  {
	//	LOG_INF("Convering text metric");
		metric->value.type = VARIANT_STR;
		metric->value.data.str = result->text;
		if (NULL == result->text ) {
			LOG_INF("Metric %ld has string variant but null pointer", itemid);
		}
		DEBUG_ITEM(itemid,"Metric converted from agent result with str value %s",  result->text);
		return;
	}
	

	if (ISSET_LOG(result)) {
		//LOG_INF("Convering str metric");
		metric->value.type = VARIANT_STR;
		metric->value.data.str = result->log->value;
		return;
	}
	

	if (ISSET_UI64(result)) {
	//	LOG_INF("Convering UI64 metric");
		metric->value.type = VARIANT_UI64;
		metric->value.data.ui64 = result->ui64;
		return;
	}
	if (ISSET_DBL(result)) {
	//	LOG_INF("Convering DBL metric");
		metric->value.type = VARIANT_DBL;
		metric->value.data.dbl= result->dbl;
		return;
	}
	
	//value isn't set 
	metric->value.type = VARIANT_NONE;
	return;
}

void 	send_metric_to_processing(metric_t *metric) {
	/* skip proxy  timestamps of empty (throttled etc) values to update nextchecks for queue */
	if ( VARIANT_NONE == metric->value.type && 0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return;

	//LOG_INF("Sending metric %p to processing, itemid %ld, hostid %ld", metric, metric->itemid, metric->hostid);
	DEBUG_ITEM(metric->itemid,"Submitting metric to PROCESSING ipc ts is %d", metric->sec);
	if (FAIL == glb_ipc_send(ipc_processing, metric->hostid % CONFIG_HISTSYNCER_FORKS, metric)) {
		LOG_INF("Couldn't send metric to processing: queue is FULL");
	};
	
	static int last_dump = 0;

	if (last_dump != time(NULL)) {
		glb_ipc_dump_sender_queues(ipc_processing, "Sender side");
		last_dump = time(NULL);
	}
}


int glb_processing_ipc_init(int consumers, int metrics_queue_size, int notify_queue_size) {
    void *ret;
	char *error;

	LOG_INF("Doing processing ipc init");

	if (SUCCEED != zbx_mem_create(&proc_ipc_mem, CONFIG_PROCESSING_IPC_SIZE, "Processing IPC queue", "Processing IPC queue", 1, &error))
		return FAIL;
//	LOG_INF("Allocating memory for ipc config, func addr is %p ",metric_ipc_create_cb);

    ipc_processing = glb_ipc_init(IPC_PROCESSING, CONFIG_PROCESSING_IPC_SIZE , "Processing queue", 
				metrics_queue_size, sizeof(metric_t), CONFIG_HISTSYNCER_FORKS,  &memf, metric_ipc_create_cb, metric_ipc_free_cb);


	if (FAIL ==processing_trigger_timers_init(notify_queue_size, &memf)) 
		return FAIL;

	return SUCCEED;
};
