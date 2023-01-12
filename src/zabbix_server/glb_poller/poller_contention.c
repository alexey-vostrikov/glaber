/*
** Glaber
** Copyright (C)  Glaber
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
#include "log.h"
#include "zbxtime.h"
#include "zbxcommon.h"
#include "zbxalgo.h"


typedef struct
{
    zbx_hashset_t sessions;
} poller_contention_conf_t;

static poller_contention_conf_t conf = {0};
static int sessions = 0;

typedef struct
{
    const char *resource_name; //expected to be strpooled strings
    unsigned int sess_count;
    int last_added_time;
} sess_t;


// static int	session_compare(const void *d1, const void *d2)
// {
// 	const sess_t *sess1 = d1;
// 	const sess_t *sess2	= d2;
    
//     if  (sess1->resource_name == sess2->resource_name)
//         return 0;

// 	return  strcmp(sess1->resource_name, sess2->resource_name);
// }

void poller_contention_add_session(const char *resource_name)
{
    sess_t local_session, *session;
    local_session.resource_name = resource_name;

    sessions++;
    
    if (NULL != (session = zbx_hashset_search(&conf.sessions, &local_session))) {
        session->sess_count++;
        session->last_added_time = time(NULL);
       // LOG_INF("Updating session for %s, total sessions %d", session->resource_name, session->sess_count);
        return;
    };
   
    local_session.resource_name = resource_name;
    local_session.sess_count = 1;
    local_session.last_added_time = time(NULL);

   // LOG_INF("Adding new session for %s, total sessions %d", local_session.resource_name, local_session.sess_count);
    zbx_hashset_insert(&conf.sessions, &local_session, sizeof(local_session));
    return;
}

void    poller_contention_remove_session(const char *resource_name) 
{
    sess_t *sess, local_sess;
    local_sess.resource_name = resource_name;
    
    if (NULL == resource_name)
        return;

    if (NULL == (sess = zbx_hashset_search(&conf.sessions, &local_sess)))
        return;

    if (sess->sess_count > 0)
        sess->sess_count --;
    
    sessions--;
  //  LOG_INF("Removing session for %s, total sessions %d", sess->resource_name, sess->sess_count);
    if (0 == sess->sess_count) {
        zbx_hashset_remove_direct(&conf.sessions, sess);
    }
    return;
}

extern int CONFIG_UNREACHABLE_DELAY;

int poller_contention_get_sessions(const char *resource_name) {

    sess_t *sess, local_sess;
    local_sess.resource_name = resource_name;
    
    int now = time(NULL);

    if (NULL == (sess = zbx_hashset_search(&conf.sessions, &local_sess))) {
        return 0;
    }


    if (sess->last_added_time + CONFIG_UNREACHABLE_DELAY  < now) {
        sessions -= sess->sess_count;

        zbx_hashset_remove_direct(&conf.sessions, sess);

        return 0;
    }

    return sess->sess_count;
}

int  poller_contention_sessions_count()
{
    return sessions;
}

void  poller_contention_init()
{
    zbx_hashset_create(&conf.sessions, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

void poller_contention_destroy()
{   zbx_hashset_iter_t iter;
    sess_t *sess; 
    zbx_hashset_iter_reset(&conf.sessions, &iter);
    
    while (NULL!=(sess = zbx_hashset_iter_next(&iter))) {
        zbx_hashset_iter_remove(&iter);
    }
}

void poller_contention_housekeep()
{   zbx_hashset_iter_t iter;
    sess_t *sess; 
    static int last_housekeep = 0;

    int now = time(NULL);

    if (now - last_housekeep < CONFIG_UNREACHABLE_DELAY/4) 
        return;
    last_housekeep = now;

    zbx_hashset_iter_reset(&conf.sessions, &iter);
    sessions = 0;

    while (NULL!=(sess = zbx_hashset_iter_next(&iter))) {   
        if (sess->last_added_time + CONFIG_UNREACHABLE_DELAY < now) {
            zbx_hashset_iter_remove(&iter);
        }
        sessions += sess->sess_count;
    }
}
