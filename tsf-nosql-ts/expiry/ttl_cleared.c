/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief expiry group: an overwrite without a TTL
 *
 * Store an entry that expires in 2 s, then overwrite it with no TTL:
 * the overwrite replaces the entry, expiry included, so it is still
 * there well after the first TTL would have ended (and after MongoDB's
 * background pass would have removed it).
 */

#define TE_TEST_NAME    "expiry/ttl_cleared"

#include "te_config.h"
#include "tapi_test.h"

#include <unistd.h>

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
    double           chart[1];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    d1 = json_pack("{s:i}", "v", 1);
    d2 = json_pack("{s:i}", "v", 2);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_ttl_cleared"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "ttl");

    TEST_STEP("Store with a 2 s TTL, overwrite with none");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "k", d1,
                                                2, NULL));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "k", d2,
                                                0, NULL));

    TEST_STEP("Still there 5 s later");
    sleep(5);
    TSAPI_NOSQL_ASSERT(tapi_nosql_assert_doc(sess.conn, sess.coll, "k", d2));
    chart[0] = 1;
    tsapi_nosql_chart("Present after the first TTL", NULL, NULL, "present",
                      chart, 1);
    TEST_SUCCESS;

cleanup:
    json_decref(d1);
    json_decref(d2);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
