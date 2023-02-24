/******************************************************************
*   Retruns SUCCEED if trigger needs an action to be executed 
*
*  An action is to be executred when 
    a) host of the trigger is enabled
    b) the trigger is enabled
    c) all the triggers the trigger depends on are not in the incident state ( they might be unknown )
*/

//CALC_TRIGGER *triger

typedef struct {
    const char		*error;
    const char		*opdata;
   	int			    lastchange;
   	unsigned char	value;
    unsigned char	state; //TODO: figure what is what
    unsigned char	status;
    unsigned char	functional;		/* see TRIGGER_FUNCTIONAL_* defines      */ /*it's a hash to the item's state so that trigger will not be calculated for disabled items */
} glb_trigger_meta_t;

#define GLB_TRIGGER_MAX_DEP_LEVELS 32

/* trigger is functional unless its expression contains disabled or not monitored items */
#define TRIGGER_FUNCTIONAL_TRUE		0
#define TRIGGER_FUNCTIONAL_FALSE	1

int glb_trigger_init(mem_funcs_t *state_mem_funcs);

int glb_trigger_get_functional(u_int64_t triggerid, unsigned char *functional);
int glb_trigger_set_functional(u_int64_t triggerid, unsigned char functional);
void glb_triggers_reset_functional();
obj_index_t *glb_trigger_get_idx();