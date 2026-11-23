/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: each assertion bites
 *
 * Provoke one tapi_nosql_assert_*() to fail on purpose - a document that
 * differs, one that is present, a wrong count, a wrong key set, a
 * counter off by six, a document that never expires - and let it emit
 * its verdict. Every iteration is an expected FAILED in TRC with that
 * verdict: an assertion that cannot fail would pass any test it guards.
 * The collection is named "violation" and the key "probe" so the
 * verdict text is the same on every run.
 */

#define TE_TEST_NAME    "basic/assert_violation"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_assert.h"
#include "tsapi_nosql.h"

#define COLL    "violation"
#define KEY     "probe"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    const char      *check;
    json_t          *doc = NULL;
    json_t          *other = NULL;
    json_t          *keys = NULL;
    te_errno         arc = 0;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    TEST_GET_STRING_PARAM(check);
    doc = json_pack("{s:i}", "a", 1);
    other = json_pack("{s:i}", "a", 2);
    keys = json_pack("[s]", "some-other-key");

    TEST_STEP("Connect to %s and store the probe", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_violation"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_drop(sess.conn, COLL));
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, COLL, KEY, doc, 0,
                                                NULL));

    TEST_STEP("Provoke the '%s' assertion", check);
    if (strcmp(check, "doc") == 0)
        arc = tapi_nosql_assert_doc(sess.conn, COLL, KEY, other);
    else if (strcmp(check, "absent") == 0)
        arc = tapi_nosql_assert_absent(sess.conn, COLL, KEY);
    else if (strcmp(check, "count") == 0)
        arc = tapi_nosql_assert_count(sess.conn, COLL, NULL, 5);
    else if (strcmp(check, "keys") == 0)
        arc = tapi_nosql_assert_keys(sess.conn, COLL, keys);
    else if (strcmp(check, "counter") == 0)
    {
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_incr(sess.conn, COLL, KEY, 1,
                                                     NULL));
        arc = tapi_nosql_assert_counter(sess.conn, COLL, KEY, 7);
    }
    else if (strcmp(check, "eventually") == 0)
        arc = tapi_nosql_assert_eventually_absent(sess.conn, COLL, KEY, 500,
                                                  100, NULL);
    else
        TEST_FAIL("Unknown check '%s'", check);

    if (TE_RC_GET_ERROR(arc) == TE_EFAIL)
        TEST_STOP;
    if (arc != 0)
        TEST_FAIL("The assertion could not check: %r", arc);
    TEST_VERDICT("The provoked assertion held");

cleanup:
    if (sess.conn != NULL)
        tapi_nosql_drop(sess.conn, COLL);
    json_decref(doc);
    json_decref(other);
    json_decref(keys);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
