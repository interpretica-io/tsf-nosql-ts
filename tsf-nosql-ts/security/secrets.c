/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: secrets stay secret
 *
 * The password of a configuration must appear in nothing the module
 * returns. A wrong password carrying a marker: its error message does
 * not contain the marker. With the right one: the server's answer to
 * "info", the RPC server's registry and the audit report do not contain
 * the password.
 */

#define TE_TEST_NAME    "security/secrets"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_audit.h"
#include "tapi_nosql_rpc.h"
#include "tsapi_nosql.h"

#define MARKER  "SeCrEt-marker-77xq-tsf"

static bool
leaks(const json_t *v, const char *secret)
{
    char *s = tapi_nosql_json_text(v);
    bool  found = s != NULL && strstr(s, secret) != NULL;

    free(s);
    return found;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess  sess = { 0 };
    tapi_nosql_conn  *conn = NULL;
    tapi_nosql_report rep = TAPI_NOSQL_REPORT_INIT;
    const char       *target;
    const char       *engine;
    const char       *pw;
    json_t           *cfg = NULL;
    json_t           *info = NULL;
    json_t           *reg = NULL;
    double            places = 0;
    unsigned int      i;

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_secrets"));
    engine = tsapi_nosql_engine_of(target);
    if (!tapi_nosql_supports(sess.pco, engine))
        TEST_SKIP("The agent has no %s driver", engine);

    TEST_STEP("A wrong password's error does not show it");
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    json_object_set_new(cfg, "password", json_string(MARKER));
    {
        rpc_nosql_err err = { 0, 0, NULL };
        char         *text = tapi_nosql_json_text(cfg);
        char         *res = NULL;
        int           handle = 0;
        te_errno      orc;

        orc = rpc_nosql_open(sess.pco, engine, text, &handle, &res, &err);
        free(text);
        free(res);
        if (orc == 0)
        {
            rpc_nosql_err_free(&err);
            TEST_VERDICT("A wrong password was accepted");
        }
        RING("Refused as %s: %s", tapi_nosql_errcls_name(err.cls),
             err.msg != NULL ? err.msg : "");
        if (err.msg != NULL && strstr(err.msg, MARKER) != NULL)
        {
            rpc_nosql_err_free(&err);
            TEST_VERDICT("The error of a refused login shows the password");
        }
        rpc_nosql_err_free(&err);
    }
    json_decref(cfg);
    cfg = NULL;
    places++;

    TEST_STEP("Nothing the module returns shows the real password");
    tsapi_nosql_sess_open(&sess, target);
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    pw = json_string_value(json_object_get(cfg, "password"));
    if (leaks(sess.conn->info, pw))
        TEST_VERDICT("The connection's description holds the password");
    places++;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_info(sess.conn, &info));
    if (leaks(info, pw))
        TEST_VERDICT("The server's description holds the password");
    places++;
    CHECK_RC(tapi_nosql_registry(sess.pco, &reg));
    if (leaks(reg, pw))
        TEST_VERDICT("The registry holds the password");
    places++;
    CHECK_RC(tapi_nosql_audit(sess.pco, engine, cfg, true, &rep));
    for (i = 0; i < rep.n; i++)
    {
        if (strstr(rep.items[i].detail, pw) != NULL ||
            strstr(rep.items[i].subject, pw) != NULL)
            TEST_VERDICT("An audit finding holds the password");
    }
    places++;
    tsapi_nosql_chart("Places checked for the password", NULL, NULL,
                      "places", &places, 1);
    TEST_SUCCESS;

cleanup:
    tapi_nosql_report_free(&rep);
    tapi_nosql_close(conn);
    json_decref(cfg);
    json_decref(info);
    json_decref(reg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
