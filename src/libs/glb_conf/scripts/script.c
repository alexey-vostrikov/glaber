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

#include "zbxvariant.h"
#include "zbxserver.h"

typedef struct {
	const char *name;
	const char *value;
} script_params_t;

struct glb_conf_script_t {
    const char *name;
	const char *command;
	int host_access;
	u_int64_t usrgrpid;
	u_int64_t groupid;
	const char *description;
	const char *confirmation;
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
//	const char *menu_path;
	const char *url;
//	int new_window;
	int params_count;
	script_params_t *params;
} script_t;


void glb_conf_script_free(glb_conf_script_t *script, mem_funcs_t *memf, strpool_t *strpool) {

    memf->free_func(script);
}

glb_conf_script_t* json_to_script(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    char value[64], newvalue[64], type[MAX_ID_LEN];
    zbx_json_type_t jtype;
  
    if (SUCCEED == zbx_json_value_by_name(jp, "type", type, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "value", value, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "newvalue", newvalue, MAX_ID_LEN, &jtype )) {
        glb_conf_script_t *script = memf->malloc_func(NULL, sizeof(glb_conf_script_t));

        return script;
    }

    return NULL;
}

// int parse_valuemap(glb_conf_valuemap_t *vm, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
//     const char *mapping_ptr = NULL;
//     struct zbx_json_parse jp_mapping;
//     mapping_t *mapping;
//     int i = 0;
    
//     //LOG_INF("In %s", __func__);
    
//     while (NULL != (mapping_ptr = zbx_json_next(jp, mapping_ptr)))
//     {
//         //LOG_INF("In %s 1", __func__);
//         if ( SUCCEED == zbx_json_brackets_open(mapping_ptr, &jp_mapping) &&
//             NULL != (mapping = json_to_valuemap(&jp_mapping, memf, strpool))) {
//             zbx_vector_ptr_append(&vm->mappings, mapping);
//             i++;
//         }
//     }
//     return i;
// }


/*takes the whole mapping object and */
glb_conf_script_t *glb_conf_script_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {

    struct zbx_json_parse jp_mappings;
    glb_conf_script_t *script;
    int errflag;
//    LOG_INF("In %s", __func__);

    if (NULL ==(script = memf->malloc_func(NULL, sizeof(glb_conf_script_t))))
        return NULL;

    //zbx_vector_ptr_create_ext(&vm->mappings, memf->malloc_func, memf->realloc_func, memf->free_func);

    // if (SUCCEED != zbx_json_brackets_by_name(jp, "mappings", &jp_mappings) ||
    //        0 ==( vm->id = glb_json_get_uint64_value_by_name(jp, "valuemapid", &errflag)) || 
    //        0 == parse_valuemap(vm, &jp_mappings, memf, strpool) ) {
    
    //     zbx_vector_ptr_destroy(&vm->mappings);
    //     memf->free_func(vm);

    //     return NULL;
    // } 

    return script;
};   

