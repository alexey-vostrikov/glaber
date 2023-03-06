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

/* set of of functions to fetch and return object's data*/

#include "zbxcommon.h"
#include "zbxcacheconfig.h"
#include "glb_macro_defs.h"
#include "zbxexpr.h"

static macroses_t host_macros  =  
	{"{HOST.", 6, { {"{HOST.DNS}",	10,	MACRO_HOST_DNS,  MACRO_OBJ_INTERFACE,	1 },
					{"{HOST.CONN}",	11, MACRO_HOST_CONN, MACRO_OBJ_INTERFACE,	1 },
					{"{HOST.IP}",   9,	MACRO_HOST_IP,	MACRO_OBJ_INTERFACE, 1 },
					{"{HOST.PORT}", 11,	MACRO_HOST_PORT,	MACRO_OBJ_INTERFACE, 1 },
					{"{HOST.HOST}",	11, MACRO_HOST_HOST,	MACRO_OBJ_HOST, 1 },
					{"{HOST.NAME}",	11, MACRO_HOST_NAME,	MACRO_OBJ_HOST, 1 },
					{"{HOST.ID}",	9,	MACRO_HOST_ID,	MACRO_OBJ_HOST, 1 },
					{"{HOST.METADATA}",	14, MACRO_HOST_METADATA,	MACRO_OBJ_HOST, 1 },
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
   {"{TRIGGER.", 9,{ {"{TRIGGER.DESCRIPTION}", 21, MACRO_TRIGGER_DESCRIPTION, MACRO_OBJ_TRIGGER, 0},
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
// #define MVAR_TRIGGER_COMMENT			"{TRIGGER.COMMENT}"		/* deprecated */
// #define MVAR_STATUS				"{STATUS}"			/* deprecated */
// #define MVAR_TRIGGER_NAME_ORIG			"{TRIGGER.NAME.ORIG}"

};  
static macroses_t function_macros =
    { "{FUNCTION.", 10 , {
                {"{FUNCTION.VALUE}", 16 , MACRO_FUNCTION_VALUE, MACRO_OBJ_TRIGGER, 1},
                {"{FUNCTION.RECOVERY.VALUE}", 24 , MACRO_FUNCTION_RECOVERY_VALUE, MACRO_OBJ_TRIGGER, 1},
                {NULL, 0,	0, 0}
            }
    };

static macroses_t time_macros =
    { "{TIME}", 6 , {
                {"{TIME}", 6 , MACRO_TIME_VALUE, MACRO_OBJ_SYSTEM, 0},
                {NULL, 0,	0, 0}
            }
    };

static macroses_t *macro_defs[] = {&host_macros, &item_macros, &trigger_macros, &function_macros, &time_macros, NULL};

static int set_data_by_suffix(macro_proc_data_t *macro_proc, macroses_t *obj_macro) {
    int idx = 0;
    macro_info_t* macro_info = &obj_macro->macroses[idx++];
    while ( NULL != macro_info->name) {
//        LOG_INF("Checking if macro is %s",  macro_info->name);

        if (0 == strncmp(macro_proc->macro, macro_info->name, macro_info->name_len)) {
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
//    LOG_INF("Checking if macro %s is built-in", macro_proc->macro);

    if (NULL == macro_proc->macro || ZBX_TOKEN_MACRO != macro_proc->token.type)
        return FAIL;
    
    int idx = 0;
    macroses_t *obj_macro = macro_defs[0];
  //  LOG_INF("Iterate start %p", obj_macro);

    while (NULL != (obj_macro) ) {
//        LOG_INF("Checking if macro");
//        LOG_INF("Checking group of macro, prefix is %s", obj_macro->prefix );
        if (0 == strncmp(macro_proc->macro, obj_macro->prefix, obj_macro->prefix_len)) {
  //          LOG_INF("Found group of macro, prefix is %s", obj_macro->prefix );

            return set_data_by_suffix(macro_proc, obj_macro);
        }
        
//        LOG_INF("Not matched, idx is %d, obj_macro is %p", idx, obj_macro);
        idx++;
        obj_macro = macro_defs[idx];
//        LOG_INF("Not matched, idx is %d, obj_macro is %p", idx, obj_macro);
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
            return SUCCEED;
        //TODO: fix if possible to avoid using DB
        case MACRO_HOST_METADATA:
        case MACRO_HOST_DESCRIPTION:
            HALT_HERE("Not implemented");
            return FAIL;
    }
    return FAIL;
}

/*item-related data*/
int builtin_expand_by_item(macro_proc_data_t *macro_proc, DC_ITEM *item) {
    switch (macro_proc->macro_type) {
        case MACRO_ITEM_ID:
            macro_proc->replace_to = zbx_dsprintf(macro_proc->replace_to, ZBX_FS_UI64, item->itemid);
            return SUCCEED;
        case MACRO_ITEM_LASTVALUE:
            macro_proc->replace_to = NULL;
            HALT_HERE("Implement fetch from the Value Cache");
            return SUCCEED;
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
//  in order to fetch descriptions, need to operate from DB, 
// or need to sync the host, implement after items normalization, for now - will not support
//   {"{ITEM.DESCRIPTION",   MACRO_ITEM_DESCRIPTION, MACRO_OBJ_ITEM, 1},
//   {"{ITEM.DESCRIPTION.ORIG",  MACRO_ITEM_DESCRIPTION_ORIG, MACRO_OBJ_ITEM, 1},
    }
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
    return FAIL;
}

int builtin_expand_by_calc_trigger(macro_proc_data_t *macro_proc, CALC_TRIGGER *tr) {
    switch (macro_proc->macro_type) {
    }
}

int builtin_expand_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid) {
    switch (macro_proc->macro_type) {
    }
    HALT_HERE("Not implemented");
}
    
int builtin_expand_by_itemid(macro_proc_data_t *macro_proc, u_int64_t itemid) {
    switch (macro_proc->macro_type) {
    }
    HALT_HERE("Not implemented");
}


int glb_macro_builtin_expand_by_host(macro_proc_data_t *macro_proc, DC_HOST *host) {
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
    switch (macro_proc->object_type) {
        case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
            return;
        case  MACRO_OBJ_HOST:
        case  MACRO_OBJ_INTERFACE:
            return glb_macro_builtin_expand_by_host(macro_proc, &item->host);
        case  MACRO_OBJ_ITEM:
            return builtin_expand_by_item(macro_proc, item);
    }
    return FAIL;
}


int glb_macro_builtin_expand_by_itemid(macro_proc_data_t* macro_proc, u_int64_t itemid) {
    //note: for now, implementation easiness a simple fetch by DCget_item 
    //might be used, however, it feasible to switch to fine-grained data
    //fetch methods
    HALT_HERE("Not implemented, item %lld", itemid);
}

int glb_macro_builtin_expand_by_hostid(macro_proc_data_t *macro_proc, u_int64_t hostid) {
    //note: for now, implementation easiness a simple fetch by DCget_host 
    //might be used, however, it feasible to switch to fine-grained data
    //fetch methods
    HALT_HERE("Not implemented, host %lld", hostid);
}

/*trigger's macros might contain items and hosts referneces*/
int glb_macro_builtin_expand_by_calc_trigger(macro_proc_data_t* macro_proc, CALC_TRIGGER *tr) {

	switch (macro_proc->macro_type) { 
		case MACRO_OBJ_SYSTEM:
            builtin_expand_system(macro_proc);
			return;
		case MACRO_OBJ_HOST:
            builtin_expand_by_hostid(macro_proc, tr->hostids.values[macro_proc->Nfuncid-1]);
			return;
		case MACRO_OBJ_ITEM:
			builtin_expand_by_itemid(macro_proc, tr->itemids.values[macro_proc->Nfuncid-1]);
			return;
		case MACRO_OBJ_TRIGGER:
			builtin_expand_by_calc_trigger(macro_proc, tr);
			return;
	}

    return FAIL;
}