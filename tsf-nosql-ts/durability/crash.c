/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief durability group: a crash
 *
 * Write 20 documents, each acknowledged, then SIGKILL the server - no
 * shutdown, no flush - and start it again. A durable server
 * (@p expect = kept) has every acknowledged write: Redis with an
 * append-only file, MongoDB's journal, Cassandra's commit log,
 * Elasticsearch's translog, Neo4j's and etcd's logs. Two are the
 * known-bad controls (@p expect = lost) and must lose them, or the test
 * could not see a loss: Redis with no append-only file (redis-weak:
 * only its periodic snapshot, and none was due) and Memcached, which
 * keeps nothing.
 */

#define TE_TEST_NAME    "durability/crash"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

#define N   20

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *target;
    const char      *expect;
    char             coll[32];
    double           t0;
    double           chart[2];
    unsigned int     found = 0;
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    TEST_GET_STRING_PARAM(expect);

    TEST_STEP("Connect to %s and write %d documents", target, N);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_crash"));
    tsapi_nosql_sess_open(&sess, target);
    snprintf(coll, sizeof(coll), "%s", sess.coll);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = json_pack("{s:i}", "i", i);

        snprintf(k, sizeof(k), "c%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, coll, k, d, 0,
                                                    NULL));
        json_decref(d);
    }

    TEST_STEP("SIGKILL the server and start it again");
    tapi_nosql_close(sess.conn);
    sess.conn = NULL;
    t0 = tsapi_nosql_now();
    CHECK_RC(tsapi_nosql_servers(sess.pco, "kill", target));
    chart[0] = tsapi_nosql_now() - t0;

    TEST_STEP("Count what is left");
    tsapi_nosql_sess_open(&sess, target);
    snprintf(sess.coll, sizeof(sess.coll), "%s", coll);
    for (i = 0; i < N; i++)
    {
        char    k[16];
        json_t *d = NULL;

        snprintf(k, sizeof(k), "c%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, coll, k, &d,
                                                    NULL));
        if (d != NULL)
            found++;
        json_decref(d);
    }
    chart[1] = found;
    RING("%u of %d acknowledged writes survived; back after %.1f s", found, N,
         chart[0]);
    tsapi_nosql_chart("Recovery time (s) and writes found of 20 after "
                      "SIGKILL", NULL, NULL, "value", chart, 2);
    if (strcmp(expect, "kept") == 0 && found != N)
        TEST_VERDICT("A durable server lost acknowledged writes in a crash");
    if (strcmp(expect, "lost") == 0 && found != 0)
        TEST_VERDICT("A server that keeps nothing on disk kept writes over a "
                     "crash: the control did not bite");
    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
