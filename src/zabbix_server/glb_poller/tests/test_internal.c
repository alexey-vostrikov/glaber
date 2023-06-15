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
#include "glb_common.h"
#include "../internal.h"

int dfs = 0;

INTERNAL_METRIC_CALLBACK(test_callback){
    LOG_INF("Callback is called");
    *result = zbx_strdup(NULL, "Hello world");
    dfs  = 123;
    return SUCCEED;
};

void test_internal_metrics() {
    LOG_INF("Testing registration");
    glb_internal_metrics_init();
    
    LOG_INF("Testing registration SUCCEED");
    assert(SUCCEED == glb_register_internal_metric_handler("test_handler", test_callback) &&
        "callback func should register");
    LOG_INF("Testing registration FAIL");
    assert(FAIL == glb_register_internal_metric_handler(NULL, test_callback) &&
        "callback func should fail");
    assert(FAIL == glb_register_internal_metric_handler("test_handler", test_callback) &&
        "Registration of the same name should fail");
    
    char *result = NULL;
    
    LOG_INF("Testing functions run");
    
    assert(FAIL == glb_get_internal_metric("non_existant_key", 0, NULL, &result) &&
            "Processing of nonexistant metric should fail");

    assert(SUCCEED == glb_get_internal_metric("test_handler", 0, NULL, &result) &&
            "Processing of existant metric should succeed");

    assert(123 == dfs && "Processing of existant metric should run func and set dfs");
    assert(0 == strcmp(result, "Hello world") && "Callback should return hello world");
    
    zbx_free(result);
 
    glb_internal_metrics_destory();
}


void run_internal_metric_tests() {
    sleep(1);
    test_internal_metrics();
    LOG_INF("Running something");
    HALT_HERE("Internal metrics testing compeleted");
}