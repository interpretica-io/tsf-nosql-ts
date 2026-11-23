/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: privilege separation
 *
 * A command that only an administrator should run, read-only so the
 * test changes nothing:
 *  - Redis: CONFIG GET maxmemory (the app user has -@dangerous);
 *  - MongoDB: usersInfo on admin (the app user has readWrite on tsf);
 *  - Elasticsearch: GET /_security/user;
 *  - etcd: auth/user/list.
 * The application account is refused; the administrator is not.
 */

#define TE_TEST_NAME    "security/privilege"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

/* 1 if the account may run the target's administrative command. */
static int
allowed(tsapi_nosql_sess *sess, const char *target, tsapi_nosql_role role)
{
    const char       *engine = tsapi_nosql_engine_of(target);
    tapi_nosql_conn  *conn = NULL;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    json_t           *cfg = tsapi_nosql_config(target, role);
    json_t           *cmd;
    json_t           *reply = NULL;
    te_errno          rc;
    int               ok;

    rc = tapi_nosql_open(sess->pco, engine, cfg, &conn, &cls);
    json_decref(cfg);
    if (rc != 0)
        TEST_FAIL("Cannot connect to %s: %s", target,
                  tapi_nosql_errcls_name(cls));
    if (strcmp(engine, "redis") == 0)
        cmd = json_pack("{s:[s, s, s]}", "argv", "CONFIG", "GET", "maxmemory");
    else if (strcmp(engine, "mongodb") == 0)
        cmd = json_pack("{s:s, s:{s:i}}", "db", "admin", "command",
                        "usersInfo", 1);
    else if (strcmp(engine, "elasticsearch") == 0)
        cmd = json_pack("{s:s, s:s}", "method", "GET", "path",
                        "/_security/user");
    else
        cmd = json_pack("{s:s, s:s, s:{}}", "method", "POST", "path",
                        "/v3/auth/user/list", "body");
    rc = tapi_nosql_native(conn, cmd, &reply);
    json_decref(cmd);
    if (rc != 0)
    {
        RING("%s refused: %s: %s", target,
             tapi_nosql_errcls_name(tapi_nosql_last_cls(conn)),
             tapi_nosql_last_msg(conn));
        ok = 0;
        if (tapi_nosql_last_cls(conn) != TAPI_NOSQL_ERR_PERMISSION &&
            tapi_nosql_last_cls(conn) != TAPI_NOSQL_ERR_AUTH)
            TEST_FAIL("The command failed for another reason: %s",
                      tapi_nosql_last_msg(conn));
    }
    else if (json_object_get(reply, "status") != NULL)
    {
        /* An HTTP engine answers with a status. */
        long long st = json_integer_value(json_object_get(reply, "status"));

        RING("%s answered HTTP %lld", target, st);
        ok = st == 200;
    }
    else
    {
        ok = 1;
    }
    json_decref(reply);
    tapi_nosql_close(conn);
    return ok;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *target;
    double           chart[2];

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_privilege"));
    if (!tapi_nosql_supports(sess.pco, tsapi_nosql_engine_of(target)))
        TEST_SKIP("The agent has no %s driver",
                  tsapi_nosql_engine_of(target));

    TEST_STEP("The application account is refused");
    chart[0] = allowed(&sess, target, TSAPI_NOSQL_ROLE_APP);
    TEST_STEP("The administrator is not");
    chart[1] = allowed(&sess, target, TSAPI_NOSQL_ROLE_ADMIN);
    tsapi_nosql_chart("Administrative command allowed: application, "
                      "administrator", NULL, NULL, "allowed", chart, 2);
    if (chart[0] != 0)
        TEST_VERDICT("The application account can administer the server");
    if (chart[1] != 1)
        TEST_VERDICT("The administrator cannot administer the server");
    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
