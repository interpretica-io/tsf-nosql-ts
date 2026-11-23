#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica Unipessoal Lda
#
# The database servers the suite tests against, one container each,
# started for one run and removed after it, all on loopback:
#
#   redis        56379  no password, protected-mode off, AOF on   (open)
#   redis-tls    56380  TLS only, ACL users, default user off     (hardened)
#   redis-weak   56381  requirepass foobared                     (weak)
#   mongodb      57017  no auth, one-member replica set rs0      (open)
#   mongodb-tls  57018  auth, TLS required                       (hardened)
#   cassandra    59042  no authenticator (the image's default)   (open)
#   elasticsearch 59200 security on, HTTP without TLS            (auth)
#   neo4j        57474  password set, HTTP                       (auth)
#   memcached    51211  TCP and UDP                              (open)
#   etcd         52379  no auth                                  (open)
#   etcd-auth    52380  auth on, root and an application user    (auth)
#
# The "open" and "weak" ones are the known-bad controls of the audit:
# the audit must report them. The others are configured the way a
# deployment should be, and the audit must not report what is not there.
#
# Usage: scripts/nosql-servers.sh up|down|restart <name>|pause <name>|
#                                 unpause <name> <work directory>
#
# The suite prologue runs "up" on the agent and the epilogue "down";
# the agent needs a docker client and the Docker socket (the suite's
# build container has both). "restart", "pause" and "unpause" are for
# the durability and fault tests. With TSF_NOSQL_SERVERS=external
# nothing is started or stopped: the suite expects servers set up the
# same way on the same ports.
#
# The passwords are fixtures of servers that live for one run on
# loopback; they are the same constants lib/tsapi_nosql.h has. They are
# still kept off command lines where the image allows it, because the
# suite checks that the module keeps secrets out of what it reports.

set -euo pipefail

cmd="${1:-}"
case "${cmd}" in
    restart|pause|unpause)
        target="${2:-}"
        work="${3:-}"
        ;;
    *)
        target=""
        work="${2:-}"
        ;;
esac
if [ -z "${cmd}" ] || [ -z "${work}" ] ; then
    echo "usage: $0 up|down|restart <name>|pause <name>|unpause <name> <work>" >&2
    exit 2
fi

if [ "${TSF_NOSQL_SERVERS:-}" = "external" ] ; then
    echo "TSF_NOSQL_SERVERS=external: not touching any server"
    exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1 ; then
    echo "no usable docker on this host" >&2
    exit 3
fi

prefix=tsf-nosql-ts

# Fixture credentials; keep in step with lib/tsapi_nosql.h.
app_pw='tsf-fixture-app-3Wq'
admin_pw='tsf-fixture-admin-8Jt'

redis_image="${TSF_NOSQL_REDIS_IMAGE:-redis:7.4}"
mongo_image="${TSF_NOSQL_MONGO_IMAGE:-mongo:8.2}"
cassandra_image="${TSF_NOSQL_CASSANDRA_IMAGE:-cassandra:5.0}"
es_image="${TSF_NOSQL_ES_IMAGE:-docker.elastic.co/elasticsearch/elasticsearch:8.15.3}"
neo4j_image="${TSF_NOSQL_NEO4J_IMAGE:-neo4j:5.26}"
memcached_image="${TSF_NOSQL_MEMCACHED_IMAGE:-memcached:1.6}"
etcd_image="${TSF_NOSQL_ETCD_IMAGE:-gcr.io/etcd-development/etcd:v3.5.17}"

all=(redis redis-tls redis-weak mongodb mongodb-tls cassandra elasticsearch
     neo4j memcached etcd etcd-auth)

name_of() { echo "${prefix}-$1" ; }

down() {
    local n

    for n in "${all[@]}" ; do
        docker rm -f "$(name_of "${n}")" >/dev/null 2>&1 || true
    done
    rm -rf "${work}"
}

make_pki() {
    local pki="${work}/pki"

    mkdir -p "${pki}"
    openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
        -keyout "${pki}/ca.key" -out "${work}/ca.crt" \
        -subj "/CN=tsf-nosql-ts test CA" >/dev/null 2>&1
    openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
        -keyout "${pki}/rogue.key" -out "${work}/rogue-ca.crt" \
        -subj "/CN=tsf-nosql-ts unrelated CA" >/dev/null 2>&1
    openssl req -newkey rsa:2048 -nodes \
        -keyout "${pki}/server.key" -out "${pki}/server.csr" \
        -subj "/CN=localhost" >/dev/null 2>&1
    printf 'subjectAltName=IP:127.0.0.1,DNS:localhost\n' > "${pki}/ext"
    openssl x509 -req -in "${pki}/server.csr" -CA "${work}/ca.crt" \
        -CAkey "${pki}/ca.key" -CAcreateserial -days 30 \
        -extfile "${pki}/ext" -out "${pki}/server.crt" >/dev/null 2>&1
    cat "${pki}/server.crt" "${pki}/server.key" > "${pki}/server.pem"
    chmod 644 "${work}/ca.crt" "${work}/rogue-ca.crt"
}

# Copy a directory tree into a (created, not yet started) container,
# owned by @p owner.
copy_in() {
    local owner="$3"

    # No extended attributes: macOS tar adds com.apple.* ones that the
    # container's filesystem refuses.
    COPYFILE_DISABLE=1 tar --no-xattrs -C "$1" --owner="${owner}" \
        --group="${owner}" -cf - . | docker cp -a - "$2:/"
}

up_redis() {
    docker run -d --name "$(name_of redis)" -p 127.0.0.1:56379:6379 \
        "${redis_image}" redis-server --protected-mode no \
        --appendonly yes --appendfsync always >/dev/null

    docker run -d --name "$(name_of redis-weak)" -p 127.0.0.1:56381:6379 \
        "${redis_image}" redis-server --requirepass foobared >/dev/null

    local root="${work}/redis-tls-root"
    mkdir -p "${root}/tls" "${root}/conf"
    cp "${work}/pki/server.crt" "${work}/pki/server.key" "${root}/tls/"
    cp "${work}/ca.crt" "${root}/tls/ca.crt"
    cat > "${root}/conf/redis.conf" <<CONF
port 0
tls-port 6380
tls-cert-file /tls/server.crt
tls-key-file /tls/server.key
tls-ca-cert-file /tls/ca.crt
tls-auth-clients no
appendonly yes
user default off
user tsf on >${app_pw} ~* &* +@all -@dangerous +info
user tsf_admin on >${admin_pw} ~* &* +@all
CONF
    chmod 600 "${root}/conf/redis.conf" "${root}/tls/server.key"
    docker create --name "$(name_of redis-tls)" -p 127.0.0.1:56380:6380 \
        "${redis_image}" redis-server /conf/redis.conf >/dev/null
    copy_in "${root}" "$(name_of redis-tls)" 999
    docker start "$(name_of redis-tls)" >/dev/null
}

up_mongo() {
    # The replica set member is named by the address clients use, so
    # mongod listens on that port inside the container too.
    docker run -d --name "$(name_of mongodb)" -p 127.0.0.1:57017:57017 \
        "${mongo_image}" mongod --replSet rs0 --bind_ip_all --port 57017 \
        --setParameter ttlMonitorSleepSecs=1 >/dev/null

    local root="${work}/mongo-tls-root"
    mkdir -p "${root}/tls" "${root}/docker-entrypoint-initdb.d"
    cp "${work}/pki/server.pem" "${root}/tls/"
    cp "${work}/ca.crt" "${root}/tls/ca.crt"
    chmod 600 "${root}/tls/server.pem"
    cat > "${root}/docker-entrypoint-initdb.d/10-app.js" <<JS
db.getSiblingDB("tsf").createUser({user: "tsf", pwd: "${app_pw}",
                                   roles: [{role: "readWrite", db: "tsf"}]});
JS
    ( umask 077
      printf 'MONGO_INITDB_ROOT_USERNAME=tsf_admin\nMONGO_INITDB_ROOT_PASSWORD=%s\n' \
          "${admin_pw}" > "${work}/mongo.env" )
    docker create --name "$(name_of mongodb-tls)" -p 127.0.0.1:57018:27017 \
        --env-file "${work}/mongo.env" "${mongo_image}" \
        --tlsMode requireTLS --tlsCertificateKeyFile /tls/server.pem \
        --tlsCAFile /tls/ca.crt --tlsAllowConnectionsWithoutCertificates \
        --setParameter ttlMonitorSleepSecs=1 >/dev/null
    copy_in "${root}" "$(name_of mongodb-tls)" 999
    docker start "$(name_of mongodb-tls)" >/dev/null
}

up_cassandra() {
    docker run -d --name "$(name_of cassandra)" -p 127.0.0.1:59042:9042 \
        -e MAX_HEAP_SIZE=512M -e HEAP_NEWSIZE=128M \
        -e CASSANDRA_CLUSTER_NAME=tsf "${cassandra_image}" >/dev/null
}

up_elasticsearch() {
    ( umask 077
      printf 'ELASTIC_PASSWORD=%s\n' "${admin_pw}" > "${work}/es.env" )
    docker run -d --name "$(name_of elasticsearch)" \
        -p 127.0.0.1:59200:9200 --env-file "${work}/es.env" \
        -e discovery.type=single-node -e "ES_JAVA_OPTS=-Xms512m -Xmx512m" \
        -e xpack.security.enabled=true \
        -e xpack.security.http.ssl.enabled=false \
        -e xpack.security.transport.ssl.enabled=false \
        -e cluster.routing.allocation.disk.threshold_enabled=false \
        "${es_image}" >/dev/null
}

up_neo4j() {
    ( umask 077
      printf 'NEO4J_AUTH=neo4j/%s\n' "${app_pw}" > "${work}/neo4j.env" )
    docker run -d --name "$(name_of neo4j)" -p 127.0.0.1:57474:7474 \
        --env-file "${work}/neo4j.env" \
        -e NEO4J_server_memory_heap_max__size=512m \
        -e NEO4J_server_memory_pagecache_size=128m \
        -e NEO4J_dbms_security_auth__max__failed__attempts=0 \
        "${neo4j_image}" >/dev/null
}

up_memcached() {
    docker run -d --name "$(name_of memcached)" \
        -p 127.0.0.1:51211:11211/tcp -p 127.0.0.1:51211:11211/udp \
        "${memcached_image}" memcached -U 11211 -m 64 >/dev/null
}

up_etcd() {
    local n

    for n in etcd etcd-auth ; do
        local port=52379
        [ "${n}" = etcd-auth ] && port=52380
        docker run -d --name "$(name_of "${n}")" \
            -p "127.0.0.1:${port}:2379" "${etcd_image}" \
            /usr/local/bin/etcd --name "${n}" --data-dir /etcd-data \
            --listen-client-urls http://0.0.0.0:2379 \
            --advertise-client-urls "http://127.0.0.1:${port}" >/dev/null
    done
}

# Wait until @p cmd (run in container @p name) succeeds.
wait_for() {
    local name="$1" timeout="$2"
    shift 2
    local i

    for i in $(seq 1 "${timeout}") ; do
        if docker exec "$(name_of "${name}")" "$@" >/dev/null 2>&1 ; then
            return 0
        fi
        if [ "$(docker inspect -f '{{.State.Running}}' "$(name_of "${name}")" \
                2>/dev/null)" != "true" ] ; then
            echo "${name} exited:" >&2
            docker logs --tail 30 "$(name_of "${name}")" >&2 || true
            return 1
        fi
        sleep 1
    done
    echo "${name} did not become ready in ${timeout} s" >&2
    docker logs --tail 30 "$(name_of "${name}")" >&2 || true
    return 1
}

es_curl() {
    docker exec "$(name_of elasticsearch)" curl -s -f -u "elastic:${admin_pw}" \
        -H 'Content-Type: application/json' "$@"
}

ready_all() {
    wait_for redis 60 redis-cli ping
    wait_for redis-weak 60 redis-cli -a foobared --no-auth-warning ping
    wait_for redis-tls 60 redis-cli --tls -p 6380 --cacert /tls/ca.crt \
        --user tsf --pass "${app_pw}" --no-auth-warning ping

    wait_for mongodb 120 mongosh --port 57017 --quiet --eval 'db.runCommand({ping:1})'
    docker exec "$(name_of mongodb)" mongosh --port 57017 --quiet --eval \
        'rs.initiate({_id:"rs0",members:[{_id:0,host:"127.0.0.1:57017"}]})' \
        >/dev/null
    wait_for mongodb 120 mongosh --port 57017 --quiet --eval \
        'if (!db.hello().isWritablePrimary) quit(1)'
    wait_for mongodb-tls 180 mongosh --quiet --tls --tlsCAFile /tls/ca.crt \
        --host localhost -u tsf -p "${app_pw}" --authenticationDatabase tsf \
        --eval 'db.runCommand({ping:1})'

    wait_for memcached 30 true
    wait_for etcd 60 etcdctl endpoint health
    wait_for etcd-auth 60 etcdctl endpoint health
    if ! docker exec "$(name_of etcd-auth)" etcdctl auth status 2>/dev/null |
            grep -q 'Authentication Status: true' ; then
        # The image has no shell: one etcdctl per step.
        local e=(docker exec "$(name_of etcd-auth)" etcdctl)
        "${e[@]}" user add --interactive=false "root:${admin_pw}" >/dev/null
        "${e[@]}" role add tsf_rw >/dev/null
        "${e[@]}" role grant-permission tsf_rw readwrite --prefix=true / \
            >/dev/null
        "${e[@]}" user add --interactive=false "tsf:${app_pw}" >/dev/null
        "${e[@]}" user grant-role tsf tsf_rw >/dev/null
        "${e[@]}" auth enable >/dev/null
    fi

    wait_for elasticsearch 240 curl -s -f -u "elastic:${admin_pw}" \
        'http://localhost:9200/_cluster/health?wait_for_status=yellow&timeout=1s'
    es_curl -X PUT http://localhost:9200/_security/role/tsf_rw \
        -d '{"cluster":["monitor"],"indices":[{"names":["*"],"privileges":["all"]}]}' >/dev/null
    es_curl -X PUT http://localhost:9200/_security/user/tsf \
        -d "{\"password\":\"${app_pw}\",\"roles\":[\"tsf_rw\"]}" >/dev/null

    wait_for neo4j 240 cypher-shell -u neo4j -p "${app_pw}" 'RETURN 1'
    wait_for cassandra 300 cqlsh -e 'SELECT release_version FROM system.local'
}

ready_one() {
    case "$1" in
        redis)          wait_for redis 60 redis-cli ping ;;
        mongodb)        wait_for mongodb 120 mongosh --port 57017 --quiet --eval \
                            'if (!db.hello().isWritablePrimary) quit(1)' ;;
        cassandra)      wait_for cassandra 300 cqlsh -e \
                            'SELECT release_version FROM system.local' ;;
        elasticsearch)  wait_for elasticsearch 240 curl -s -f \
                            -u "elastic:${admin_pw}" \
                            'http://localhost:9200/_cluster/health?wait_for_status=yellow&timeout=1s' ;;
        neo4j)          wait_for neo4j 240 cypher-shell -u neo4j \
                            -p "${app_pw}" 'RETURN 1' ;;
        memcached)      wait_for memcached 30 true ;;
        etcd)           wait_for etcd 60 etcdctl endpoint health ;;
        *)              sleep 2 ;;
    esac
}

# All the servers running (TSF_NOSQL_KEEP=1 left them up)?
all_running() {
    local n

    [ -f "${work}/ca.crt" ] || return 1
    for n in "${all[@]}" ; do
        [ "$(docker inspect -f '{{.State.Running}}' "$(name_of "${n}")" \
              2>/dev/null)" = "true" ] || return 1
    done
}

case "${cmd}" in
    up)
        if [ "${TSF_NOSQL_KEEP:-}" = "1" ] && all_running ; then
            echo "TSF_NOSQL_KEEP=1: reusing the running servers"
            ready_all
            exit 0
        fi
        down
        mkdir -p "${work}"
        make_pki
        up_redis
        up_mongo
        up_cassandra
        up_elasticsearch
        up_neo4j
        up_memcached
        up_etcd
        ready_all
        echo "all servers are up"
        ;;
    down)
        down
        ;;
    restart)
        docker restart -t 30 "$(name_of "${target}")" >/dev/null
        ready_one "${target}"
        ;;
    pause)
        docker pause "$(name_of "${target}")" >/dev/null
        ;;
    unpause)
        docker unpause "$(name_of "${target}")" >/dev/null
        ;;
    *)
        echo "unknown command ${cmd}" >&2
        exit 2
        ;;
esac
