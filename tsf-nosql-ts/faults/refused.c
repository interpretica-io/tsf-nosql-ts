/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: nothing listening
 *
 * Connect to a port where nothing listens: the connection fails as a
 * connection error (not a timeout, not a crash), well before its 5 s
 * deadline. The time it took is charted.
 */

#define TE_TEST_NAME    "faults/refused"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess  sess = { 0 };
    tapi_nosql_conn  *conn = NULL;
    const char       *backend;
    json_t           *cfg = NULL;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    double            t0;
    double            chart[1];
    te_errno          orc;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s on a port where nothing listens", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_refused"));
    if (!tapi_nosql_supports(sess.pco, backend))
        TEST_SKIP("The agent has no %s driver", backend);
    cfg = tsapi_nosql_config(backend, TSAPI_NOSQL_ROLE_APP);
    json_object_set_new(cfg, "port", json_integer(1));
    json_object_set_new(cfg, "connect_timeout_ms", json_integer(5000));
    t0 = tsapi_nosql_now();
    orc = tapi_nosql_open(sess.pco, backend, cfg, &conn, &cls);
    chart[0] = tsapi_nosql_now() - t0;
    RING("Failed after %.3f s as %s", chart[0], tapi_nosql_errcls_name(cls));
    tsapi_nosql_chart("Time to a refused connection (s)", NULL, NULL,
                      "seconds", chart, 1);
    if (orc == 0)
        TEST_VERDICT("A connection to a closed port succeeded");
    if (cls != TAPI_NOSQL_ERR_CONNECT)
        TEST_VERDICT("A closed port did not give a connection error");
    if (chart[0] > 4.5)
        TEST_VERDICT("A refused connection took the whole deadline");
    TEST_SUCCESS;

cleanup:
    tapi_nosql_close(conn);
    json_decref(cfg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
