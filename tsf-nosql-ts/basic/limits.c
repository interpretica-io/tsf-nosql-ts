/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief basic group: limits are reported as limits
 *
 * Push one limit per iteration and check that the failure has the
 * class "limit" - not "invalid", not a crash, not a hang - so a test
 * can tell "too big" from "wrong":
 *  - module_doc: a document over the module's 8 MiB;
 *  - module_conns: one more connection than an RPC server holds (255);
 *  - es_fields: Elasticsearch's 1000 fields per index;
 *  - mongo_depth: MongoDB's 180 levels of nesting;
 *  - etcd_request: etcd's 1.5 MiB request;
 *  - redis_maxmemory: Redis at maxmemory with noeviction (set for the
 *    test and restored);
 *  - memcached_item: Memcached's 1 MiB item.
 */

#define TE_TEST_NAME    "basic/limits"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tsapi_nosql.h"

/* A document with one string field of @p size bytes. */
static json_t *
big(size_t size)
{
    char   *p = malloc(size + 1);
    json_t *d;

    memset(p, 'x', size);
    p[size] = '\0';
    d = json_pack("{s:s}", "p", p);
    free(p);
    return d;
}

static json_t *
deep(int levels)
{
    json_t *d = json_pack("{s:i}", "x", 1);
    int     i;

    for (i = 0; i < levels; i++)
        d = json_pack("{s:o}", "n", d);
    return d;
}

int
main(int argc, char **argv)
{
    tsapi_nosql_sess  sess = { 0 };
    tapi_nosql_conn **many = NULL;
    const char       *c;
    const char       *target = NULL;
    json_t           *doc = NULL;
    te_errno          lrc = 0;
    tapi_nosql_errcls cls = TAPI_NOSQL_OK;
    bool              redis_limited = false;
    int               opened = 0;
    double            chart[1];
    int               i;

    TEST_START;
    /* "case" is a C keyword: no TEST_GET_STRING_PARAM. */
    c = test_get_string_param(argc, argv, "case");
    if (c == NULL)
        TEST_FAIL("No case parameter");
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_limits"));

    if (strcmp(c, "module_doc") == 0 || strcmp(c, "redis_maxmemory") == 0)
        target = "redis";
    else if (strcmp(c, "module_conns") == 0 ||
             strcmp(c, "memcached_item") == 0)
        target = "memcached";
    else if (strcmp(c, "es_fields") == 0)
        target = "elasticsearch";
    else if (strcmp(c, "mongo_depth") == 0)
        target = "mongodb";
    else if (strcmp(c, "etcd_request") == 0)
        target = "etcd";
    else
        TEST_FAIL("Unknown case '%s'", c);

    TEST_STEP("Connect to %s", target);
    tsapi_nosql_sess_open(&sess, target);

    TEST_STEP("Push the limit: %s", c);
    if (strcmp(c, "module_doc") == 0)
    {
        doc = big(9u << 20);
        lrc = tapi_nosql_put(sess.conn, sess.coll, "big", doc, 0, NULL);
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else if (strcmp(c, "memcached_item") == 0)
    {
        doc = big(3u << 19);
        lrc = tapi_nosql_put(sess.conn, sess.coll, "big", doc, 0, NULL);
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else if (strcmp(c, "etcd_request") == 0)
    {
        doc = big(2u << 20);
        lrc = tapi_nosql_put(sess.conn, sess.coll, "big", doc, 0, NULL);
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else if (strcmp(c, "mongo_depth") == 0)
    {
        doc = deep(190);
        lrc = tapi_nosql_put(sess.conn, sess.coll, "deep", doc, 0, NULL);
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else if (strcmp(c, "es_fields") == 0)
    {
        doc = json_object();
        for (i = 0; i < 1100; i++)
        {
            char f[16];

            snprintf(f, sizeof(f), "f%d", i);
            json_object_set_new(doc, f, json_integer(i));
        }
        lrc = tapi_nosql_put(sess.conn, sess.coll, "wide", doc, 0, NULL);
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else if (strcmp(c, "redis_maxmemory") == 0)
    {
        json_t *cmd;

        cmd = json_pack("{s:[s, s, s, s]}", "argv", "CONFIG", "SET",
                        "maxmemory-policy", "noeviction");
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_native(sess.conn, cmd, NULL));
        json_decref(cmd);
        cmd = json_pack("{s:[s, s, s, s]}", "argv", "CONFIG", "SET",
                        "maxmemory", "4mb");
        TSAPI_NOSQL_CHECK(sess.conn, tapi_nosql_native(sess.conn, cmd, NULL));
        json_decref(cmd);
        redis_limited = true;
        doc = big(1u << 20);
        for (i = 0; i < 20; i++)
        {
            char k[16];

            snprintf(k, sizeof(k), "fill%d", i);
            lrc = tapi_nosql_put(sess.conn, sess.coll, k, doc, 0, NULL);
            if (lrc != 0)
                break;
        }
        cls = tapi_nosql_last_cls(sess.conn);
    }
    else
    {
        json_t *cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);

        many = calloc(300, sizeof(*many));
        for (i = 0; i < 300; i++)
        {
            lrc = tapi_nosql_open(sess.pco, "memcached", cfg, &many[i], &cls);
            if (lrc != 0)
                break;
            opened++;
        }
        json_decref(cfg);
        RING("%d connections opened before the refusal", opened + 1);
    }
    chart[0] = cls;
    tsapi_nosql_chart("Class of the refusal (9 = limit)", NULL, NULL,
                      "class", chart, 1);
    if (lrc == 0)
        TEST_VERDICT("The limit was not reached or not enforced");
    RING("Refused as %s: %s", tapi_nosql_errcls_name(cls),
         sess.conn != NULL ? tapi_nosql_last_msg(sess.conn) : "");
    if (cls != TAPI_NOSQL_ERR_LIMIT)
        TEST_VERDICT("A limit was reported as another kind of failure");
    TEST_SUCCESS;

cleanup:
    if (redis_limited)
    {
        json_t *cmd = json_pack("{s:[s, s, s, s]}", "argv", "CONFIG", "SET",
                                "maxmemory", "0");

        tapi_nosql_native(sess.conn, cmd, NULL);
        json_decref(cmd);
    }
    if (many != NULL)
    {
        for (i = 0; i < opened; i++)
            tapi_nosql_close(many[i]);
        free(many);
    }
    json_decref(doc);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
