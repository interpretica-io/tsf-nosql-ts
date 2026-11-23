# tsf-nosql-ts

The Test Environment (TE) suite of
[tsf-nosql](https://github.com/interpretica-io/tsf-nosql): seven NoSQL
engines - Redis, MongoDB, Cassandra, Elasticsearch, Neo4j, Memcached
and etcd - reached from a Test Agent over their own client libraries,
and driven from the tests over RPC.

The suite prologue starts eleven database servers in containers on
loopback (`scripts/nosql-servers.sh`), and the epilogue removes them:

| Target | Port | Set up as |
|---|---|---|
| redis | 56379 | no password, protected-mode off, append-only file (open) |
| redis-tls | 56380 | TLS only, ACL users, default user disabled |
| redis-weak | 56381 | `requirepass foobared` (weak) |
| mongodb | 57017 | no auth, one-member replica set (open) |
| mongodb-tls | 57018 | auth, TLS required |
| cassandra | 59042 | no authenticator, the image's default (open) |
| elasticsearch | 59200 | security on, HTTP without TLS |
| neo4j | 57474 | password set, HTTP |
| memcached | 51211 | TCP and UDP (open) |
| etcd | 52379 | no auth (open) |
| etcd-auth | 52380 | auth on, root and an application user |

The open and weak servers are the known-bad controls of the security
group: the audit must report each of them for exactly what was planted
in it. The others are configured the way a deployment should be, and
the audit must not report what is not there.

## Groups

| Group | What it checks |
|---|---|
| `basic` | Connection lifecycle; a document of each kind of JSON reads back equal (64-bit integers, reals, Unicode, nesting, mixed arrays, values that look like operators); keys that need escaping; documents of 1 KiB to 1 MiB; bad requests fail with their class; each assertion bites |
| `query` | Every key exactly once in bounded pages, for any page size; equality queries return exactly the matching documents, checked against the generator; counts follow inserts and deletes |
| `atomic` | Write-if-absent, compare-and-set, concurrent increments with 1, 4 and 16 threads; the lost update (an unprotected read-modify-write loses updates, the atomic counter none); several writes all or none, under a condition |
| `expiry` | Entries are there before their TTL and gone after it; an overwrite without a TTL keeps the entry |
| `durability` | Documents and counters survive a server restart on a durable engine; Memcached loses them, as documented |
| `faults` | A frozen server (docker pause) gives a timeout at the deadline and the handle recovers; nothing listening is a connection error at once; handles die with their RPC server |
| `security` | The audit of every target against what was planted in it; TLS with the right CA, an unrelated CA and none; refused logins; no password in any error, result or report; injection through an application input; the application account cannot administer the server |
| `perf` | Latency percentiles of one client by operation and size; throughput of 1, 4 and 16 clients |
| `native` | Each engine's own command language answers as the engine documents |
| `compat` | The same documents read back equal from every engine |

Every test charts what it measured as a native TE MI line graph.

Known results, recorded in `conf/trc.xml`:

- an operation an engine does not do natively skips, with the verdict
  "<engine> does not <capability>" (Memcached does not scan,
  Elasticsearch has no TTL, and so on);
- `basic/assert_violation` fails on purpose, each iteration with the
  verdict of the assertion it provokes;
- `basic/large` with Memcached and 1 MiB fails: Memcached keeps items of
  at most 1 MiB, overhead included.

`scripts/gen-suite.py` generates the package files, the build files and
`conf/trc.xml` from one table; edit the table and run it.

## Run

```bash
git clone https://github.com/interpretica-io/test-environment ../test-environment
./scripts/run.sh docker guess --cfg=localhost > run.out 2>&1
scripts/check-run.sh run.out
```

The first run builds the image (the Cassandra C driver from source) and
TE, which takes a while. `TE_DOCKER_CACHE=<volume>` keeps the build in a
named volume between runs, and `TSF_NOSQL_KEEP=1` leaves the servers up
for the next run. The server images can be chosen with
`TSF_NOSQL_REDIS_IMAGE`, `TSF_NOSQL_MONGO_IMAGE` and so on (see
`scripts/nosql-servers.sh`); MongoDB 8.0 does not start on recent
Docker Desktop kernels, so the default is 8.2.

`run.sh` exits 0 whenever the run completes; `scripts/check-run.sh`
decides whether it matched `conf/trc.xml`.

## Licence

MIT, see `LICENSE`.
