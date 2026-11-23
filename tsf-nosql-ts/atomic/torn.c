/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: no half-applied multi-write is visible
 *
 * One agent thread writes keys a and b to i, for i = 1 to 300, each
 * time in one multi-write; three others read b and then a. A write that
 * is atomic and isolated makes a at least the b it was written with, so
 * seeing a < b is a multi-write seen half applied. Redis (MULTI/EXEC),
 * MongoDB (a transaction), etcd (a txn) and Neo4j (a transaction)
 * promise that and must show none. A Cassandra logged batch promises
 * that all of it is applied eventually, not that a reader never sees a
 * part: there the count is recorded, not judged.
 */

#define TE_TEST_NAME    "atomic/torn"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_load.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess       sess = { 0 };
    tapi_nosql_load_args   la = { 0 };
    tapi_nosql_load_result lr = { 0 };
    const char            *backend;
    double                 chart[2];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_torn"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "multi");

    TEST_STEP("One writer of a and b together, three readers");
    la.mode = "torn";
    la.coll = sess.coll;
    la.threads = 4;
    la.ops = 300;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    chart[0] = (double)lr.torn;
    chart[1] = (double)lr.reads;
    tsapi_nosql_chart("Half-applied multi-writes seen, of read pairs", NULL,
                      NULL, "count", chart, 2);
    RING("%lld torn of %lld read pairs, %lld errors", lr.torn, lr.reads,
         lr.errors);
    if (lr.errors != 0)
        TEST_VERDICT("Operations failed while multi-writing");
    if (strcmp(backend, "cassandra") == 0)
    {
        if (lr.torn != 0)
            RING("A logged batch was seen half applied %lld times: "
                 "Cassandra does not isolate a batch across partitions",
                 lr.torn);
    }
    else if (lr.torn != 0)
    {
        TEST_VERDICT("A multi-write was seen half applied");
    }
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
