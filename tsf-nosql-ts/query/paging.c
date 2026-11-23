/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief query group: any page size
 *
 * 250 keys listed with pages of @p limit: every key once, each page at
 * most @p limit (Redis: a hint, charted only), and the number of pages
 * what the arithmetic says, give or take a final empty one.
 */

#define TE_TEST_NAME    "query/paging"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

#define N   250

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    int              limit;
    json_t          *seen = json_object();
    char            *cursor = NULL;
    double           sizes[N + 2];
    unsigned int     pages = 0;
    unsigned int     dup = 0;
    unsigned int     expect_pages;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(limit);

    TEST_STEP("Connect to %s and write %d documents", backend, N);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_paging"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "scan");
    TSAPI_NOSQL_CHECK(sess.conn,
                      tapi_nosql_bulk_generate(sess.conn, sess.coll, N, 8,
                                               "p", 3, NULL, NULL));

    TEST_STEP("List them %d at a time", limit);
    do {
        json_t *page = NULL;
        char   *next = NULL;
        json_t *k;
        size_t  i;

        TSAPI_NOSQL_CHECK(sess.conn,
                          tapi_nosql_scan(sess.conn, sess.coll, limit, cursor,
                                          &page, &next));
        free(cursor);
        cursor = next;
        if (pages < N + 2)
            sizes[pages] = (double)json_array_size(page);
        if ((int)json_array_size(page) > limit && strcmp(backend, "redis") != 0)
            TEST_VERDICT("A page was larger than the limit");
        json_array_foreach(page, i, k)
        {
            if (json_object_get(seen, json_string_value(k)) != NULL)
                dup++;
            json_object_set_new(seen, json_string_value(k), json_true());
        }
        json_decref(page);
        pages++;
    } while (cursor != NULL && pages < 100000);

    RING("%zu distinct keys in %u pages, %u repeated",
         json_object_size(seen), pages, dup);
    if (json_object_size(seen) != N)
        TEST_VERDICT("Paging did not give every key");
    if (strcmp(backend, "redis") != 0)
    {
        if (dup != 0)
            TEST_VERDICT("A key was listed twice");
        expect_pages = (N + limit - 1) / limit;
        if (pages != expect_pages && pages != expect_pages + 1)
            TEST_VERDICT("The number of pages is not what the page size "
                         "gives");
    }
    tsapi_nosql_chart("Page sizes", NULL, NULL, "keys", sizes,
                      pages < N + 2 ? pages : N + 2);
    TEST_SUCCESS;

cleanup:
    free(cursor);
    json_decref(seen);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
