/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief query group: counting
 *
 * The count of a collection follows what is done to it: 0 when new,
 * 500 after a bulk load, 450 after 50 deletes, 460 after 10 more
 * inserts. Where the engine queries by field, the count of group = 3
 * follows too.
 */

#define TE_TEST_NAME    "query/count"

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
    json_t          *f = NULL;
    json_t          *doc = NULL;
    double           counts[4] = { 0, 500, 450, 460 };
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    f = json_pack("{s:i}", "group", 3);
    doc = json_pack("{s:i, s:s}", "group", 3, "name", "extra");

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_count"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "count");

    TEST_STEP("A new collection counts 0");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_count(sess.conn, sess.coll, NULL, 0));

    TEST_STEP("500 after a bulk load");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_bulk_generate(sess.conn, sess.coll, 500, 8,
                                               "k", 5, NULL, NULL));
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_count(sess.conn, sess.coll, NULL,
                                               500));

    TEST_STEP("450 after 50 deletes");
    for (i = 0; i < 50; i++)
    {
        char k[16];

        snprintf(k, sizeof(k), "k%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_delete(sess.conn, sess.coll,
                                                       k, NULL));
    }
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_count(sess.conn, sess.coll, NULL,
                                               450));

    TEST_STEP("460 after 10 more inserts");
    for (i = 0; i < 10; i++)
    {
        char k[16];

        snprintf(k, sizeof(k), "extra%d", i);
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, k,
                                                    doc, 0, NULL));
    }
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_count(sess.conn, sess.coll, NULL,
                                               460));

    if (tapi_nosql_has_cap(sess.conn, "find"))
    {
        /* group 3 of k0..k499 is 50; k3..k43 of it were deleted (5),
         * and the 10 extras are group 3. */
        TEST_STEP("group = 3 counts 55");
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_count(sess.conn, sess.coll, f,
                                                   55));
    }
    tsapi_nosql_chart("Count after: nothing, bulk, deletes, inserts", NULL,
                      NULL, "documents", counts, 4);
    TEST_SUCCESS;

cleanup:
    json_decref(f);
    json_decref(doc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
