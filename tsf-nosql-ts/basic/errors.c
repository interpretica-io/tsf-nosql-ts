/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: bad requests
 *
 * A collection name that is not a lower-case identifier, a document
 * field the module reserves, a document that is not an object, a key
 * over the limit, an unknown operation, malformed arguments and a page
 * over the limit: each fails with its class (invalid, or limit) before
 * reaching the server, and the connection works afterwards.
 */

#define TE_TEST_NAME    "basic/errors"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tapi_nosql_rpc.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *doc = NULL;
    json_t          *bad = NULL;
    json_t          *arr = NULL;
    rpc_nosql_err    err = { 0, 0, NULL };
    char            *out = NULL;
    char             longkey[202];
    double           classes[TAPI_NOSQL_ERR_RPC + 1] = { 0 };

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    memset(longkey, 'k', 201);
    longkey[201] = '\0';
    doc = json_pack("{s:i}", "a", 1);
    bad = json_pack("{s:i}", "_id", 1);
    arr = json_pack("[i, i]", 1, 2);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_errors"));
    tsapi_nosql_sess_open(&sess, backend);

#define EXPECT_FAIL(_what, _cls, _expr) \
    do {                                                                \
        te_errno _r = (_expr);                                          \
                                                                        \
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_failed_as(sess.conn, _r,  \
                                                       (_cls), _what)); \
        classes[_cls]++;                                                \
    } while (0)

    TEST_STEP("Each bad request fails with its class");
    EXPECT_FAIL("A bad collection name", TAPI_NOSQL_ERR_INVALID,
                tapi_nosql_put(sess.conn, "Bad-Name", "k", doc, 0, NULL));
    EXPECT_FAIL("A reserved field", TAPI_NOSQL_ERR_INVALID,
                tapi_nosql_put(sess.conn, sess.coll, "k", bad, 0, NULL));
    EXPECT_FAIL("A document that is an array", TAPI_NOSQL_ERR_INVALID,
                tapi_nosql_put(sess.conn, sess.coll, "k", arr, 0, NULL));
    EXPECT_FAIL("A key over 200 bytes", TAPI_NOSQL_ERR_INVALID,
                tapi_nosql_put(sess.conn, sess.coll, longkey, doc, 0, NULL));
    EXPECT_FAIL("An unknown operation", TAPI_NOSQL_ERR_INVALID,
                tapi_nosql_call(sess.conn, "frobnicate", NULL, NULL));
    if (tapi_nosql_has_cap(sess.conn, "scan"))
        EXPECT_FAIL("A page over the limit", TAPI_NOSQL_ERR_LIMIT,
                    tapi_nosql_callf(sess.conn, "scan", NULL, "{s:s, s:i}",
                                     "coll", sess.coll, "limit", 100000));

    TEST_STEP("Malformed arguments are refused by the agent");
    rc = rpc_nosql_call(sess.pco, sess.conn->handle, "get", "{not json", &out,
                        &err);
    if (rc == 0 || err.cls != TAPI_NOSQL_ERR_INVALID)
        TEST_VERDICT("Malformed arguments were not refused as invalid");
    classes[TAPI_NOSQL_ERR_INVALID]++;

    TEST_STEP("The connection still works");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_ping(sess.conn, NULL));
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll, "k"));

    tsapi_nosql_chart("Refusals by error class", NULL, NULL, "refusals",
                      classes, TAPI_NOSQL_ERR_RPC + 1);
    TEST_SUCCESS;

cleanup:
    rpc_nosql_err_free(&err);
    free(out);
    json_decref(doc);
    json_decref(bad);
    json_decref(arr);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
