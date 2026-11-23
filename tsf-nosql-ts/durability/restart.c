/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief durability group: a server restart
 *
 * Write 20 documents and a counter, restart the server's container,
 * connect again: a durable server has every document and the counter
 * (Redis here runs with an append-only file fsynced per write); a
 * memory-only server - Memcached - has none of them, which is its
 * documented behaviour and what this checks for it. The restart time
 * is charted.
 */

#define TE_TEST_NAME    "durability/restart"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

#define N   20

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    char             coll[32];
    bool             durable;
    bool             counter;
    double           t0;
    double           chart[2];
    unsigned int     found = 0;
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s and write %d documents and a counter", backend,
              N);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_restart"));
    tsapi_nosql_sess_open(&sess, backend);
    snprintf(coll, sizeof(coll), "%s", sess.coll);
    durable = tapi_nosql_has_cap(sess.conn, "durable");
    counter = tapi_nosql_has_cap(sess.conn, "incr");
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = json_pack("{s:i, s:s}", "i", i, "kept", "yes");

        snprintf(k, sizeof(k), "d%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, coll, k, d, 0,
                                                    NULL));
        json_decref(d);
    }
    if (counter)
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_incr(sess.conn, coll, "ctr",
                                                     7, NULL));

    TEST_STEP("Restart the server");
    tapi_nosql_close(sess.conn);
    sess.conn = NULL;
    t0 = tsapi_nosql_now();
    CHECK_RC(tsapi_nosql_servers(sess.pco, "restart", backend));
    chart[0] = tsapi_nosql_now() - t0;
    RING("The server was back after %.1f s", chart[0]);

    TEST_STEP("Connect again and look");
    tsapi_nosql_sess_open(&sess, backend);
    snprintf(sess.coll, sizeof(sess.coll), "%s", coll);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = NULL;

        snprintf(k, sizeof(k), "d%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, coll, k, &d,
                                                    NULL));
        if (d != NULL)
            found++;
        json_decref(d);
    }
    chart[1] = found;
    tsapi_nosql_chart("Restart time (s) and documents found of 20", NULL,
                      NULL, "value", chart, 2);
    if (durable)
    {
        if (found != N)
            TEST_VERDICT("A durable server lost documents over a restart");
        if (counter)
            TSAPI_NOSQL_ASSERT(tapi_nosql_assert_counter(sess.conn, coll,
                                                         "ctr", 7));
    }
    else if (found != 0)
    {
        TEST_VERDICT("A memory-only server kept documents over a restart");
    }
    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
