/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: shaping a server's traffic with netem
 *
 * tc netem in the server container's network namespace (through
 * scripts/nosql-servers.sh netem): what the server sends is delayed or
 * dropped, while the server itself keeps running - unlike docker pause.
 */

#ifndef __TS_FAULTS_NET_H__
#define __TS_FAULTS_NET_H__

#include <unistd.h>

#include "tsapi_nosql.h"

/* Shape @p target's traffic ("delay 200ms", "loss 30%"). */
static inline te_errno
net_shape(rcf_rpc_server *pco, const char *target, const char *how)
{
    char arg[128];

    snprintf(arg, sizeof(arg), "%s %s", target, how);
    return tsapi_nosql_servers(pco, "netem", arg);
}

/* Median of @p n pings, in microseconds; -1 if a ping failed. */
static inline double
net_rtt(tapi_nosql_conn *conn, int n)
{
    double v[64];
    int    i;
    int    j;

    if (n > 64)
        n = 64;
    for (i = 0; i < n; i++)
    {
        long long us = 0;

        if (tapi_nosql_ping(conn, &us) != 0)
            return -1;
        v[i] = (double)us;
    }
    for (i = 1; i < n; i++)
    {
        for (j = i; j > 0 && v[j - 1] > v[j]; j--)
        {
            double t = v[j];

            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    }
    return v[n / 2];
}

/* After the network heals: the handle answers again within 60 s. */
static inline bool
net_recovers(tapi_nosql_conn *conn)
{
    int i;

    for (i = 0; i < 60; i++)
    {
        if (tapi_nosql_ping(conn, NULL) == 0)
            return true;
        sleep(1);
    }
    return false;
}

#endif /* !__TS_FAULTS_NET_H__ */
