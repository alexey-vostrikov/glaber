
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

/* this is simple monitoring engine to export internal metrics
via prometheus protocol via standart monitoring url

now it's NOT follows standards as it doesn't support HELP and TYPE keywords
*/

//TODO idea for improvement - implement a kind of a buffer pool to avoid alloc cluttering

#include "common.h"
#include "zbxalgo.h"
#include "log.h"

int test_parse_preproc_params() {
    char buffer[256];
    
    preproc_params_t *params;
    params = item_preproc_parse_params(NULL);
    assert ( -1 == params->count );
    
    zbx_snprintf(buffer, 256,"a param");
    params = item_preproc_parse_params(buffer);
    assert (1 == params->count );
    
    zbx_snprintf(buffer, 256,"a param\nanotherparam");
    params = item_preproc_parse_params(buffer);
    assert (2 == params->count );
    
    zbx_snprintf(buffer, 256,"1\n'222'\ndfd\ncheck_param");
    params = item_preproc_parse_params(buffer);
    assert( 0 == strcmp("check_param",params->params[3]));
    assert( 4 == params->count);
}

void test_get_host_name_from_json() {
    char *hostname = get_param_name_from_json("{\"test\":\"test\"}", NULL);
    assert(NULL == hostname);

    hostname = get_param_name_from_json( "{\"test\":\"test\"}", "test");
    assert(NULL != hostname && 0 == strcmp(hostname, "test"));

    hostname = get_param_name_from_json( NULL, "test");
    assert(NULL == hostname);
    
    hostname = get_param_name_from_json("{\"test\":\"test\"", "test");
    assert(NULL == hostname);

    hostname = get_param_name_from_json("{\"test1\":\"test1\"}", "test1");
    assert(NULL != hostname && 0 == strcmp(hostname, "test1"));

    hostname = get_param_name_from_json("{\"test1\":\"test1\"}", "$.test1");
    assert(NULL != hostname && 0 == strcmp(hostname, "test1"));

    hostname = get_param_name_from_json("{\"vtest1\":\"test1\"}", "{vtest1}.server.name");
    LOG_INF("Result hostname is %s", hostname);
    assert(NULL != hostname && 0 == strcmp(hostname, "test1.server.name"));

    hostname = get_param_name_from_json("{\"test1\":\"vtest1\"}", "{}.server22.name");
    assert(NULL == hostname);
 
    hostname = get_param_name_from_json("{\"vtest1\":\"test11\"}", "{vtest1}.server22.name");
    assert(NULL != hostname && 0 == strcmp(hostname, "test11.server22.name"));

    hostname = get_param_name_from_json("{\"vtest1\":\"test11\"}", "{$.vtest1}.server22.name");
    assert(NULL != hostname && 0 == strcmp(hostname, "test11.server22.name"));

    hostname = get_param_name_from_json("{\"vtest1\":\"test11\"}", "prefix.{vtest1}.server22.name");
    assert(NULL != hostname && 0 == strcmp(hostname, "prefix.test11.server22.name"));
    
    hostname = get_param_name_from_json("{\"vtest1\":\"test11\"}", "prefix.{$.vtest1}.server22.name");
    assert(NULL != hostname && 0 == strcmp(hostname, "prefix.test11.server22.name"));
}

void test_get_ip_from_json() {
    char *ip_str, *ip;
    ip_str = get_param_name_from_json("{\"ip\":\"10.100.1.2\"}","ip");
    assert(NULL != ip_str && 0 == strcmp(ip_str, "10.100.1.2"));
    
    ip_str = get_param_name_from_json("{\"ip\":\"10.100.1.2:8016\"}","ip");
    assert(NULL != ip_str && 0 == strcmp(ip_str, "10.100.1.2:8016"));
    
//    ip = get_ip_from_string(ip_str);

    
    //HALT_HERE("Test has failed");    

    LOG_INF("%s tests is OK",__func__);
}

int test_worker_json_to_host() {

	LOG_INF("Check if all null isn't fine");
	
    //assert(NULL == get_item_and_key_name_from_json(0, NULL, NULL));

	//assert( NULL == 1);

	HALT_HERE("Test has failed");
}