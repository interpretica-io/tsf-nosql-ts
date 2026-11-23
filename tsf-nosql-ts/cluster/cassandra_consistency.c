/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: Cassandra consistency levels
 *
 * A ring of three with replication factor 3, and one connection per
 * consistency level, ONE, QUORUM and ALL:
 *  - all nodes up: every level writes;
 *  - one node down: ALL cannot reach every replica and fails; QUORUM
 *    (2 of 3) and ONE write;
 *  - two nodes down: QUORUM fails too; ONE still writes.
 * A failure is "unavailable" (the coordinator knows the replicas are
 * down) or "timeout" (it does not know yet). The nodes are started
 * again and ALL reads back everything written. The matrix is charted.
 */

#define TE_TEST_NAME    "cluster/cassandra_consistency"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "cluster.h"

static const char *const levels[] = { "ONE", "QUORUM", "ALL" };

/* Write with level @p l until it works (want) or fails (!want). */
static bool
writes(tapi_nosql_conn *c, const char *coll, const char *key, bool want)
{
    json_t *d = json_pack("{s:s}", "k", key);
    int     i;
    bool    ok = false;

    for (i = 0; i < 60; i++)
    {
        ok = tapi_nosql_put(c, coll, key, d, 0, NULL) == 0;
        if (ok == want)
            break;
        if (!ok)
            RING("Refused: %s: %s",
                 tapi_nosql_errcls_name(tapi_nosql_last_cls(c)),
                 tapi_nosql_last_msg(c));
        usleep(500000);
    }
    if (!ok && tapi_nosql_last_cls(c) != TAPI_NOSQL_ERR_UNAVAILABLE &&
        tapi_nosql_last_cls(c) != TAPI_NOSQL_ERR_TIMEOUT)
        TEST_VERDICT("A write without enough replicas failed for another "
                     "reason than unavailable replicas");
    json_decref(d);
    return ok;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    tapi_nosql_conn *conns[3] = { NULL, NULL, NULL };
    bool             down2 = false;
    bool             down3 = false;
    double           ok[9];
    unsigned int     l;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_cass_cl"));
    if (!tapi_nosql_supports(sess.pco, "cassandra"))
        TEST_SKIP("The agent has no cassandra driver");
    for (l = 0; l < 3; l++)
    {
        char extra[64];

        snprintf(extra, sizeof(extra), "{\"consistency\":\"%s\","
                 "\"timeout_ms\":20000}", levels[l]);
        conns[l] = cl_open(sess.pco, "cassandra-ring", extra);
    }

    TEST_STEP("All three up: every level writes");
    for (l = 0; l < 3; l++)
    {
        ok[l] = writes(conns[l], sess.coll, "up", true);
        if (!ok[l])
            TEST_VERDICT("A write failed with every replica up");
    }

    TEST_STEP("One node down: ALL fails, QUORUM and ONE write");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-stop", "cas3"));
    down3 = true;
    ok[3] = writes(conns[0], sess.coll, "one-down-1", true);
    ok[4] = writes(conns[1], sess.coll, "one-down-q", true);
    ok[5] = writes(conns[2], sess.coll, "one-down-a", false);
    if (!ok[3] || !ok[4])
        TEST_VERDICT("ONE or QUORUM failed with one replica of three down");
    if (ok[5])
        TEST_VERDICT("ALL succeeded with a replica down");

    TEST_STEP("Two nodes down: QUORUM fails, ONE writes");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-stop", "cas2"));
    down2 = true;
    ok[6] = writes(conns[0], sess.coll, "two-down-1", true);
    ok[7] = writes(conns[1], sess.coll, "two-down-q", false);
    ok[8] = writes(conns[2], sess.coll, "two-down-a", false);
    if (!ok[6])
        TEST_VERDICT("ONE failed with one replica of three up");
    if (ok[7])
        TEST_VERDICT("QUORUM succeeded with one replica of three up");
    tsapi_nosql_chart("Writes that succeeded: (ONE, QUORUM, ALL) with 0, 1 "
                      "and 2 nodes down", NULL, NULL, "ok", ok, 9);

    TEST_STEP("Start them again: ALL reads every write back");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-start", "cas2"));
    down2 = false;
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-start", "cas3"));
    down3 = false;
    {
        static const char *const keys[] = { "up", "one-down-1", "one-down-q",
                                            "two-down-1" };
        unsigned int k;

        for (k = 0; k < TE_ARRAY_LEN(keys); k++)
        {
            json_t *d = NULL;
            int     tries;

            for (tries = 0; tries < 60; tries++)
            {
                if (tapi_nosql_get(conns[2], sess.coll, keys[k], &d,
                                   NULL) == 0)
                    break;
                usleep(500000);
            }
            if (d == NULL)
                TEST_VERDICT("A write made with replicas down is not read "
                             "back at ALL");
            json_decref(d);
        }
    }
    TEST_SUCCESS;

cleanup:
    if (down2)
        tsapi_nosql_servers(sess.pco, "node-start", "cas2");
    if (down3)
        tsapi_nosql_servers(sess.pco, "node-start", "cas3");
    for (l = 0; l < 3; l++)
        tapi_nosql_close(conns[l]);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
