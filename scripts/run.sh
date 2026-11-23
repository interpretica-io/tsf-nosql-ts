#!/bin/bash
RUNDIR="$(pwd -P)"
export TS_TOPDIR="$(cd "$(dirname "$(which "$0")")"/.. ; pwd -P)"
export TE_TS_TSF_NOSQL=${TS_TOPDIR}/tsf-nosql-ts

function show_usage() {
    echo "Usage:"
    echo "    $0 [parameters] ..."
    echo
    echo "For the ease of use, you can use:"
    echo "    $0 guess [parameters] ..."
    echo "This variant expect the following folder structure:"
    echo " - tsf-nosql-ts"
    echo " - test-environment"
    echo
}

function run_fail() {
    echo "Error:" $@ >&2
    exit 1
}

if [ -f /tmp/te.dhcpd.leases ] ; then
    sudo rm /tmp/te.dhcpd*.leases
fi

is_docker="0"
if [ "$1" == "docker" ] ; then
    is_docker="1"
    shift 1
fi

if [ "$1" == "guess" ] ; then
    . ${TS_TOPDIR}/scripts/env.sh
    shift 1
fi

if [ "$is_docker" == "1" ] ; then
    export TE_DOCKER_FILE_DIR="${TS_TOPDIR}/scripts/docker/build"
    export TE_DOCKER_TAG="nosql-ts-build"
    # The sibling directory as well as the suite and TE, because
    # conf/external.yml tells you to point a library's url at the
    # checkout you are working in and git clones a filesystem path as
    # happily as a URL - but only if that path exists inside the
    # container too. Without this the advice in external.yml is true
    # outside Docker and quietly false within it.
    export TE_DOCKER_MOUNT_PATHS="${TS_TOPDIR}:${TE_BASE}:${TS_TOPDIR}/.."
    export TE_DOCKER_WORK_DIR="$(pwd)"
    # git refuses to touch a repository whose directory belongs to
    # another user, and everything under a bind mount does: the
    # container runs as the host uid, the files carry whatever the
    # mount driver reports, and the two need not agree. The Builder
    # clones the external repositories under build/, so without this
    # it stops with "detected dubious ownership" before anything is
    # compiled. Passed as environment rather than written into a
    # gitconfig because the container's home directory is part of the
    # image and does not survive it.
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0="safe.directory"
    export GIT_CONFIG_VALUE_0="*"
    # Agents in containers that share the host network share abstract
    # sockets named TE_{LOGGER,RCF,CS}_$TE_ID, and dispatcher.sh defaults
    # TE_ID to its PID: two containers whose dispatchers got the same low
    # PID collide (EADDRINUSE). Pin a suite-unique TE_ID instead.
    export TE_ID="${TE_ID:-nosql$$}"
    export TE_DOCKER_ENV="TE_BASE:TS_TOPDIR:TE_ID:GIT_CONFIG_COUNT:GIT_CONFIG_KEY_0:GIT_CONFIG_VALUE_0"
    # Where the database servers are, when the suite is not to start its
    # own (TSF_NOSQL_SERVERS=external), and which images to start
    # otherwise. Names only: the values go from this environment into
    # the container's and appear on no command line.
    for ts_var in TSF_NOSQL_SERVERS TSF_NOSQL_KEEP TSF_NOSQL_PROFILE \
                  TSF_NOSQL_REDIS_IMAGE \
                  TSF_NOSQL_MONGO_IMAGE TSF_NOSQL_CASSANDRA_IMAGE \
                  TSF_NOSQL_ES_IMAGE TSF_NOSQL_NEO4J_IMAGE \
                  TSF_NOSQL_MEMCACHED_IMAGE TSF_NOSQL_ETCD_IMAGE ; do
        if [ -n "${!ts_var:-}" ] ; then
            export TE_DOCKER_ENV="${TE_DOCKER_ENV}:${ts_var}"
        fi
    done
    # The log directory lives on the bind mount; on Docker Desktop a
    # directory made inside the container can be missing for a moment
    # on the host side and the Logger fails to start. Make it here.
    mkdir -p "${TS_TOPDIR}/tmp"
    # TE_DOCKER_CACHE=<volume name> keeps the build tree (TE, the module,
    # ccache) in a named Docker volume between runs, so a run after a
    # change rebuilds only what changed instead of all of TE. Off by
    # default: a CI run should start clean.
    if [ -n "${TE_DOCKER_CACHE:-}" ] ; then
        export TE_DOCKER_EXTRA_ARGS="-v ${TE_DOCKER_CACHE}:/tmp/aibuild"
    fi
    ${TS_TOPDIR}/scripts/docker_env.sh ${TS_TOPDIR}/scripts/run.sh $@
    exit $?
fi

if [ -d "${TS_TOPDIR}/../test-environment" ] ; then
    export TE_BASE="${TS_TOPDIR}/../test-environment"
fi

if test -z "${TE_BASE}" ; then
    if test -e dispatcher.sh ; then
        export TE_BASE="${RUNDIR}"
    elif test -e "${TS_TOPDIR}/dispatcher.sh" ; then
        export TE_BASE="${TS_TOPDIR}"
    else
        show_usage
        run_fail "Path to TE sources MUST be specified in TE_BASE"
    fi
fi

usage() {
    cat <<EOF
USAGE: run.sh [run.sh options] [dispatcher.sh options]
Options:
    --cfg=<CFG>             Configuration to be used
EOF
${TE_BASE}/dispatcher.sh --help
exit 1
}

TS_OPTS=""
TS_CFG="localhost"
while test -n "$1" ; do
    case $1 in
        --help)
            usage
            ;;
        --cfg=*)
            TS_CFG=${1#--cfg=}
            ;;
        *)
            TS_OPTS+="$1 "
            ;;
    esac
    shift 1
done

source "${TE_BASE}/scripts/lib.meta"

te_meta_test_suite tsf-nosql-ts ATS
te_meta_add "CFG" "$TS_CFG"

test -z "${TS_CFG}" || TS_OPTS+="--opts=run.conf.${TS_CFG} "

echo "Config is ${TS_CFG}"

# Build off the bind mount: on Docker Desktop the mounted tree's
# virtiofs timestamps drift against the VM clock and ninja loops with
# "manifest still dirty". A container-local build dir avoids it.
export TE_BUILD_LOCAL_OVERRIDE=1
export TE_BUILD="/tmp/aibuild"
mkdir -p "${TE_BUILD}"
if test -z "${TE_BUILD}" ; then
    if test "${TS_TOPDIR}" = "${RUNDIR}" ; then
        TE_BUILD="${TS_TOPDIR}/build"
        mkdir -p build
    else
        TE_BUILD="${RUNDIR}"
    fi
    export TE_BUILD
fi

if [ "${TE_WORKSPACE_DIR}" == "" ] ; then
    export TE_WORKSPACE_DIR="${TE_BUILD}/workspace"
fi
# The Builder only uses the workspace directory when it exists; keep it
# (and ccache) under TE_BUILD so a cached TE_BUILD caches everything.
mkdir -p "${TE_WORKSPACE_DIR}"
export CCACHE_DIR="${CCACHE_DIR:-${TE_BUILD}/ccache}"

TS_CONF_DIRS=
TS_CONF_DIRS+="\"${TS_TOPDIR}\"/conf:"

TS_DEFAULT_OPTS=

TS_DEFAULT_OPTS+="--conf-dirs=${TS_CONF_DIRS} "
TS_DEFAULT_OPTS+="--external=external.yml "
TS_DEFAULT_OPTS+="--build-parallel "
TS_DEFAULT_OPTS+="--trc-db=\"${TS_TOPDIR}\"/conf/trc.xml "
TS_DEFAULT_OPTS+="--trc-tag=trc_test1 --trc-tag=trc_test2 "
# The other implementations and versions (scripts/nosql-servers.sh) have
# expected results of their own in conf/trc.xml.
if [ "${TSF_NOSQL_PROFILE:-}" = "alt" ] ; then
    TS_DEFAULT_OPTS+="--trc-tag=profile_alt "
fi

export TE_TMP="${TS_TOPDIR}/tmp"
mkdir -p "${TE_TMP}"

export TE_TS_SRC="${TS_TOPDIR}/tsf-nosql-ts"

eval "${TE_BASE}/dispatcher.sh ${TS_DEFAULT_OPTS} ${TS_OPTS} " \
    "--script=scripts/ta-def"
if [ "$?" == "0" ] ; then
    echo OK
else
    echo FAIL
fi

mkdir -p ${TS_TOPDIR}/meson-logs
rm -rf ${TS_TOPDIR}/meson-logs/*
cp -R /tmp/te_ws*/build/meson-logs/* ${TS_TOPDIR}/meson-logs/
