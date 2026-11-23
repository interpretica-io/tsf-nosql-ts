/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: TLS
 *
 * The TLS-only servers, with the CA of @p ca:
 *  - good: the suite's CA - the connection is up and reports TLS;
 *  - rogue: an unrelated CA - refused as a TLS failure, so a server
 *    whose certificate cannot be verified is never talked to;
 *  - plain: no TLS at all - refused, and not as a login failure (a
 *    plaintext client never reaches authentication).
 * The time to an answer is charted.
 */

#define TE_TEST_NAME    "security/tls"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess  sess = { 0 };
    tapi_nosql_conn  *conn = NULL;
    const char       *target;
    const char       *ca;
    const char       *engine;
    json_t           *cfg = NULL;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    double            t0;
    double            chart[1];
    te_errno          orc;

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    TEST_GET_STRING_PARAM(ca);

    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_tls"));
    engine = tsapi_nosql_engine_of(target);
    if (!tapi_nosql_supports(sess.pco, engine))
        TEST_SKIP("The agent has no %s driver", engine);
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    if (strcmp(ca, "rogue") == 0)
        json_object_set_new(cfg, "tls", json_pack("{s:b, s:s}", "enabled", 1,
                                                  "ca", TSAPI_NOSQL_WORKDIR
                                                  "/rogue-ca.crt"));
    else if (strcmp(ca, "plain") == 0)
        json_object_set_new(cfg, "tls", json_false());

    TEST_STEP("Connect to %s with the %s CA", target, ca);
    t0 = tsapi_nosql_now();
    orc = tapi_nosql_open(sess.pco, engine, cfg, &conn, &cls);
    chart[0] = tsapi_nosql_now() - t0;
    RING("%s after %.3f s", orc == 0 ? "Connected" :
         tapi_nosql_errcls_name(cls), chart[0]);
    tsapi_nosql_chart("Time to the answer (s)", NULL, NULL, "seconds", chart,
                      1);
    if (strcmp(ca, "good") == 0)
    {
        if (orc != 0)
            TEST_VERDICT("The suite's CA was not accepted");
        if (!tapi_nosql_tls(conn))
            TEST_VERDICT("The connection does not report TLS");
        TSAPI_NOSQL_CHECK(conn, tapi_nosql_ping(conn, NULL));
    }
    else if (strcmp(ca, "rogue") == 0)
    {
        if (orc == 0)
            TEST_VERDICT("A server was trusted under an unrelated CA");
        if (cls != TAPI_NOSQL_ERR_TLS)
            TEST_VERDICT("An untrusted certificate was not a TLS failure");
    }
    else
    {
        if (orc == 0)
            TEST_VERDICT("A TLS-only server took a plaintext client");
        if (cls == TAPI_NOSQL_ERR_AUTH)
            TEST_VERDICT("A plaintext client reached authentication");
    }
    TEST_SUCCESS;

cleanup:
    tapi_nosql_close(conn);
    json_decref(cfg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
