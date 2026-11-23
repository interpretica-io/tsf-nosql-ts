/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief query group: equality queries
 *
 * 1000 generated documents (the generator is the agent's, and the test
 * computes the same documents itself): group = 3 matches exactly 100,
 * each one the document the generator made for its key; a string field
 * matches one; two fields together match one; a value nothing has
 * matches none. Pages are cut by key.
 */

#define TE_TEST_NAME    "query/find"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

#define N       1000
#define SEED    17
#define SIZE    24

static unsigned int
find_count(tsapi_nosql_sess *sess, json_t *filter, json_t **docs)
{
    unsigned int pages = 0;

    TSAPI_NOSQL_CHECK(sess->conn,
                      tapi_nosql_find_all(sess->conn, sess->coll, filter, 30,
                                          docs, &pages));
    return (unsigned int)json_array_size(*docs);
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *f = NULL;
    json_t          *docs = NULL;
    json_t          *d;
    size_t          i;
    double          matches[4];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s and write %d documents", backend, N);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_find"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "find");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_bulk_generate(sess.conn, sess.coll, N, SIZE,
                                               "k", SEED, NULL, NULL));

    TEST_STEP("group = 3: the 100 documents, each as generated");
    f = json_pack("{s:i}", "group", 3);
    matches[0] = find_count(&sess, f, &docs);
    if (matches[0] != N / 10)
        TEST_VERDICT("An integer field matched the wrong number of "
                     "documents");
    json_array_foreach(docs, i, d)
    {
        const char *key = json_string_value(json_object_get(d, "key"));
        long        n = strtol(key + 1, NULL, 10);
        json_t     *want = tapi_nosql_generated_doc(n, SEED, SIZE);

        if (!json_equal(json_object_get(d, "doc"), want))
        {
            ERROR("Document %s differs from the one generated", key);
            json_decref(want);
            TEST_VERDICT("A found document is not the one stored");
        }
        json_decref(want);
    }
    json_decref(f);
    json_decref(docs);
    docs = NULL;

    TEST_STEP("name = item-42: one");
    f = json_pack("{s:s}", "name", "item-42");
    matches[1] = find_count(&sess, f, &docs);
    if (matches[1] != 1 || strcmp(json_string_value(json_object_get(
            json_array_get(docs, 0), "key")), "k42") != 0)
        TEST_VERDICT("A string field did not match exactly one document");
    json_decref(f);
    json_decref(docs);
    docs = NULL;

    TEST_STEP("group = 2 and i = 42: one");
    f = json_pack("{s:i, s:i}", "group", 2, "i", 42);
    matches[2] = find_count(&sess, f, &docs);
    if (matches[2] != 1)
        TEST_VERDICT("Two fields together did not match exactly one "
                     "document");
    json_decref(f);
    json_decref(docs);
    docs = NULL;

    TEST_STEP("group = 99: none");
    f = json_pack("{s:i}", "group", 99);
    matches[3] = find_count(&sess, f, &docs);
    if (matches[3] != 0)
        TEST_VERDICT("A value no document has matched something");

    tsapi_nosql_chart("Matches: group=3, name, two fields, nothing", NULL,
                      NULL, "documents", matches, 4);
    TEST_SUCCESS;

cleanup:
    json_decref(f);
    json_decref(docs);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
