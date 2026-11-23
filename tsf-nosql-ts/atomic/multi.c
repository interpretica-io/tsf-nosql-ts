/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief atomic group: several writes at once
 *
 * Three puts and a delete in one operation are all applied. Where the
 * engine takes a condition with them: when the key the condition names
 * exists, nothing of the operation is applied; when it does not,
 * everything is. (MongoDB: a transaction, on a replica set; Redis:
 * WATCH and MULTI/EXEC; etcd: a txn; Neo4j: a transaction; Cassandra: a
 * logged batch, which takes no condition across partitions.)
 */

#define TE_TEST_NAME    "atomic/multi"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

static json_t *
op_put(const char *coll, const char *key, int n)
{
    return json_pack("{s:s, s:s, s:s, s:{s:i}}", "op", "put", "coll", coll,
                     "key", key, "doc", "n", n);
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *ops = NULL;
    json_t          *d = NULL;
    bool             applied = false;
    double           chart[3] = { 0, 0, 0 };
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_multi"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "multi");
    d = json_pack("{s:i}", "n", 0);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "gone",
                                                d, 0, NULL));

    TEST_STEP("Three puts and a delete together");
    ops = json_array();
    for (i = 1; i <= 3; i++)
    {
        char k[8];

        snprintf(k, sizeof(k), "m%d", i);
        json_array_append_new(ops, op_put(sess.coll, k, i));
    }
    json_array_append_new(ops, json_pack("{s:s, s:s, s:s}", "op", "delete",
                                         "coll", sess.coll, "key", "gone"));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_multi(sess.conn, ops, NULL, NULL,
                                                  &applied));
    chart[0] = applied;
    if (!applied)
        TEST_VERDICT("A multi-write was not applied");
    for (i = 1; i <= 3; i++)
    {
        char    k[8];
        json_t *want = json_pack("{s:i}", "n", i);

        snprintf(k, sizeof(k), "m%d", i);
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, k,
                                                 want));
        json_decref(want);
    }
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll, "gone"));

    if (tapi_nosql_has_cap(sess.conn, "multi_if"))
    {
        TEST_STEP("A condition that fails applies nothing");
        json_decref(ops);
        ops = json_array();
        json_array_append_new(ops, op_put(sess.coll, "n1", 1));
        json_array_append_new(ops, json_pack("{s:s, s:s, s:s}", "op",
                                             "delete", "coll", sess.coll,
                                             "key", "m1"));
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_multi(sess.conn, ops,
                                                      sess.coll, "m2",
                                                      &applied));
        chart[1] = applied;
        if (applied)
            TEST_VERDICT("A multi-write whose condition failed was applied");
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll,
                                                    "n1"));
        json_decref(d);
        d = json_pack("{s:i}", "n", 1);
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "m1",
                                                 d));

        TEST_STEP("A condition that holds applies everything");
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_multi(sess.conn, ops,
                                                      sess.coll, "nothere",
                                                      &applied));
        chart[2] = applied;
        if (!applied)
            TEST_VERDICT("A multi-write whose condition held was not "
                         "applied");
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "n1",
                                                 d));
        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll,
                                                    "m1"));
    }
    tsapi_nosql_chart("Applied: plain, failed condition, held condition",
                      NULL, NULL, "applied", chart, 3);
    TEST_SUCCESS;

cleanup:
    json_decref(ops);
    json_decref(d);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
