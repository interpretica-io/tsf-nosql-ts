/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: added network delay
 *
 * Delay what the server sends by @p delay_ms (netem): the median round
 * trip grows by about that much; a deadline of half the delay times out
 * (class timeout), a generous one does not; with the delay removed the
 * same handle is as fast as before. Round trips are charted.
 */

#define TE_TEST_NAME    "faults/latency"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"
#include "net.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    int              delay_ms;
    bool             shaped = false;
    char             how[32];
    double           rtt[3];
    te_errno         prc;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(delay_ms);

    TEST_STEP("Connect to %s; round trip before", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_latency_net"));
    tsapi_nosql_sess_open(&sess, backend);
    rtt[0] = net_rtt(sess.conn, 9);
    if (rtt[0] < 0)
        TEST_FAIL("A ping failed before any delay");

    TEST_STEP("Delay the server's traffic by %d ms", delay_ms);
    snprintf(how, sizeof(how), "delay %dms", delay_ms);
    CHECK_RC(net_shape(sess.pco, backend, how));
    shaped = true;
    rtt[1] = net_rtt(sess.conn, 9);
    RING("Median round trip %.0f us before, %.0f us with %d ms added",
         rtt[0], rtt[1], delay_ms);
    if (rtt[1] < 0)
        TEST_VERDICT("A ping failed under a delay well inside the deadline");
    if (rtt[1] < rtt[0] + delay_ms * 1000 * 0.8)
        TEST_VERDICT("The added delay does not show in the round trip");

    TEST_STEP("A deadline of half the delay times out");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_set_timeout(sess.conn,
                                                        delay_ms / 2));
    prc = tapi_nosql_ping(sess.conn, NULL);
    if (prc == 0)
        TEST_VERDICT("An operation beat a deadline shorter than the delay");
    if (tapi_nosql_last_cls(sess.conn) != TAPI_NOSQL_ERR_TIMEOUT)
        TEST_VERDICT("A deadline shorter than the delay was not a timeout");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_set_timeout(sess.conn, 30000));

    TEST_STEP("Remove the delay: the handle is fast again");
    CHECK_RC(tsapi_nosql_servers(sess.pco, "unnetem", backend));
    shaped = false;
    if (!net_recovers(sess.conn))
        TEST_VERDICT("The connection did not recover after the delay");
    rtt[2] = net_rtt(sess.conn, 9);
    tsapi_nosql_chart("Median round trip (us): before, delayed, after", NULL,
                      NULL, "rtt_us", rtt, 3);
    if (rtt[2] < 0 || rtt[2] > rtt[0] + delay_ms * 1000 * 0.5)
        TEST_VERDICT("The round trip did not come back after the delay was "
                     "removed");
    TEST_SUCCESS;

cleanup:
    if (shaped)
        tsapi_nosql_servers(sess.pco, "unnetem", backend);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
