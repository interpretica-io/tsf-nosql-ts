/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: compare-and-set
 *
 * Read a document and its version; a write conditioned on that version
 * applies; the same condition again (now stale) does not, and the
 * document is the one the applied write made; the version changed; a
 * condition on a deleted key does not apply either.
 */

#define TE_TEST_NAME    "atomic/cas"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *d1 = NULL;
    json_t          *d2 = NULL;
    json_t          *d3 = NULL;
    json_t          *got = NULL;
    char            *v1 = NULL;
    char            *v2 = NULL;
    bool             applied = false;
    double           chart[3];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    d1 = json_pack("{s:i}", "n", 1);
    d2 = json_pack("{s:i}", "n", 2);
    d3 = json_pack("{s:i}", "n", 3);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_cas"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "cas");

    TEST_STEP("Write and read the version");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "cas",
                                                d1, 0, NULL));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll, "cas",
                                                &got, &v1));
    if (v1 == NULL)
        TEST_VERDICT("A compare-and-set engine returned no version");
    RING("Version: %s", v1);

    TEST_STEP("A write on the current version applies");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_version(sess.conn, sess.coll, "cas",
                                                d2, v1, &applied, NULL));
    chart[0] = applied;
    if (!applied)
        TEST_VERDICT("Compare-and-set on the current version did not apply");

    TEST_STEP("The same, now stale, does not");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_version(sess.conn, sess.coll, "cas",
                                                d3, v1, &applied, NULL));
    chart[1] = applied;
    if (applied)
        TEST_VERDICT("Compare-and-set on a stale version applied");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "cas", d2));
    json_decref(got);
    got = NULL;
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_get(sess.conn, sess.coll, "cas",
                                                &got, &v2));
    if (v2 == NULL || strcmp(v1, v2) == 0)
        TEST_VERDICT("The version did not change on a write");

    TEST_STEP("A condition on a deleted key does not apply");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_delete(sess.conn, sess.coll,
                                                   "cas", NULL));
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_version(sess.conn, sess.coll, "cas",
                                                d3, v2, &applied, NULL));
    chart[2] = applied;
    if (applied)
        TEST_VERDICT("Compare-and-set on a deleted key applied");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll, "cas"));
    tsapi_nosql_chart("Applied: current, stale, deleted", NULL, NULL,
                      "applied", chart, 3);
    TEST_SUCCESS;

cleanup:
    free(v1);
    free(v2);
    json_decref(got);
    json_decref(d1);
    json_decref(d2);
    json_decref(d3);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
