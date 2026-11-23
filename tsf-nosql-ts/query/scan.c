/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief query group: every key exactly once
 *
 * Write 1000 generated documents, then list the collection page by
 * page: the keys are exactly the 1000 written, a key of a neighbouring
 * collection whose name extends this one's is not among them, and no
 * page is larger than asked for - except on Redis, whose SCAN takes the
 * page size as a hint and may also return a key twice while it walks
 * (both are charted, and repeats are dropped, as its contract allows).
 */

#define TE_TEST_NAME    "query/scan"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

#define N       1000
#define PAGE    100

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    json_t          *want = json_array();
    json_t          *keys = NULL;
    json_t          *doc = NULL;
    char             neighbour[48] = "";
    unsigned int     pages = 0;
    unsigned int     largest = 0;
    unsigned int     repeats = 0;
    long             written = 0;
    double           chart[3];
    int              i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_scan"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "scan");

    TEST_STEP("Write %d documents, and one in a neighbouring collection", N);
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_bulk_generate(sess.conn, sess.coll, N, 32,
                                               "k", 11, &written, NULL));
    if (written != N)
        TEST_VERDICT("Bulk load wrote a different number of documents");
    snprintf(neighbour, sizeof(neighbour), "%sx", sess.coll);
    doc = json_pack("{s:i}", "outside", 1);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, neighbour, "k0",
                                                doc, 0, NULL));
    for (i = 0; i < N; i++)
    {
        char k[16];

        snprintf(k, sizeof(k), "k%d", i);
        json_array_append_new(want, json_string(k));
    }

    TEST_STEP("The collection lists exactly its own keys");
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_keys(sess.conn, sess.coll, want));

    TEST_STEP("Pages of %d", PAGE);
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_scan_all(sess.conn, sess.coll, PAGE, &keys,
                                          &pages, &largest, &repeats));
    RING("%zu keys in %u pages, the largest %u, %u repeated",
         json_array_size(keys), pages, largest, repeats);
    if (json_array_size(keys) != N)
        TEST_VERDICT("Paging did not reach every key");
    if (strcmp(backend, "redis") != 0)
    {
        if (largest > PAGE)
            TEST_VERDICT("A page was larger than the limit");
        if (repeats != 0)
            TEST_VERDICT("A key was listed twice");
    }
    chart[0] = pages;
    chart[1] = largest;
    chart[2] = repeats;
    tsapi_nosql_chart("Scan of 1000 keys: pages, largest page, repeats",
                      NULL, NULL, "count", chart, 3);
    TEST_SUCCESS;

cleanup:
    if (sess.conn != NULL && neighbour[0] != '\0')
        tapi_nosql_drop(sess.conn, neighbour);
    json_decref(want);
    json_decref(keys);
    json_decref(doc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
