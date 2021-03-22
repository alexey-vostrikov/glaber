/*
** Glaber
** Copyright (C) 2001-2030 Glaber JSC
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

#ifndef GLABER_PINGER_H
#define GLABER_PINGER_H
#include "zbxicmpping.h"
#include "glb_poller.h"

#define GLB_ICMP 20
#define ZBX_ICMP 21

#define GLB_ICMP_NAME "glbmap"
#define ZBX_ICMP_NAME "fping"

typedef struct
{
	u_int64_t time;
	u_int64_t itemid; //using weak linking assuming events might be outdated

} GLB_PINGER_EVENT;

typedef struct {
	//config params
	//const char *key;
	u_int64_t finish_time;
	icmpping_t icmpping; //type of measurement (accesibility check or rtt)
	icmppingsec_type_t	type; //type of rtt check - min,max,avg
	int 	interval; //in ms time between packets
	int 	size; //payload size
	char *addr; //hostname to use
	unsigned int lastresolve; //when it was last resolved, it's a kind of a DNS cache
	char state; //internal item state to distinguish async ops
	char *ip; //ip address of the host
	int count; //how many packets to send
    unsigned int timeout; //timeout in ms - for how long to wait for the packet
	u_int64_t lastpacket_sent; //to control proper intrevals between packets in case is for some reason 
							   //there was a deleay in sending a packet, we need control that next 
							   //packet won't be sent too fast

	//measurement values, zeored each measurement
	double	min;
	double	sum;
	double	max;
	int	rcv;
	int sent;
//	int	cnt;

} GLB_PINGER_ITEM;

unsigned int glb_pinger_init_item(DC_ITEM *dc_item, GLB_PINGER_ITEM *pinger_item);
void glb_pinger_free_item(GLB_PINGER_ITEM *glb_pinger_item );
void glb_pinger_shutdown(void *engine);
int glb_pinger_start_ping(void *engine, GLB_POLLER_ITEM *glb_item);
void* glb_pinger_init(zbx_hashset_t *items, int *requests, int *responces );
void  glb_pinger_handle_async_io(void *engine);
int  glb_can_process_glb_pinger(const char *key);

#endif
