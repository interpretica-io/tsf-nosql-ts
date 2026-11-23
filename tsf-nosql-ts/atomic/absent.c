/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: write-if-absent
 *
 * The first write-if-absent of a key applies, the second does not and
 * leaves the first document as it was; once the key is deleted, a
 * write-if-absent applies again.
 */

#define TE_TEST_NAME    "atomic/absent"

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
    json_t          *first = NULL;
    json_t          *second = NULL;
    bool             applied = false;
    double           chart[3];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    first = json_pack("{s:s}", "who", "first");
    second = json_pack("{s:s}", "who", "second");

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_absent"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "absent");

    TEST_STEP("The first write-if-absent applies");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_absent(sess.conn, sess.coll, "once",
                                               first, 0, &applied));
    chart[0] = applied;
    if (!applied)
        TEST_VERDICT("Write-if-absent of a new key was not applied");

    TEST_STEP("The second does not, and changes nothing");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_absent(sess.conn, sess.coll, "once",
                                               second, 0, &applied));
    chart[1] = applied;
    if (applied)
        TEST_VERDICT("Write-if-absent of an existing key was applied");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "once",
                                             first));

    TEST_STEP("After a delete it applies again");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_delete(sess.conn, sess.coll,
                                                   "once", NULL));
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_put_if_absent(sess.conn, sess.coll, "once",
                                               second, 0, &applied));
    chart[2] = applied;
    if (!applied)
        TEST_VERDICT("Write-if-absent of a deleted key was not applied");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "once",
                                             second));
    tsapi_nosql_chart("Applied: new key, existing key, deleted key", NULL,
                      NULL, "applied", chart, 3);
    TEST_SUCCESS;

cleanup:
    json_decref(first);
    json_decref(second);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
