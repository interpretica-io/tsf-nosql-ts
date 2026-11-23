/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: keys that need escaping somewhere
 *
 * Each engine puts a key somewhere that has its own syntax: a URL path
 * (Elasticsearch), a base64 field (etcd), a key prefix (Redis,
 * Memcached), a Cypher parameter (Neo4j). Keys with a slash, a percent
 * sign, query-string characters, Unicode, a colon and 200 bytes must
 * all reach their own document and no other.
 */

#define TE_TEST_NAME    "basic/keys"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    static const char *const keys[] = {
        "plain", "a/b/c", "100%25", "x?y=z&w=1#f", "ключ-鍵", "colon:in:key",
        "dot.dot", "trailing-", "UPPER", "123",
    };
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    char             longkey[201];
    double           lens[TE_ARRAY_LEN(keys) + 1];
    double           ok[TE_ARRAY_LEN(keys) + 1];
    unsigned int     i;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    memset(longkey, 'k', 200);
    longkey[200] = '\0';

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_keys"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("Write a document under each key");
    for (i = 0; i <= TE_ARRAY_LEN(keys); i++)
    {
        const char *k = i < TE_ARRAY_LEN(keys) ? keys[i] : longkey;
        json_t     *doc = json_pack("{s:i, s:s}", "n", (int)i, "key", k);

        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, k,
                                                    doc, 0, NULL));
        json_decref(doc);
    }

    TEST_STEP("Each key reads back its own document");
    for (i = 0; i <= TE_ARRAY_LEN(keys); i++)
    {
        const char *k = i < TE_ARRAY_LEN(keys) ? keys[i] : longkey;
        json_t     *doc = json_pack("{s:i, s:s}", "n", (int)i, "key", k);

        TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, k,
                                                 doc));
        json_decref(doc);
        lens[i] = (double)strlen(k);
        ok[i] = 1;
    }
    tsapi_nosql_chart("Keys round-tripped, by key length (bytes)", "length",
                      lens, "ok", ok, TE_ARRAY_LEN(keys) + 1);
    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
