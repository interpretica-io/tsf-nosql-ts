/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: compare-and-set under a race
 *
 * Eight agent threads each read one document {"n"} and write n + 1
 * under the version they read, 25 times each, at the same moment. Some
 * writes apply, the rest are refused as conflicts; the final n must be
 * exactly the number that applied. A larger n would be two writes
 * applied on one version - the lost update compare-and-set exists to
 * prevent. (This test found that defect in tsf-nosql's Neo4j driver,
 * which compared before taking the node's lock.)
 */

#define TE_TEST_NAME    "atomic/cas_race"

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
    double                 chart[4];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    doc = json_pack("{s:i}", "n", 0);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_cas_race"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "cas");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "cas",
                                                doc, 0, NULL));

    TEST_STEP("8 clients compare-and-set one document, 25 times each");
    la.mode = "cas";
    la.coll = sess.coll;
    la.key = "cas";
    la.threads = 8;
    la.ops = 25;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    chart[0] = (double)lr.applied;
    chart[1] = (double)lr.conflicts;
    chart[2] = (double)lr.final;
    chart[3] = (double)lr.lost;
    tsapi_nosql_chart("Compare-and-set race: applied, conflicts, final, lost",
                      NULL, NULL, "count", chart, 4);
    RING("applied %lld, conflicts %lld, final %lld, errors %lld", lr.applied,
         lr.conflicts, lr.final, lr.errors);
    if (lr.errors != 0)
        TEST_VERDICT("Compare-and-set failed in the race");
    if (lr.applied == 0)
        TEST_VERDICT("No compare-and-set applied");
    if (lr.final != lr.applied)
        TEST_VERDICT("Two compare-and-sets applied on one version: an "
                     "update was lost");
    TEST_SUCCESS;

cleanup:
    json_decref(doc);
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
