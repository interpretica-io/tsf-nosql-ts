/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Test Suite prologue
 *
 * Record which NoSQL drivers the agent's RPC server has, then start the
 * database servers the suite tests against (scripts/nosql-servers.sh):
 * eleven containers on loopback, the open and weak ones being the
 * known-bad controls of the audit.
 *
 * @author Maxim Menshikov <maxim.menshikov@interpretica.io>
 */

#define TE_TEST_NAME    "prologue"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    json_t          *info = NULL;
    json_t          *e;
    size_t           i;
    unsigned int     engines = 0;
    double           present[7];
    static const unsigned int bits[7] = {
        TAPI_NOSQL_ENGINE_REDIS, TAPI_NOSQL_ENGINE_MONGODB,
        TAPI_NOSQL_ENGINE_CASSANDRA, TAPI_NOSQL_ENGINE_ELASTIC,
        TAPI_NOSQL_ENGINE_NEO4J, TAPI_NOSQL_ENGINE_MEMCACHED,
        TAPI_NOSQL_ENGINE_ETCD,
    };

    TEST_START;

    TEST_STEP("See which database drivers the agent carries");
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_nosql_prologue"));
    CHECK_RC(tapi_nosql_available(sess.pco, &engines, &info));
    json_array_foreach(json_object_get(info, "engines"), i, e)
    {
        char *caps = tapi_nosql_json_text(json_object_get(e, "caps"));

        RING("Driver %s: %s, caps %s",
             json_string_value(json_object_get(e, "name")),
             json_string_value(json_object_get(e, "client")), caps);
        free(caps);
    }
    for (i = 0; i < 7; i++)
        present[i] = (engines & bits[i]) != 0;
    tsapi_nosql_chart("Drivers present (Redis, MongoDB, Cassandra, "
                      "Elasticsearch, Neo4j, Memcached, etcd)", NULL, NULL,
                      "present", present, 7);
    if (engines == 0)
        WARN("The agent has no NoSQL driver: every test will skip");

    TEST_STEP("Start the database servers in containers");
    rc = tsapi_nosql_servers(sess.pco, "up", NULL);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        TEST_VERDICT("The agent has no usable docker: no server to test");
    if (rc != 0)
        TEST_VERDICT("The database servers could not be started");

    TEST_SUCCESS;

cleanup:
    json_decref(info);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
