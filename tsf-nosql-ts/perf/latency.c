/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief perf group: latency of one client
 *
 * One agent thread runs 200 operations (@p op: put of a document of
 * @p size bytes over 100 keys, or get of 100 documents of that size
 * written first) and every operation's latency is recorded on the
 * agent; the exact percentiles go out as an MI line graph of latency
 * over percentile. No operation may fail.
 */

#define TE_TEST_NAME    "perf/latency"

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
    const char            *op;
    int                    size;
    char                   name[96];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_STRING_PARAM(op);
    TEST_GET_INT_PARAM(size);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_latency"));
    tsapi_nosql_sess_open(&sess, backend);
    if (strcmp(op, "get") == 0)
        TSAPI_NOSQL_CHECK(sess.conn,
                          tapi_nosql_bulk_generate(sess.conn, sess.coll, 100,
                                                   size, "k", 1, NULL, NULL));

    TEST_STEP("200 %s operations of %d bytes", op, size);
    la.mode = op;
    la.coll = sess.coll;
    la.prefix = "k";
    la.keys = 100;
    la.size = size;
    la.threads = 1;
    la.ops = 200;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    snprintf(name, sizeof(name), "%s %s %d B", backend, op, size);
    tapi_nosql_load_mi(name, &lr);
    if (lr.errors != 0)
        TEST_VERDICT("Operations failed under a single client");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
