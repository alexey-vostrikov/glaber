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

#include "zbxalgo.h"
#include "glbstr.h"
#include "glb_log.h"
#include "zbxjson.h"
#include "conf_valuemap.h"
#include "../items/conf_items.h"
#include "zbxstr.h"

typedef struct
{
    elems_hash_t *valuemaps;
    mem_funcs_t memf;
    strpool_t strpool;

} valuemap_conf_t;

static valuemap_conf_t *conf = NULL;

ELEMS_CREATE(valuemap_new_cb)
{
    elem->data = NULL;
}

ELEMS_FREE(valuemap_free_cb)
{
    if (NULL != elem->data) 
        glb_conf_valuemap_free(elem->data, memf, &conf->strpool);
    
    return SUCCEED;
}

int glb_conf_valuemaps_init(mem_funcs_t *memf)
{
    if (NULL == (conf = memf->malloc_func(NULL, sizeof(valuemap_conf_t))))
    {
        LOG_WRN("Cannot allocate memory for glb cache struct");
        exit(-1);
    };
    
    conf->valuemaps = elems_hash_init(memf, valuemap_new_cb, valuemap_free_cb);
    conf->memf = *memf;
    strpool_init(&conf->strpool, memf);
    
    return SUCCEED;
}

int glb_conf_valuemaps_destroy() {
    elems_hash_destroy(conf->valuemaps);
    strpool_destroy(&conf->strpool);
    conf->memf.free_func(conf);
}

ELEMS_CALLBACK(create_update_valuemap) {
    struct zbx_json_parse *jp = data;

    if (NULL != elem->data) 
        glb_conf_valuemap_free(elem->data, memf, &conf->strpool);
        
    if (NULL != (elem->data = glb_conf_valuemap_create_from_json(jp, memf, &conf->strpool)))
        return SUCCEED;
    
    return FAIL;
} 

int glb_conf_valuemaps_set_data(char *json_buff){
    struct zbx_json_parse	jp, jp_result, jp_mapping;
    const char *mapping = NULL;
    int err;
    zbx_vector_uint64_t ids;
    
    zbx_vector_uint64_create(&ids);
    zbx_vector_uint64_reserve(&ids, 65536);

    if (SUCCEED != zbx_json_open(json_buff, &jp) ||
        SUCCEED != zbx_json_brackets_by_name(&jp, "result", &jp_result)) {
		zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't open JSON data (broken or malformed json) %s", json_buff);
		return FAIL;
	}

    while (NULL != (mapping = zbx_json_next(&jp_result, mapping))) {
        if (SUCCEED == zbx_json_brackets_open(mapping, &jp_mapping)) {
           
            u_int64_t vm_id = glb_json_get_uint64_value_by_name(&jp_mapping, "valuemapid", &err);

            if (err)
                continue;
            
//            LOG_INF("Got a valemapping %lld", vm_id);
            zbx_vector_uint64_append(&ids, vm_id);
            //note: for large tables it might be feasible to separate update and create and add cahange check
            if (SUCCEED == elems_hash_process(conf->valuemaps, vm_id, create_update_valuemap, &jp_mapping, 0)) 
                zbx_vector_uint64_append(&ids, vm_id);
  //          else 
  //              LOG_INF("Couldn't add valuemap %ld", vm_id);
        }
    }

    elems_hash_remove_absent_in_vector(conf->valuemaps, &ids);
    zbx_vector_uint64_destroy(&ids);
    return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: Process suffix 'uptime'                                           *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *                                                                            *
 ******************************************************************************/
static void	add_value_suffix_uptime(char *value, size_t max_len)
{
	double	secs, days;
	size_t	offset = 0;
	int	hours, mins;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 > (secs = round(atof(value))))
	{
		offset += zbx_snprintf(value, max_len, "-");
		secs = -secs;
	}

	days = floor(secs / SEC_PER_DAY);
	secs -= days * SEC_PER_DAY;

	hours = (int)(secs / SEC_PER_HOUR);
	secs -= (double)hours * SEC_PER_HOUR;

	mins = (int)(secs / SEC_PER_MIN);
	secs -= (double)mins * SEC_PER_MIN;

	if (0 != days)
	{
		if (1 == days)
			offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) " day, ", days);
		else
			offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) " days, ", days);
	}

	zbx_snprintf(value + offset, max_len - offset, "%02d:%02d:%02d", hours, mins, (int)secs);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: Process suffix 's'                                                *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *                                                                            *
 ******************************************************************************/
static void	add_value_suffix_s(char *value, size_t max_len)
{
	double	secs, n;
	size_t	offset = 0;
	int	n_unit = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	secs = atof(value);

	if (0 == floor(fabs(secs) * 1000))
	{
		zbx_snprintf(value, max_len, "%s", (0 == secs ? "0s" : "< 1ms"));
		goto clean;
	}

	if (0 > (secs = round(secs * 1000) / 1000))
	{
		offset += zbx_snprintf(value, max_len, "-");
		secs = -secs;
	}
	else
		*value = '\0';

	if (0 != (n = floor(secs / SEC_PER_YEAR)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) "y ", n);
		secs -= n * SEC_PER_YEAR;
		if (0 == n_unit)
			n_unit = 4;
	}

	if (0 != (n = floor(secs / SEC_PER_MONTH)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dM ", (int)n);
		secs -= n * SEC_PER_MONTH;
		if (0 == n_unit)
			n_unit = 3;
	}

	if (0 != (n = floor(secs / SEC_PER_DAY)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dd ", (int)n);
		secs -= n * SEC_PER_DAY;
		if (0 == n_unit)
			n_unit = 2;
	}

	if (4 > n_unit && 0 != (n = floor(secs / SEC_PER_HOUR)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dh ", (int)n);
		secs -= n * SEC_PER_HOUR;
		if (0 == n_unit)
			n_unit = 1;
	}

	if (3 > n_unit && 0 != (n = floor(secs / SEC_PER_MIN)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dm ", (int)n);
		secs -= n * SEC_PER_MIN;
	}

	if (2 > n_unit && 0 != (n = floor(secs)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%ds ", (int)n);
		secs -= n;
	}

	if (1 > n_unit && 0 != (n = round(secs * 1000)))
		offset += zbx_snprintf(value + offset, max_len - offset, "%dms", (int)n);

	if (0 != offset && ' ' == value[--offset])
		value[offset] = '\0';
clean:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose: add only units to the value                                       *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *             units - units (bps, b, B, etc)                                 *
 *                                                                            *
 ******************************************************************************/
static void	add_value_units_no_kmgt(char *value, size_t max_len, const char *units)
{
	const char	*minus = "";
	char		tmp[64];
	double		value_double;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 > (value_double = atof(value)))
	{
		minus = "-";
		value_double = -value_double;
	}

	if (SUCCEED != zbx_double_compare(round(value_double), value_double))
	{
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(2), value_double);
		zbx_del_zeros(tmp);
	}
	else
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(0), value_double);

	zbx_snprintf(value, max_len, "%s%s %s", minus, tmp, units);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
/******************************************************************************
 *                                                                            *
 * Purpose: add units with K,M,G,T prefix to the value                        *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *             units - units (bps, b, B, etc)                                 *
 *                                                                            *
 ******************************************************************************/
static void	add_value_units_with_kmgt(char *value, size_t max_len, const char *units)
{
	const char	*minus = "";
	char		kmgt[8];
	char		tmp[64];
	double		base;
	double		value_double;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 > (value_double = atof(value)))
	{
		minus = "-";
		value_double = -value_double;
	}

	base = (0 == strcmp(units, "B") || 0 == strcmp(units, "Bps") ? 1024 : 1000);

	if (value_double < base)
	{
		zbx_strscpy(kmgt, "");
	}
	else if (value_double < base * base)
	{
		zbx_strscpy(kmgt, "K");
		value_double /= base;
	}
	else if (value_double < base * base * base)
	{
		zbx_strscpy(kmgt, "M");
		value_double /= base * base;
	}
	else if (value_double < base * base * base * base)
	{
		zbx_strscpy(kmgt, "G");
		value_double /= base * base * base;
	}
	else
	{
		zbx_strscpy(kmgt, "T");
		value_double /= base * base * base * base;
	}

	if (SUCCEED != zbx_double_compare(round(value_double), value_double))
	{
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(2), value_double);
		zbx_del_zeros(tmp);
	}
	else
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(0), value_double);

	zbx_snprintf(value, max_len, "%s%s %s%s", minus, tmp, kmgt, units);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Purpose:  check if unit is blacklisted or not                              *
 *                                                                            *
 * Parameters: unit - unit to check                                           *
 *                                                                            *
 * Return value: SUCCEED - unit blacklisted                                   *
 *               FAIL - unit is not blacklisted                               *
 *                                                                            *
 ******************************************************************************/
static int	is_blacklisted_unit(const char *unit)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = zbx_str_in_list("%,ms,rpm,RPM", unit, ',');

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}


static void	add_value_suffix(strlen_t *val, glb_conf_item_valuemap_info_t *vm_info)
{
	struct tm	*local_time;
	time_t		time;

//	zabbix_log(LOG_LEVEL_DEBUG, "In %s() value:'%s' units:'%s' value_type:%d",
//			__func__, value, units, (int)value_type);

	switch (vm_info->value_type)
	{
		case ITEM_VALUE_TYPE_UINT64:
			if (0 == strcmp(vm_info->units, "unixtime"))
			{
				time = (time_t)atoi(val->str);
				local_time = localtime(&time);
				strftime(val->str, val->len, "%Y.%m.%d %H:%M:%S", local_time);
				break;
			}
			ZBX_FALLTHROUGH;
		case ITEM_VALUE_TYPE_FLOAT:
			if (0 == strcmp(vm_info->units, "s"))
				add_value_suffix_s(val->str, val->len);
			else if (0 == strcmp(vm_info->units, "uptime"))
				add_value_suffix_uptime(val->str, val->len);
			else if ('!' == *vm_info->units)
				add_value_units_no_kmgt(val->str, val->len, (const char *)(vm_info->units + 1));
			else if (SUCCEED == is_blacklisted_unit(vm_info->units))
				add_value_units_no_kmgt(val->str, val->len, vm_info->units);
			else if ('\0' != *vm_info->units)
				add_value_units_with_kmgt(val->str, val->len, vm_info->units);
			break;
		default:
			;
	}
	//zabbix_log(LOG_LEVEL_DEBUG, "End of %s() value:'%s'", __func__, value);
}

typedef struct {
    glb_conf_item_valuemap_info_t *vm_info;
    strlen_t *value;
} value_format_process_t;

ELEMS_CALLBACK(format_value) {
    glb_conf_valuemap_t *vmap = elem->data;

}

void glb_conf_valuemaps_format_value(strlen_t *value, glb_conf_item_valuemap_info_t *vm_info) {
    value_format_process_t fmt_data = {.vm_info = vm_info, .value = value};

	switch (vm_info->value_type)
	{
		case ITEM_VALUE_TYPE_STR:
			elems_hash_process(conf->valuemaps, vm_info->valuemapid, format_value, &fmt_data, ELEM_FLAG_DO_NOT_CREATE);
            break;
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_del_zeros(value->str);
			ZBX_FALLTHROUGH;
		case ITEM_VALUE_TYPE_UINT64:
			
			if (SUCCEED != elems_hash_process(conf->valuemaps, vm_info->valuemapid, format_value, &fmt_data, ELEM_FLAG_DO_NOT_CREATE))
				add_value_suffix(value, vm_info);
			break;
		default:
			;
	}
}