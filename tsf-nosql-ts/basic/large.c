/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: large documents
 *
 * A document whose one string field is @p size bytes (1 KiB, 256 KiB,
 * 1 MiB) is stored and read back intact. An engine whose item limit is
 * below the size refuses it as too large (class "limit"), which this
 * test reports as its verdict: Memcached's default item size is 1 MiB
 * with the item's own overhead, so a 1 MiB document is over it and that
 * iteration is an expected failure in TRC.
 */

#define TE_TEST_NAME    "basic/large"

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
    int              size;
    char            *payload = NULL;
    json_t          *doc = NULL;
    double           t[2];
    double           t0;
    int              k;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_INT_PARAM(size);

    payload = malloc((size_t)size + 1);
    CHECK_NOT_NULL(payload);
    for (k = 0; k < size; k++)
        payload[k] = (char)('a' + (k * 7 + k / 26) % 26);
    payload[size] = '\0';
    doc = json_pack("{s:i, s:s}", "size", size, "payload", payload);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_large"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("Store a document of %d bytes", size);
    t0 = tsapi_nosql_now();
    rc = tapi_nosql_put(sess.conn, sess.coll, "large", doc, 0, NULL);
    t[0] = (tsapi_nosql_now() - t0) * 1000;
    if (rc != 0)
    {
        if (tapi_nosql_last_cls(sess.conn) == TAPI_NOSQL_ERR_LIMIT)
        {
            RING("Refused: %s", tapi_nosql_last_msg(sess.conn));
            TEST_VERDICT("The engine refused the document as too large");
        }
        TEST_FAIL("Storing %d bytes failed: %s: %s", size,
                  tapi_nosql_errcls_name(tapi_nosql_last_cls(sess.conn)),
                  tapi_nosql_last_msg(sess.conn));
    }

    TEST_STEP("Read it back intact");
    t0 = tsapi_nosql_now();
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "large",
                                             doc));
    t[1] = (tsapi_nosql_now() - t0) * 1000;
    tsapi_nosql_chart("Put and get of a large document (ms)", NULL, NULL,
                      "ms", t, 2);
    TEST_SUCCESS;

cleanup:
    free(payload);
    json_decref(doc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
