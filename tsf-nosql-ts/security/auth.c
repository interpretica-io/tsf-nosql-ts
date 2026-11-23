/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: authentication
 *
 * On a server that requires a login: a wrong password is refused as an
 * authentication failure; no credentials at all are refused too -
 * either at connection or at the first read, depending on the engine
 * (MongoDB lets an anonymous client ping); the right credentials read.
 */

#define TE_TEST_NAME    "security/auth"

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
    const char       *engine;
    json_t           *cfg = NULL;
    json_t           *got = NULL;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    double            chart[3] = { 0, 0, 0 };
    te_errno          orc;

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_auth"));
    engine = tsapi_nosql_engine_of(target);
    if (!tapi_nosql_supports(sess.pco, engine))
        TEST_SKIP("The agent has no %s driver", engine);

    TEST_STEP("A wrong password is refused");
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    json_object_set_new(cfg, "password", json_string("not-the-password"));
    orc = tapi_nosql_open(sess.pco, engine, cfg, &conn, &cls);
    if (orc == 0)
        TEST_VERDICT("A wrong password was accepted");
    if (cls != TAPI_NOSQL_ERR_AUTH)
        TEST_VERDICT("A wrong password was not an authentication failure");
    chart[0] = 1;
    json_decref(cfg);

    TEST_STEP("No credentials are refused");
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_NONE);
    orc = tapi_nosql_open(sess.pco, engine, cfg, &conn, &cls);
    if (orc == 0)
    {
        te_errno grc = tapi_nosql_get(conn, "tsfauth", "probe", &got, NULL);

        if (grc == 0)
            TEST_VERDICT("A client with no credentials could read");
        cls = tapi_nosql_last_cls(conn);
        tapi_nosql_close(conn);
        conn = NULL;
    }
    if (cls != TAPI_NOSQL_ERR_AUTH && cls != TAPI_NOSQL_ERR_PERMISSION)
        TEST_VERDICT("No credentials were refused for another reason");
    chart[1] = 1;
    json_decref(cfg);

    TEST_STEP("The right credentials read");
    tsapi_nosql_sess_open(&sess, target);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll, "probe",
                                                &got, NULL));
    chart[2] = 1;
    cfg = NULL;
    tsapi_nosql_chart("Refused: wrong password, no credentials; accepted: "
                      "right ones", NULL, NULL, "as expected", chart, 3);
    TEST_SUCCESS;

cleanup:
    tapi_nosql_close(conn);
    json_decref(got);
    json_decref(cfg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
