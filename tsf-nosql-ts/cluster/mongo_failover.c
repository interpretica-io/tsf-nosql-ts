/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: MongoDB primary failover
 *
 * A three-member replica set (election timeout 2 s). Write 50 documents
 * acknowledged by a majority, SIGKILL the primary, and write until a
 * write succeeds again: a new primary is elected, the client finds it,
 * and every majority-acknowledged write is there - a majority write is
 * on a member that can win the election. The killed member is started
 * again and rejoins. Failover time is charted.
 */

#define TE_TEST_NAME    "cluster/mongo_failover"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "cluster.h"

#define N   50

static void
primary_of(tapi_nosql_conn *conn, char *out, size_t len)
{
    json_t *cmd = json_pack("{s:s, s:{s:i}}", "db", "admin", "command",
                            "hello", 1);
    json_t *r = NULL;

    TSAPI_NOSQL_CHECK(conn, tapi_nosql_native(conn, cmd, &r));
    snprintf(out, len, "%s", json_string_value(json_object_get(r, "primary"))
                             != NULL
                             ? json_string_value(json_object_get(r, "primary"))
                             : "");
    json_decref(cmd);
    json_decref(r);
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    tapi_nosql_conn *conn = NULL;
    char             before[64];
    char             after[64];
    char             member[16] = "";
    bool             killed = false;
    double           chart[2];
    unsigned int     found = 0;
    int              i;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_mongo_failover"));
    if (!tapi_nosql_supports(sess.pco, "mongodb"))
        TEST_SKIP("The agent has no mongodb driver");

    TEST_STEP("Write %d documents with majority acknowledgement", N);
    conn = cl_open(sess.pco, "mongodb-rs3", NULL);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = json_pack("{s:i}", "i", i);

        snprintf(k, sizeof(k), "m%d", i);
        TSAPI_NOSQL_CHECK(conn, tapi_nosql_put(conn, sess.coll, k, d, 0,
                                               NULL));
        json_decref(d);
    }
    primary_of(conn, before, sizeof(before));
    cl_member(before, "mrs", member, sizeof(member));
    RING("Primary: %s (%s)", before, member);

    TEST_STEP("SIGKILL the primary; write until it works again");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-kill", member));
    killed = true;
    chart[0] = cl_write_until_ok(conn, sess.coll, "after", 60);
    if (chart[0] < 0)
        TEST_VERDICT("No new primary took writes within 60 s");
    primary_of(conn, after, sizeof(after));
    RING("New primary %s after %.1f s", after, chart[0]);
    if (strcmp(before, after) == 0)
        TEST_VERDICT("The killed member is still reported as primary");

    TEST_STEP("Every majority-acknowledged write is there");
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = NULL;

        snprintf(k, sizeof(k), "m%d", i);
        TSAPI_NOSQL_CHECK(conn, tapi_nosql_get(conn, sess.coll, k, &d, NULL));
        if (d != NULL)
            found++;
        json_decref(d);
    }
    chart[1] = found;
    tsapi_nosql_chart("Failover time (s), majority writes found of 50", NULL,
                      NULL, "value", chart, 2);
    if (found != N)
        TEST_VERDICT("A majority-acknowledged write was lost in a failover");

    TEST_STEP("The killed member rejoins");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "node-start", member));
    killed = false;
    TEST_SUCCESS;

cleanup:
    if (killed)
        tsapi_nosql_servers(sess.pco, "node-start", member);
    if (conn != NULL)
        tapi_nosql_drop(conn, sess.coll);
    tapi_nosql_close(conn);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
