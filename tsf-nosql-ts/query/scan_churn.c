/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief query group: a scan while others write
 *
 * 300 keys s0..s299 are written first and never touched again. Then one
 * agent thread makes five full scans of the collection while three
 * others put and delete keys x0..x199 in it as fast as they can. Every
 * scan must see all 300 stable keys: a key present for the whole scan
 * is returned by it (Redis SCAN's guarantee, and what a cursor or a
 * key-ordered page gives the others), whatever came and went around it.
 */

#define TE_TEST_NAME    "query/scan_churn"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_load.h"
#include "tsapi_nosql.h"

#define STABLE  300

int
main(int argc, char **argv)
{
    tsapi_nosql_sess       sess = { 0 };
    tapi_nosql_load_args   la = { 0 };
    tapi_nosql_load_result lr = { 0 };
    const char            *backend;
    double                 chart[3];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s and write %d stable keys", backend, STABLE);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_scan_churn"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "scan");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_bulk_generate(sess.conn, sess.coll, STABLE,
                                               8, "s", 9, NULL, NULL));

    TEST_STEP("Five full scans while three clients write and delete");
    la.mode = "scan_churn";
    la.coll = sess.coll;
    la.threads = 4;
    la.ops = 200000;
    la.passes = 5;
    la.stable = STABLE;
    la.keys = 200;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    chart[0] = (double)lr.passes;
    chart[1] = (double)lr.missing;
    chart[2] = (double)(lr.ops - lr.passes);
    tsapi_nosql_chart("Scans, stable keys missed, writes and deletes around "
                      "them", NULL, NULL, "count", chart, 3);
    RING("%lld passes, %lld stable keys missed, %lld churn operations, "
         "%lld errors", lr.passes, lr.missing, lr.ops - lr.passes, lr.errors);
    if (lr.errors != 0)
        TEST_VERDICT("Operations failed during the scans");
    if (lr.passes != 5)
        TEST_VERDICT("Not every scan completed");
    if (lr.missing != 0)
        TEST_VERDICT("A scan missed a key present throughout it");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
