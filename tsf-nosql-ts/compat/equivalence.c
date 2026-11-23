/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief compat group: the same data in every engine
 *
 * The same six documents go into every engine the agent can reach and
 * are read back from each: every engine must return what was written,
 * so a test written against one engine reads the same data from any
 * other. Documents equal per engine are charted.
 */

#define TE_TEST_NAME    "compat/equivalence"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

static const char *const engines[] = {
    "redis", "mongodb", "cassandra", "elasticsearch", "neo4j", "memcached",
    "etcd",
};

static const char *const docs[] = {
    "{\"name\":\"alice\",\"age\":30}",
    "{\"i64\":9223372036854775807,\"real\":0.1,\"one\":1.0}",
    "{\"text\":\"κόσμε 🙂\",\"esc\":\"\\\"\\\\\\n\"}",
    "{\"deep\":{\"a\":[1,{\"b\":null}],\"c\":{}}}",
    "{\"mixed\":[1,\"a\",true,null,2.5],\"t\":true,\"f\":false}",
    "{\"op\":{\"$gt\":1},\"s\":\"' OR '1'='1\"}",
};

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    tapi_nosql_conn *conns[TE_ARRAY_LEN(engines)] = { NULL };
    double           equal[TE_ARRAY_LEN(engines)];
    unsigned int     reached = 0;
    unsigned int     e;
    unsigned int     d;

    TEST_START;
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_equivalence"));

    TEST_STEP("Connect to every engine the agent has");
    for (e = 0; e < TE_ARRAY_LEN(engines); e++)
    {
        json_t *cfg;

        equal[e] = 0;
        if (!tapi_nosql_supports(sess.pco, engines[e]))
            continue;
        cfg = tsapi_nosql_config(engines[e], TSAPI_NOSQL_ROLE_APP);
        CHECK_RC(tapi_nosql_open(sess.pco, engines[e], cfg, &conns[e],
                                 NULL));
        json_decref(cfg);
        reached++;
    }
    if (reached < 2)
        TEST_SKIP("Fewer than two engines to compare");

    TEST_STEP("Write the same documents everywhere, read them back");
    for (d = 0; d < TE_ARRAY_LEN(docs); d++)
    {
        json_t *want = tapi_nosql_json(docs[d]);
        char    key[16];

        snprintf(key, sizeof(key), "doc%u", d);
        for (e = 0; e < TE_ARRAY_LEN(engines); e++)
        {
            json_t *got = NULL;

            if (conns[e] == NULL)
                continue;
            TSAPI_NOSQL_CHECK(conns[e], tapi_nosql_put(conns[e], sess.coll,
                                                       key, want, 0, NULL));
            TSAPI_NOSQL_CHECK(conns[e], tapi_nosql_get(conns[e], sess.coll,
                                                       key, &got, NULL));
            if (got != NULL && json_equal(got, want))
            {
                equal[e]++;
            }
            else
            {
                char *s = got != NULL ? tapi_nosql_json_text(got) : NULL;

                ERROR("%s returned %s for %s", engines[e],
                      s != NULL ? s : "nothing", docs[d]);
                free(s);
            }
            json_decref(got);
        }
        json_decref(want);
    }
    tsapi_nosql_chart("Documents read back equal of 6 (Redis, MongoDB, "
                      "Cassandra, Elasticsearch, Neo4j, Memcached, etcd)",
                      NULL, NULL, "equal", equal, TE_ARRAY_LEN(engines));
    for (e = 0; e < TE_ARRAY_LEN(engines); e++)
    {
        if (conns[e] != NULL && equal[e] != TE_ARRAY_LEN(docs))
            TEST_VERDICT("An engine returned a document different from the "
                         "one written");
    }
    TEST_SUCCESS;

cleanup:
    for (e = 0; e < TE_ARRAY_LEN(engines); e++)
    {
        if (conns[e] == NULL)
            continue;
        if (strcmp(engines[e], "memcached") != 0)
            tapi_nosql_drop(conns[e], sess.coll);
        tapi_nosql_close(conns[e]);
    }
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
