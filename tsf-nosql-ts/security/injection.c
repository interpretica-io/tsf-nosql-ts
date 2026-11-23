/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: injection through an application input
 *
 * An application checks a login by looking up the user with the
 * password it was given. The attacker gives a password that is really
 * query syntax:
 *  - MongoDB: {"$ne": null} - "any password that is not null";
 *  - Neo4j: x' OR '1'='1 - Cypher closing the string;
 *  - Elasticsearch: * - a query_string wildcard.
 * The known-bad control builds the query by pasting the input into it,
 * as an application would that gets it wrong, and must let the
 * attacker in - otherwise the test cannot see the class of defect at
 * all. The same input through tapi_nosql_find() (a parameterised
 * equality on a value) must not.
 */

#define TE_TEST_NAME    "security/injection"

#include "te_config.h"
#include "tapi_test.h"

#include "te_string.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

/* The vulnerable application: how many users the pasted query found. */
static long long
vulnerable_login(tsapi_nosql_sess *sess, const char *backend,
                 const char *input)
{
    te_string q = TE_STRING_INIT;
    json_t   *cmd = NULL;
    json_t   *reply = NULL;
    long long n = -1;

    if (strcmp(backend, "mongodb") == 0)
    {
        json_t *command;

        te_string_append(&q, "{\"find\":\"%s\",\"filter\":{\"user\":\"alice\","
                         "\"pass\":%s}}", sess->coll, input);
        command = tapi_nosql_json(q.ptr);
        if (command == NULL)
            TEST_FAIL("The pasted query does not parse");
        cmd = json_pack("{s:s, s:o}", "db", "tsf", "command", command);
        TSAPI_NOSQL_CHECK(sess->conn, tapi_nosql_native(sess->conn, cmd,
                                                        &reply));
        n = (long long)json_array_size(json_object_get(
                json_object_get(reply, "cursor"), "firstBatch"));
    }
    else if (strcmp(backend, "neo4j") == 0)
    {
        te_string_append(&q, "MATCH (n:`%s`) WHERE n.user = 'alice' AND "
                         "n.pass = '%s' RETURN count(n)", sess->coll, input);
        cmd = json_pack("{s:s}", "cypher", q.ptr);
        TSAPI_NOSQL_CHECK(sess->conn, tapi_nosql_native(sess->conn, cmd,
                                                        &reply));
        n = json_integer_value(json_array_get(json_array_get(
                json_object_get(reply, "rows"), 0), 0));
    }
    else
    {
        te_string path = TE_STRING_INIT;

        te_string_append(&q, "user:alice AND pass:%s", input);
        te_string_append(&path, "/%s/_count", sess->coll);
        cmd = json_pack("{s:s, s:s, s:{s:{s:{s:s}}}}", "method", "POST",
                        "path", path.ptr, "body", "query", "query_string",
                        "query", q.ptr);
        te_string_free(&path);
        TSAPI_NOSQL_CHECK(sess->conn, tapi_nosql_native(sess->conn, cmd,
                                                        &reply));
        n = json_integer_value(json_object_get(json_object_get(reply, "body"),
                                               "count"));
    }
    te_string_free(&q);
    json_decref(cmd);
    json_decref(reply);
    return n;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    const char      *input;
    json_t          *user = NULL;
    json_t          *f = NULL;
    json_t          *docs = NULL;
    long long        bad;
    long long        good;
    long long        legit;
    double           chart[3];

    TEST_START;
    TEST_GET_STRING_PARAM(backend);
    input = strcmp(backend, "mongodb") == 0 ? "{\"$ne\": null}"
          : strcmp(backend, "neo4j") == 0 ? "x' OR '1'='1" : "*";

    TEST_STEP("Connect to %s and store a user", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_injection"));
    tsapi_nosql_sess_open(&sess, backend);
    TSAPI_NOSQL_REQUIRE(&sess, "find");
    user = json_pack("{s:s, s:s}", "user", "alice", "pass", "s3cret-pass");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_put(sess.conn, sess.coll, "alice",
                                                user, 0, NULL));
    /* Elasticsearch searches what the last refresh made visible. */
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_count(sess.conn, sess.coll, NULL,
                                                  NULL));

    TEST_STEP("The right password logs in through tapi_nosql_find()");
    f = json_pack("{s:s, s:s}", "user", "alice", "pass", "s3cret-pass");
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_find(sess.conn, sess.coll, f, 10,
                                                 NULL, &docs, NULL));
    legit = (long long)json_array_size(docs);
    if (legit != 1)
        TEST_VERDICT("The right password did not find the user");
    json_decref(f);
    json_decref(docs);
    docs = NULL;

    TEST_STEP("The pasted query lets the attacker in (the control)");
    bad = vulnerable_login(&sess, backend, input);
    RING("Pasting '%s' into the query found %lld user(s)", input, bad);
    if (bad < 1)
        TEST_VERDICT("The injection control did not bypass the login: the "
                     "test cannot see injection");

    TEST_STEP("The same input through tapi_nosql_find() does not");
    f = json_pack("{s:s, s:s}", "user", "alice", "pass", input);
    TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_find(sess.conn, sess.coll, f, 10,
                                                 NULL, &docs, NULL));
    good = (long long)json_array_size(docs);
    if (good != 0)
        TEST_VERDICT("tapi_nosql_find() let injected input bypass the login");

    chart[0] = (double)legit;
    chart[1] = (double)bad;
    chart[2] = (double)good;
    tsapi_nosql_chart("Users found: right password, pasted input, "
                      "parameterised input", NULL, NULL, "users", chart, 3);
    TEST_SUCCESS;

cleanup:
    json_decref(user);
    json_decref(f);
    json_decref(docs);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
