/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: reading from a secondary
 *
 * Twenty times: write a document with majority acknowledgement through
 * one connection, then read it through another that reads from
 * secondaries only, until it is there. Every write reaches a secondary
 * within 5 s; the replication lag of each is charted.
 */

#define TE_TEST_NAME    "cluster/mongo_secondary"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "cluster.h"

#define N   20

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    tapi_nosql_conn *w = NULL;
    tapi_nosql_conn *r = NULL;
    json_t          *info = NULL;
    double           lag[N];
    int              i;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_mongo_secondary"));
    if (!tapi_nosql_supports(sess.pco, "mongodb"))
        TEST_SKIP("The agent has no mongodb driver");
    w = cl_open(sess.pco, "mongodb-rs3", NULL);
    r = cl_open(sess.pco, "mongodb-rs3",
                "{\"read_preference\":\"secondary\"}");

    TEST_STEP("Write on the primary, read on a secondary, %d times", N);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = json_pack("{s:i}", "i", i);
        double  t0;
        int     tries;

        snprintf(k, sizeof(k), "s%d", i);
        TSAPI_NOSQL_CHECK(w, tapi_nosql_put(w, sess.coll, k, d, 0, NULL));
        t0 = tsapi_nosql_now();
        for (tries = 0; tries < 500; tries++)
        {
            json_t *got = NULL;

            TSAPI_NOSQL_CHECK(r, tapi_nosql_get(r, sess.coll, k, &got, NULL));
            if (got != NULL)
            {
                json_decref(got);
                break;
            }
            if (tsapi_nosql_now() - t0 > 5)
                break;
            usleep(10000);
        }
        lag[i] = (tsapi_nosql_now() - t0) * 1000;
        json_decref(d);
        if (lag[i] > 5000)
            TEST_VERDICT("A majority write did not reach a secondary within "
                         "5 s");
    }
    tsapi_nosql_chart("Time until a secondary has the write (ms)", NULL, NULL,
                      "ms", lag, N);
    TEST_SUCCESS;

cleanup:
    if (w != NULL)
        tapi_nosql_drop(w, sess.coll);
    json_decref(info);
    tapi_nosql_close(w);
    tapi_nosql_close(r);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
