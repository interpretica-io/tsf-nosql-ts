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

The cluster group starts four clusters of its own on a Docker network
(172.31.77.0/24), and removes them after it: a three-member MongoDB
replica set, a Cassandra ring of three, a three-member etcd cluster, and
a Redis primary with a replica.

The open and weak servers are the known-bad controls of the security
group: the audit must report each of them for exactly what was planted
in it. The others are configured the way a deployment should be, and
the audit must not report what is not there.

## Groups

| Group | What it checks |
|---|---|
| `basic` | Connection lifecycle; a document of each kind of JSON reads back equal (64-bit integers, reals, Unicode, nesting, mixed arrays, values that look like operators); keys that need escaping; documents of 1 KiB to 1 MiB; bad requests fail with their class; each engine's and the module's limits reported as limits; each assertion bites |
| `query` | Every key exactly once in bounded pages, for any page size; a key present throughout a scan is seen while other clients write and delete around it; equality queries return exactly the matching documents, checked against the generator; counts follow inserts and deletes |
| `atomic` | Write-if-absent, compare-and-set, concurrent increments with 1, 4 and 16 threads; the lost update (an unprotected read-modify-write loses updates, the atomic counter none); races: 16 clients writing one key if absent (one applies), 8 clients compare-and-setting one document (none lost); several writes all or none, and never seen half applied |
| `expiry` | Entries are there before their TTL and gone after it; an overwrite without a TTL keeps the entry |
| `durability` | Documents and counters survive a graceful restart and acknowledged writes survive SIGKILL on a durable engine; Redis without an append-only file and Memcached lose them (the controls) |
| `faults` | A frozen server (docker pause) and a network partition give a timeout at the deadline and the handle recovers; added delay shows in every round trip and a shorter deadline times out; 30 % packet loss slows reads but fails none; nothing listening is a connection error at once; handles die with their RPC server |
| `cluster` | MongoDB primary killed: a new one is elected and no majority write is lost; secondaries catch up within a bounded lag; etcd leader killed: the client moves to the others and no write is lost; Cassandra consistency levels with one and two nodes down; a Redis replica follows, refuses writes, and WAIT confirms it |
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

Network faults are made with tc netem in the server container's
network namespace, from a small helper image the script builds the
first time (alpine with iproute2).

## Other versions and implementations

`TSF_NOSQL_PROFILE=alt` runs the same suite against other
implementations and older versions of the engines: Valkey 8.1 for
Redis, MongoDB 7.0, ScyllaDB 6.2 for Cassandra (without tablets),
Elasticsearch 7.17, Neo4j 4.4, Memcached 1.5 and etcd 3.4. ScyllaDB needs
more asynchronous I/O contexts than the Docker VM allows by default, so
this profile raises `fs.aio-max-nr` there (a privileged one-shot
container), as ScyllaDB's documentation asks.

`run.sh` adds the TRC tag `profile_alt` for it, and `conf/trc.xml` holds
what differs. One thing does: ScyllaDB acknowledges a write before its
commit log reaches the disk (`commitlog_sync: periodic`, every 10 s)
and keeps that log in its own buffers, so a SIGKILL loses the writes it
acknowledged since the last sync. In the runs so far it lost all 20 of
`durability/crash`, where Cassandra kept all 20. Whether a sync falls
between the last write and the kill is timing, so TRC accepts both
outcomes there.

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
