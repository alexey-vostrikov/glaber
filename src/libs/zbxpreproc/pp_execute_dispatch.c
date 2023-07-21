/*
** Glaber
** Copyright (C) 2001-2100 Glaber
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

#include "zbxcommon.h"
#include "zbxjson.h"
#include "metric.h"
#include "zbxcacheconfig.h"
#include "item_preproc.h"
#include "pp_execute_json_discovery.h"
#include "../glb_state/glb_state_interfaces.h"
#include "glb_preproc.h"

#include "../zbxipcservice/glb_ipc.h"
extern int		CONFIG_FORKS[ZBX_PROCESS_TYPE_COUNT];

typedef struct
{
	char *params[16];
	char count;
} 
preproc_params_t;

static preproc_params_t *item_preproc_parse_params(char *params_str)
{
	static preproc_params_t params = {0};

	if (NULL == params_str)
	{
		params.count = -1;
		return &params;
	}

	params.count = 0;

	char *ptr = strtok(params_str, "\n");

	while (NULL != ptr)
	{
		params.params[params.count] = ptr;
		ptr = strtok(NULL, "\n");
		params.count++;
	}

	return &params;
}

/***************************************************************************
 * dispatches or routes the item to another host/item stated in the cfg    *
 * *************************************************************************/
int pp_execute_dispatch(metric_t *orig_metric, zbx_variant_t *value,  const char *params_in)
{
	zbx_uint64_pair_t host_item_ids;
	char *hostname = NULL, *error = NULL, params_copy[MAX_STRING_LEN];

	DEBUG_ITEM(orig_metric->itemid, "In %s: starting", __func__);

	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, &error) || NULL == params_in)
		return SUCCEED;

    zbx_strlcpy(params_copy, params_in, MAX_STRING_LEN);
	preproc_params_t *params = item_preproc_parse_params(params_copy);

	if (params->count != 2)
	{
		DEBUG_ITEM(orig_metric->itemid, "Cannot dispatch: Wrong number of params: %d instead of 2", params->count);
		return SUCCEED;
	}

	if (NULL == (hostname = get_param_name_from_json(value->data.str, params->params[0])))
	{
		DEBUG_ITEM(orig_metric->itemid, "Cannot dispatch: cannot find host name '%s'", params->params[0]);
		return SUCCEED;
	}

	zbx_host_key_t host_key = {.host = hostname, .key = params->params[1]};

	if (SUCCEED == DCconfig_get_itemid_by_key(&host_key, &host_item_ids))
	{
		
		DEBUG_ITEM(orig_metric->itemid, "Redirect: [%ld, %ld] -> [%ld, %ld]", orig_metric->hostid, orig_metric->itemid, host_item_ids.first, host_item_ids.second);
		DEBUG_ITEM(host_item_ids.second, "Redirect: [%ld, %ld] -> [%ld, %ld]", orig_metric->hostid, orig_metric->itemid, host_item_ids.first, host_item_ids.second);

		orig_metric->hostid = host_item_ids.first;
		orig_metric->itemid = host_item_ids.second;

		//this will dispatch current metric to a new, correct fork as soon as we finish processing
		ipc_set_redirect_queue(orig_metric->hostid % CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR]);
		
		zbx_variant_clear(value);
		zbx_variant_set_none(value);
	
		return SUCCEED; // this intentional to be able to stop processing via 'custom on fail checkbox'
	}

	DEBUG_ITEM(orig_metric->itemid, "Couldn find itemid for host %s item %s", host_key.host, host_key.key);
	DEBUG_ITEM(orig_metric->itemid, "In %s: finished", __func__);

	return SUCCEED; // we actially failed, but return succeed to continue the item preproc steps to process unmatched items
}

/***************************************************************************
 * dispatches or routes the item to another host/item stated in the cfg    *
 * *************************************************************************/
int pp_execute_dispatch_by_ip(metric_t *orig_metric, zbx_variant_t *value, const char *params_in)
{
	u_int64_t hostid, new_itemid;
	char *ip_str = NULL, *key = NULL, params_copy[MAX_STRING_LEN], *error = NULL;

	DEBUG_ITEM(orig_metric->itemid, "In %s: starting", __func__);

	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, &error))
		return SUCCEED;
    
    if (NULL == params_in)
        return SUCCEED;

    zbx_strlcpy(params_copy, params_in, MAX_STRING_LEN);

	preproc_params_t *params = item_preproc_parse_params(params_copy);

	if (params->count != 2)
	{
		DEBUG_ITEM(orig_metric->itemid, "Cannot dispatch: Wrong number of params: %d instead of 2", params->count);
        return SUCCEED;
	}
	key = params->params[1];

	if (NULL == (ip_str = get_param_name_from_json(value->data.str, params->params[0])))
	{
		DEBUG_ITEM(orig_metric->itemid, "Cannot dispatch: cannot find ip addr '%s'", params->params[0]);
        return SUCCEED;
	}
    
    //ip often comes with port, so ignore everything after semicolon
	char *semicolon = NULL;

	if (NULL != (semicolon = strchr(ip_str, ':'))) {
		*semicolon ='\0';
		DEBUG_ITEM(orig_metric->itemid, "Found semicolon, new IP string is %s", ip_str);
	}		

	if (0 == (hostid = glb_state_interfaces_find_host_by_ip(ip_str))) {
		DEBUG_ITEM(orig_metric->itemid,"Cannot dispatch: cannot find host for ip addr '%s'", ip_str);
    	return SUCCEED;
	}

	if (SUCCEED == DCconfig_get_itemid_by_item_key_hostid(hostid, key, &new_itemid))
	{
	
		DEBUG_ITEM(orig_metric->itemid, "Redirect: [%ld, %ld] -> [%ld, %ld]", orig_metric->hostid, orig_metric->itemid, hostid, new_itemid);
		DEBUG_ITEM(new_itemid, "Redirect: [%ld, %ld] -> [%ld, %ld]", orig_metric->hostid, orig_metric->itemid, hostid, new_itemid);

		orig_metric->hostid = hostid;
		orig_metric->itemid = new_itemid;

		ipc_set_redirect_queue(hostid %CONFIG_FORKS[GLB_PROCESS_TYPE_PREPROCESSOR]);
		
		zbx_variant_clear(value);
		zbx_variant_set_none(value);

		return SUCCEED; 
	}

	DEBUG_ITEM(orig_metric->itemid, "Couldn find itemid for host %ld item %s", hostid, key);

	DEBUG_ITEM(orig_metric->itemid, "In %s: finished", __func__);
	return SUCCEED; //actially failed, but return succeed to continue the item preproc steps to process unmatched items
}
