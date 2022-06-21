/*
** Glaber
** Copyright (C) 2001-2038 Glaber 
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

#include "log.h"
#include "common.h"
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include "zbxalgo.h"
#include "zbxjson.h"
#include "glb_poller.h"
#include "csnmp.h"
#include "snmp.h"
#include "snmp_walk.h"
#include "poller_sessions.h"

/*most of this code is from chkecs_snmp.c */

/* discovered SNMP object, identified by its index */
typedef struct
{
	/* object index returned by zbx_snmp_walk */
	char	*index;

	/* an array of OID values stored in the same order as defined in OID key */
	char	**values;
}
snmp_dobject_t;

/* helper data structure used by snmp discovery */
typedef struct
{

	csnmp_oid_t root_oid;
	
	size_t root_oid_str_len;
	size_t root_oid_num_len;


	csnmp_oid_t last_oid;
	int oids_looped;

	/* the index of OID being currently processed (walked) */
	int			num;

	/* the discovered SNMP objects */
	zbx_hashset_t		objects;

	/* the index (order) of discovered SNMP objects */
	zbx_vector_ptr_t	index;

	/* request data structure used to parse discovery OID key */
	AGENT_REQUEST		request;
}
snmp_ddata_t;

/* discovery objects hashset support */
static zbx_hash_t	snmp_dobject_hash(const void *data)
{
	const char	*index = *(const char **)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(index, strlen(index), ZBX_DEFAULT_HASH_SEED);
}

static int	snmp_dobject_compare(const void *d1, const void *d2)
{
	const char	*i1 = *(const char **)d1;
	const char	*i2 = *(const char **)d2;

	return strcmp(i1, i2);
}


static int	snmp_ddata_init(poller_item_t *poller_item)
{
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	snmp_ddata_t *data = zbx_calloc(NULL, 0, sizeof(snmp_ddata_t));
	int	i, j, ret = CONFIG_ERROR;

	init_request(&data->request);

	if (SUCCEED != parse_item_key(snmp_item->oid, &data->request))
	{
		poller_preprocess_error(poller_item, "Invalid SNMP OID: cannot parse expression.");
		free_request(&data->request);
		zbx_free(data);

		return FAIL;
	}

	if (0 == data->request.nparam || 0 != (data->request.nparam & 1))
	{
		poller_preprocess_error(poller_item,"Invalid SNMP OID: pairs of macro and OID are expected.");
		free_request(&data->request);
		zbx_free(data);
		return FAIL;
	}

	for (i = 0; i < data->request.nparam; i += 2)
	{
		if (SUCCEED != is_discovery_macro(data->request.params[i]))
		{
			poller_preprocess_error(poller_item, "Invalid SNMP OID: macro is invalid");
			free_request(&data->request);
			zbx_free(data);
			return FAIL;

		}

		if (0 == strcmp(data->request.params[i], "{#SNMPINDEX}"))
		{
			poller_preprocess_error(poller_item, "Invalid SNMP OID: macro \"{#SNMPINDEX}\" is not allowed.");
			free_request(&data->request);
			zbx_free(data);
			return FAIL;
		}
	}

	for (i = 2; i < data->request.nparam; i += 2)
	{
		for (j = 0; j < i; j += 2)
		{
			if (0 == strcmp(data->request.params[i], data->request.params[j]))
			{
				poller_preprocess_error(poller_item, "Invalid SNMP OID: unique macros are expected.");
				free_request(&data->request);
				zbx_free(data);
				return FAIL;
			}
		}
	}

	zbx_hashset_create(&data->objects, 10, snmp_dobject_hash, snmp_dobject_compare);
	zbx_vector_ptr_create(&data->index);
	
	data->num = -1;
	data->oids_looped = 0;
	snmp_item->data = data;

	return SUCCEED;
}


static void	snmp_ddata_clean(poller_item_t *poller_item)
{
	int			i;
	zbx_hashset_iter_t	iter;
	snmp_dobject_t	*obj;
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_ddata_t *ddata = snmp_item->data;
	
	if (NULL == ddata)
		return;

	snmp_item->data = NULL;

	zbx_vector_ptr_destroy(&ddata->index);
	zbx_hashset_iter_reset(&ddata->objects, &iter);

	while (NULL != (obj = (snmp_dobject_t *)zbx_hashset_iter_next(&iter)))
	{
		for (i = 0; i < ddata->request.nparam / 2; i++)
			zbx_free(obj->values[i]);

		zbx_free(obj->index);
		zbx_free(obj->values);
	}

	zbx_hashset_destroy(&ddata->objects);
	free_request(&ddata->request);
	asn1_free_oid(&ddata->last_oid);
	asn1_free_oid(&ddata->root_oid);
	zbx_free(ddata);
}


void stop_item_poll(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	poller_return_item_to_queue(poller_item);
	snmp_ddata_clean(poller_item);
	poller_sessions_close_session(snmp_item->sessid);

	snmp_item->data = NULL;

}

static int snmp_walk_submit_result(poller_item_t *poller_item) {
    snmp_dobject_t	*obj;
	struct zbx_json		js;
    int			i, j, ret;
    snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_ddata_t *ddata = snmp_item->data;

	zbx_json_initarray(&js, ZBX_JSON_STAT_BUF_LEN);

 	for (i = 0; i < ddata->index.values_num; i++)
	{
		obj = (snmp_dobject_t *)ddata->index.values[i];

		zbx_json_addobject(&js, NULL);
		zbx_json_addstring(&js, "{#SNMPINDEX}", obj->index, ZBX_JSON_TYPE_STRING);

		for (j = 0; j < ddata->request.nparam / 2; j++)
		{
			if (NULL == obj->values[j])
				continue;

			zbx_json_addstring(&js, ddata->request.params[j * 2], obj->values[j], ZBX_JSON_TYPE_STRING);
		}
		zbx_json_close(&js);
	}

	DEBUG_ITEM(poller_get_item_id(poller_item),"Finished walk for the item, result is %s", js.buffer);
	zbx_json_close(&js);
	
	AGENT_RESULT result = {0};

	init_result(&result);
	result.type = AR_TEXT;
	result.text = zbx_strdup(NULL, js.buffer);
	poller_preprocess_value(poller_item, &result , glb_ms_time(), ITEM_STATE_NORMAL, NULL);
	
	free_result(&result);
	zbx_json_free(&js);

	return ret;
}

static void copy_oid(asn1_oid_t *dest_oid, asn1_oid_t *src_oid) {
	dest_oid->len = src_oid->len;
	dest_oid->b = zbx_malloc(NULL, sizeof(int) * dest_oid->len);
	memcpy(dest_oid->b, src_oid->b, sizeof(int) * dest_oid->len);
}


void snmp_walk_destroy_item(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);

	//snmp_ddata_clean(poller_item);
	stop_item_poll(poller_item);

}

#define ZBX_OID_INDEX_STRING	0
#define ZBX_OID_INDEX_NUMERIC	1

static int	snmp_print_oid(char *buffer, size_t buffer_len, const oid *objid, size_t objid_len, int format)
{
	if (SNMPERR_SUCCESS != netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_BREAKDOWN_OIDS,
			format))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot set \"dontBreakdownOids\" option to %d for Net-SNMP", format);
		return -1;
	}

	return snprint_objid(buffer, buffer_len, objid, objid_len);
}

/* there is a nice reasoning explanation for this func in checks_snmp.c */
static int snmp_choose_index(char *buffer, size_t buffer_len, const oid *objid, size_t objid_len,
		size_t root_string_len, size_t root_numeric_len)
{
	oid	parsed_oid[MAX_OID_LEN];
	size_t	parsed_oid_len = MAX_OID_LEN;
	char	printed_oid[MAX_STRING_LEN];
	char	*printed_oid_escaped;
	
	if (-1 == snmp_print_oid(printed_oid, sizeof(printed_oid), objid, objid_len, ZBX_OID_INDEX_STRING))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print OID with string indices", __func__);
		goto numeric;
	}

	if (NULL == strchr(printed_oid, '"') && NULL == strchr(printed_oid, '\''))
	{
		zbx_strlcpy(buffer, printed_oid + root_string_len + 1, buffer_len);
		return SUCCEED;
	}

	printed_oid_escaped = zbx_dyn_escape_string(printed_oid, "\\");

	if (NULL == snmp_parse_oid(printed_oid_escaped, parsed_oid, &parsed_oid_len))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot parse OID '%s'", __func__, printed_oid_escaped);
		zbx_free(printed_oid_escaped);
		goto numeric;
	}
	zbx_free(printed_oid_escaped);

	if (parsed_oid_len == objid_len && 0 == memcmp(parsed_oid, objid, parsed_oid_len * sizeof(oid)))
	{
		zbx_strlcpy(buffer, printed_oid + root_string_len + 1, buffer_len);
		return SUCCEED;
	}
numeric:
	if (-1 == snmp_print_oid(printed_oid, sizeof(printed_oid), objid, objid_len, ZBX_OID_INDEX_NUMERIC))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print OID with numeric indices", __func__);
		return FAIL;
	}

	zbx_strlcpy(buffer, printed_oid + root_numeric_len + 1, buffer_len);
	return SUCCEED;
}



int  snmp_walk_start_next_oid(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_ddata_t *ddata = snmp_item->data;

	//LOG_INF("Starting next oid processing %d", poller_get_item_id(poller_item));

	if (NULL == ddata) {
		stop_item_poll(poller_item);
		return FAIL;
	}

	ddata->num++;

	if (ddata->num >= ddata->request.nparam / 2) {
		DEBUG_ITEM(poller_get_item_id(poller_item), "All oids are parsed for item, submitting the result");
		snmp_walk_submit_result(poller_item);
		stop_item_poll(poller_item);
		return SUCCEED;
	}

	DEBUG_ITEM(poller_get_item_id(poller_item),"Starting walking oid %d out of %d", ddata->num + 1,  ddata->request.nparam / 2);
	char *next_oid = ddata->request.params[ddata->num * 2 + 1];
	
	char buffer[MAX_STRING_LEN];
	unsigned long root_oid[MAX_OID_LEN];
	size_t root_oid_len = MAX_OID_LEN;

	if (NULL == snmp_parse_oid(next_oid, root_oid, &root_oid_len))
	{
		poller_preprocess_error(poller_item, "snmp_parse_oid(): cannot parse OID");
		stop_item_poll(poller_item);
	  	return FAIL;
	}

	if (FAIL == snmp_print_oid(buffer, MAX_STRING_LEN, root_oid, root_oid_len, ZBX_OID_INDEX_STRING))
	{
		poller_preprocess_error(poller_item, "snmp_print_oid(): cannot print OID with string indices.");
		stop_item_poll(poller_item);
		return FAIL;
	}

	ddata->root_oid_str_len = strlen(buffer);

	if (FAIL == snmp_print_oid(buffer, MAX_STRING_LEN, root_oid, root_oid_len, ZBX_OID_INDEX_NUMERIC))
	{
		poller_preprocess_error(poller_item, "snmp_print_oid(): cannot print OID  with numeric indices.");
		stop_item_poll(poller_item);
		return FAIL;
	}

	ddata->root_oid_num_len = strlen(buffer);

	csnmp_pdu_t pdu={0};
	asn1_oid_t coid;
	
	snmp_fill_pdu_header(poller_item, &pdu, SNMP_CMD_GET_NEXT);

	if ( FAIL == snmp_item_oid_to_asn(next_oid, &coid)) {
	  	poller_preprocess_error(poller_item, "Cannot parse oid");
		stop_item_poll(poller_item);

		csnmp_free_pdu(&pdu);
	  	return FAIL;
	} 
	
	copy_oid(&ddata->root_oid, &coid);
	copy_oid(&ddata->last_oid, &coid);
	
	ddata->oids_looped = 0;

	DEBUG_ITEM(poller_get_item_id(poller_item), "Sending first walk request for oid %s", next_oid);
	
	csnmp_add_var(&pdu, coid, SNMP_TP_NULL, NULL);
	snmp_send_packet(poller_item, &pdu);
	csnmp_free_pdu(&pdu);
	
	return SUCCEED;
}

static void	snmp_walk_save_result_value(snmp_ddata_t *data, const char *snmp_oid, const char *index, const char *value)
{
	snmp_dobject_t	*obj;
	ZBX_UNUSED(snmp_oid);

	if (NULL == (obj = (snmp_dobject_t *)zbx_hashset_search(&data->objects, &index)))
	{
		snmp_dobject_t	new_obj;

		new_obj.index = zbx_strdup(NULL, index);
		new_obj.values = (char **)zbx_malloc(NULL, sizeof(char *) * data->request.nparam / 2);
		memset(new_obj.values, 0, sizeof(char *) * data->request.nparam / 2);

		obj = (snmp_dobject_t *)zbx_hashset_insert(&data->objects, &new_obj, sizeof(new_obj));
		zbx_vector_ptr_append(&data->index, obj);
	}
	obj->values[data->num] = zbx_strdup(NULL, value);
}

/* returns SUCEED is get next requst is needed */
int snmp_walk_process_var(poller_item_t *poller_item, csnmp_var_t *var) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_ddata_t *ddata = snmp_item->data;
	
	if (NULL == ddata) {
		LOG_WRN("Arrived responce for the item %ld, but walk is not active for it", poller_get_item_id(poller_item));
		return FAIL;
	}

	if (SNMP_TP_END_OF_MIB_VIEW == var->type || var->oid.len < ddata->root_oid.len ||
			0 != memcmp(ddata->root_oid.b, var->oid.b, ddata->root_oid.len * sizeof(int))) {
		return FAIL;
	}

	if (SNMP_TP_NO_SUCH_INSTANCE == var->type && SNMP_TP_NO_SUCH_INSTANCE == var->type) {
		poller_preprocess_error(poller_item, snmp_err_to_text(var->type));
		return FAIL;
	}
	
	if ( 0 == asn1_cmp_oids(ddata->last_oid, var->oid)) {
		poller_preprocess_error(poller_item, "Cannot walk item: oid is not increasing");
		return FAIL;
	}
	
	if ( ddata->oids_looped++ > SNMP_WALK_MAX_OIDS ) {
		poller_preprocess_error(poller_item, "Cannot walk item: too many oids or cycle");
		return FAIL;
	}

	char oid_index[MAX_STRING_LEN];
	unsigned long obj_oid[MAX_OID_LEN];

	csnmp_oid_2_netsnmp(&var->oid, obj_oid);

	if (SUCCEED != snmp_choose_index(oid_index, sizeof(oid_index), obj_oid,
						var->oid.len, ddata->root_oid_str_len, ddata->root_oid_num_len)) {
		poller_preprocess_error(poller_item,"zbx_snmp_choose_index():"
			" cannot choose appropriate index while walking for OID");
		return FAIL;
	}

	AGENT_RESULT result = {0};
	init_result(&result);

	/*note: result absence is OK, not adding to indexes, but continue walking */
 	if (SUCCEED == snmp_set_result(poller_item, var, &result)) {
		//if (ISSET_TEXT(&result) && ZBX_SNMP_STR_HEX == val_type)
		//				zbx_remove_chars(snmp_result.text, "\r\n");
		
//		char **str_res = NULL;
//		str_res = GET_TEXT_RESULT(&result);
		
		DEBUG_ITEM(poller_get_item_id(poller_item),"Saving walk responce '%s'", result.text);


		if (NULL != result.text) 
			snmp_walk_save_result_value(ddata, ddata->request.params[ddata->num * 2 + 1], oid_index, result.text );
	}

	free_result(&result);
	return SUCCEED;
}

void snmp_walk_process_result(poller_item_t *poller_item, const csnmp_pdu_t *pdu) {
	csnmp_pdu_t new_pdu;
	csnmp_oid_t next_oid;
	int i, ret;
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	snmp_ddata_t *ddata = snmp_item->data;
	
	DEBUG_ITEM(poller_get_item_id(poller_item), "Processing responce for the walk item");

	if (pdu->vars_len < 1) {
		snmp_walk_start_next_oid(poller_item);
		return;
	}

	for (i = 0; i < pdu->vars_len; i++) {
	//	LOG_INF("Processing var for item %d", poller_get_item_id(poller_item));
		/* either tree finished or walk fail - processing next oid or finishing the walk */
		
		if (FAIL == snmp_walk_process_var(poller_item, &pdu->vars[i])) {
	//		LOG_INF("Failed to process item %d", poller_get_item_id(poller_item));
			snmp_walk_start_next_oid(poller_item);
			return;
		}
	}

//	LOG_INF("Sending next packet for item %d", poller_get_item_id(poller_item));




	copy_oid(&next_oid, &pdu->vars[pdu->vars_len-1].oid);
	asn1_free_oid(&ddata->last_oid);
	copy_oid(&ddata->last_oid, &pdu->vars[pdu->vars_len-1].oid);
	
	snmp_fill_pdu_header(poller_item, &new_pdu, SNMP_CMD_GET_NEXT);
	csnmp_add_var(&new_pdu, next_oid, SNMP_TP_NULL, NULL);
	snmp_send_packet(poller_item, &new_pdu);
	csnmp_free_pdu(&new_pdu);
}

int snmp_walk_send_first_request(poller_item_t *poller_item) {
	csnmp_pdu_t pdu;
	asn1_oid_t oid;
	char *start_oid;

	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
//	LOG_INF("Starting walk item %d", poller_get_item_id(poller_item));
	DEBUG_ITEM(poller_get_item_id(poller_item), "Starting async walk on item ");
	
	if (NULL != snmp_item->data) {
		poller_preprocess_error(poller_item, "Cannot start snmp walk: old request isn't finished yet");
		return FAIL;
	}

	if (FAIL == snmp_ddata_init(poller_item)) 
		return FAIL;
	
	snmp_walk_start_next_oid(poller_item);
}

void snmp_walk_timeout(poller_item_t *poller_item) {
	snmp_item_t *snmp_item = poller_get_item_specific_data(poller_item);
	
	DEBUG_ITEM(poller_get_item_id(poller_item),"Registered timeout for the item, try %d of %d", snmp_item->retries + 1 , SNMP_MAX_RETRIES);

	if (snmp_item->retries >= SNMP_MAX_RETRIES) {
		
		DEBUG_ITEM(poller_get_item_id(poller_item),"SNMP WALK timeout, all retries exceeded");
		poller_preprocess_error(poller_item, "SNMP timeout: max retries exceeded");
		snmp_ddata_clean(poller_item);
		poller_return_item_to_queue(poller_item);
	//	snmp_item->sessid = 0;
	} else { /*one more retry */
		
		csnmp_pdu_t pdu;
		snmp_ddata_t *ddata = snmp_item->data;
		asn1_oid_t last_oid;
		DEBUG_ITEM( poller_get_item_id(poller_item),"Sending retry, retry %d", snmp_item->retries);
		
		snmp_fill_pdu_header(poller_item, &pdu, SNMP_CMD_GET_NEXT);
		copy_oid(&last_oid, &ddata->last_oid);

		csnmp_add_var(&pdu, last_oid, SNMP_TP_NULL, NULL);
		snmp_send_packet(poller_item, &pdu);
		csnmp_free_pdu(&pdu);
		snmp_item->retries++;
	}
}