

#ifndef GLB_API_CONF_IO
#define GLB_API_CONF_IO

typedef enum {
    API_CACHE_READ_CACHED = 0,
    API_CACHE_ON_FAIL, 
    API_CACHE_ALWAYS,
    API_NO_CACHE
} api_cache_mode_t;


typedef int (*api_response_cb_func_t)(int code, char *response);

//reads set of objects from API, uses async http/https iface needs event-based loop 
int read_objects_from_api(char *request, char *slug, api_cache_mode_t cache_type, api_response_cb_func_t callback);

int glb_api_conf_sync_request(char *request, char **response, char *slug, api_cache_mode_t cache_mode);

#endif