
#include "zbxcommon.h"
#include "zbxalgo.h"
#include "log.h"
#include "glb_trigger.h"

static mem_funcs_t config_memf, state_memf;

static elems_hash_t *t_state;
//static obj_index_t *deps;

typedef struct {
    glb_trigger_meta_t meta;
} trigger_state_t;

//triggers live in two memory segments:
//cache - their state is maintained there
//config - their configuration and dependancy is kept there



int glb_trigger_init(mem_funcs_t *state_mem_funcs) {

    config_memf = *config_mem_funcs;
    state_memf = *state_mem_funcs;

   	if (NULL == (deps = (obj_index_t *)(*config_memf.malloc_func)(NULL, sizeof(obj_index_t))) ||
	    NULL == (t_state = (elems_hash_t *)(*state_memf.malloc_func)(NULL, sizeof(elems_hash_t))) ||
        //NULL == (t_conf = (elems_hash_t *)(*config_memf.malloc_func)(NULL, sizeof(elems_hash_t))) )
        
        return FAIL;

    if (FAIL == obj_index_init(deps, &config_memf) ) return FAIL;
}

/* note: this isn't good in terms of internal inegrity, but provides max comatibility by now */
//obj_index_t *glb_trigger_get_idx() {
//    return deps;
//}


/* checks trigger if any of it's dependend triggers has FAIL state
  if so, FAILS, or returns SUCCEED */
static int check_if_actionable( u_int64_t triggerid, int level ) {

    int ret = SUCCEED;
    glb_trigger_meta_t meta;
    zbx_vector_uint64_t deps_list;

    DEBUG_TRIGGER(triggerid, "Checking if actionable (doesn't have dependand failed triggers)");
    level--;

    if ( 0 == level )  /*not checking further, max level has been reached */
        return SUCCEED;

    //checking the trigger
    glb_trigger_get_meta(triggerid, meta);
			
	if (TRIGGER_STATUS_ENABLED == meta.status &&
		TRIGGER_FUNCTIONAL_TRUE == meta.functional &&
		TRIGGER_VALUE_TRUE == meta.value) {
			DEBUG_TRIGGER(triggerid, "Trigger is in TRUE(ACTIVE) status, check FAILED ");		
			return FAIL;
	}
    
    zbx_vector_uint64_create(&deps_list);
	zbx_vector_uint64_clear(&deps_list);
    obj_index_get_refs_from(&deps, triggerid, &deps_list);
    
    for (int i = 0; i < deps_list.values_num; i++) {
        if (FAIL == (ret = check_if_actionable(deps_list.values[i], level)))
          break;
    }
    
    zbx_vector_uint64_destroy(&deps_list);	
    return ret;
}

int glb_triggers_check_if_actionable(u_int64_t triggerid) {
    return check_if_actionable(triggerid, GLB_TRIGGER_MAX_DEP_LEVELS);
}

void free_meta(glb_trigger_meta_t *meta) {
    zbx_free(meta->error);
}

static int get_meta_cb(elems_hash_elem_t *elem, void *data) {
    trigger_state_t *state = elem->data;
    glb_trigger_meta_t *meta = data;
    
    memcpy(meta, &state->meta, sizeof(glb_trigger_meta_t) );
    
    if (NULL != state->meta.error )
        meta->error = zbx_strdup(NULL, state->meta.error);

}


static int get_functional_cb(elems_hash_elem_t *elem, void *data) {
    trigger_state_t *state = elem->data;
    return state->meta.functional;
}

int glb_trigger_get_functional(u_int64_t triggerid, unsigned char *functional) {
    if (FAIL == (*functional = elems_hash_process(&t_state, triggerid, get_functional_cb, NULL, ELEM_FLAG_DO_NOT_CREATE))) 
        return FAIL;

    return SUCCEED;
};







int glb_triggers_get_master_triggers(u_int64_t triggerid, zbx_vector_uint64_t *master_ids) {
    
}

static void set_functional_cb(elems_hash_elem_t *elem, void *data) {
    trigger_state_t *state = elem->data;
     state->meta.functional = (unsigned char)(*data);
}

void glb_triggers_reset_functional() {
    unsigned char state = TRIGGER_FUNCTIONAL_TRUE;
    elems_hash_iterate(&t_state,set_functional_cb, &state);
}


void glb_trigger_set_states( zbx_vector_ptr_t *trigger_diff ) {


}


void glb_triggers_get_deps_list(const zbx_vector_uint64_t *triggerids, zbx_vector_ptr_t *deps) {
    int i;
    zbx_vector_uint64_t 		dep_list;
    zbx_vector_uint64_create(&dep_list);
    zbx_trigger_dep_t		*dep;

    for (i = 0; i < triggerids->values_num; i++) {



    }
}


//glb_relations_t interface to manage relations

void	zbx_dc_get_trigger_dependencies(const zbx_vector_uint64_t *triggerids, zbx_vector_ptr_t *deps)
{
	int				i, ret;
	const ZBX_DC_TRIGGER_DEPLIST	*trigdep;
	zbx_vector_uint64_t		masterids;
	zbx_trigger_dep_t		*dep;
	zbx_vector_uint64_t 		dep_list;

	zbx_vector_uint64_create(&masterids);
	zbx_vector_uint64_reserve(&masterids, 64);

	zbx_vector_uint64_create(&dep_list);

	RDLOCK_CACHE;

	for (i = 0; i < triggerids->values_num; i++)
	{
		zbx_vector_uint64_clear(&dep_list);

		obj_index_get_refs_from(&glb_config->trigger_deps, triggerids->values[i], &dep_list);
		
		if ( 0 == dep_list.values_num)  //no deps
			continue;

		if (FAIL == (ret = DCconfig_check_trigger_dependencies_rec(triggerids->values[i], &dep_list, 0, triggerids, &masterids)) ||
				0 != masterids.values_num)
		{
			dep = (zbx_trigger_dep_t *)zbx_malloc(NULL, sizeof(zbx_trigger_dep_t));
			dep->triggerid = triggerids->values[i];
			zbx_vector_uint64_create(&dep->masterids);

			if (SUCCEED == ret)
			{
				DEBUG_TRIGGER(dep->triggerid, "Trigger dependancy is unresolved");
				dep->status = ZBX_TRIGGER_DEPENDENCY_UNRESOLVED;
				zbx_vector_uint64_append_array(&dep->masterids, masterids.values, masterids.values_num);
			}
			else {
				DEBUG_TRIGGER(dep->triggerid, "Trigger dependancy failed");
				dep->status = ZBX_TRIGGER_DEPENDENCY_FAIL;
			
			}

			zbx_vector_ptr_append(deps, dep);
		} else {
			DEBUG_TRIGGER(dep->triggerid, "Trigger dependancy isn't failed");
		}

		zbx_vector_uint64_clear(&masterids);
	}

	UNLOCK_CACHE;

	zbx_vector_uint64_destroy(&masterids);
}



/*
static int	DCconfig_check_trigger_dependencies_rec(u_int64_t triggerid, zbx_vector_uint64_t *dep_list, int level,
		const zbx_vector_uint64_t *triggerids, zbx_vector_uint64_t *master_triggerids)
{
	int  i, ret, t_status, t_functional, t_value;
	zbx_vector_uint64_t next_dep_list;
	u_int64_t next_triggerid;
	
	


	if (ZBX_TRIGGER_DEPENDENCY_LEVELS_MAX < level)
	{
		zabbix_log(LOG_LEVEL_CRIT, "recursive trigger dependency is too deep (triggerid:" ZBX_FS_UI64 ")", triggerid);
		return SUCCEED;
	}
	
	if (0 != dep_list->values_num)
	{
		for (i = 0; i < dep_list->values_num; i++)
		{	
			next_triggerid = dep_list->values[i];
			DEBUG_TRIGGER(triggerid, "Checking dependand trigger %ld", next_triggerid);		
			//checking if trigger has failed
			glb_get_trigger_status(next_triggerid, &t_status, &t_functional, &t_value);
			
			if (TRIGGER_STATUS_ENABLED == t_status &&
				TRIGGER_FUNCTIONAL_TRUE == t_functional &&
				TRIGGER_VALUE_PROBLEM == t_value) {
				
				DEBUG_TRIGGER(triggerid, "Dependand trigger %ld has PROBLEM status, check FAILED ", next_triggerid);		
				return FAIL;
			}
			
			if (NULL != master_triggerids)
				zbx_vector_uint64_append(master_triggerids, next_triggerid);
		

			zbx_vector_uint64_create(&next_dep_list);
			zbx_vector_uint64_clear(&next_dep_list);
			obj_index_get_refs_from(&glb_config->trigger_deps, dep_list->values[i], &next_dep_list);
			
			DEBUG_TRIGGER(triggerid, "Checking recursevly trigger %ld dependancies", next_triggerid);
			ret == DCconfig_check_trigger_dependencies_rec(next_triggerid, &next_dep_list, level + 1, triggerids,
					master_triggerids);
			
			DEBUG_TRIGGER(triggerid, "Depandand ttrigger %ld dependancies check returned %d", next_triggerid, ret);
			
			zbx_vector_uint64_destroy(&next_dep_list);		
		
			if (FAIL == ret)	
				return FAIL;		
		}
	}
	return SUCCEED;
}

*/