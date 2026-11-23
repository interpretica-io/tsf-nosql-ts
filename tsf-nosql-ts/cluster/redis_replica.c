/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief cluster group: a Redis replica
 *
 * Twenty writes on the primary, each read on the replica until it is
 * there (the lag is charted); WAIT on the primary confirms that one
 * replica has every write; a write on the replica is refused as a
 * permission failure (READONLY).
 */

#define TE_TEST_NAME    "cluster/redis_replica"

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
    tapi_nosql_conn *p = NULL;
    tapi_nosql_conn *r = NULL;
    json_t          *cmd = NULL;
    json_t          *reply = NULL;
    json_t          *d = NULL;
    double           lag[N];
    te_errno         wrc;
    int              i;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_redis_replica"));
    if (!tapi_nosql_supports(sess.pco, "redis"))
        TEST_SKIP("The agent has no redis driver");
    p = cl_open(sess.pco, "redis-primary", NULL);
    r = cl_open(sess.pco, "redis-replica", NULL);

    TEST_STEP("Write on the primary, read on the replica, %d times", N);
    for (i = 0; i < N; i++)
    {
        char   k[16];
        double t0;

        snprintf(k, sizeof(k), "r%d", i);
        d = json_pack("{s:i}", "i", i);
        TSAPI_NOSQL_CHECK(p, tapi_nosql_put(p, sess.coll, k, d, 0, NULL));
        json_decref(d);
        d = NULL;
        t0 = tsapi_nosql_now();
        for (;;)
        {
            json_t *got = NULL;

            TSAPI_NOSQL_CHECK(r, tapi_nosql_get(r, sess.coll, k, &got, NULL));
            if (got != NULL)
            {
                json_decref(got);
                break;
            }
            if (tsapi_nosql_now() - t0 > 5)
                TEST_VERDICT("A write did not reach the replica within 5 s");
            usleep(2000);
        }
        lag[i] = (tsapi_nosql_now() - t0) * 1000;
    }
    tsapi_nosql_chart("Time until the replica has the write (ms)", NULL,
                      NULL, "ms", lag, N);

    TEST_STEP("WAIT: one replica has every write");
    cmd = json_pack("{s:[s, s, s]}", "argv", "WAIT", "1", "2000");
    TSAPI_NOSQL_CHECK(p, tapi_nosql_native(p, cmd, &reply));
    if (json_integer_value(reply) < 1)
        TEST_VERDICT("WAIT did not confirm a replica");

    TEST_STEP("A write on the replica is refused");
    d = json_pack("{s:i}", "on", 1);
    wrc = tapi_nosql_put(r, sess.coll, "no", d, 0, NULL);
    if (wrc == 0)
        TEST_VERDICT("The replica took a write");
    if (tapi_nosql_last_cls(r) != TAPI_NOSQL_ERR_PERMISSION)
        TEST_VERDICT("A write on the replica was not a permission failure");
    TEST_SUCCESS;

cleanup:
    if (p != NULL)
        tapi_nosql_drop(p, sess.coll);
    json_decref(cmd);
    json_decref(reply);
    json_decref(d);
    tapi_nosql_close(p);
    tapi_nosql_close(r);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
