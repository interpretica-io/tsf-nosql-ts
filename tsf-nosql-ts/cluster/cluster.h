/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: helpers
 */

#ifndef __TS_CLUSTER_H__
#define __TS_CLUSTER_H__

#include <unistd.h>

#include "tsapi_nosql.h"

/* Connect to a cluster target, with configuration extras merged in. */
static inline tapi_nosql_conn *
cl_open(rcf_rpc_server *pco, const char *target, const char *extra)
{
    json_t           *cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    tapi_nosql_conn  *conn = NULL;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;

    /* A killed member's address answers nothing: give up on it fast. */
    json_object_set_new(cfg, "connect_timeout_ms", json_integer(2000));
    if (extra != NULL)
    {
        json_t *x = tapi_nosql_json(extra);

        json_object_update(cfg, x);
        json_decref(x);
    }
    if (tapi_nosql_open(pco, tsapi_nosql_engine_of(target), cfg, &conn,
                        &cls) != 0)
        TEST_FAIL("Cannot connect to %s: %s", target,
                  tapi_nosql_errcls_name(cls));
    json_decref(cfg);
    return conn;
}

/* The member name of an address 172.31.77.<p><n> ("mrs2" for .12). */
static inline void
cl_member(const char *addr, const char *prefix, char *out, size_t len)
{
    const char *dot = strrchr(addr, '.');
    int         last = dot != NULL ? atoi(dot + 1) : 0;

    snprintf(out, len, "%s%d", prefix, last % 10);
}

/* Retry a write until it succeeds (at most @p sec seconds): the time. */
static inline double
cl_write_until_ok(tapi_nosql_conn *conn, const char *coll, const char *key,
                  int sec)
{
    json_t *d = json_pack("{s:s}", "after", "failover");
    double  t0 = tsapi_nosql_now();
    int     i;

    for (i = 0; i < sec * 4; i++)
    {
        if (tapi_nosql_put(conn, coll, key, d, 0, NULL) == 0)
        {
            json_decref(d);
            return tsapi_nosql_now() - t0;
        }
        RING("Write refused: %s: %s",
             tapi_nosql_errcls_name(tapi_nosql_last_cls(conn)),
             tapi_nosql_last_msg(conn));
        usleep(250000);
    }
    json_decref(d);
    return -1;
}

#endif /* !__TS_CLUSTER_H__ */
