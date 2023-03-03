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

#ifndef GLB_MACRO_H
#define GLB_MACRO_H
#include "zbxcommon.h"
#include "zbxcacheconfig.h"

int glb_macro_translate_event_name(CALC_TRIGGER *trigger, char **event_name,  char *error, size_t errlen);
int glb_macro_translate_string(const char *expression, int token_type, char *result, int result_size);

int glb_macro_expand_common_unmasked(char **data, char *error, size_t errlen);
int glb_macro_expand_item_key(char **data, int key_type, char *error, size_t errlen);
int glb_macro_expand_item_key_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen);

int glb_macro_expand_common_by_hostid(char **data, u_int64_t hostid, char *error, size_t errlen);
int glb_macro_expand_common_by_hostid_unmasked(char **data, u_int64_t hostid, char *error, size_t errlen);

int glb_macro_expand_by_host(char **data, const DC_HOST *host, int field_type, char *error, size_t errlen);
int glb_macro_expand_by_host_unmasked(char **data, const DC_HOST *host, int field_type, char *error, size_t errlen);

int glb_macro_expand_by_item(char **data, const DC_ITEM *item, int type, char *error, size_t errlen);	
int glb_macro_expand_by_item_unmasked(char **data, const DC_ITEM *item, int type, char *error, size_t errlen);	

int glb_macro_expand_alert_data(DB_ALERT *db_alert, char *param);

int glb_macro_expand_trigger_ctx_expression(CALC_TRIGGER *trigger, char **data, int token_type, char *error, size_t errlen);

#endif