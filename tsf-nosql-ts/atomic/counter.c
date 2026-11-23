/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: concurrent increments
 *
 * @p threads agent threads, each with a connection of its own, add 1 to
 * one counter 400 times between them. The counter ends at 400, with no
 * failed increment; on an engine whose counter returns the value its
 * own increment made, the 400 returned values are 400 distinct ones -
 * two equal ones would be two increments seeing the same state.
 * Cassandra's counter update returns nothing, so the value is read
 * after it and only the final count is checked.
 */

#define TE_TEST_NAME    "atomic/counter"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tapi_nosql_load.h"
#include "tsapi_nosql.h"

#define TOTAL   400

int
main(int argc, char **argv)
{
    tsapi_nosql_sess       sess = { 0 };
    tapi_nosql_load_args   la = { 0 };
    tapi_nosql_load_result lr = { 0 };
    const char            *backend;
    int                    threads;
    char                   name[64];
    double                 chart[3];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(threads);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_counter"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "incr");

    TEST_STEP("%d threads add 1 to one counter %d times between them",
              threads, TOTAL);
    la.mode = "incr";
    la.coll = sess.coll;
    la.key = "hot";
    la.threads = threads;
    la.ops = TOTAL / threads;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    snprintf(name, sizeof(name), "%s incr x%d", backend, threads);
    tapi_nosql_load_mi(name, &lr);
    RING("final %lld, succeeded %lld, distinct %lld, errors %lld", lr.final,
         lr.succeeded, lr.distinct, lr.errors);
    chart[0] = (double)lr.final;
    chart[1] = (double)lr.distinct;
    chart[2] = (double)lr.errors;
    tsapi_nosql_chart("Counter: final value, distinct returns, errors", NULL,
                      NULL, "count", chart, 3);
    if (lr.errors != 0)
        TEST_VERDICT("Some increments failed");
    if (lr.final != TOTAL)
        TEST_VERDICT("Concurrent increments were lost");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_counter(sess.conn, sess.coll, "hot",
                                                 TOTAL));
    if (tapi_nosql_has_cap(sess.conn, "incr_exact") && lr.distinct != TOTAL)
        TEST_VERDICT("Two increments returned the same value");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
