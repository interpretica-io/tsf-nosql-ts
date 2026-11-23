/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: a server that stops answering
 *
 * Set a 2 s deadline, freeze the server's container (docker pause: the
 * connection stays open, nothing answers), and read: the read fails as
 * a timeout, after the deadline and not much later - not a hang, not a
 * connection error. Then thaw it: the same handle works again (the
 * driver reconnects). The time the failure took is charted against the
 * deadline. The container is thawed whatever happens.
 */

#define TE_TEST_NAME    "faults/timeout"

#include "te_config.h"
#include "tapi_test.h"

#include <unistd.h>

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

#define DEADLINE_MS 2000

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *doc = NULL;
    json_t          *got = NULL;
    bool             paused = false;
    double           t0;
    double           chart[2];
    te_errno         grc;
    int              tries;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    doc = json_pack("{s:i}", "a", 1);

    TEST_STEP("Connect to %s, write, set a %d ms deadline", backend,
              DEADLINE_MS);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_timeout"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "k",
                                                doc, 0, NULL));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_set_timeout(sess.conn,
                                                        DEADLINE_MS));

    TEST_STEP("Freeze the server and read");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "pause", backend));
    paused = true;
    t0 = tsapi_nosql_now();
    grc = tapi_nosql_get(sess.conn, sess.coll, "k", &got, NULL);
    chart[0] = DEADLINE_MS / 1000.0;
    chart[1] = tsapi_nosql_now() - t0;
    RING("The read returned after %.2f s: %r (%s: %s)", chart[1], grc,
         tapi_nosql_errcls_name(tapi_nosql_last_cls(sess.conn)),
         tapi_nosql_last_msg(sess.conn));
    tsapi_nosql_chart("Deadline and time to fail (s)", NULL, NULL, "seconds",
                      chart, 2);
    if (grc == 0)
        TEST_VERDICT("A read from a frozen server succeeded");
    if (tapi_nosql_last_cls(sess.conn) != TAPI_NOSQL_ERR_TIMEOUT)
        TEST_VERDICT("A frozen server did not give a timeout");
    if (chart[1] < DEADLINE_MS / 1000.0 * 0.9)
        TEST_VERDICT("The timeout came before the deadline");
    if (chart[1] > DEADLINE_MS / 1000.0 + 5)
        TEST_VERDICT("The timeout came long after the deadline");

    TEST_STEP("Thaw it: the same handle works again");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "unpause", backend));
    paused = false;
    for (tries = 0; tries < 60; tries++)
    {
        json_decref(got);
        got = NULL;
        if (tapi_nosql_get(sess.conn, sess.coll, "k", &got, NULL) == 0 &&
            got != NULL)
            break;
        sleep(1);
    }
    if (got == NULL)
        TEST_VERDICT("The connection did not recover after the server "
                     "answered again");
    RING("Recovered after %d retries", tries);
    TEST_SUCCESS;

cleanup:
    if (paused)
        tsapi_nosql_servers(sess.pco, "unpause", backend);
    json_decref(doc);
    json_decref(got);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
