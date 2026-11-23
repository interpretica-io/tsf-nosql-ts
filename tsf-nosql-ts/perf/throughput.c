/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief perf group: throughput of concurrent clients
 *
 * @p threads agent threads, each with a connection of its own, write
 * 600 documents of 256 bytes between them over 500 keys. Throughput
 * and the latency percentiles go out as MI; no write may fail.
 */

#define TE_TEST_NAME    "perf/throughput"

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
    int                    threads;
    char                   name[96];
    double                 tp;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(threads);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_throughput"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("%d threads write 600 documents", threads);
    la.mode = "put";
    la.coll = sess.coll;
    la.prefix = "k";
    la.keys = 500;
    la.size = 256;
    la.threads = threads;
    la.ops = 600 / threads;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_load(sess.conn, &la, &lr));
    snprintf(name, sizeof(name), "%s put x%d", backend, threads);
    tapi_nosql_load_mi(name, &lr);
    tp = lr.throughput;
    tsapi_nosql_chart("Throughput (operations per second)", NULL, NULL,
                      "ops_per_s", &tp, 1);
    if (lr.errors != 0)
        TEST_VERDICT("Writes failed under concurrent clients");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_load_result_free(&lr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
