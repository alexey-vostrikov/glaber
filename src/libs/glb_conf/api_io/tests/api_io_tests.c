/*
** Copyright Glaber 2018-2023
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

#include "zbxalgo.h"
#include "glb_common.h"
#include "../glb_api_conf.h"

#ifdef HAVE_GLB_TESTS

static void test_api_init() {
    assert( FAIL == glb_api_conf_init(NULL, NULL) && 
            FAIL == glb_api_conf_init("123", NULL) && 
            FAIL == glb_api_conf_init(NULL, "123") && 
            "API init should fail on absent uri and/or auth");

    assert( FAIL == glb_api_conf_init("http://:ya.ru/123", "1234") && "should fail on malformed url");
    assert( FAIL == glb_api_conf_init("http1://ya.ru/123", "1234") && "should fail on non {http/https} scheme");
    assert( FAIL == glb_api_conf_init("http1://", "1234") && "should fail if host not exists");
}

static void glb_api_sync_operations_test() {
    char *buffer = NULL;
    char *query = 
    "{  \"jsonrpc\": \"2.0\", \
        \"method\": \"action.get\",\
        \"params\": \
            { \"output\": \"extend\", \
              \"selectOperations\": \"extend\",\
              \"selectRecoveryOperations\": \"extend\",\
              \"selectUpdateOperations\": \"extend\",\
              \"selectFilter\": \"extend\"\
            }, \
            \"id\": 28\
     }";

    LOG_INF("Loading API query is %s", query);
    glb_api_conf_sync_request(query, &buffer, "actions_operations", API_NO_CACHE);
    LOG_INF("Got response %s", buffer);
    zbx_free(buffer);
}


static void test_sync_api() {
    char *buffer = NULL;
    LOG_INF("Doing sync api tests");
   // glb_api_conf_sync_request("{\"jsonrpc\":\"2.0\",\"method\":\"action.get\",\"params\":{\"output\":\"extend\"},\"id\":1}", &buffer, "test", API_NO_CACHE);
    LOG_INF("Got response %s", buffer);
    zbx_free(buffer);
    glb_api_sync_operations_test();
    
    
    HALT_HERE("Tests aren't truly implemented yet");
}


void run_api_io_tests() {
    test_api_init();
    test_sync_api();

    HALT_HERE("Api io tests completed");
}

#endif