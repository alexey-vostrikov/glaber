#ifndef GLABER_AGENT_H
#define GLABER_AGENT_H

#include "zbxalgo.h"
#include "glb_poller.h"
#include "dbcache.h"
#define GLB_MAX_AGENT_CONNS 8192



int     glb_agent_init(glb_poll_engine_t *poll);

//int 	glb_agent_init_item(glb_poll_module_t *poll_engine, DC_ITEM *dc_item, GLB_POLLER_ITEM *glb_poller_item);
//void  glb_agent_free_item(glb_poll_module_t *poll_agent, GLB_POLLER_ITEM *glb_poller_item );
//void  glb_agent_shutdown(glb_poll_module_t *poll_agent);
//void  glb_agent_add_poll_item(glb_poll_module_t *poll_agent, GLB_POLLER_ITEM *glb_poller_item);
//void  glb_agent_handle_async_io(glb_poll_module_t *poll_agent);

#endif