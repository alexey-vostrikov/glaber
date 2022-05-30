/*
** Glaber
** Copyright (C)  Glaber
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
#include "glb_poller.h"
#include "../poller/checks_calculated.h"

typedef struct {
    strpool_t strpool;
    mem_funcs_t memf;
} calc_module_conf_t;

static calc_module_conf_t conf;

typedef struct {
    const char *params;
    unsigned char *formula_bin; 
    const char *key;
    const char *host;
    const char *hostname;
    u_int64_t hostid;
} calculated_item_t;

/*note: no async processing needed for calc, it's sync */
void handle_async_io(void *m_conf) {
    usleep(1000);
}

int init_item(void *m_conf, DC_ITEM *dc_item, poller_item_t *poller_item) {
    calculated_item_t *calc_item = zbx_calloc(NULL,0,sizeof(calculated_item_t));
    poller_set_item_specific_data(poller_item, calc_item);

	calc_item->params = strpool_add(&conf.strpool, dc_item->params);
    calc_item->key = strpool_add(&conf.strpool, dc_item->key);
    calc_item->host = strpool_add(&conf.strpool, dc_item->host.host);
    calc_item->hostname = strpool_add(&conf.strpool, dc_item->host.name);
    
	calc_item->formula_bin = dc_item->formula_bin; 
    dc_item->formula_bin = NULL;
    calc_item->hostid = dc_item->host.hostid;

    return SUCCEED;
}

void free_item(void *cfg, poller_item_t *poller_item) {
    calculated_item_t *calc_item = poller_get_item_specific_data(poller_item);
    
    strpool_free(&conf.strpool, calc_item->params);
    strpool_free(&conf.strpool, calc_item->key);
    strpool_free(&conf.strpool, calc_item->host);
    strpool_free(&conf.strpool, calc_item->hostname);

    zbx_free(calc_item->formula_bin);

	zbx_free(calc_item);
}

static void prepare_dc_item(DC_ITEM *dc_item, poller_item_t *poller_item) {
       calculated_item_t *calc_item = poller_get_item_specific_data(poller_item);
       dc_item->itemid = poller_get_item_id(poller_item);
       dc_item->params = (char *)calc_item->params;
       dc_item->formula_bin = calc_item->formula_bin;
       dc_item->key = (char *)calc_item->key;
       zbx_strlcpy(dc_item->key_orig, calc_item->key, strlen(calc_item->key) + 1);
       zbx_strlcpy(dc_item->host.host, calc_item->host, strlen(calc_item->host) + 1);
       zbx_strlcpy(dc_item->host.name, calc_item->hostname, strlen(calc_item->hostname) + 1);
       dc_item->host.hostid = calc_item->hostid;
}

void poll_item(void *m_conf, poller_item_t *poller_item) {
    DC_ITEM dc_item = {0};

    AGENT_RESULT result;
    u_int64_t now = glb_ms_time();

    prepare_dc_item(&dc_item, poller_item);
    init_result(&result);

    poller_inc_requests();

    if (SUCCEED != get_value_calculated(&dc_item, &result) ) {
        poller_preprocess_value(poller_item, &result, now, ITEM_STATE_NOTSUPPORTED, result.msg);
    } else 
        poller_preprocess_value(poller_item, &result, now, ITEM_STATE_NORMAL, NULL);
    
    poller_inc_responces();
    poller_return_item_to_queue(poller_item);   
    
    free_result(&result);
}

int forks_count(void *m_conf) {
    return CONFIG_HISTORYPOLLER_FORKS;
}

void poller_shutdown(void *cfg) {
    strpool_destroy(&conf.strpool);
}

int calculated_poller_init(poll_engine_t *poll){

    conf.memf.free_func = ZBX_DEFAULT_MEM_FREE_FUNC;
    conf.memf.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC;
    conf.memf.realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC;

    strpool_init(&conf.strpool, &conf.memf);
	poller_set_poller_callbacks(init_item, free_item, handle_async_io, poll_item, poller_shutdown, forks_count);
    
    return SUCCEED;
}