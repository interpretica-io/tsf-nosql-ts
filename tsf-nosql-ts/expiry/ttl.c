/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief expiry group: an entry with a TTL
 *
 * Store an entry that expires in @p ttl_s seconds: it is there at once,
 * still there at half its life (for a TTL of 3 s), and gone within its
 * TTL plus the engine's slack - MongoDB removes expired documents in a
 * background pass (once a second in this suite's server,
 * ttlMonitorSleepSecs=1), the others at or near the deadline. Memcached
 * counts in whole seconds, so a 1 s entry may live anywhere up to 1 s.
 * The time it was seen gone is charted against the TTL.
 */

#define TE_TEST_NAME    "expiry/ttl"

#include "te_config.h"
#include "tapi_test.h"

#include <unistd.h>

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    int              ttl_s;
    json_t          *doc = NULL;
    json_t          *got = NULL;
    double           t0;
    double           put_at;
    int              slack_ms;
    int              gone_ms = 0;
    double           chart[2];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(ttl_s);
    doc = json_pack("{s:s}", "expires", "soon");
    slack_ms = strcmp(backend, "mongodb") == 0 ? 5000 : 2500;

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_ttl"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "ttl");

    TEST_STEP("Store an entry that expires in %d s", ttl_s);
    t0 = tsapi_nosql_now();
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "ttl",
                                                doc, ttl_s, NULL));
    put_at = tsapi_nosql_now();
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll, "ttl",
                                                &got, NULL));
    if (got == NULL)
    {
        if (strcmp(backend, "memcached") == 0 && ttl_s == 1)
            RING("Memcached's second ticked over between write and read");
        else
            TEST_VERDICT("An entry with a TTL is gone at once");
    }
    json_decref(got);
    got = NULL;

    if (ttl_s >= 3)
    {
        TEST_STEP("Still there at half its life");
        {
            double left = ttl_s * 0.5 - (tsapi_nosql_now() - put_at);

            if (left > 0)
                usleep((useconds_t)(left * 1e6));
        }
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll,
                                                    "ttl", &got, NULL));
        if (got == NULL)
            TEST_VERDICT("An entry expired before half its TTL");
        json_decref(got);
        got = NULL;
    }

    TEST_STEP("Gone within the TTL and the engine's slack");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_eventually_absent(
                           sess.conn, sess.coll, "ttl",
                           ttl_s * 1000 + slack_ms, 100, &gone_ms));
    chart[0] = ttl_s;
    chart[1] = tsapi_nosql_now() - t0;
    RING("A %d s entry was seen gone after %.2f s", ttl_s, chart[1]);
    tsapi_nosql_chart("TTL and the time the entry was seen gone (s)", NULL,
                      NULL, "seconds", chart, 2);
    TEST_SUCCESS;

cleanup:
    json_decref(doc);
    json_decref(got);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
