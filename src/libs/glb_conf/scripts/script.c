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

/*
   [{       "scriptid": "4",
            "name": "Webhook",
			"command": "try {\n var request = new HttpRequest(),\n response,\n data;\n\n request.addHeader('Content-Type: application/json');\n\n response = request.post('https://localhost/post', value);\n\n try {\n response = JSON.parse(response);\n }\n catch (error) {\n response = null;\n }\n\n if (request.getStatus() !== 200 || !('data' in response)) {\n throw 'Unexpected response.';\n }\n\n data = JSON.stringify(response.data);\n\n Zabbix.log(3, '[Webhook Script] response data: ' + data);\n\n return data;\n}\ncatch (error) {\n Zabbix.log(3, '[Webhook Script] script execution failed: ' + error);\n throw 'Execution failed: ' + error + '.';\n}",
			"host_access": "2",
			"usrgrpid": "7",
			"groupid": "0",
			"description": "",
			"confirmation": "",         
			"type": "5",
			"execute_on": "1",
			"timeout": "30s",
			"scope": "2",
			"port": "",
			"authtype": "0",
			"username": "",           
			"password": "",
			"publickey": "",
			"privatekey": "",
			"menu_path": "",
			"url": "",
			"new_window": "1",
			"parameters": 
				[ {"name": "token", "value": "{$WEBHOOK.TOKEN}"},
			      {"name": "host",  "value": "{HOST.HOST}"},
				  {"name": "v", "value": "2.2" }
				]
			}, .....
	]
*/

#include "glb_common.h"
#include "zbxcommon.h"
#include "zbxjson.h"
#include "scripts.h"
#include "zbxstr.h"
#include "../conf.h"
#include "script.h"

#include "zbxvariant.h"
#include "zbxserver.h"

typedef struct {
	char *name;
	char *value;
} script_param_t;

struct glb_conf_script_t {
    const char *name;
	const char *command;
	
	int type;
	int execute_on;
	const char *timeout;
	int scope;

	u_int64_t port;
	int authtype;

	const char *username;
	const char *password;
	const char *publickey;
	const char *privatekey;

	const char *url;

	int params_count;
	script_param_t *params;
	/*next params are probably not needed for the server, but exists in DB/API*/

	//int host_access;
	//u_int64_t usrgrpid;
	//u_int64_t groupid;
	//const char *description;
	//const char *confirmation;
	//const char *menu_path;
	//int new_window;
} script_t;

CONF_ARRAY_CREATE_FROM_JSON_CB(parameter_create_cb) {
	script_param_t *param = data;
	
	if (SUCCEED == glb_conf_add_json_param_memf(jp, memf, "name", &param->name) &&
		SUCCEED == glb_conf_add_json_param_memf(jp, memf, "value", &param->value)) 
		return SUCCEED;

	param->name = NULL;
	param->value = NULL;
	
	return FAIL;
}

CONF_ARRAY_FREE_CB(parameter_free_cb){
	script_param_t *param = data;
	
	if (NULL != param->name)
		memf->free_func(param->name);
	
	if (NULL != param->value)
		memf->free_func(param->value);
}

void glb_conf_script_free(glb_conf_script_t *script, mem_funcs_t *memf, strpool_t *strpool) {
	glb_conf_script_clear(script, memf, strpool);
	memf->free_func(script);
}

void glb_conf_script_clear(glb_conf_script_t *script, mem_funcs_t *memf, strpool_t *strpool) {

	strpool_free(strpool, script->name);
	strpool_free(strpool, script->command);
	strpool_free(strpool, script->timeout);
	strpool_free(strpool, script->username);
	strpool_free(strpool, script->password);
	strpool_free(strpool, script->publickey);
	strpool_free(strpool, script->privatekey);
	strpool_free(strpool, script->url);

	glb_conf_free_json_array(script->params, script->params_count, sizeof(script_param_t), memf, strpool,  parameter_free_cb);
	bzero(script, sizeof(glb_conf_script_t));
}

static int script_fill_from_json(glb_conf_script_t *script, struct zbx_json_parse *jp, mem_funcs_t *memf,  strpool_t *strpool) {
	int errflg;
	bzero(script, sizeof(glb_conf_script_t));
	
	glb_conf_add_json_param_strpool(jp, strpool, "name",  &script->name);
	glb_conf_add_json_param_strpool(jp, strpool, "command",  &script->command);
	glb_conf_add_json_param_strpool(jp, strpool, "timeout",  &script->timeout);
	glb_conf_add_json_param_strpool(jp, strpool, "username",  &script->username);
	glb_conf_add_json_param_strpool(jp, strpool, "password",  &script->password);
	glb_conf_add_json_param_strpool(jp, strpool, "publickey",  &script->publickey);
	glb_conf_add_json_param_strpool(jp, strpool, "privatekey",  &script->privatekey);
	glb_conf_add_json_param_strpool(jp, strpool, "url",  &script->url);
	
	script->type 	   = glb_json_get_uint64_value_by_name(jp, "type", &errflg);
	script->execute_on = glb_json_get_uint64_value_by_name(jp, "execute_on", &errflg);
	script->scope      = glb_json_get_uint64_value_by_name(jp, "scope", &errflg);
	script->port       = glb_json_get_uint64_value_by_name(jp, "port", &errflg);
	script->authtype   = glb_json_get_uint64_value_by_name(jp, "authtype", &errflg);
	
	script->params_count = glb_conf_create_array_from_json((void **)&script->params, "parameters", jp, sizeof(script_param_t), 
				memf, strpool, parameter_create_cb);
	
	//LOG_INF("Params filled, total %d params", script->params_count);
	return SUCCEED;
}

glb_conf_script_t *glb_conf_script_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    glb_conf_script_t *script;

    if (NULL ==(script = memf->malloc_func(NULL, sizeof(glb_conf_script_t))))
        return NULL;

	if (FAIL == script_fill_from_json(script, jp, memf,  strpool))  {
		glb_conf_script_clear(script, memf, strpool);
		memf->free_func(script);
	
		return NULL;
	}
	return script;
};   

glb_conf_script_t *glb_conf_script_copy(glb_conf_script_t *src_script, mem_funcs_t *memf, strpool_t *strpool) {
 	glb_conf_script_t *dst_script;

 	if (NULL ==(dst_script = memf->malloc_func(NULL, sizeof(glb_conf_script_t))))
         return NULL;
	
	//this will copy all the numerical values
 	memcpy(dst_script, src_script, sizeof(glb_conf_script_t));
	dst_script->name = strpool_add(strpool, src_script->name);
	dst_script->command = strpool_add(strpool, src_script->command);
	dst_script->timeout = strpool_add(strpool, src_script->timeout);
	dst_script->username = strpool_add(strpool, src_script->username);
	dst_script->password = strpool_add(strpool, src_script->password);
	dst_script->publickey = strpool_add(strpool, src_script->publickey);	
	dst_script->privatekey = strpool_add(strpool, src_script->privatekey);
	dst_script->url = strpool_add(strpool, src_script->url);
	
	return dst_script;
}