#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# One table -> the suite's package.xml files, meson.build files, group
# prologues and epilogues, and conf/trc.xml. Edit the table, run this,
# review the diff. The test sources themselves (<group>/<test>.c) are
# written by hand.
#
# TRC: every iteration is expected PASSED, except
#  - SKIPPED, with the verdict TSAPI_NOSQL_REQUIRE() emits, where the
#    engine lacks what the test needs (the CAPS table, which must agree
#    with the drivers' capabilities in tsf-nosql);
#  - the entries of EXPECTED: known limits and the assertion controls.
import itertools
import os
import sys
from xml.sax.saxutils import escape

TOP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SUITE = os.path.join(TOP, "tsf-nosql-ts")
NAME = "tsf-nosql-ts"

BACKENDS = ["redis", "mongodb", "cassandra", "elasticsearch", "neo4j",
            "memcached", "etcd"]

# What each engine does natively: the drivers' TA_NOSQL_CAP_* sets.
ALL = {"scan", "find", "count", "ttl", "cas", "absent", "incr",
       "incr_exact", "multi", "multi_if", "durable"}
CAPS = {
    "redis": ALL - {"find", "count"},
    "mongodb": ALL,
    "cassandra": ALL - {"find", "incr_exact", "multi_if"},
    "elasticsearch": ALL - {"ttl", "multi", "multi_if"},
    "neo4j": ALL - {"ttl"},
    "memcached": {"ttl", "cas", "absent", "incr", "incr_exact"},
    "etcd": ALL - {"find"},
}

TARGETS = ["redis", "redis-tls", "redis-weak", "mongodb", "mongodb-tls",
           "cassandra", "elasticsearch", "neo4j", "memcached", "etcd",
           "etcd-auth"]

# The findings at MEDIUM or above each target's audit must report: the
# open and weak servers are the known-bad controls.
AUDIT_EXPECT = {
    "redis": "nosql.auth.none,nosql.tls.plaintext,redis.config-access,"
             "redis.protected-mode",
    "redis-tls": "none",
    "redis-weak": "nosql.auth.default,nosql.tls.plaintext,"
                  "redis.config-access",
    "mongodb": "nosql.auth.none,nosql.tls.plaintext",
    "mongodb-tls": "none",
    "cassandra": "nosql.auth.none,nosql.tls.plaintext",
    "elasticsearch": "nosql.tls.plaintext",
    "neo4j": "nosql.tls.plaintext",
    "memcached": "memcached.udp-reflection,nosql.auth.none,"
                 "nosql.tls.plaintext",
    "etcd": "nosql.auth.none,nosql.tls.plaintext",
    "etcd-auth": "nosql.tls.plaintext",
}

B = ("backend", BACKENDS)

# group -> (description, [ (test, objective, [args], needs) ]); args is a
# list of (name, [values]) whose product is the iterations, or a list of
# such lists (one <run> each); needs is the capability the test skips
# without, or a function of the iteration giving it.
GROUPS = [
    ("basic", "Connections, documents, keys, sizes, errors", [
        ("lifecycle", "Open, ask the server about itself, ping, close; a "
         "closed handle is refused.", [B], None),
        ("crud", "A document of each kind of JSON reads back equal; "
         "overwrite replaces it, delete removes it.",
         [B, ("doc", ["scalars", "int64", "reals", "unicode", "nested",
                      "arrays", "operators", "empty"])], None),
        ("keys", "Keys that need escaping somewhere (slash, percent, "
         "query characters, Unicode, 200 bytes) round-trip.", [B], None),
        ("large", "A document of the given size round-trips intact.",
         [B, ("size", ["1024", "262144", "1048576"])], None),
        ("errors", "Bad requests fail as invalid and leave the server "
         "alone.", [B], None),
        ("assert_violation", "Each assertion of tapi_nosql_assert, "
         "provoked, reports its stable verdict.",
         [("backend", ["mongodb"]),
          ("check", ["doc", "absent", "count", "keys", "counter",
                     "eventually"])], None),
    ]),
    ("query", "Listing, paging, querying and counting", [
        ("scan", "Every key of a collection exactly once, in bounded "
         "pages.", [B], "scan"),
        ("paging", "Any page size gives every key once.",
         [B, ("limit", ["1", "7", "100", "1000"])], "scan"),
        ("find", "Equality queries on top-level fields return exactly "
         "the matching documents.", [B], "find"),
        ("count", "The count follows inserts and deletes.", [B], "count"),
    ]),
    ("atomic", "Conditional writes, counters, lost updates, multi-key "
     "writes", [
        ("absent", "Write-if-absent applies once.", [B], "absent"),
        ("cas", "Compare-and-set applies with the current version only.",
         [B], "cas"),
        ("counter", "Concurrent increments: none lost; an exact counter "
         "returns each value once.", [B, ("threads", ["1", "4", "16"])],
         "incr"),
        ("lost_update", "Unprotected read-modify-write loses updates (the "
         "control); the atomic counter loses none.", [B], "incr"),
        ("multi", "Several writes are all applied or none; a failed "
         "condition applies nothing.", [B], "multi"),
    ]),
    ("expiry", "Entries that expire", [
        ("ttl", "An entry is there before its TTL and gone after it.",
         [B, ("ttl_s", ["1", "3"])], "ttl"),
        ("ttl_cleared", "Overwriting without a TTL keeps the entry.",
         [B], "ttl"),
    ]),
    ("durability", "Data across a server restart", [
        ("restart", "Documents and counters survive a restart of a "
         "durable server; a memory-only one loses them.", [B], None),
    ]),
    ("faults", "Deadlines, dead servers, stale handles", [
        ("timeout", "A server that stops answering gives a timeout "
         "within the deadline; the handle works again after.", [B], None),
        ("refused", "Nothing listening: a connection error, at once.",
         [B], None),
        ("handles", "Handles die with the RPC server that held them.",
         [B], None),
    ]),
    ("security", "Audit with known-bad controls, TLS, authentication, "
     "secrets, injection, privileges", [
        ("audit", "The audit reports exactly the planted weaknesses.",
         [[("target", [t]), ("expect", [AUDIT_EXPECT[t]])]
          for t in TARGETS], None),
        ("tls", "The suite's CA is trusted, an unrelated CA is refused, "
         "plaintext to a TLS port fails.",
         [("target", ["redis-tls", "mongodb-tls"]),
          ("ca", ["good", "rogue", "plain"])], None),
        ("auth", "A wrong password and no credentials are refused; the "
         "right ones work.",
         [("target", ["redis-tls", "redis-weak", "mongodb-tls",
                      "elasticsearch", "neo4j", "etcd-auth"])], None),
        ("secrets", "No password in an error, a result, the registry or "
         "an audit.",
         [("target", ["redis-tls", "redis-weak", "mongodb-tls",
                      "elasticsearch", "neo4j", "etcd-auth"])], None),
        ("injection", "Input concatenated into a query bypasses a login "
         "(the control); the same input through tapi_nosql_find() does "
         "not.", [("backend", ["mongodb", "neo4j", "elasticsearch"])],
         None),
        ("privilege", "The application account cannot administer the "
         "server; the administrator can.",
         [("target", ["redis-tls", "mongodb-tls", "elasticsearch",
                      "etcd-auth"])], None),
    ]),
    ("perf", "Latency and throughput", [
        ("latency", "Latency percentiles of one client by operation and "
         "document size.", [B, ("op", ["put", "get"]),
                            ("size", ["64", "1024", "16384"])], None),
        ("throughput", "Throughput of concurrent clients.",
         [B, ("threads", ["1", "4", "16"])], None),
    ]),
    ("native", "Each engine's own command language", [
        ("commands", "Native commands answer as the engine documents.",
         [B], None),
    ]),
    ("compat", "The same data across engines", [
        ("equivalence", "The same documents read back equal from every "
         "engine.", [], None),
    ]),
]

# (group, test, iteration) -> (result, [verdicts])
EXPECTED = {}
for check, verdict in [
        ("doc", "Document %s/%s is not what was expected"),
        ("absent", "Document %s/%s is present but should be absent"),
        ("count", "Collection %s does not hold the expected number of "
                  "documents"),
        ("keys", "Collection %s does not hold the expected keys"),
        ("counter", "Counter %s/%s does not hold the expected value"),
        ("eventually", "Document %s/%s did not expire in time")]:
    v = verdict.replace("%s/%s", "violation/probe").replace(
        "%s", "violation")
    EXPECTED[("basic", "assert_violation",
              (("backend", "mongodb"), ("check", check)))] = ("FAILED", [v])
# Memcached keeps items of at most 1 MiB (its -I default), the item's
# own overhead included: a 1 MiB document is over it.
EXPECTED[("basic", "large", (("backend", "memcached"),
                             ("size", "1048576")))] = (
    "FAILED", ["The engine refused the document as too large"])


def runs_of(args):
    if args and isinstance(args[0], list):
        return args
    return [args]


def iterations(run):
    names = [a[0] for a in run]
    for combo in itertools.product(*[a[1] for a in run]):
        yield tuple(zip(names, combo))


def expectation(group, test, needs, it):
    if (group, test, it) in EXPECTED:
        return EXPECTED[(group, test, it)]
    d = dict(it)
    if needs is not None and "backend" in d:
        cap = needs(d) if callable(needs) else needs
        if cap is not None and cap not in CAPS[d["backend"]]:
            return ("SKIPPED", ["%s does not %s" % (d["backend"], cap)])
    return ("PASSED", [])


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    old = open(path).read() if os.path.exists(path) else None
    if old != text:
        with open(path, "w") as f:
            f.write(text)


def group_files(group, desc, tests):
    runs = []
    for test, obj, args, _ in tests:
        for run in runs_of(args):
            lines = ['        <run>',
                     '            <script name="%s"><objective>%s'
                     '</objective></script>' % (test, escape(obj))]
            for name, values in run:
                lines.append('            <arg name="%s">%s</arg>' % (
                    name, "".join("<value>%s</value>" % escape(v)
                                  for v in values)))
            lines.append('        </run>')
            runs.append("\n".join(lines))
    pkg = """<?xml version="1.0" ?>
<!-- Generated by scripts/gen-suite.py; edit the table there. -->
<package version="1.0">
    <description>%s</description>
    <author mailto="maxim.menshikov@interpretica.io" />
    <session track_conf="silent">
        <prologue><script name="prologue"><objective>Prepare the %s group.</objective></script></prologue>
        <epilogue><script name="epilogue"><objective>Finalize the %s group.</objective></script></epilogue>
%s
    </session>
</package>
""" % (escape(desc), group, group, "\n".join(runs))
    write(os.path.join(SUITE, group, "package.xml"), pkg)
    names = ["prologue", "epilogue"] + [t[0] for t in tests]
    meson = """# Copyright (C) 2026 Interpretica Unipessoal Lda
# Generated by scripts/gen-suite.py; edit the table there.
tests = [ %s ]
foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach
install_data(['package.xml'], install_dir: package_dir)
""" % ", ".join("'%s'" % n for n in names)
    write(os.path.join(SUITE, group, "meson.build"), meson)
    for which in ("prologue", "epilogue"):
        c = """/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief %s group %s
 *
 * Generated by scripts/gen-suite.py. Nothing to set up or tear down:
 * each test opens its own RPC server and connection and uses a
 * collection of its own, so a test can run alone.
 */

#define TE_TEST_NAME    "%s/%s"

#include "te_config.h"
#include "tapi_test.h"

int
main(int argc, char **argv)
{
    TEST_START;
    TEST_STEP("%s group %s");
    TEST_SUCCESS;
cleanup:
    TEST_END;
}
""" % (group, which, group, which, group, which)
        write(os.path.join(SUITE, group, which + ".c"), c)


def trc_test(group, name, obj, needs, its, ind):
    out = ['%s<test name="%s" type="script">' % (ind, name),
           "%s  <objective>%s</objective>" % (ind, escape(obj)),
           "%s  <notes />" % ind]
    for it in its:
        res, verdicts = expectation(group, name, needs, it)
        out.append('%s  <iter result="%s">' % (ind, res))
        for k, v in it:
            out.append('%s    <arg name="%s">%s</arg>' % (ind, k, escape(v)))
        out.append("%s    <notes />" % ind)
        if verdicts:
            out.append('%s    <results><result value="%s">%s</result>'
                       '</results>' % (ind, res, "".join(
                           "<verdict>%s</verdict>" % escape(v)
                           for v in verdicts)))
        else:
            out.append('%s    <results><result value="%s" /></results>'
                       % (ind, res))
        out.append("%s  </iter>" % ind)
    out.append("%s</test>" % ind)
    return out


def main():
    top_runs = "\n".join('        <run><package name="%s" /></run>' % g[0]
                         for g in GROUPS)
    write(os.path.join(SUITE, "package.xml"), """<?xml version="1.0" ?>
<!-- Generated by scripts/gen-suite.py; edit the table there. -->
<package version="1.0">
    <description>tsf-nosql NoSQL database testing suite</description>
    <author mailto="maxim.menshikov@interpretica.io" />

    <!-- @page nosql-ts NoSQL Test Suite
          @objective Exercise tsf-nosql against real servers of seven
                     engines - Redis, MongoDB, Cassandra, Elasticsearch,
                     Neo4j, Memcached and etcd - started in containers
                     by the suite prologue.

          @par Test Groups:
%s
    -->

    <session track_conf="silent">
        <prologue>
            <script name="prologue"><objective>Start the database servers; record the drivers the agent has.</objective></script>
        </prologue>
        <epilogue>
            <script name="epilogue"><objective>Stop the database servers.</objective></script>
        </epilogue>

%s
    </session>
</package>
""" % ("\n".join("          - %s - %s" % (g[0], escape(g[1]))
                 for g in GROUPS), top_runs))

    for group, desc, tests in GROUPS:
        group_files(group, desc, tests)

    subdirs = "\n".join("""package_dir = join_paths(mydir, '%s')
package_tests_c = []
subdir('%s')
""" % (g[0], g[0]) for g in GROUPS)
    with open(os.path.join(SUITE, "meson.build.in")) as f:
        tmpl = f.read()
    write(os.path.join(SUITE, "meson.build"),
          tmpl.replace("@GROUPS@", subdirs))

    lines = ['<?xml version="1.0" encoding="UTF-8" ?>',
             '<!-- Generated by scripts/gen-suite.py; edit the table '
             'there. -->',
             '<trc_db unknown_exp_status="passed_ok">',
             '  <test name="%s" type="package">' % NAME,
             "    <objective>NoSQL Test Suite</objective>",
             "    <notes />", '    <iter result="PASSED">', "      <notes />"]
    lines += trc_test("", "prologue", "Test suite prologue", None, [()],
                      "      ")
    total = 0
    for group, desc, tests in GROUPS:
        lines += ['      <test name="%s" type="package">' % group,
                  "        <objective>%s</objective>" % escape(desc),
                  "        <notes />", '        <iter result="PASSED">',
                  "          <notes />"]
        lines += trc_test(group, "prologue", "Group prologue", None, [()],
                          "          ")
        for test, obj, args, needs in tests:
            its = []
            for run in runs_of(args):
                its += list(iterations(run))
            total += len(its)
            lines += trc_test(group, test, obj, needs, its, "          ")
        lines += trc_test(group, "epilogue", "Group epilogue", None, [()],
                          "          ")
        lines += ["        </iter>", "      </test>"]
    lines += trc_test("", "epilogue", "Test suite epilogue", None, [()],
                      "      ")
    lines += ["    </iter>", "  </test>", "</trc_db>"]
    write(os.path.join(TOP, "conf", "trc.xml"), "\n".join(lines) + "\n")
    print("%d test iterations in %d groups" % (total, len(GROUPS)))


if __name__ == "__main__":
    sys.exit(main())
