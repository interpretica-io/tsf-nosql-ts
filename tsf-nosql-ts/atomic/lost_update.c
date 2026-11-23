/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: the lost update, and its cure
 *
 * Four threads increment a field by reading the document, waiting 3 ms
 * and writing it back with no condition: the known-bad control, which
 * must lose updates - if it does not, the test cannot see a lost update
 * at all. Then the same 100 increments through the engine's atomic
 * counter, which must lose none.
 */

#define TE_TEST_NAME    "atomic/lost_update"

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
    tapi_nosql_load_result rmw = { 0 };
    tapi_nosql_load_result inc = { 0 };
    const char            *backend;
    double                 lost[2];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_lost_update"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "incr");

    TEST_STEP("Unprotected read-modify-write from 4 threads (the control)");
    la.mode = "rmw";
    la.coll = sess.coll;
    la.key = "rmw";
    la.threads = 4;
    la.ops = 25;
    la.think_us = 3000;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &rmw));
    RING("read-modify-write: %lld of %lld updates lost", rmw.lost,
         rmw.succeeded);
    if (rmw.lost <= 0)
        TEST_VERDICT("The unprotected read-modify-write lost no update: the "
                     "control did not bite");

    TEST_STEP("The same through the atomic counter");
    la.mode = "incr";
    la.key = "inc";
    la.think_us = 0;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &inc));
    lost[0] = (double)rmw.lost;
    lost[1] = (double)(inc.succeeded - inc.final);
    tsapi_nosql_chart("Updates lost of 100: read-modify-write, counter", NULL,
                      NULL, "lost", lost, 2);
    if (inc.errors != 0 || inc.final != 100)
        TEST_VERDICT("The atomic counter lost updates");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&rmw);
    tapi_nosql_load_result_free(&inc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
