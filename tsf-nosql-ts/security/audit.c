/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief security group: the audit and its known-bad controls
 *
 * Audit @p target as the application account, trying the engines'
 * well-known default credentials too, and compare the checks that
 * found something at MEDIUM or above with @p expect: the open servers
 * (no authentication), the weak one (requirepass foobared) and
 * Memcached (UDP on) are the known-bad controls and must be reported
 * for exactly what was planted in them; the hardened ones (TLS, real
 * accounts) must not be reported for what is not there. Findings by
 * severity are charted.
 */

#define TE_TEST_NAME    "security/audit"

#include "te_config.h"
#include "tapi_test.h"

#include "tapi_nosql.h"
#include "tapi_nosql_audit.h"
#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess  sess = { 0 };
    tapi_nosql_report rep = TAPI_NOSQL_REPORT_INIT;
    const char       *target;
    const char       *expect;
    const char       *engine;
    json_t           *cfg = NULL;
    char             *got = NULL;
    double            sev[5] = { 0, 0, 0, 0, 0 };
    unsigned int      i;

    TEST_START;
    TEST_GET_STRING_PARAM(target);
    TEST_GET_STRING_PARAM(expect);
    if (strcmp(expect, "none") == 0)
        expect = "";

    TEST_STEP("Audit %s", target);
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_audit"));
    engine = tsapi_nosql_engine_of(target);
    if (!tapi_nosql_supports(sess.pco, engine))
        TEST_SKIP("The agent has no %s driver", engine);
    cfg = tsapi_nosql_config(target, TSAPI_NOSQL_ROLE_APP);
    CHECK_RC(tapi_nosql_audit(sess.pco, engine, cfg, true, &rep));
    tapi_nosql_report_log(&rep);
    for (i = 0; i < rep.n; i++)
        sev[rep.items[i].severity]++;
    tsapi_nosql_chart("Findings by severity (INFO, LOW, MEDIUM, HIGH, "
                      "CRITICAL)", NULL, NULL, "findings", sev, 5);

    TEST_STEP("It reports exactly what was planted");
    got = tapi_nosql_report_checks(&rep, TAPI_NOSQL_SEV_MEDIUM);
    RING("Found {%s}, planted {%s}", got, expect);
    if (strcmp(got, expect) != 0)
        TEST_VERDICT("The audit of %s found other weaknesses than the "
                     "planted ones", target);
    TEST_SUCCESS;

cleanup:
    free(got);
    tapi_nosql_report_free(&rep);
    json_decref(cfg);
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
