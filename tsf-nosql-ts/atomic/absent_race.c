/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: write-if-absent under a race
 *
 * Sixteen agent threads, each with a connection of its own, write one
 * key if it is absent, twenty times each and at the same moment:
 * exactly one of the 320 writes applies, and the key holds that one.
 */

#define TE_TEST_NAME    "atomic/absent_race"

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
    json_t                *doc = NULL;
    double                 chart[2];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_absent_race"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "absent");

    TEST_STEP("16 clients write one key if absent, 20 times each");
    la.mode = "absent";
    la.coll = sess.coll;
    la.key = "once";
    la.threads = 16;
    la.ops = 20;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    chart[0] = (double)lr.applied;
    chart[1] = (double)lr.errors;
    tsapi_nosql_chart("Write-if-absent race: applied, errors", NULL, NULL,
                      "count", chart, 2);
    RING("%lld of %lld writes applied, %lld errors", lr.applied, lr.ops,
         lr.errors);
    if (lr.errors != 0)
        TEST_VERDICT("Writes failed in the race");
    if (lr.applied != 1)
        TEST_VERDICT("Write-if-absent applied more or less than once under "
                     "a race");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll, "once",
                                                &doc, NULL));
    if (doc == NULL || json_object_get(doc, "t") == NULL)
        TEST_VERDICT("The key does not hold the write that applied");
    TEST_SUCCESS;

cleanup:
    json_decref(doc);
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
