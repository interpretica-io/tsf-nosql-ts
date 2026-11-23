/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: connection lifecycle
 *
 * Open a connection, ask the server what it is, ping it twenty times
 * (charted), see the connection in the RPC server's registry, close it,
 * and check that its handle is refused afterwards - the agent never
 * mistakes a closed handle for a live one.
 */

#define TE_TEST_NAME    "basic/lifecycle"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_rpc.h"
#include "tsapi_nosql.h"

#define PINGS 20

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *info = NULL;
    json_t          *reg = NULL;
    double           rtt[PINGS];
    long long        open_before;
    int              handle;
    rpc_nosql_err    err = { 0, 0, NULL };
    char            *out = NULL;
    unsigned int     i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_lifecycle"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("The server says what it is");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_info(sess.conn, &info));
    if (json_string_length(json_object_get(info, "server_version")) == 0)
        TEST_VERDICT("The server reported no version");
    if (strcmp(json_string_value(json_object_get(info, "engine")),
               tsapi_nosql_engine_of(backend)) != 0)
        TEST_VERDICT("The connection reports another engine");
    RING("Server %s %s, client %s", backend,
         json_string_value(json_object_get(info, "server_version")),
         json_string_value(json_object_get(info, "client")));

    TEST_STEP("Ping it %d times", PINGS);
    for (i = 0; i < PINGS; i++)
    {
        long long us = 0;

        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_ping(sess.conn, &us));
        rtt[i] = (double)us;
    }
    tsapi_nosql_chart("Ping round trip (us)", NULL, NULL, "rtt_us", rtt,
                      PINGS);

    TEST_STEP("The RPC server holds the connection");
    CHECK_RC(tapi_nosql_registry(sess.pco, &reg));
    open_before = json_integer_value(json_object_get(reg, "open"));
    if (open_before < 1)
        TEST_VERDICT("The registry does not list the open connection");
    json_decref(reg);
    reg = NULL;

    TEST_STEP("Close it; its handle is refused afterwards");
    handle = sess.conn->handle;
    CHECK_RC(tapi_nosql_close(sess.conn));
    sess.conn = NULL;
    CHECK_RC(tapi_nosql_registry(sess.pco, &reg));
    if (json_integer_value(json_object_get(reg, "open")) != open_before - 1)
        TEST_VERDICT("Closing did not remove the connection");
    rc = rpc_nosql_call(sess.pco, handle, "ping", "{}", &out, &err);
    if (rc == 0 || err.cls != TAPI_NOSQL_ERR_HANDLE)
        TEST_VERDICT("A closed handle is still accepted");

    TEST_STEP("A new connection works");
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_ping(sess.conn, NULL));

    TEST_SUCCESS;

cleanup:
    rpc_nosql_err_free(&err);
    free(out);
    json_decref(info);
    json_decref(reg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
