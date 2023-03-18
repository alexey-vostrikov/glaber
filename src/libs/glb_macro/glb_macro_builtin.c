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

/* set of of functions to fetch and return object's data*/

#include "zbxcommon.h"
#include "zbxcacheconfig.h"
#include "glb_macro_defs.h"
#include "zbxexpr.h"
#include "zbx_trigger_constants.h"
#include "glbstr.h"
#include "glb_macro.h"

#include "../glb_state/glb_state_items.h"
#include "../glb_state/glb_state_trigger.h"
#include "../glb_state/glb_state_problems.h"
#include "../glb_state/glb_state_triggers.h"
#include "../glb_conf/conf.h"
#include "../glb_conf/triggers/conf_calc_trigger.h"
#include "../glb_conf/valuemaps/conf_valuemaps.h"

static macroses_t host_macros  =  
	{"{HOST.", 6, { {"{HOST.DNS}",	10,	MACRO_HOST_DNS,  MACRO_OBJ_INTERFACE,	1 },
					{"{HOST.CONN}",	11, MACRO_HOST_CONN, MACRO_OBJ_INTERFACE,	1 },
					{"{HOST.IP}",   9,	MACRO_HOST_IP,	MACRO_OBJ_INTERFACE, 1 },
					{"{HOST.PORT}", 11,	MACRO_HOST_PORT,	MACRO_OBJ_INTERFACE, 1 },
					{"{HOST.HOST}",	11, MACRO_HOST_HOST,	MACRO_OBJ_HOST, 1 },
					{"{HOST.NAME}",	11, MACRO_HOST_NAME,	MACRO_OBJ_HOST, 1 },
					{"{HOST.ID}",	9,	MACRO_HOST_ID,	MACRO_OBJ_HOST, 1 },
					{"{HOST.METADATA}",	14, MACRO_HOST_METADATA, MACRO_OBJ_AUTOREGISTER, 1 },
					{"{HOST.DESCRIPTION}",	17, MACRO_HOST_DESCRIPTION,	MACRO_OBJ_HOST, 1 },
					{NULL, 0, 0, 0}
				} 
	};

static macroses_t item_macros = 
    {"{ITEM.", 6, { {"{ITEM.LASTVALUE}",16,  MACRO_ITEM_LASTVALUE, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.VALUE}",    12,  MACRO_ITEM_LASTVALUE, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.VALUETYPE}",16,  MACRO_ITEM_VALUETYPE, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.ID}",       9,   MACRO_ITEM_ID, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.NAME}",     11,  MACRO_ITEM_NAME, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.KEY}",      10,  MACRO_ITEM_KEY, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.KEY.ORIG}", 15,  MACRO_ITEM_KEY_ORIG, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.STATE}",    12,  MACRO_ITEM_STATE, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.DESCRIPTION}", 18,  MACRO_ITEM_DESCRIPTION, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.DESCRIPTION.ORIG}", 23,  MACRO_ITEM_DESCRIPTION_ORIG, MACRO_OBJ_ITEM, 1},
                    {"{ITEM.STATE.ERROR}", 18, MACRO_ITEM_STATE_ERROR, MACRO_OBJ_ITEM, 1},
                    {NULL, 0, 0, 0}
                }
    };

static macroses_t trigger_macros = 
   {"{TRIGGER.", 9,{ {"{TRIGGER.COMMENT}", 17, MACRO_TRIGGER_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.DESCRIPTION}", 21, MACRO_TRIGGER_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.ID}", 12, MACRO_TRIGGER_ID, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.NAME}", 14, MACRO_TRIGGER_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.NAME_ORIG}", 19, MACRO_TRIGGER_NAME_ORIG, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EXPRESSION}", 20, MACRO_TRIGGER_EXPRESSION, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EXPRESSION_RECOVERY}", 28, MACRO_TRIGGER_EXPRESSION_RECOVERY, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.SEVERITY}", 18,     MACRO_TRIGGER_SEVERITY, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.NSEVERITY}", 19,    MACRO_TRIGGER_NSEVERITY, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.STATUS}", 16,   MACRO_TRIGGER_VALUE, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.VALUE}", 15,    MACRO_TRIGGER_VALUE, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.STATE}", 15,        MACRO_TRIGGER_ADMIN_STATE, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.ADMIN.STATE}", 21,  MACRO_TRIGGER_ADMIN_STATE, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.TEMPLATE.NAME}", 23,    MACRO_TRIGGER_TEMPLATE_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.HOSTGROUP.NAME}", 24,   MACRO_TRIGGER_HOSTGROUP_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EXPRESSION.EXPLAIN}", 28,   MACRO_TRIGGER_EXPRESSION_EXPLAIN, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EXPRESSION.RECOVERY.EXPLAIN}", 37,   MACRO_TRIGGER_EXPRESSION_RECOVERY_EXPLAIN, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.URL}", 13,   MACRO_TRIGGER_URL, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.URL.NAME}", 18,   MACRO_TRIGGER_URL_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EVENTS.ACK}", 20,   MACRO_TRIGGER_PROBLEMS_ACK, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.PROBLEMS.ACK}", 22,   MACRO_TRIGGER_PROBLEMS_ACK, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.EVENTS.UNACK}", 22,   MACRO_TRIGGER_PROBLEMS_UNACK, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.PROBLEMS.UNACK}", 24,   MACRO_TRIGGER_PROBLEMS_UNACK, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.STATE.ERROR}", 21,   MACRO_TRIGGER_ERROR, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.ERROR}", 15,   MACRO_TRIGGER_ERROR, MACRO_OBJ_TRIGGER, 0},
                     {NULL, 0,	0, 0}
                }
// #define MVAR_TRIGGER_NAME_ORIG			"{TRIGGER.NAME.ORIG}"

};  
static macroses_t function_macros =
    { "{FUNCTION.", 10 , {
                {"{FUNCTION.VALUE}", 16 , MACRO_FUNCTION_VALUE, MACRO_OBJ_TRIGGER, 1},
                {"{FUNCTION.RECOVERY.VALUE}", 24 , MACRO_FUNCTION_RECOVERY_VALUE, MACRO_OBJ_TRIGGER, 1},
                {NULL, 0,	0, 0}
            }
    };

static macroses_t assorted_macros =
    { "{", 1 , {
                {"{HOSTNAME}", 10 , MACRO_HOST_NAME, MACRO_OBJ_HOST, 0},
                {"{TIME}", 6 , MACRO_TIME_VALUE, MACRO_OBJ_SYSTEM, 0},
                {"{STATUS}", 6 , MACRO_TRIGGER_ADMIN_STATE, MACRO_OBJ_TRIGGER, 0},
                {NULL, 0,	0, 0}
            }
    };

static macroses_t *macro_defs[] = {&host_macros, &item_macros, &trigger_macros, &function_macros, &assorted_macros,  NULL};

static void warn_macro_unknown(macro_proc_data_t *macro_proc) {
    LOG_INF("Warn: in %s: unknown builtin macro %s (obj_type %d, macro_type %d) in host context expand", 
            __func__, macro_proc->macro, macro_proc->object_type, macro_proc->macro_type);
}

static void set_result_value(macro_proc_data_t *macro_proc, char *result) {
    if (NULL == result) {
        macro_proc->replace_to = zbx_strdup(NULL, "");
        return;
    }
}

static int macro_compare(const char* in_macro, const char* cfg_macro, size_t cfg_macro_len, int is_indexed) {
    size_t in_macro_len = strlen(in_macro);
    
    if (in_macro_len == cfg_macro_len && 0 == strncmp(in_macro, cfg_macro, cfg_macro_len) )
        return 0;

    if (is_indexed && ( in_macro_len == 1 + cfg_macro_len)) {//in_macro has one more symbol
        //LOG_INF("Found indexed macro in %s, cfg %s", in_macro, cfg_macro);
        if (0 == strncmp(in_macro, cfg_macro, cfg_macro_len -1 ) && //checking if first part matches
            (in_macro[in_macro_len-2] >='1' && in_macro[in_macro_len-2] <='9' 
            && in_macro[in_macro_len-1] == '}')) //following 1-9 digit and '}'
                        
            return 0;
    }
    return 1;
}

static int set_data_by_suffix(macro_proc_data_t *macro_proc, macroses_t *obj_macro) {
    int idx = 0;
    macro_info_t* macro_info = &obj_macro->macroses[idx++];
    while ( NULL != macro_info->name) {
        if (0 == macro_compare(macro_proc->macro, macro_info->name, macro_info->name_len, macro_info->has_idx)) {
            macro_proc->macro_type = macro_info->macro_id;
            macro_proc->object_type = macro_info->obj_type_id;
            return SUCCEED;
        }
        macro_info = &obj_macro->macroses[idx++];
    }
    return FAIL;
}

int recognize_macro_type_and_object_type(macro_proc_data_t *macro_proc) {
    macro_proc->macro_type = MACRO_NONE;
    macro_proc->object_type = MACRO_OBJ_NONE;

    if (NULL == macro_proc->macro || ZBX_TOKEN_MACRO != macro_proc->token.type)
        return FAIL;
    
    int idx = 0;
    macroses_t *obj_macro = macro_defs[0];

    while (NULL != (obj_macro) ) {
        if (0 == strncmp(macro_proc->macro, obj_macro->prefix, obj_macro->prefix_len)) 
            return set_data_by_suffix(macro_proc, obj_macro);
        
        idx++;
        obj_macro = macro_defs[idx];
    }

    return FAIL;
}

/*system, object-independent macroses*/
int builtin_expand_system(macro_proc_data_t *macro_proc) {
    switch (macro_proc->macro_type) {
        case MACRO_TIME_VALUE:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, zbx_time2str(time(NULL), NULL));
            return SUCCEED;
    }

    warn_macro_unknown(macro_proc);
    return FAIL;
}

/*host-related data*/
int builtin_expand_by_host(macro_proc_data_t *macro_proc, DC_HOST *host) {
    switch (macro_proc->macro_type) {
        case MACRO_HOST_HOST:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, host->name);
            return SUCCEED;
        case MACRO_HOST_NAME:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, host->host);
            LOG_INF("Replaced macro with %s", macro_proc->replace_to);
            return SUCCEED;
        case MACRO_HOST_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, host->hostid);
            return SUCCEED;
        //TODO: fix if possible to avoid using DB
        case MACRO_HOST_DESCRIPTION:
            HALT_HERE("Not implemented");
            return FAIL;
    }
    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int macro_fetch_item_lastvalue(u_int64_t itemid, strlen_t *val) {
    glb_conf_item_valuemap_info_t vm_info;

    if (SUCCEED == glb_state_items_get_lastvalue_str(itemid, val) && 
        SUCCEED == glb_conf_items_get_valuemap_info(itemid, &vm_info)) 
    {
        glb_conf_valuemaps_format_value(val, &vm_info);
        return SUCCEED;
    } 

    return FAIL;
}

/*item-related data*/
int builtin_expand_by_item(macro_proc_data_t *macro_proc, DC_ITEM *item) {
    char tmp[MAX_STRING_LEN];
    strlen_t val = {.str = tmp, .len = MAX_STRING_LEN};
    
    switch (macro_proc->macro_type) {
    
        case MACRO_ITEM_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, item->itemid);
            return SUCCEED;
    
        case MACRO_ITEM_LASTVALUE: {
            if (SUCCEED != macro_fetch_item_lastvalue(item->itemid, &val))
                return FAIL;
           
            macro_proc->replace_to = zbx_strdup(NULL, val.str);
            return SUCCEED;
    	}
        
        case MACRO_ITEM_VALUETYPE: 
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, item->value_type);
            return SUCCEED;
    
        case MACRO_ITEM_NAME:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, item->name);
            return SUCCEED;
    
        case MACRO_ITEM_KEY:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, item->key);
            return SUCCEED;
    
        case MACRO_ITEM_KEY_ORIG:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, item->key_orig);
            return SUCCEED;
    
        case MACRO_ITEM_STATE: {
            int state;
            
            if (FAIL == (state = glb_state_item_get_state(item->itemid)))
                return FAIL;
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, state);
            
            return SUCCEED;
            }
    
        case MACRO_ITEM_STATE_ERROR: {
            char error[MAX_STRING_LEN];

            if (FAIL == glb_state_item_get_error(item->itemid, error, sizeof(error)))
                return FAIL;
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, error);
            
            return SUCCEED;
            }
            
        case MACRO_ITEM_DESCRIPTION:
            //todo: add macro translating
            HALT_HERE("Not implemented");
            macro_proc->replace_to = zbx_strdup(NULL, item->description);
            return SUCCEED;

        case MACRO_ITEM_DESCRIPTION_ORIG:
            macro_proc->replace_to = zbx_strdup(NULL, item->description);
            return SUCCEED;
    }
    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int builtin_expand_by_interface(macro_proc_data_t *macro_proc, DC_INTERFACE *iface) {
    switch (macro_proc->macro_type) {
        case MACRO_HOST_IP:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, iface->ip_orig);
            return SUCCEED;
        case MACRO_HOST_DNS:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, iface->dns_orig);
            return SUCCEED;
        case MACRO_HOST_CONN:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, iface->addr);
            return SUCCEED;
    }   
    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int builtin_expand_by_calc_trigger(macro_proc_data_t *macro_proc, calc_trigger_t *tr) {
    switch (macro_proc->macro_type) {
        case MACRO_TRIGGER_NAME:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, tr->description);
// 					substitute_simple_macros_impl(NULL, c_event, NULL, NULL, NULL, NULL, NULL, NULL,
// 							NULL, NULL, NULL, tz, NULL, &replace_to,
// 							MACRO_TYPE_TRIGGER_COMMENTS, error, maxerrlen);
            return SUCCEED;
        case MACRO_TRIGGER_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, tr->triggerid);
            return SUCCEED;
        case MACRO_TRIGGER_NAME_ORIG:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, tr->description);
        case MACRO_TRIGGER_EXPRESSION:
            conf_calc_trigger_get_expression(tr, &macro_proc->replace_to);
            return SUCCEED;
        case MACRO_TRIGGER_EXPRESSION_RECOVERY:
            conf_calc_trigger_get_recovery_expression(tr, &macro_proc->replace_to);
            // if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
			// 	glb_expression_get_by_trigger(tr->eval_ctx_r, &macro_proc->replace_to);
			// else
			// 	macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, "");
            return SUCCEED;
        case MACRO_TRIGGER_SEVERITY:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, 
                                conf_calc_trigger_get_severity_name(tr->priority));
            return SUCCEED;
        case MACRO_TRIGGER_NSEVERITY:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, "%d", (int)tr->priority);
            return SUCCEED;
        case MACRO_TRIGGER_VALUE:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, "%d", (int)tr->value);
            return SUCCEED;
        case MACRO_TRIGGER_ADMIN_STATE:
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, conf_calc_trigger_get_admin_state_name(tr->value));
            return SUCCEED;
        case MACRO_TRIGGER_EXPRESSION_EXPLAIN:
            conf_calc_trigger_explain_expression(tr, &macro_proc->replace_to);
            //glb_expression_explain_by_trigger(tr->eval_ctx, &macro_proc->replace_to);
            return SUCCEED;
        case MACRO_TRIGGER_EXPRESSION_RECOVERY_EXPLAIN:
            conf_calc_trigger_explain_recovery_expression(tr, &macro_proc->replace_to);
            // if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == tr->recovery_mode)
			// 	glb_expression_explain_by_trigger(tr->eval_ctx_r, &macro_proc->replace_to);
			// else
			// 	macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, "");
            return SUCCEED;
        case MACRO_TRIGGER_URL:
			//the following two needs either db support or config cache methods
            //
            //macro_proc->replace_to = zbx_strdup(replace_to, tr-> ->trigger.url);
			//		substitute_simple_macros_impl(NULL, trigger, NULL, NULL, NULL, NULL, NULL,
			//				NULL, NULL, NULL, tz, NULL, &replace_to, MACRO_TYPE_TRIGGER_URL,
			//				error, maxerrlen);
            HALT_HERE("Not implemented");
            return FAIL;
        case MACRO_TRIGGER_URL_NAME:
            //
            HALT_HERE("Not implemented");
            return FAIL;
        case MACRO_TRIGGER_PROBLEMS_ACK:
           // macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, 
             //           "%d", glb_state_problems_get_count_by_trigger_acknowledged(tr->triggerid));
             HALT_HERE("Not implemented");
            return SUCCEED;
        case MACRO_TRIGGER_PROBLEMS_UNACK:
            //macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, 
            //          "%d", glb_state_problems_get_count_by_trigger_unacknowledged(tr->triggerid));
            HALT_HERE("Not implemented");
            return SUCCEED;
        case MACRO_TRIGGER_ERROR:
            if (SUCCEED == glb_state_trigger_get_error(tr->triggerid, &macro_proc->replace_to ))
                return SUCCEED;
            
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, "");
            return SUCCEED;

        /*
        case 
            Thees needs db access, not doing yet         
                     {"{TRIGGER.TEMPLATE.NAME}", 23,    MACRO_TRIGGER_TEMPLATE_NAME, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.HOSTGROUP.NAME}", 24,   MACRO_TRIGGER_HOSTGROUP_NAME, MACRO_OBJ_TRIGGER, 0},
             
                     {"{TRIGGER.URL}", 13,   MACRO_TRIGGER_URL, MACRO_OBJ_TRIGGER, 0},
                     {"{TRIGGER.URL.NAME}", 18,   MACRO_TRIGGER_URL_NAME, MACRO_OBJ_TRIGGER, 0},
    */
    }
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int builtin_expand_host_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid) {
    LOG_INF("In %s",__func__);
    switch (macro_proc->macro_type) {

        case MACRO_HOST_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, hostid);
            return SUCCEED;
 
        case MACRO_HOST_HOST:
            return glb_conf_host_get_host(hostid, &macro_proc->replace_to);

        case MACRO_HOST_NAME:
            return glb_conf_host_get_name(hostid, &macro_proc->replace_to);

        case MACRO_HOST_DESCRIPTION:
            HALT_HERE("Not implemented");

    }
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int builtin_expand_iface_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid) {
    switch (macro_proc->macro_type) {
        case MACRO_HOST_DNS:
        case MACRO_HOST_CONN:
        case MACRO_HOST_IP:
        case MACRO_HOST_HOST:
            HALT_HERE("Not implemened");
    }
    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int builtin_expand_by_itemid(macro_proc_data_t *macro_proc, u_int64_t itemid) {
    char tmp[MAX_STRING_LEN];
    strlen_t val = {.str = tmp, .len = MAX_STRING_LEN};

    LOG_INF("Expanding by id item %ld", itemid);
    switch (macro_proc->macro_type) {
        case MACRO_ITEM_LASTVALUE: {
            if (SUCCEED != macro_fetch_item_lastvalue(itemid, &val))
                return FAIL;
           
            macro_proc->replace_to = zbx_strdup(NULL, val.str);
            return SUCCEED;
            
		}
        case MACRO_ITEM_VALUETYPE: {
            int valuetype = glb_conf_items_get_valuetype(itemid);
            if (FAIL == valuetype)
                return FAIL;
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, valuetype);
            return SUCCEED;
        }
        case MACRO_ITEM_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, itemid);
            return SUCCEED;
        
        case MACRO_ITEM_NAME: 
            macro_proc->replace_to = glb_conf_item_get_name(itemid);
            return SUCCEED;
        
        case MACRO_ITEM_KEY_ORIG:
            macro_proc->replace_to = glb_conf_item_get_key(itemid);
            return SUCCEED;
        
        case MACRO_ITEM_KEY: {
            char *tmp = glb_conf_item_get_key(itemid);
            u_int64_t hostid = glb_conf_item_get_hostid(itemid);
            glb_macro_expand_by_hostid(&tmp, hostid, NULL, 0);        
            return SUCCEED;
        }
        case MACRO_ITEM_STATE: {
            int state;
            
            if (FAIL == (state = glb_state_item_get_state(itemid)))
                return FAIL;
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, state);
            
            return SUCCEED;
            }
        case MACRO_ITEM_STATE_ERROR: {
            char error[MAX_STRING_LEN];

            if (FAIL == glb_state_item_get_error(itemid, error, sizeof(error)))
                return FAIL;
            macro_proc->replace_to = zbx_strdup(macro_proc->replace_to, error);
            
            return SUCCEED;
            }
        case MACRO_ITEM_DESCRIPTION_ORIG:
        case MACRO_ITEM_DESCRIPTION:
            HALT_HERE("Not implemented");
            return SUCCEED;
    }    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int glb_macro_builtin_expand_by_host(macro_proc_data_t *macro_proc, DC_HOST *host) {
    LOG_INF("In %s, funcid is %d", __func__, macro_proc->Nfuncid);
    switch (macro_proc->object_type) {
        
        case  MACRO_OBJ_HOST:
            return builtin_expand_by_host(macro_proc, host);

        case  MACRO_OBJ_INTERFACE: {
            DC_INTERFACE iface;
            DCconfig_get_interface(&iface, host->hostid, 0);
            return builtin_expand_by_interface(macro_proc, &iface);
        }
    }
    return FAIL;
}

int glb_macro_builtin_expand_by_item(macro_proc_data_t* macro_proc, DC_ITEM *item) {
    LOG_INF("In %s, funcid is %d", __func__, macro_proc->Nfuncid);
    switch (macro_proc->object_type) {
        case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
            return SUCCEED;
        
        case  MACRO_OBJ_HOST:
        case  MACRO_OBJ_INTERFACE:
            return glb_macro_builtin_expand_by_host(macro_proc, &item->host);
        
        case  MACRO_OBJ_ITEM:
            return builtin_expand_by_item(macro_proc, item);
    }
    
    warn_macro_unknown(macro_proc);
    return FAIL;
}

int glb_macro_builtin_expand_by_itemid(macro_proc_data_t* macro_proc, u_int64_t itemid) {
    switch (macro_proc->object_type) {
        case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
            return SUCCEED;
        
        case  MACRO_OBJ_HOST:
        case  MACRO_OBJ_INTERFACE:{
            u_int64_t hostid = glb_conf_item_get_hostid(itemid);
            return glb_macro_builtin_expand_by_hostid(macro_proc, hostid);
        }
        case  MACRO_OBJ_ITEM:
            return builtin_expand_by_itemid(macro_proc, itemid);
    }
    warn_macro_unknown(macro_proc);
    return FAIL;
}


int glb_macro_builtin_expand_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid) {
    switch (macro_proc->object_type) { 
		case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
			return SUCCEED;
        
        case MACRO_OBJ_INTERFACE:
		case MACRO_OBJ_HOST:
            builtin_expand_host_by_hostid(macro_proc, hostid);
            return SUCCEED;
    }
  
    warn_macro_unknown(macro_proc);
    return FAIL;
}


int glb_macro_builtin_expand_by_trigger(macro_proc_data_t* macro_proc, calc_trigger_t *tr) {
    LOG_INF("In %s, funcid is %d, macro obj is %d, macro_type id %d", __func__, 
                    macro_proc->Nfuncid, macro_proc->object_type, macro_proc->macro_type);

	switch (macro_proc->object_type) { 
        case MACRO_OBJ_TRIGGER:
			builtin_expand_by_calc_trigger(macro_proc, tr);
			return SUCCEED;

        case MACRO_OBJ_ITEM: {
            u_int64_t itemid;
            if (SUCCEED == conf_calc_trigger_get_N_itemid(tr, macro_proc->Nfuncid, &itemid))
                return glb_macro_builtin_expand_by_itemid(macro_proc, itemid);
            return FAIL;
        }

        case MACRO_OBJ_INTERFACE:
   		case MACRO_OBJ_HOST: {
            u_int64_t hostid;
            if (SUCCEED == conf_calc_trigger_get_N_hostid(tr, macro_proc->Nfuncid, &hostid))
                return glb_macro_builtin_expand_by_hostid(macro_proc, hostid);
            return FAIL;
        }
    	
        case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
			return SUCCEED;
	}

    warn_macro_unknown(macro_proc);
    return FAIL;
}