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

//CREATE TABLE valuemap_mapping (
//	valuemap_mappingid       bigint                                    NOT NULL,
//	valuemapid               bigint                                    NOT NULL,
//	value                    varchar(64)     DEFAULT ''                NOT NULL,
//	newvalue                 varchar(64)     DEFAULT ''                NOT NULL,
//	type                     integer         DEFAULT '0'               NOT NULL,
//	sortorder                integer         DEFAULT '0'               NOT NULL,
//	PRIMARY KEY (valuemap_mappingid)

/*jsonrpc": "2.0",     "result": [  
        {           "valuemapid": "4",
                    "name": "APC Battery Replacement Status",
                    "mappings": [ 
                        {  "type": "0",
                            "value": "1",
                            "newvalue": "unknown"
                        }, 
                        {   "type": "0",
                            "value": "2",                     "newvalue": "notInstalled"                 },                 {                     "type": "0",                     "value": "3",                     "newvalue": "ok"                 },
                        .... */
#include "glb_common.h"
#include "zbxcommon.h"
#include "zbxjson.h"
#include "conf_valuemap.h"
#include "zbxstr.h"
#include "zbxregexp.h"
#include "zbxvariant.h"
#include "zbxserver.h"
#include "zbxparam.h"
#include "../items/conf_items.h"

#define VALUEMAP_TYPE_MATCH			0
#define VALUEMAP_TYPE_GREATER_OR_EQUAL	1
#define VALUEMAP_TYPE_LESS_OR_EQUAL		2
#define VALUEMAP_TYPE_RANGE			3
#define VALUEMAP_TYPE_REGEX			4
#define VALUEMAP_TYPE_DEFAULT		5

typedef struct {
    int type;
    const char *value;
    const char *newvalue;
} mapping_t;

struct glb_conf_valuemap_t {
    u_int64_t id;
    zbx_vector_ptr_t mappings;
};

void glb_conf_valuemap_free(glb_conf_valuemap_t *vm, mem_funcs_t *memf, strpool_t *strpool) {
    
    for (int i = 0; i < vm->mappings.values_num; i++) {
        mapping_t *map = vm->mappings.values[i];
        strpool_free(strpool,map->value);
        strpool_free(strpool,map->newvalue);
        memf->free_func(map);
    }
    zbx_vector_ptr_destroy(&vm->mappings);
    memf->free_func(vm);
}

mapping_t* json_to_valuemap(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    char value[64], newvalue[64], type[MAX_ID_LEN];
    zbx_json_type_t jtype;
  //  LOG_INF("In %s", __func__);

    if (SUCCEED == zbx_json_value_by_name(jp, "type", type, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "value", value, MAX_ID_LEN, &jtype ) && 
        SUCCEED == zbx_json_value_by_name(jp, "newvalue", newvalue, MAX_ID_LEN, &jtype )) {
        mapping_t *map = memf->malloc_func(NULL, sizeof(mapping_t));

        if (NULL == map) 
            return NULL;
        
        map->type = strtol(type, NULL, 10);

		if (VALUEMAP_TYPE_RANGE == map->type) {
			zbx_trim_str_list(value, ',');
			zbx_trim_str_list(value, '-');
		}

        map->value = strpool_add(strpool, value);
        map->newvalue = strpool_add(strpool, newvalue);
     //   LOG_INF("Succesifully parsed valuemap %d %s %s", map->type, map->value, map->newvalue);
        return map;
    }

    return NULL;
}

int parse_valuemap(glb_conf_valuemap_t *vm, struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {
    const char *mapping_ptr = NULL;
    struct zbx_json_parse jp_mapping;
    mapping_t *mapping;
    int i = 0;
    
    //LOG_INF("In %s", __func__);
    
    while (NULL != (mapping_ptr = zbx_json_next(jp, mapping_ptr)))
    {
        //LOG_INF("In %s 1", __func__);
        if ( SUCCEED == zbx_json_brackets_open(mapping_ptr, &jp_mapping) &&
             NULL != (mapping = json_to_valuemap(&jp_mapping, memf, strpool))) {
                //LOG_INF("In %s 3", __func__);
            zbx_vector_ptr_append(&vm->mappings, mapping);
            i++;
        }
    }
    return i;
}


/*takes the whole mapping object and */
glb_conf_valuemap_t *glb_conf_valuemap_create_from_json(struct zbx_json_parse *jp, mem_funcs_t *memf, strpool_t *strpool) {

    struct zbx_json_parse jp_mappings;
    glb_conf_valuemap_t *vm;
    int errflag;
//    LOG_INF("In %s", __func__);

    if (NULL ==(vm = memf->malloc_func(NULL, sizeof(glb_conf_valuemap_t))))
        return NULL;

    zbx_vector_ptr_create_ext(&vm->mappings, memf->malloc_func, memf->realloc_func, memf->free_func);

    if (SUCCEED != zbx_json_brackets_by_name(jp, "mappings", &jp_mappings) ||
           0 ==( vm->id = glb_json_get_uint64_value_by_name(jp, "valuemapid", &errflag)) || 
           0 == parse_valuemap(vm, &jp_mappings, memf, strpool) ) {
    
        zbx_vector_ptr_destroy(&vm->mappings);
        memf->free_func(vm);

        return NULL;
    } 

    return vm;
};   


/******************************************************************************
  *evaluates and resolves valuemap based on valuemap object                   *
 ******************************************************************************/
int	glb_conf_valuemap_evaluate(char *value, size_t max_len, glb_conf_valuemap_t *vmap,
		unsigned char value_type)
{
	char		*value_tmp;
	int		i, ret = FAIL;
	double		input_value;
	mapping_t	*valuemap;

	for (i = 0; i < vmap->mappings.values_num; i++)
	{
		const char			*pattern;
		int			match;
		zbx_vector_expression_t	regexps;

		valuemap = (mapping_t *)vmap->mappings.values[i];

		if (VALUEMAP_TYPE_MATCH == valuemap->type)
		{
			if (ITEM_VALUE_TYPE_STR != value_type)
			{
				double	num1, num2;

				if (ZBX_INFINITY != (num1 = zbx_evaluate_string_to_double(value)) &&
						ZBX_INFINITY != (num2 = zbx_evaluate_string_to_double(valuemap->value)) &&
						SUCCEED == zbx_double_compare(num1, num2))
				{
					goto map_value;
				}
			}
			else if (0 == strcmp(valuemap->value, value))
				goto map_value;
		}

		if (ITEM_VALUE_TYPE_STR == value_type && VALUEMAP_TYPE_REGEX == valuemap->type)
		{
			zbx_vector_expression_create(&regexps);

			pattern = valuemap->value;

			match = zbx_regexp_match_ex(&regexps, value, pattern, ZBX_CASE_SENSITIVE);

			zbx_regexp_clean_expressions(&regexps);
			zbx_vector_expression_destroy(&regexps);

			if (ZBX_REGEXP_MATCH == match)
				goto map_value;
		}

		if (ITEM_VALUE_TYPE_STR != value_type &&
				ZBX_INFINITY != (input_value = zbx_evaluate_string_to_double(value)))
		{
			double	min, max;

			if (VALUEMAP_TYPE_LESS_OR_EQUAL == valuemap->type &&
					ZBX_INFINITY != (max = zbx_evaluate_string_to_double(valuemap->value)))
			{
				if (input_value <= max)
					goto map_value;
			}
			else if (VALUEMAP_TYPE_GREATER_OR_EQUAL == valuemap->type &&
					ZBX_INFINITY != (min = zbx_evaluate_string_to_double(valuemap->value)))
			{
				if (input_value >= min)
					goto map_value;
			}
			else if (VALUEMAP_TYPE_RANGE == valuemap->type)
			{
				int	num, j;

				num = zbx_num_param(valuemap->value);

				for (j = 0; j < num; j++)
				{
					int	found = 0;
					char	*ptr, *range_str;

					range_str = ptr = zbx_get_param_dyn(valuemap->value, j + 1, NULL);

					if (1 < strlen(ptr) && '-' == *ptr)
						ptr++;

					while (NULL != (ptr = strchr(ptr, '-')))
					{
						if (ptr > range_str && 'e' != ptr[-1] && 'E' != ptr[-1])
							break;
						ptr++;
					}

					if (NULL == ptr)
					{
						min = zbx_evaluate_string_to_double(range_str);
						found = ZBX_INFINITY != min && SUCCEED == zbx_double_compare(input_value, min);
					}
					else
					{
						*ptr = '\0';
						min = zbx_evaluate_string_to_double(range_str);
						max = zbx_evaluate_string_to_double(ptr + 1);
						if (ZBX_INFINITY != min && ZBX_INFINITY != max &&
								input_value >= min && input_value <= max)
						{
							found = 1;
						}
					}

					zbx_free(range_str);

					if (0 != found)
						goto map_value;
				}
			}
		}
	}

	for (i = 0; i < vmap->mappings.values_num; i++)
	{
		valuemap = (mapping_t *)vmap->mappings.values[i];

		if (VALUEMAP_TYPE_DEFAULT == valuemap->type)
			goto map_value;
	}
map_value:
	if (i < vmap->mappings.values_num)
	{
		value_tmp = zbx_dsprintf(NULL, "%s (%s)", valuemap->newvalue, value);
		zbx_strlcpy_utf8(value, value_tmp, max_len);
		zbx_free(value_tmp);

		ret = SUCCEED;
	}

	return ret;
}

typedef struct {
	unsigned char value_type;

} vm_params_t;

