/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: a document of each kind of JSON
 *
 * Store one document of the kind the iteration names, read it back and
 * compare it with what was written, value by value and type by type
 * (an integer stays an integer, 1.0 stays a real); overwrite it and see
 * the new one; delete it twice (deleted, then not there). The kinds are
 * the ones engines get wrong: 64-bit integers at both ends, reals that
 * do not survive a float, Unicode and escapes, deep nesting, empty and
 * mixed arrays, and values that look like operators of some query
 * language.
 */

#define TE_TEST_NAME    "basic/crud"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

static const struct {
    const char *kind;
    const char *json;
} docs[] = {
    { "scalars", "{\"name\":\"alice\",\"age\":30,\"ok\":true,\"none\":null}" },
    { "int64", "{\"max\":9223372036854775807,\"min\":-9223372036854775808,"
               "\"i32\":2147483647,\"neg\":-1,\"zero\":0}" },
    { "reals", "{\"tenth\":0.1,\"big\":1.7976931348623157e308,"
               "\"tiny\":5e-324,\"one\":1.0,\"neg\":-2.5,\"pi\":3.141592653589793}" },
    { "unicode", "{\"text\":\"κόσμε 🙂 日本語\",\"esc\":\"q\\\" b\\\\ n\\n t\\t\","
                 "\"empty\":\"\"}" },
    { "nested", "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":[1,{\"g\":null}]}}}}}}" },
    { "arrays", "{\"empty\":[],\"ints\":[1,2,3],\"mixed\":[1,\"a\",true,null,2.5],"
                "\"objs\":[{\"x\":1},{\"y\":[]}],\"obj\":{}}" },
    { "operators", "{\"q\":{\"$gt\":1},\"l\":{\"$numberLong\":\"5\"},"
                   "\"c\":\"MATCH (n) DETACH DELETE n\",\"s\":\"' OR '1'='1\"}" },
    { "empty", "{}" },
};

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    const char      *doc;
    json_t          *want = NULL;
    json_t          *other = NULL;
    double           ms[3];
    double           t0;
    bool             deleted = false;
    unsigned int     i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_STRING_PARAM(doc);

    for (i = 0; i < TE_ARRAY_LEN(docs); i++)
    {
        if (strcmp(docs[i].kind, doc) == 0)
            break;
    }
    if (i == TE_ARRAY_LEN(docs))
        TEST_FAIL("Unknown document kind '%s'", doc);
    want = tapi_nosql_json(docs[i].json);
    other = json_pack("{s:s}", "replaced", "yes");

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_crud"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("Store a '%s' document and read it back", doc);
    t0 = tsapi_nosql_now();
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "doc",
                                                want, 0, NULL));
    ms[0] = (tsapi_nosql_now() - t0) * 1000;
    t0 = tsapi_nosql_now();
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "doc",
                                             want));
    ms[1] = (tsapi_nosql_now() - t0) * 1000;

    TEST_STEP("Overwrite it");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "doc",
                                                other, 0, NULL));
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "doc",
                                             other));

    TEST_STEP("Delete it twice");
    t0 = tsapi_nosql_now();
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_delete(sess.conn, sess.coll, "doc",
                                                   &deleted));
    ms[2] = (tsapi_nosql_now() - t0) * 1000;
    if (!deleted)
        TEST_VERDICT("Deleting a present document reported nothing deleted");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_delete(sess.conn, sess.coll, "doc",
                                                   &deleted));
    if (deleted)
        TEST_VERDICT("Deleting an absent document reported a deletion");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_absent(sess.conn, sess.coll, "doc"));

    tsapi_nosql_chart("Put, get, delete (ms)", NULL, NULL, "ms", ms, 3);
    TEST_SUCCESS;

cleanup:
    json_decref(want);
    json_decref(other);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
