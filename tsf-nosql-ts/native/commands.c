/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief native group: each engine's own command language
 *
 * Commands in the engine's own terms, answered as the engine documents:
 *  - Redis: a hash (HSET, HGET, HLEN) and a list (RPUSH, LRANGE);
 *  - MongoDB: buildInfo, insert and count through runCommand;
 *  - Cassandra: CQL with typed columns (bigint, text, double, boolean,
 *    list, map) and bound parameters;
 *  - Elasticsearch: a full-text index and search of its own;
 *  - Neo4j: two nodes, a relationship and a path query in Cypher;
 *  - Memcached: stats and settings;
 *  - etcd: kv put and range on the raw gateway.
 */

#define TE_TEST_NAME    "native/commands"

#include "te_config.h"
#include "tapi_test.h"

#include "te_string.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

static unsigned int checks;

static json_t *
run(tsapi_nosql_sess *sess, json_t *cmd)
{
    json_t *reply = NULL;
    char   *text = tapi_nosql_json_text(cmd);

    RING("native %s", text);
    free(text);
    TSAPI_NOSQL_CHECK(sess->conn, tapi_nosql_native(sess->conn, cmd, &reply));
    json_decref(cmd);
    checks++;
    return reply;
}

#define EXPECT(_cond, _what) \
    do {                                                                \
        if (!(_cond))                                                   \
            TEST_VERDICT("%s: the native answer is not as documented",  \
                         _what);                                        \
    } while (0)

static void
redis_cmds(tsapi_nosql_sess *s)
{
    char    h[48];
    char    l[48];
    json_t *r;

    snprintf(h, sizeof(h), "%s:hash", s->coll);
    snprintf(l, sizeof(l), "%s:list", s->coll);
    r = run(s, json_pack("{s:[s, s, s, s, s, s]}", "argv", "HSET", h, "f1",
                         "v1", "f2", "v2"));
    EXPECT(json_integer_value(r) == 2, "HSET");
    json_decref(r);
    r = run(s, json_pack("{s:[s, s, s]}", "argv", "HGET", h, "f2"));
    EXPECT(json_is_string(r) && strcmp(json_string_value(r), "v2") == 0,
           "HGET");
    json_decref(r);
    r = run(s, json_pack("{s:[s, s]}", "argv", "HLEN", h));
    EXPECT(json_integer_value(r) == 2, "HLEN");
    json_decref(r);
    r = run(s, json_pack("{s:[s, s, s, s, s]}", "argv", "RPUSH", l, "a", "b",
                         "c"));
    EXPECT(json_integer_value(r) == 3, "RPUSH");
    json_decref(r);
    r = run(s, json_pack("{s:[s, s, s, s]}", "argv", "LRANGE", l, "0", "-1"));
    EXPECT(json_array_size(r) == 3 &&
           strcmp(json_string_value(json_array_get(r, 2)), "c") == 0,
           "LRANGE");
    json_decref(r);
    json_decref(run(s, json_pack("{s:[s, s, s]}", "argv", "DEL", h, l)));
}

static void
mongo_cmds(tsapi_nosql_sess *s)
{
    char    coll[48];
    json_t *r;

    snprintf(coll, sizeof(coll), "%sn", s->coll);
    r = run(s, json_pack("{s:s, s:{s:i}}", "db", "admin", "command",
                         "buildInfo", 1));
    EXPECT(json_is_string(json_object_get(r, "version")), "buildInfo");
    json_decref(r);
    r = run(s, json_pack("{s:s, s:{s:s, s:[{s:s, s:i}, {s:s, s:i}]}}", "db",
                         "tsf", "command", "insert", coll, "documents",
                         "_id", "a", "x", 1, "_id", "b", "x", 2));
    EXPECT(json_integer_value(json_object_get(r, "n")) == 2, "insert");
    json_decref(r);
    r = run(s, json_pack("{s:s, s:{s:s, s:{s:{s:i}}}}", "db", "tsf",
                         "command", "count", coll, "query", "x", "$gt", 1));
    EXPECT(json_integer_value(json_object_get(r, "n")) == 1, "count");
    json_decref(r);
    json_decref(run(s, json_pack("{s:s, s:{s:s}}", "db", "tsf", "command",
                                 "drop", coll)));
}

static void
cassandra_cmds(tsapi_nosql_sess *s)
{
    te_string q = TE_STRING_INIT;
    json_t   *r;
    json_t   *row;

    te_string_append(&q, "CREATE TABLE IF NOT EXISTS tsf.%sn (k bigint "
                     "PRIMARY KEY, s text, d double, b boolean, l list<int>, "
                     "m map<text, int>)", s->coll);
    json_decref(run(s, json_pack("{s:s}", "cql", q.ptr)));
    te_string_reset(&q);
    te_string_append(&q, "INSERT INTO tsf.%sn (k, s, d, b, l, m) VALUES "
                     "(?, ?, ?, ?, [1, 2, 3], {'a': 1})", s->coll);
    json_decref(run(s, json_pack("{s:s, s:[i, s, f, b]}", "cql", q.ptr,
                                 "params", 1, "x", 2.5, 1)));
    te_string_reset(&q);
    te_string_append(&q, "SELECT k, s, d, b, l, m FROM tsf.%sn WHERE k = 1",
                     s->coll);
    r = run(s, json_pack("{s:s}", "cql", q.ptr));
    row = json_array_get(json_object_get(r, "rows"), 0);
    EXPECT(json_integer_value(json_array_get(row, 0)) == 1 &&
           strcmp(json_string_value(json_array_get(row, 1)), "x") == 0 &&
           json_real_value(json_array_get(row, 2)) == 2.5 &&
           json_is_true(json_array_get(row, 3)) &&
           json_array_size(json_array_get(row, 4)) == 3 &&
           json_integer_value(json_object_get(json_array_get(row, 5), "a"))
           == 1, "typed CQL row");
    json_decref(r);
    te_string_reset(&q);
    te_string_append(&q, "DROP TABLE tsf.%sn", s->coll);
    json_decref(run(s, json_pack("{s:s}", "cql", q.ptr)));
    te_string_free(&q);
}

static void
es_cmds(tsapi_nosql_sess *s)
{
    te_string p = TE_STRING_INIT;
    json_t   *r;

    te_string_append(&p, "/%sn/_doc/1?refresh=true", s->coll);
    r = run(s, json_pack("{s:s, s:s, s:{s:s}}", "method", "PUT", "path",
                         p.ptr, "body", "t", "the quick brown fox"));
    EXPECT(json_integer_value(json_object_get(r, "status")) == 201,
           "index a document");
    json_decref(r);
    te_string_reset(&p);
    te_string_append(&p, "/%sn/_search", s->coll);
    r = run(s, json_pack("{s:s, s:s, s:{s:{s:{s:s}}}}", "method", "POST",
                         "path", p.ptr, "body", "query", "match", "t",
                         "brown"));
    EXPECT(json_integer_value(json_object_get(json_object_get(
               json_object_get(json_object_get(r, "body"), "hits"),
               "total"), "value")) == 1, "full-text search");
    json_decref(r);
    te_string_reset(&p);
    te_string_append(&p, "/%sn", s->coll);
    json_decref(run(s, json_pack("{s:s, s:s}", "method", "DELETE", "path",
                                 p.ptr)));
    te_string_free(&p);
}

static void
neo4j_cmds(tsapi_nosql_sess *s)
{
    te_string q = TE_STRING_INIT;
    json_t   *r;
    json_t   *row;

    te_string_append(&q, "CREATE (a:`%sP` {n: $a})-[:KNOWS]->(b:`%sP` "
                     "{n: $b})", s->coll, s->coll);
    json_decref(run(s, json_pack("{s:s, s:{s:s, s:s}}", "cypher", q.ptr,
                                 "params", "a", "ann", "b", "bob")));
    te_string_reset(&q);
    te_string_append(&q, "MATCH p = (x:`%sP`)-[:KNOWS]->(y:`%sP`) "
                     "RETURN x.n, y.n, length(p)", s->coll, s->coll);
    r = run(s, json_pack("{s:s}", "cypher", q.ptr));
    row = json_array_get(json_object_get(r, "rows"), 0);
    EXPECT(strcmp(json_string_value(json_array_get(row, 0)), "ann") == 0 &&
           strcmp(json_string_value(json_array_get(row, 1)), "bob") == 0 &&
           json_integer_value(json_array_get(row, 2)) == 1, "path query");
    json_decref(r);
    te_string_reset(&q);
    te_string_append(&q, "MATCH (n:`%sP`) DETACH DELETE n", s->coll);
    json_decref(run(s, json_pack("{s:s}", "cypher", q.ptr)));
    te_string_free(&q);
}

static void
memcached_cmds(tsapi_nosql_sess *s)
{
    json_t *r = run(s, json_pack("{s:s}", "stats", ""));

    EXPECT(json_is_string(json_object_get(r, "version")) &&
           json_is_string(json_object_get(r, "curr_items")), "stats");
    json_decref(r);
    r = run(s, json_pack("{s:s}", "stats", "settings"));
    EXPECT(json_is_string(json_object_get(r, "maxbytes")), "stats settings");
    json_decref(r);
}

static void
etcd_cmds(tsapi_nosql_sess *s)
{
    char    key[64];
    char   *k64;
    json_t *r;
    json_t *kv;

    /* base64 of "/<coll>native" and of "v1". */
    snprintf(key, sizeof(key), "/%snative", s->coll);
    {
        static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                                "rstuvwxyz0123456789+/";
        size_t n = strlen(key);
        size_t i;
        size_t o = 0;

        k64 = calloc(1, 4 * ((n + 2) / 3) + 1);
        for (i = 0; i < n; i += 3)
        {
            unsigned v = (unsigned char)key[i] << 16 |
                         (i + 1 < n ? (unsigned char)key[i + 1] << 8 : 0) |
                         (i + 2 < n ? (unsigned char)key[i + 2] : 0);

            k64[o++] = t[v >> 18 & 63];
            k64[o++] = t[v >> 12 & 63];
            k64[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
            k64[o++] = i + 2 < n ? t[v & 63] : '=';
        }
    }
    r = run(s, json_pack("{s:s, s:{s:s, s:s}}", "path", "/v3/kv/put", "body",
                         "key", k64, "value", "djE="));
    EXPECT(json_integer_value(json_object_get(r, "status")) == 200, "kv put");
    json_decref(r);
    r = run(s, json_pack("{s:s, s:{s:s}}", "path", "/v3/kv/range", "body",
                         "key", k64));
    kv = json_array_get(json_object_get(json_object_get(r, "body"), "kvs"),
                        0);
    EXPECT(strcmp(json_string_value(json_object_get(kv, "value")), "djE=")
           == 0, "kv range");
    json_decref(r);
    json_decref(run(s, json_pack("{s:s, s:{s:s}}", "path",
                                 "/v3/kv/deleterange", "body", "key", k64)));
    free(k64);
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *backend;
    double           n;

    TEST_START;
    TEST_GET_STRING_PARAM(backend);

    TEST_STEP("Connect to %s", backend);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_native"));
    tsapi_nosql_sess_open(&sess, backend);

    TEST_STEP("Native commands");
    if (strcmp(backend, "redis") == 0)
        redis_cmds(&sess);
    else if (strcmp(backend, "mongodb") == 0)
        mongo_cmds(&sess);
    else if (strcmp(backend, "cassandra") == 0)
        cassandra_cmds(&sess);
    else if (strcmp(backend, "elasticsearch") == 0)
        es_cmds(&sess);
    else if (strcmp(backend, "neo4j") == 0)
        neo4j_cmds(&sess);
    else if (strcmp(backend, "memcached") == 0)
        memcached_cmds(&sess);
    else
        etcd_cmds(&sess);
    n = checks;
    tsapi_nosql_chart("Native commands answered as documented", NULL, NULL,
                      "commands", &n, 1);
    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
