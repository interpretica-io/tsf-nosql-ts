/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Test Suite epilogue
 *
 * Stop and remove the database servers, unless TSF_NOSQL_KEEP=1 asks
 * to leave them up for the next run (scripts/nosql-servers.sh "up"
 * reuses running ones then).
 *
 * @author Maxim Menshikov <maxim.menshikov@interpretica.io>
 */

#define TE_TEST_NAME    "epilogue"

#include "te_config.h"
#include "tapi_test.h"

#include "tsapi_nosql.h"

int
main(int argc, char **argv)
{
    tsapi_nosql_sess sess = { 0 };
    const char      *keep = getenv("TSF_NOSQL_KEEP");

    TEST_START;

    if (keep != NULL && strcmp(keep, "1") == 0)
    {
        RING("TSF_NOSQL_KEEP=1: leaving the servers up");
        TEST_SUCCESS;
    }
    TEST_STEP("Stop the database servers");
    CHECK_RC(tsapi_nosql_sess_init(&sess, "pco_nosql_epilogue"));
    rc = tsapi_nosql_servers(sess.pco, "down", NULL);
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_EOPNOTSUPP)
        WARN("Stopping the database servers failed: %r", rc);

    TEST_SUCCESS;

cleanup:
    tsapi_nosql_sess_fini(&sess);
    TEST_END;
}
