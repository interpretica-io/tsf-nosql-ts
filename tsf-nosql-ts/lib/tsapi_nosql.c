/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Test Suite library: what every tsf-nosql test needs
 */

#define TE_LGR_USER     "TSAPI NOSQL"

#include "te_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logger_api.h"
#include "te_defs.h"
#include "te_string.h"
#include "te_mi_log.h"
#include "tapi_rpc_stdio.h"

#include "tsapi_nosql.h"

/** How long starting every database container may take. */
#define SERVERS_TIMEOUT_MS  900000

static const struct {
    const char *target;
    const char *engine;
    int         port;
    bool        tls;
    const char *app_user;       /**< NULL: no account (open server). */
    const char *admin_user;
    const char *app_pw;
    const char *extra;          /**< More configuration, JSON. */
} targets[] = {
    { "redis", "redis", 56379, false, NULL, NULL, NULL, NULL },
    { "redis-tls", "redis", 56380, true, "tsf", "tsf_admin",
      TSAPI_NOSQL_APP_PW, NULL },
    { "redis-weak", "redis", 56381, false, NULL, NULL, "foobared", NULL },
    { "mongodb", "mongodb", 57017, false, NULL, NULL, NULL,
      "{\"replica_set\":\"rs0\"}" },
    { "mongodb-tls", "mongodb", 57018, true, "tsf", "tsf_admin",
      TSAPI_NOSQL_APP_PW, "{\"auth_db\":\"tsf\"}" },
    { "cassandra", "cassandra", 59042, false, NULL, NULL, NULL, NULL },
    { "elasticsearch", "elasticsearch", 59200, false, "tsf", "elastic",
      TSAPI_NOSQL_APP_PW, NULL },
    { "neo4j", "neo4j", 57474, false, "neo4j", "neo4j", TSAPI_NOSQL_APP_PW,
      NULL },
    { "memcached", "memcached", 51211, false, NULL, NULL, NULL, NULL },
    { "etcd", "etcd", 52379, false, NULL, NULL, NULL, NULL },
    { "etcd-auth", "etcd", 52380, false, "tsf", "root", TSAPI_NOSQL_APP_PW,
      NULL },
};

/* Run a shell command on the agent; its exit status, or -1. */
static int
agent_sh(rcf_rpc_server *pco, unsigned int timeout_ms, const char *cmd)
{
    rpc_wait_status st;
    unsigned int    saved = pco->def_timeout;

    pco->timeout = timeout_ms;
    RPC_AWAIT_IUT_ERROR(pco);
    st = rpc_system(pco, cmd);
    pco->timeout = saved;
    if (st.flag != RPC_WAIT_STATUS_EXITED)
        return -1;
    return (int)st.value;
}

/* See description in tsapi_nosql.h */
const char *
tsapi_nosql_engine_of(const char *target)
{
    size_t i;

    for (i = 0; i < TE_ARRAY_LEN(targets); i++)
    {
        if (strcmp(targets[i].target, target) == 0)
            return targets[i].engine;
    }
    return NULL;
}

/* See description in tsapi_nosql.h */
json_t *
tsapi_nosql_config(const char *target, tsapi_nosql_role role)
{
    size_t  i;
    json_t *cfg;

    for (i = 0; i < TE_ARRAY_LEN(targets); i++)
    {
        if (strcmp(targets[i].target, target) == 0)
            break;
    }
    if (i == TE_ARRAY_LEN(targets))
    {
        ERROR("Unknown target '%s'", target);
        return NULL;
    }
    cfg = json_pack("{s:s, s:i, s:i, s:i}", "host", "127.0.0.1",
                    "port", targets[i].port, "timeout_ms", 30000,
                    "connect_timeout_ms", 10000);
    if (targets[i].extra != NULL)
    {
        json_t *x = json_loads(targets[i].extra, 0, NULL);

        json_object_update(cfg, x);
        json_decref(x);
    }
    if (targets[i].tls)
        json_object_set_new(cfg, "tls", json_pack("{s:b, s:s}", "enabled", 1,
                                                  "ca", TSAPI_NOSQL_WORKDIR
                                                  "/ca.crt"));
    if (role == TSAPI_NOSQL_ROLE_APP && targets[i].app_pw != NULL)
    {
        if (targets[i].app_user != NULL)
            json_object_set_new(cfg, "user",
                                json_string(targets[i].app_user));
        json_object_set_new(cfg, "password", json_string(targets[i].app_pw));
    }
    else if (role == TSAPI_NOSQL_ROLE_ADMIN && targets[i].admin_user != NULL)
    {
        json_object_set_new(cfg, "user", json_string(targets[i].admin_user));
        json_object_set_new(cfg, "password",
                            json_string(strcmp(targets[i].target, "neo4j") == 0
                                        ? TSAPI_NOSQL_APP_PW
                                        : TSAPI_NOSQL_ADMIN_PW));
        if (strcmp(targets[i].target, "mongodb-tls") == 0)
            json_object_set_new(cfg, "auth_db", json_string("admin"));
    }
    else if (role == TSAPI_NOSQL_ROLE_ADMIN && targets[i].app_pw != NULL)
    {
        /* One password, one account (redis-weak). */
        json_object_set_new(cfg, "password", json_string(targets[i].app_pw));
    }
    return cfg;
}

/* See description in tsapi_nosql.h */
te_errno
tsapi_nosql_sess_init(tsapi_nosql_sess *sess, const char *name)
{
    te_errno rc;

    memset(sess, 0, sizeof(*sess));
    sess->ta = TSAPI_NOSQL_TA;
    snprintf(sess->coll, sizeof(sess->coll), "c%ld", (long)getpid());
    rc = rcf_rpc_server_create(sess->ta, name, &sess->pco);
    if (rc != 0)
        ERROR("Cannot create the RPC server '%s' on %s: %r", name, sess->ta,
              rc);
    return rc;
}

/* See description in tsapi_nosql.h */
void
tsapi_nosql_sess_open(tsapi_nosql_sess *sess, const char *target)
{
    const char       *engine = tsapi_nosql_engine_of(target);
    json_t           *cfg;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    te_errno          rc;

    if (engine == NULL)
        TEST_FAIL("Unknown target '%s'", target);
    sess->target = target;
    if (!tapi_nosql_supports(sess->pco, engine))
        TEST_SKIP("The agent has no %s driver", engine);
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    rc = tapi_nosql_open(sess->pco, engine, cfg, &sess->conn, &cls);
    json_decref(cfg);
    if (rc != 0)
        TEST_FAIL("Cannot connect to %s: %r (%s)", target, rc,
                  tapi_nosql_errcls_name(cls));
    RING("Connected to %s: %s %s%s", target, engine,
         tapi_nosql_server_version(sess->conn),
         tapi_nosql_tls(sess->conn) ? " over TLS" : "");
}

/* See description in tsapi_nosql.h */
void
tsapi_nosql_sess_fini(tsapi_nosql_sess *sess)
{
    if (sess->conn != NULL)
    {
        /* Best effort: a test that broke the connection is over anyway. */
        if (strcmp(sess->conn->engine, "memcached") != 0)
            tapi_nosql_drop(sess->conn, sess->coll);
        tapi_nosql_close(sess->conn);
        sess->conn = NULL;
    }
    if (sess->pco != NULL)
    {
        rcf_rpc_server_destroy(sess->pco);
        sess->pco = NULL;
    }
}

/* See description in tsapi_nosql.h */
te_errno
tsapi_nosql_servers(rcf_rpc_server *pco, const char *action,
                    const char *target)
{
    const char *top = getenv("TS_TOPDIR");
    te_string   cmd = TE_STRING_INIT;
    int         status;

    if (top == NULL)
    {
        ERROR("TS_TOPDIR is not set: cannot find scripts/nosql-servers.sh");
        return TE_RC(TE_TAPI, TE_ENOENT);
    }
    te_string_append(&cmd, "bash %s/scripts/nosql-servers.sh %s%s%s %s", top,
                     action, target != NULL ? " " : "",
                     target != NULL ? target : "", TSAPI_NOSQL_WORKDIR);
    status = agent_sh(pco, SERVERS_TIMEOUT_MS, cmd.ptr);
    te_string_free(&cmd);
    if (status == 0)
        return 0;
    if (status == 3)
        return TE_RC(TE_TAPI, TE_EOPNOTSUPP);
    ERROR("nosql-servers.sh %s %s failed (status %d)", action,
          target != NULL ? target : "", status);
    return TE_RC(TE_TAPI, TE_EFAIL);
}

/* See description in tsapi_nosql.h */
void
tsapi_nosql_chart(const char *title, const char *xname, const double *xs,
                  const char *yname, const double *ys, unsigned int n)
{
    te_mi_logger *logger = NULL;
    unsigned int  i;

    if (n == 0 || te_mi_logger_meas_create("tsf-nosql-ts", &logger) != 0)
        return;
    for (i = 0; i < n; i++)
    {
        if (xs != NULL)
            te_mi_logger_add_meas(logger, NULL, TE_MI_MEAS_UNITLESS_VALUE,
                                  xname, TE_MI_MEAS_AGGR_SINGLE, xs[i],
                                  TE_MI_MEAS_MULTIPLIER_PLAIN);
        te_mi_logger_add_meas(logger, NULL, TE_MI_MEAS_UNITLESS_VALUE, yname,
                              TE_MI_MEAS_AGGR_SINGLE, ys[i],
                              TE_MI_MEAS_MULTIPLIER_PLAIN);
    }
    te_mi_logger_add_meas_view(logger, NULL, TE_MI_MEAS_VIEW_LINE_GRAPH,
                               "chart", title);
    if (xs != NULL)
        te_mi_logger_meas_graph_axis_add_name(logger, NULL,
                                              TE_MI_MEAS_VIEW_LINE_GRAPH,
                                              "chart", TE_MI_GRAPH_AXIS_X,
                                              xname);
    else
        te_mi_logger_meas_graph_axis_add_name(logger, NULL,
                                              TE_MI_MEAS_VIEW_LINE_GRAPH,
                                              "chart", TE_MI_GRAPH_AXIS_X,
                                              TE_MI_GRAPH_AUTO_SEQNO);
    te_mi_logger_meas_graph_axis_add_name(logger, NULL,
                                          TE_MI_MEAS_VIEW_LINE_GRAPH,
                                          "chart", TE_MI_GRAPH_AXIS_Y, yname);
    te_mi_logger_destroy(logger);
}

/* See description in tsapi_nosql.h */
double
tsapi_nosql_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
