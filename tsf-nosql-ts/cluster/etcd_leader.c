/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: losing the etcd leader
 *
 * A three-member etcd cluster, the client given all three endpoints.
 * Write 30 keys, SIGKILL the leader, and write until a write succeeds:
 * the remaining two elect a leader and the client moves to them. Every
 * write acknowledged before is there (raft commits on a majority), and
 * the leader is another member. The killed one is started again. The
 * time without a leader is charted.
 */

#define TE_TEST_NAME    "cluster/etcd_leader"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "cluster.h"

#define N   30

/* The leader's member name (etcd1..3) and its ID. */
static void
leader_of(tapi_nosql_conn *conn, char *name, size_t nlen, char *id,
          size_t idlen)
{
    json_t     *cmd = json_pack("{s:s, s:{}}", "path", "/v3/maintenance/status",
                                "body");
    json_t     *st = NULL;
    json_t     *ml = NULL;
    json_t     *m;
    const char *leader;
    size_t      i;

    name[0] = '\0';
    TSAPI_NOSQL_CHECK(conn, tapi_nosql_native(conn, cmd, &st));
    json_decref(cmd);
    leader = json_string_value(json_object_get(json_object_get(st, "body"),
                                               "leader"));
    snprintf(id, idlen, "%s", leader != NULL ? leader : "");
    cmd = json_pack("{s:s, s:{}}", "path", "/v3/cluster/member/list", "body");
    TSAPI_NOSQL_CHECK(conn, tapi_nosql_native(conn, cmd, &ml));
    json_decref(cmd);
    json_array_foreach(json_object_get(json_object_get(ml, "body"),
                                       "members"), i, m)
    {
        if (leader != NULL &&
            strcmp(json_string_value(json_object_get(m, "ID")), leader) == 0)
            snprintf(name, nlen, "%s",
                     json_string_value(json_object_get(m, "name")));
    }
    json_decref(st);
    json_decref(ml);
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    tapi_nosql_conn *conn = NULL;
    char             leader[16];
    char             id[32];
    char             leader2[16];
    char             id2[32];
    bool             killed = false;
    double           chart[2];
    unsigned int     found = 0;
    int              i;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_etcd_leader"));
    if (!tapi_nosql_supports(sess.pco, "etcd"))
        TEST_SKIP("The agent has no etcd driver");
    conn = cl_open(sess.pco, "etcd-cluster", NULL);

    TEST_STEP("Write %d keys", N);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = json_pack("{s:i}", "i", i);

        snprintf(k, sizeof(k), "e%d", i);
        TSAPI_NOSQL_CHECK(conn, tapi_nosql_put(conn, sess.coll, k, d, 0,
                                               NULL));
        json_decref(d);
    }
    leader_of(conn, leader, sizeof(leader), id, sizeof(id));
    if (leader[0] == '\0')
        TEST_FAIL("Cannot tell the leader");
    RING("Leader: %s (%s)", leader, id);

    TEST_STEP("SIGKILL the leader; write until it works again");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-kill", leader));
    killed = true;
    chart[0] = cl_write_until_ok(conn, sess.coll, "after", 60);
    if (chart[0] < 0)
        TEST_VERDICT("The cluster took no write within 60 s of losing its "
                     "leader");
    leader_of(conn, leader2, sizeof(leader2), id2, sizeof(id2));
    RING("New leader %s after %.1f s", leader2, chart[0]);
    if (strcmp(id, id2) == 0)
        TEST_VERDICT("The killed member is still reported as leader");

    TEST_STEP("Every write acknowledged before is there");
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = NULL;

        snprintf(k, sizeof(k), "e%d", i);
        TSAPI_NOSQL_CHECK(conn, tapi_nosql_get(conn, sess.coll, k, &d, NULL));
        if (d != NULL)
            found++;
        json_decref(d);
    }
    chart[1] = found;
    tsapi_nosql_chart("Time to a new leader (s), writes found of 30", NULL,
                      NULL, "value", chart, 2);
    if (found != N)
        TEST_VERDICT("An acknowledged write was lost with the leader");

    TEST_STEP("The killed member rejoins");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-start", leader));
    killed = false;
    TEST_SUCCESS;

cleanup:
    if (killed)
        tsapi_nosql_servers(sess.pco, "node-start", leader);
    if (conn != NULL)
        tapi_nosql_drop(conn, sess.coll);
    tapi_nosql_close(conn);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
