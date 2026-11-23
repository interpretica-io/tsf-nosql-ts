/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: a lossy network
 *
 * Drop 30 % of what the server sends (netem): TCP retransmits, so
 * every one of 40 reads still succeeds - slower in the tail, never
 * wrong, never an error. The latency percentiles are charted.
 */

#define TE_TEST_NAME    "faults/loss"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"
#include "net.h"

#define N   40

static int
cmp(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return x < y ? -1 : x > y;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *doc = NULL;
    bool             shaped = false;
    double           ms[N];
    double           pct[3];
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    doc = json_pack("{s:s}", "through", "loss");

    TEST_STEP("Connect to %s and write a document", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_loss"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "k",
                                                doc, 0, NULL));

    TEST_STEP("Drop 30 %% of the server's packets; read %d times", N);
    CHECK_RC(net_shape(sess.pco, backend, "loss 30%"));
    shaped = true;
    for (i = 0; i < N; i++)
    {
        double t0 = tsapi_nosql_now();

        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "k",
                                                 doc));
        ms[i] = (tsapi_nosql_now() - t0) * 1000;
    }
    CHECK_RC(tsapi_nosql_servers(sess.pco, "unnetem", backend));
    shaped = false;
    qsort(ms, N, sizeof(ms[0]), cmp);
    pct[0] = ms[N / 2];
    pct[1] = ms[N * 9 / 10];
    pct[2] = ms[N - 1];
    RING("Read latency under 30%% loss: p50 %.1f ms, p90 %.1f ms, max %.1f ms",
         pct[0], pct[1], pct[2]);
    tsapi_nosql_chart("Read latency under 30% loss (ms): p50, p90, max",
                      NULL, NULL, "ms", pct, 3);
    TEST_SUCCESS;

cleanup:
    if (shaped)
        tsapi_nosql_servers(sess.pco, "unnetem", backend);
    json_decref(doc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
