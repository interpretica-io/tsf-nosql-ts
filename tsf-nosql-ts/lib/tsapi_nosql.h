/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Test Suite library: what every tsf-nosql test needs
 *
 * An RPC server on the agent, the server a test iteration is about and
 * the configurations of its accounts, control of the server containers
 * (restart, pause), a scratch collection per test, and the chart every
 * test ends with.
 *
 * A "target" is one of the servers scripts/nosql-servers.sh starts:
 * redis, redis-tls, redis-weak, mongodb, mongodb-tls, cassandra,
 * elasticsearch, neo4j, memcached, etcd, etcd-auth. A "backend" is an
 * engine name; its target is the server with the same name, the one
 * the functional tests run on. The cluster targets - mongodb-rs3,
 * cassandra-ring, etcd-cluster, redis-primary, redis-replica - exist
 * while the cluster group runs (nosql-servers.sh cluster-up).
 */

#ifndef __TSAPI_NOSQL_H__
#define __TSAPI_NOSQL_H__

#include <stdbool.h>

#include "rcf_rpc.h"
#include "te_errno.h"
#include "tapi_test.h"

#include "tapi_nosql.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The agent this suite works on. */
#define TSAPI_NOSQL_TA          "Agt_A"

/** Where, on the agent, the servers script keeps its files (CAs). */
#define TSAPI_NOSQL_WORKDIR     "/tmp/tsf-nosql-ts"

/**
 * @name Fixture passwords
 * Of servers that live for one run on loopback; keep in step with
 * scripts/nosql-servers.sh. They protect nothing - which is why a test
 * can check that the module never shows them.
 * @{ */
#define TSAPI_NOSQL_APP_PW      "tsf-fixture-app-3Wq"
#define TSAPI_NOSQL_ADMIN_PW    "tsf-fixture-admin-8Jt"
/** @} */

/** Accounts of a target. */
typedef enum tsapi_nosql_role {
    TSAPI_NOSQL_ROLE_APP,       /**< What an application uses. */
    TSAPI_NOSQL_ROLE_ADMIN,     /**< The server's administrator. */
    TSAPI_NOSQL_ROLE_NONE,      /**< No credentials at all. */
} tsapi_nosql_role;

/** What a test needs. */
typedef struct tsapi_nosql_sess {
    const char      *ta;        /**< Agent name. */
    rcf_rpc_server  *pco;       /**< RPC server holding connections. */
    const char      *target;    /**< Target of the iteration. */
    tapi_nosql_conn *conn;      /**< Its connection (app account). */
    char             coll[32];  /**< A collection of this test's own. */
} tsapi_nosql_sess;

/** Engine of a target ("redis-tls" -> "redis"); NULL if unknown. */
extern const char *tsapi_nosql_engine_of(const char *target);

/**
 * Configuration of an account on a target: host, port, credentials,
 * TLS with the suite's CA where the target has TLS. Timeouts are
 * generous (the JVM engines are slow to answer the first request).
 *
 * @return New JSON object (json_decref() it), or NULL if unknown.
 */
extern json_t *tsapi_nosql_config(const char *target, tsapi_nosql_role role);

/**
 * Create the RPC server.
 *
 * @param sess  Session.
 * @param name  RPC server name, unique within the test.
 */
extern te_errno tsapi_nosql_sess_init(tsapi_nosql_sess *sess,
                                      const char *name);

/**
 * Connect to @p target as the application account. When the agent has
 * no driver for its engine the test is skipped (TEST_SKIP). A failure
 * to connect fails the test.
 */
extern void tsapi_nosql_sess_open(tsapi_nosql_sess *sess, const char *target);

/** Drop the scratch collection, close, destroy the RPC server. */
extern void tsapi_nosql_sess_fini(tsapi_nosql_sess *sess);

/** Skip the test when the engine does not do @p cap. */
#define TSAPI_NOSQL_REQUIRE(_sess, _cap) \
    do {                                                                \
        if (!tapi_nosql_has_cap((_sess)->conn, (_cap)))                 \
            TEST_SKIP("%s does not %s", (_sess)->conn->engine, (_cap)); \
    } while (0)

/**
 * Run scripts/nosql-servers.sh on the agent: "up", "down",
 * "restart", "kill", "pause", "unpause", "unnetem" <target>,
 * "cluster-up", "cluster-down", "node-stop", "node-start", "node-kill"
 * <member>. For "netem", @p target is "<target> <netem arguments>".
 *
 * @return 0, TE_EOPNOTSUPP when the agent has no docker, or TE_EFAIL.
 */
extern te_errno tsapi_nosql_servers(rcf_rpc_server *pco, const char *action,
                                    const char *target);

/**
 * Log one MI line graph: @p ys over @p xs (NULL: over the sample
 * number).
 */
extern void tsapi_nosql_chart(const char *title, const char *xname,
                              const double *xs, const char *yname,
                              const double *ys, unsigned int n);

/** Seconds since some fixed point, as a double. */
extern double tsapi_nosql_now(void);

/** CHECK_RC for a tapi_nosql call, with the failure class in the log. */
#define TSAPI_NOSQL_CHECK(_conn, _expr) \
    do {                                                                \
        te_errno _rc = (_expr);                                         \
                                                                        \
        if (_rc != 0)                                                   \
            TEST_FAIL("%s failed: %r (%s: %s)", #_expr, _rc,            \
                      tapi_nosql_errcls_name(tapi_nosql_last_cls(_conn)), \
                      tapi_nosql_last_msg(_conn));                      \
    } while (0)

/** A tapi_nosql_assert_*() that must hold (it emits its own verdict). */
#define TSAPI_NOSQL_ASSERT(_expr) \
    do {                                                                \
        te_errno _rc = (_expr);                                         \
                                                                        \
        if (TE_RC_GET_ERROR(_rc) == TE_EFAIL)                           \
            TEST_STOP;                                                  \
        if (_rc != 0)                                                   \
            TEST_FAIL("%s could not check: %r", #_expr, _rc);           \
    } while (0)

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSAPI_NOSQL_H__ */
