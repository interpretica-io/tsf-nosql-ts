/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: a network partition
 *
 * Drop everything the server sends (netem loss 100 %): the server keeps
 * running but nothing reaches the client. A read with a 2 s deadline
 * fails as a timeout, at the deadline; after the partition heals the
 * same handle works again. Unlike faults/timeout, the server is not
 * frozen: it may have answered, and the client must still not hang.
 */

#define TE_TEST_NAME    "faults/partition"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "net.h"

#define DEADLINE_MS 2000

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *doc = NULL;
    json_t          *got = NULL;
    bool             shaped = false;
    double           chart[2];
    double           t0;
    te_errno         grc;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    doc = json_pack("{s:i}", "a", 1);

    TEST_STEP("Connect to %s, write, set a %d ms deadline", backend,
              DEADLINE_MS);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_partition"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "k",
                                                doc, 0, NULL));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_set_timeout(sess.conn,
                                                        DEADLINE_MS));

    TEST_STEP("Partition the server and read");
    CHECK_RC(net_shape(sess.pco, backend, "loss 100%"));
    shaped = true;
    t0 = tsapi_nosql_now();
    grc = tapi_nosql_get(sess.conn, sess.coll, "k", &got, NULL);
    chart[0] = DEADLINE_MS / 1000.0;
    chart[1] = tsapi_nosql_now() - t0;
    RING("The read returned after %.2f s: %s: %s", chart[1],
         tapi_nosql_errcls_name(tapi_nosql_last_cls(sess.conn)),
         tapi_nosql_last_msg(sess.conn));
    tsapi_nosql_chart("Deadline and time to fail under a partition (s)", NULL,
                      NULL, "seconds", chart, 2);
    if (grc == 0)
        TEST_VERDICT("A read across a partition succeeded");
    if (tapi_nosql_last_cls(sess.conn) != TAPI_NOSQL_ERR_TIMEOUT)
        TEST_VERDICT("A partition did not give a timeout");
    if (chart[1] > DEADLINE_MS / 1000.0 + 5)
        TEST_VERDICT("The timeout came long after the deadline");

    TEST_STEP("Heal it: the handle works again");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "unnetem", backend));
    shaped = false;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_set_timeout(sess.conn, 30000));
    if (!net_recovers(sess.conn))
        TEST_VERDICT("The connection did not recover after the partition");
    TEST_SUCCESS;

cleanup:
    if (shaped)
        tsapi_nosql_servers(sess.pco, "unnetem", backend);
    json_decref(doc);
    json_decref(got);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
