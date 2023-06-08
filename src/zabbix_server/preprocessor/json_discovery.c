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
#include "zbxalgo.h"
#include "json_discovery.h"
#include "zbxstr.h"

#define MIN_ITEM_EXPORT_FREQUENCY 5
#define REEXPORT_FREQUENCY 15

typedef struct preproc_discovery_agg_conf_t {
	elems_hash_t *discovery_data;
	strpool_t strpool;
} preproc_discovery_agg_conf_t;


typedef struct {
	preproc_discovery_agg_conf_t *conf;
	const char *in_json;
	struct zbx_json *json;
	char **response;
	int response_count;
	int export_time;
	int ttl_time;
	zbx_vector_uint64_t *delids;
} cb_data_params_t;


typedef struct {
	elems_hash_t *elems;
	int last_export;
} discoveries_data_t;

typedef struct {
	const char* value; 
	int last_exported;
	int last_added;
} discovery_element_t;

ELEMS_CREATE(discovery_create_func) {
	elem->data = NULL;
}

ELEMS_FREE(discovery_free_func) {
	if ( NULL != elem->data) 
		memf->free_func(elem->data);
}

ELEMS_CALLBACK(discovery_process_cb) {
	discovery_element_t *disc_elem  = elem->data;
	const char* pooled_addr  = (const char *)elem->id; //using addr as the string id

	if (NULL == disc_elem) {
		disc_elem = memf->malloc_func(NULL, sizeof(discovery_element_t));
		disc_elem->value = strpool_copy(pooled_addr);
		disc_elem->last_exported = 0;
		elem->data = disc_elem;
	}

	disc_elem->last_added = time(NULL);
	return SUCCEED;
}

ELEMS_CREATE(discoveries_create_cb) {
	elem->data = memf->malloc_func(NULL, sizeof(discoveries_data_t));
	discoveries_data_t *ddata = elem->data;
	ddata->elems = elems_hash_init(memf, discovery_create_func, discovery_free_func);
	ddata->last_export = time(NULL);
}

ELEMS_FREE(discoveries_free_cb) {
	LOG_INF("JSON_DE destroyng data for item %ld",elem->id);
	discoveries_data_t *ddata = elem->data;
	elems_hash_destroy(ddata->elems);

}
		      
preproc_discovery_agg_conf_t *preproc_discovery_agg_init() {
	preproc_discovery_agg_conf_t *conf = zbx_calloc(NULL, 0, sizeof(preproc_discovery_agg_conf_t));
	conf->discovery_data = elems_hash_init(NULL, discoveries_create_cb, discoveries_free_cb);
	strpool_init(&conf->strpool, NULL);
	return conf;
};

ELEMS_CALLBACK(collect_elems_to_export_cb) {
	discovery_element_t *disc_elem  = elem->data;
	cb_data_params_t *cbdata = data;

	if (NULL == disc_elem)
		return FAIL;
	
	if (disc_elem->last_exported < cbdata->export_time) {
		zbx_json_addraw(cbdata->json, NULL, disc_elem->value);
		cbdata->response_count++;
		disc_elem->last_exported = time(NULL);
	}
	
	if (disc_elem->last_added < cbdata->ttl_time) {
		strpool_free(&cbdata->conf->strpool, disc_elem->value);
		elem->flags|=ELEM_FLAG_DELETE;
		zbx_vector_uint64_append(cbdata->delids, elem->id);
	}
	return SUCCEED;
}

ELEMS_CALLBACK(account_json_cb) {
	
	discoveries_data_t *ddata = elem->data;
	cb_data_params_t *cbdata = data;
	struct zbx_json j;
	zbx_vector_uint64_t delids;

	const char *pooled_str = strpool_add(&cbdata->conf->strpool, cbdata->in_json);

	elems_hash_process(ddata->elems, (u_int64_t)pooled_str, discovery_process_cb, cbdata, 0);
	strpool_free(&cbdata->conf->strpool, pooled_str);

	if (ddata->last_export + MIN_ITEM_EXPORT_FREQUENCY > time(NULL)) {
		return FAIL;
	}

	ddata->last_export = time(NULL);
	*cbdata->response = NULL;
	cbdata->response_count = 0;
	cbdata->json = &j;
	
	zbx_json_init(&j, 16386);
	zbx_json_addarray(&j, NULL);
	zbx_vector_uint64_create(&delids);
	cbdata->delids = &delids;

	elems_hash_iterate(ddata->elems, collect_elems_to_export_cb, cbdata, 0);
	elems_hash_mass_delete(ddata->elems, &delids);

	zbx_vector_uint64_destroy(&delids);

	//copy, but without outer { }
	if (cbdata->response_count > 0)
		*cbdata->response = zbx_strdup(NULL, j.buffer + 1);
	zbx_rtrim(*cbdata->response,"}");

	zbx_json_free(&j);

	return SUCCEED;
}

int   preproc_discovery_account_json(preproc_discovery_agg_conf_t * conf, u_int64_t itemid, const char *json, char **response) {
	cb_data_params_t cbdata = {.conf = conf, .in_json = json, .response = response, 
				.export_time = time(NULL) - REEXPORT_FREQUENCY, .ttl_time = time(NULL) - 2*REEXPORT_FREQUENCY };

	if (FAIL  == elems_hash_process(conf->discovery_data, itemid, account_json_cb, &cbdata, 0)) 
		return FAIL;

	return SUCCEED;
}
