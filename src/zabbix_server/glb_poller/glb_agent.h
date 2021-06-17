#ifndef GLABER_AGENT_H
#define GLABER_AGENT_H

#include "../preprocessor/linked_list.h"
#include "zbxalgo.h"
#include "glb_poller.h"
#include "dbcache.h"
#define GLB_MAX_AGENT_CONNS 8192

typedef struct
{
	struct agent_session *sess;/* SNMP session data */
	zbx_int64_t current_item;  /* itemid of currently processing item */
	int state;				   /* state of the connection */
	int finish_time;		   /* unix time till we wait for data to come */
	zbx_list_t items_list;     /* list of itemids assigned to the session */
	int socket; 	  
	void* conf; 
	int idx;
}  GLB_ASYNC_AGENT_CONNECTION;

typedef struct {
	zbx_hashset_t *items;  //hashsets of items and hosts to change their state
	zbx_hashset_t *hosts;  
	zbx_hashset_t lists_idx;
	GLB_ASYNC_AGENT_CONNECTION conns[GLB_MAX_AGENT_CONNS]; 
	int *requests;
	int *responses;
} GLB_ASYNC_AGENT_CONF;

typedef struct {
	//const char *hostname; //item's hostname value (strpooled)
	const char *interface_addr;
	unsigned int interface_port;
	const char *key; //item key value (strpooled)
} GLB_AGENT_ITEM;

unsigned int glb_agent_init_item(DC_ITEM *dc_item, GLB_AGENT_ITEM *glb_agent_item);
void    glb_agent_free_item(GLB_AGENT_ITEM *glb_agent_item );
void    glb_agent_shutdown(void *engine);
void   	glb_agent_add_poll_item(void *engine, GLB_POLLER_ITEM *glb_poller_item);
void*   glb_agent_init(zbx_hashset_t *items, zbx_hashset_t *hosts, int *requests, int *responses );
void    glb_agent_handle_async_io(void *engine);

#endif