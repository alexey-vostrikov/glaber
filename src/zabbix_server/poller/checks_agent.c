/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
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

#include "common.h"
#include "comms.h"
#include "log.h"
#include "../../libs/zbxcrypto/tls_tcp_active.h"
#include "../preprocessor/linked_list.h"

#include "checks_agent.h"

extern u_int64_t CONFIG_DEBUG_ITEM;
extern u_int64_t CONFIG_DEBUG_HOST;

#if !(defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
extern unsigned char program_type;
#endif

/******************************************************************************
 *                                                                            *
 * Function: get_value_agent                                                  *
 *                                                                            *
 * Purpose: retrieve data from Zabbix agent                                   *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NETWORK_ERROR - network related error occurred               *
 *               NOTSUPPORTED - item not supported by the agent               *
 *               AGENT_ERROR - uncritical error on agent side occurred        *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: error will contain error message                                 *
 *                                                                            *
 ******************************************************************************/
int get_value_agent(DC_ITEM *item, AGENT_RESULT *result)
{
	zbx_socket_t s;
	char *tls_arg1, *tls_arg2;
	int ret = SUCCEED;
	ssize_t received_len;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' addr:'%s' key:'%s' conn:'%s'", __func__, item->host.host,
			   item->interface.addr, item->key, zbx_tcp_connection_type_name(item->host.tls_connect));

	switch (item->host.tls_connect)
	{
	case ZBX_TCP_SEC_UNENCRYPTED:
		tls_arg1 = NULL;
		tls_arg2 = NULL;
		break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	case ZBX_TCP_SEC_TLS_CERT:
		tls_arg1 = item->host.tls_issuer;
		tls_arg2 = item->host.tls_subject;
		break;
	case ZBX_TCP_SEC_TLS_PSK:
		tls_arg1 = item->host.tls_psk_identity;
		tls_arg2 = item->host.tls_psk;
		break;
#else
	case ZBX_TCP_SEC_TLS_CERT:
	case ZBX_TCP_SEC_TLS_PSK:
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "A TLS connection is configured to be used with agent"
												  " but support for TLS was not compiled into %s.",
											get_program_type_string(program_type)));
		ret = CONFIG_ERROR;
		goto out;
#endif
	default:
		THIS_SHOULD_NEVER_HAPPEN;
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid TLS connection parameters."));
		ret = CONFIG_ERROR;
		goto out;
	}

	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, item->interface.addr, item->interface.port, CONFIG_TIMEOUT,
										  item->host.tls_connect, tls_arg1, tls_arg2)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Sending [%s]", item->key);

		if (SUCCEED != zbx_tcp_send(&s, item->key))
			ret = NETWORK_ERROR;
		else if (FAIL != (received_len = zbx_tcp_recv_ext(&s, 0)))
			ret = SUCCEED;
		else if (SUCCEED == zbx_alarm_timed_out())
			ret = TIMEOUT_ERROR;
		else
			ret = NETWORK_ERROR;
	}
	else
		ret = NETWORK_ERROR;

	if (SUCCEED == ret)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "get value from agent result: '%s'", s.buffer);

		if (0 == strcmp(s.buffer, ZBX_NOTSUPPORTED))
		{
			/* 'ZBX_NOTSUPPORTED\0<error message>' */
			if (sizeof(ZBX_NOTSUPPORTED) < s.read_bytes)
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "%s", s.buffer + sizeof(ZBX_NOTSUPPORTED)));
			else
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Not supported by Zabbix Agent"));

			ret = NOTSUPPORTED;
		}
		else if (0 == strcmp(s.buffer, ZBX_ERROR))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Zabbix Agent non-critical error"));
			ret = AGENT_ERROR;
		}
		else if (0 == received_len)
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Received empty response from Zabbix Agent at [%s]."
													  " Assuming that agent dropped connection because of access permissions.",
												item->interface.addr));
			ret = NETWORK_ERROR;
		}
		else
			set_result_type(result, ITEM_VALUE_TYPE_TEXT, s.buffer);
	}
	else
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Get value from agent failed: %s", zbx_socket_strerror()));

	zbx_tcp_close(&s);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

//this function follows the socket status and
//handles operations according to the socket state

struct async_agent_conf
{
	int *errcodes;
	AGENT_RESULT *results;
	const DC_ITEM *items;
	int active_hosts;	/* hosts that we have not completed */
	int max_items;		 //items count in the items array, to stop iterating
	int max_connections; //how many snmp connections to utilize at max
};

struct async_agent_session
{
	//struct			snmp_session *sess;		/* SNMP session data */
	zbx_socket_t *socket;   /* sockets to connect to agents */
	int current_item;		/* Items index  in the items array we've processing */
	int state;				/* state of the session - might be free or in_work */
	int stop_time;			/* unix time till we wait for data to come */
	zbx_list_t *items_list; /* list of items assigned to the session */
};

static struct async_agent_conf conf;
static struct async_agent_session **hs;

void handle_socket_operation(struct async_agent_session *sess)
{

	//ZBX_SOCKADDR	servaddr_in;
	//struct hostent	*hp;
	ssize_t received_len;

	//int status;

	uint64_t item_idx = sess->current_item;

	switch (sess->state)
	{

	case POLL_CONNECT_SENT:
//		if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
//					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Sending request to host %s, item  %ld", conf.items[item_idx].host.host, 
//								conf.items[item_idx].itemid);

		//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld sending request %ld, %s  ",conf.items[item_idx].itemid,sess,conf.items[item_idx].key);
		if (SUCCEED != zbx_tcp_send(sess->socket, conf.items[item_idx].key))
		{ //we are using async, so it might be that the socket still not ready, if so, we just have to wait  a bit
			//on next call of handle op's (probably) the socket will be ready
			if (EAGAIN == zbx_socket_last_error())
			{
//				zabbix_log(LOG_LEVEL_TRACE, "Socket isn't ready yet for item %ld", conf.items[item_idx].itemid);
//				if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
//					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Socket still not ready for %s, item  %ld", conf.items[item_idx].host.host, 
//								conf.items[item_idx].itemid);
				return;
			}

			conf.errcodes[item_idx] = NETWORK_ERROR;
			sess->state = POLL_FINISHED;
			zbx_tcp_close(sess->socket);

			if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Couldn't send request to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

			SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Cannot send request to the agent"));
		}
		else
		{
			if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Completed sending request to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

			sess->state = POLL_REQ_SENT;
		}
		break;

	case POLL_REQ_SENT:
		//zabbix_log(LOG_LEVEL_INFORMATION,"handle req send start");
		if (FAIL != (received_len = zbx_tcp_recv_ext(sess->socket, 0)))
		{
			//	zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld got response data",conf.items[item_idx].itemid);
			conf.errcodes[item_idx] = SUCCEED;
			
			if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Got responce for host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);
			//zabbix_log(LOG_LEVEL_INFORMATION, "get value from agent result: '%s'", sess->socket->buffer);

			zbx_rtrim(sess->socket->buffer, " \r\n");
			zbx_ltrim(sess->socket->buffer, " ");

			if (0 == strcmp(sess->socket->buffer, ZBX_NOTSUPPORTED))
			{
				/* 'ZBX_NOTSUPPORTED\0<error message>' */
				if (sizeof(ZBX_NOTSUPPORTED) < sess->socket->read_bytes)
					SET_MSG_RESULT(&conf.results[item_idx], zbx_dsprintf(NULL, "%s", sess->socket->buffer + sizeof(ZBX_NOTSUPPORTED)));
				else
					SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Not supported by Zabbix Agent"));
				conf.errcodes[item_idx] = NOTSUPPORTED;
	
				if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: responce: not supported agent  host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

			}
			else if (0 == strcmp(sess->socket->buffer, ZBX_ERROR))
			{
				SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Zabbix Agent non-critical error"));
				conf.errcodes[item_idx] = AGENT_ERROR;
				
				if ( CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Agent error to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);
			}
			else if (0 == received_len)
			{
				SET_MSG_RESULT(&conf.results[item_idx], zbx_dsprintf(NULL, "Received empty response from Zabbix Agent at [%s]."
																		   " Assuming that agent dropped connection because of access permissions.",
																	 conf.items[item_idx].interface.addr));
				conf.errcodes[item_idx] = NETWORK_ERROR;
				
				if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: empty responce host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

			}
			else
			{
				set_result_type(&conf.results[item_idx], ITEM_VALUE_TYPE_TEXT, sess->socket->buffer);
				
				if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_DEBUG, "Debug agent: proecessed responce from  host %s, item  %ld, %s", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid, sess->socket->buffer);
			}
		}
		else
		{
			//	zabbix_log(LOG_LEVEL_DEBUG, "Get value from agent failed: %s", zbx_socket_strerror());
			if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Failed to get value from host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

			SET_MSG_RESULT(&conf.results[item_idx], zbx_dsprintf(NULL, "Get value from agent failed: %s", zbx_socket_strerror()));
			conf.errcodes[item_idx] = NETWORK_ERROR;
		}

		if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agnet: closing connection to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);

		zbx_tcp_close(sess->socket);
		sess->socket->socket = 0;
		
		if (SUCCEED != conf.errcodes[item_idx]) 
			sess->state = POLL_FINISHED;
		
		sess->state = POLL_FREE;

		if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid) 
		{
					
			zbx_list_iterator_t iterator;
			uint64_t val;
	
			zbx_list_iterator_init(sess->items_list, &iterator);
			while (SUCCEED == zbx_list_iterator_next(&iterator))
			{
				zbx_list_iterator_peek(&iterator, (void **)&val);
				zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: on session close found item waiting in the list %ld", conf.items[val].itemid);
			}
		}

		break;
	}
}

int init_async_agent(const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int max_items, int max_connections)
{

	int i;

	conf.items = items;
	conf.results = results;
	conf.errcodes = errcodes;
	conf.max_items = max_items;
	conf.max_connections = max_connections;
	//zabbix_log(LOG_LEVEL_INFORMATION,"%s: doing snmp init", __func__);

	if (NULL == (hs = zbx_malloc(NULL, sizeof(struct async_agent_session *) * max_connections)))
		return FAIL;

	for (i = 0; i < conf.max_connections; i++)
	{
		hs[i] = zbx_malloc(NULL, sizeof(struct async_agent_session));
		hs[i]->state = POLL_FREE;

		hs[i]->items_list = zbx_malloc(NULL, sizeof(zbx_list_t));
		zbx_list_create(hs[i]->items_list);
		hs[i]->socket = zbx_malloc(NULL, sizeof(zbx_socket_t));
		memset(hs[i]->socket, 0, sizeof(zbx_socket_t));
	}

	//zabbix_log(LOG_LEVEL_INFORMATION,"%s: finished snmp init", __func__);
	return SUCCEED;
}

int destroy_aync_agent()
{
	int i;
	//struct list_item *litem;
	zabbix_log(LOG_LEVEL_DEBUG, "%s: doing agent de-init", __func__);
	for (i = 0; i < conf.max_connections; i++)
	{
		//	zabbix_log(LOG_LEVEL_INFORMATION, "End of %s() freeing session for  item %d", __function_name,i);
		zbx_tcp_close(hs[i]->socket);
		if (conf.errcodes[i] == POLL_POLLING)
		{
			SET_MSG_RESULT(&conf.results[i], zbx_strdup(NULL, "Couldn't send agent packet: timeout"));
			conf.errcodes[i] = TIMEOUT_ERROR;
		}
		zbx_list_destroy(hs[i]->items_list);
		zbx_free(hs[i]->socket);
		zbx_free(hs[i]);
	}
	zbx_free(hs);
	return SUCCEED;
}

int start_agent_connection(struct async_agent_session *sess)
{

	u_int64_t item_idx;
	char *tls_arg1, *tls_arg2;
	int ret = SUCCEED;

	if (POLL_FREE != sess->state)
	{
		return SUCCEED;
	}

	if (SUCCEED != zbx_list_peek(sess->items_list, (void **)&item_idx))
	{
		return SUCCEED;
	}

	if (ITEM_TYPE_ZABBIX != conf.items[item_idx].type)
	{
		conf.errcodes[item_idx] = POLL_SKIPPED;
		return SUCCEED;
	}

	sess->socket->buf_type = ZBX_BUF_TYPE_STAT;
	sess->socket->buffer = sess->socket->buf_stat;


	if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
				zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent, starting connection to host %s, item  %ld", conf.items[item_idx].host.host, conf.items[item_idx].itemid);

	if (SUCCEED != zbx_list_pop(sess->items_list, (void **)&item_idx))
		return SUCCEED;

	//cheick if the item is agent type
	zabbix_log(LOG_LEVEL_TRACE, "In %s() host:'%s' addr:'%s' key:'%s' conn:'%s'", __func__,
			   conf.items[item_idx].host.host, conf.items[item_idx].interface.addr, conf.items[item_idx].key,
			   zbx_tcp_connection_type_name(conf.items[item_idx].host.tls_connect));

	switch (conf.items[item_idx].host.tls_connect)
	{
	case ZBX_TCP_SEC_UNENCRYPTED:
		tls_arg1 = NULL;
		tls_arg2 = NULL;
		break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	case ZBX_TCP_SEC_TLS_CERT:
		tls_arg1 = conf.items[item_idx].host.tls_issuer;
		tls_arg2 = conf.items[item_idx].host.tls_subject;
		break;
	case ZBX_TCP_SEC_TLS_PSK:
		tls_arg1 = conf.items[item_idx].host.tls_psk_identity;
		tls_arg2 = conf.items[item_idx].host.tls_psk;
		break;
#else
	case ZBX_TCP_SEC_TLS_CERT:
	case ZBX_TCP_SEC_TLS_PSK:
		SET_MSG_RESULT(&conf.results[item_idx], zbx_dsprintf(NULL, "A TLS connection is configured to be used with agent"
																   " but support for TLS was not compiled into %s.",
															 get_program_type_string(program_type)));
		conf.errcodes[item_idx] = CONFIG_ERROR;
		//zabbix_log(LOG_LEVEL_INFORMATION,"QPoller: item %ld slot %d changed state to %d at %s,%d",items[i].itemid,i,errcodes[i],__func__, __LINE__);
		return SUCCEED;
#endif
	default:
		THIS_SHOULD_NEVER_HAPPEN;
		zabbix_log(LOG_LEVEL_WARNING, "%s: Errcode is %d", __func__, conf.errcodes[item_idx]);
		SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Invalid TLS connection parameters."));
		conf.errcodes[item_idx] = CONFIG_ERROR;
		sess->state = POLL_FINISHED;
		
		return SUCCEED;
	}

	//zabbix_log(LOG_LEVEL_INFORMATION,"Agent item %ld connecting ",conf.items[item_idx].itemid);
	if (SUCCEED != (ret = zbx_tcp_connect(sess->socket, CONFIG_SOURCE_IP, conf.items[item_idx].interface.addr, conf.items[item_idx].interface.port, 0,
										  conf.items[item_idx].host.tls_connect, tls_arg1, tls_arg2)))
	{
		conf.errcodes[item_idx] = NETWORK_ERROR;
		//marking session as finished for proper cleanup
		sess->state = POLL_FINISHED;
		zbx_tcp_close(sess->socket);
		////zabbix_log(LOG_LEVEL_INFORMATION,"QPoller: item %ld slot %d changed state to %d at %s,%d",items[i].itemid,i,errcodes[i],__func__, __LINE__);
		SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Couldn't create socket"));
	
		if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: connection fail to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);


		return SUCCEED;
	}

	//marking item and connection as in polling state now
	if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
		zabbix_log(LOG_LEVEL_INFORMATION, "Sending request to host %s, item  %ld", conf.items[item_idx].host.host, conf.items[item_idx].itemid);

	conf.errcodes[item_idx] = POLL_POLLING;
	sess->current_item = item_idx;
	sess->state = POLL_CONNECT_SENT;
	sess->stop_time = time(NULL) + CONFIG_TIMEOUT + 1;

	return SUCCEED;
}

int get_values_agent_async()
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() Started", __func__);

	const DC_ITEM *items = conf.items;
	int *errcodes = conf.errcodes;
	AGENT_RESULT *results = conf.results;
	//struct list_item* litem;
	int result, ret, count;
	socklen_t result_len = sizeof(result);
	u_int64_t i;

	//stage 0 adding newly added items to the list of their connections
	for (i = 0; i < conf.max_items; i++)
	{
		if (POLL_PREPARED == conf.errcodes[i])
		{
			
			zbx_list_append(hs[conf.items[i].host.hostid % conf.max_connections]->items_list, (void **)i, NULL);

			if (CONFIG_DEBUG_HOST == conf.items[i].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Couldn't send request to host %s, item  %ld", conf.items[i].host.host, 
								conf.items[i].itemid);
			errcodes[i] = POLL_QUEUED;
		}
	}

	int pollcount=0;

	for (i = 0; i < conf.max_connections; i++)
	{
			start_agent_connection(hs[i]);
	}
	//iterating over all items we have to bind them to connections
	//to do: disable launching of new items if runtime or runcount is exceeded

	//stage 2 - looking for ready sockets and if there is any data there
	//if so, handling the data
	//since we use snmp_read all calls on connections where data exists, will be automatic

	for (i = 0; i < conf.max_connections; i++)
	{
		u_int64_t item_idx = hs[i]->current_item;

		switch (hs[i]->state)
		{
			case POLL_CONNECT_SENT:

			ret = getsockopt(hs[i]->socket->socket, SOL_SOCKET, SO_ERROR, &result, &result_len);
			if (0 > ret) continue;

			if (0 != result)
			{
				if (NULL == &conf.results[item_idx])
						SET_MSG_RESULT(&conf.results[item_idx], zbx_strdup(NULL, "Connection to the host failed: check firewall rules and agent is running"));
					zbx_tcp_close(hs[i]->socket);
					hs[i]->state = POLL_FINISHED;
					//hs[i]->current_item = -1;
					conf.errcodes[item_idx] = NETWORK_ERROR;
					if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
						zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: connection failed to host %s, item  %ld", conf.items[item_idx].host.host, 
								conf.items[item_idx].itemid);
					continue;
				}
				break;
			case POLL_REQ_SENT:
				//checking if there are some data waiting for us in the socket
				ioctl(hs[i]->socket->socket, FIONREAD, &count);
				if (0 == count)
					continue;
				break;
			default:
				continue;
			}
			//zabbix_log(LOG_LEVEL_INFORMATION,"About to start handle socket operations");
			//handling if syn ack is here or some data already waiting
			handle_socket_operation(hs[i]);
		}
	
	//	usleep(10000);
	//}
	//lets deal with timed-out connections
	//zabbix_log(LOG_LEVEL_INFORMATION,"#42 closing timeouts");
	for (i = 0; i < conf.max_connections; i++)
	{
		u_int64_t item_idx = hs[i]->current_item;

		if ( ((hs[i]->stop_time < time(NULL)) && ((POLL_FREE != hs[i]->state) && (POLL_FINISHED != hs[i]->state))) 
			|| ( (POLL_FINISHED == hs[i]->state)  && (SUCCEED != conf.errcodes[item_idx]) )
		
		 )
		{

			//int item_idx=hs[snmp_conn].current_item;
			errcodes[item_idx] = TIMEOUT_ERROR;
			SET_MSG_RESULT(&conf.results[item_idx], zbx_dsprintf(NULL, "Timed out waiting for the responce"));

			hs[i]->state = POLL_FREE; //this will make it possible to reuse the connection
			zbx_tcp_close(hs[i]->socket);

			if (CONFIG_DEBUG_HOST == conf.items[item_idx].host.hostid)
					zabbix_log(LOG_LEVEL_INFORMATION, "Debug agent: Operation timeout to host %s, item  %ld", conf.items[item_idx].host.host, conf.items[item_idx].itemid);

			//zabbix_log(LOG_LEVEL_INFORMATION,"Item %ld timed out, cleaning ",conf.items[item_idx].itemid);
			//going through all the connection's item's and removing items of the same host with timeout error
			zbx_list_iterator_t list_iter;
			u_int64_t item_idx = 0;
			int cnt = 0;

			zbx_list_iterator_init(hs[i]->items_list, &list_iter);
			while ( (SUCCEED == zbx_list_peek(hs[i]->items_list, (void **)&item_idx)) &&
				   items[hs[i]->current_item].host.hostid == items[item_idx].host.hostid )
			{
				zbx_list_pop(hs[i]->items_list, (void **)&item_idx);
				errcodes[item_idx] = TIMEOUT_ERROR;
				SET_MSG_RESULT(&results[item_idx], zbx_dsprintf(NULL, "Skipped from polling due to item %s timeout",items[item_idx].key_orig));
				cnt++;
			}

			if (0 < cnt && CONFIG_DEBUG_HOST == items[item_idx].host.hostid) 
				zabbix_log(LOG_LEVEL_INFORMATION, "Agent timed out %d items for host %s item %ld due to prev item fail %d",
				 cnt, conf.items[item_idx].host.host, conf.items[item_idx].itemid, result);

			//if (cnt > 0 ) zabbix_log(LOG_LEVEL_DEBUG, "Found additional host's items idx %ld, count %d, removing with timeout error",item_idx, cnt);
			//setting connection free to init new connection
		}
		else if (POLL_FINISHED == hs[i]->state)
		{
			//cleaning up the connection, marking as free
			zbx_tcp_close(hs[i]->socket);
			hs[i]->state = POLL_FREE;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s: Finished", __func__);
	return SUCCEED;
}
