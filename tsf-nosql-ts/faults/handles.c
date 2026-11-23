/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief faults group: handles belong to their RPC server
 *
 * Open a connection in an RPC server, restart that server: the old
 * handle is refused as unknown (the handles carry a random per-process
 * generation, so a new connection in the new process cannot be mistaken
 * for the old one), and the registry is empty.
 */

#define TE_TEST_NAME    "faults/handles"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_rpc.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *reg = NULL;
    rpc_nosql_err    err = { 0, 0, NULL };
    char            *out = NULL;
    int              handle;
    double           chart[2];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_handles"));
    tsapi_nosql_sess_open(&sess, backend);
    handle = sess.conn->handle;
    CHECK_RC(tapi_nosql_registry(sess.pco, &reg));
    chart[0] = (double)json_integer_value(json_object_get(reg, "open"));
    json_decref(reg);
    reg = NULL;

    TEST_STEP("Restart the RPC server");
    CHECK_RC(rcf_rpc_server_restart(sess.pco));

    TEST_STEP("The old handle is refused; nothing is open");
    rc = rpc_nosql_call(sess.pco, handle, "ping", "{}", &out, &err);
    if (rc == 0 || err.cls != TAPI_NOSQL_ERR_HANDLE)
        TEST_VERDICT("A handle of a restarted RPC server is accepted");
    CHECK_RC(tapi_nosql_registry(sess.pco, &reg));
    chart[1] = (double)json_integer_value(json_object_get(reg, "open"));
    tsapi_nosql_chart("Open connections before and after the restart", NULL,
                      NULL, "open", chart, 2);
    if (chart[1] != 0)
        TEST_VERDICT("A restarted RPC server holds connections");
    /* The connection object is stale: free it without the RPC. */
    json_decref(sess.conn->info);
    free(sess.conn->engine);
    free(sess.conn->last_msg);
    free(sess.conn);
    sess.conn = NULL;
    TEST_SUCCESS;

cleanup:
    rpc_nosql_err_free(&err);
    free(out);
    json_decref(reg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
