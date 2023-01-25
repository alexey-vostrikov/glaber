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
#include "zbxcommon.h"
#include "zbxalgo.h"

typedef struct
{
    zbx_hashset_t sessions;
} poller_session_conf_t;

static poller_session_conf_t conf = {0};

typedef struct
{
    u_int32_t sess_id;
    u_int64_t itemid;
    int time_added;
} sess_t;

static u_int32_t gen_random32bit()
{
    u_int32_t x;
    x = rand();
    return x;
}

/*this is for testing if the state machine performs well */
// static void dump_outdated_sessions() {
//     zbx_hashset_iter_t iter;
//     sess_t *sess;
//     static int lastrun = 0;

//     int now = time(NULL);

//     if (now-lastrun < 10)
//         return;
//     lastrun = now;

//     zbx_hashset_iter_reset(&conf.sessions, &iter);

//     while ( NULL != (sess = zbx_hashset_iter_next(&iter))) {
//         if (now - sess->time_added > 30 ) {
//             LOG_INF("Warning: found outdated session %u", sess->sess_id);
//             zbx_hashset_iter_remove(&iter);
//         }
//     }
// }

u_int32_t poller_sessions_create_session(u_int64_t itemid, u_int32_t ip)
{
    sess_t local_session, *session;

    u_int32_t sess_id = gen_random32bit();
    // itemid & 0xffffffff;

    while (NULL != (session = zbx_hashset_search(&conf.sessions, &sess_id)))
        sess_id = gen_random32bit();

    local_session.sess_id = sess_id;
    //  local_session.ip = ip;
    local_session.itemid = itemid;
    local_session.time_added = time(NULL);

    zbx_hashset_insert(&conf.sessions, &local_session, sizeof(local_session));

    //  dump_outdated_sessions();
    return sess_id;
}

u_int64_t poller_sessions_close_session(u_int32_t sess_id)
{

    sess_t *sess;
    u_int64_t itemid;
    if (NULL == (sess = zbx_hashset_search(&conf.sessions, &sess_id)))
        return 0;

    itemid = sess->itemid;
    zbx_hashset_remove_direct(&conf.sessions, sess);

    return itemid;
}

int poller_sessions_count()
{
    return conf.sessions.num_data;
}

static zbx_hash_t sess_hash(const void *data)
{
    return *(u_int32_t *)data;
}

static int sess_compare_func(const void *d1, const void *d2)
{
    const u_int32_t *i1 = (const u_int32_t *)d1;
    const u_int32_t *i2 = (const u_int32_t *)d2;

    ZBX_RETURN_IF_NOT_EQUAL(*i1, *i2);

    return 0;
}

void poller_sessions_init()
{
    zbx_hashset_create(&conf.sessions, 1000, sess_hash, sess_compare_func);
}

void poller_sessions_destroy()
{
    zbx_hashset_destroy(&conf.sessions);
}
