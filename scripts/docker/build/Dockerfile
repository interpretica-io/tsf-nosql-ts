# Build environment
#
# Ubuntu 24.04: hiredis 1.2 (with TLS), libmongoc 1.26, libmemcached 1.1
# and libcurl 8 from apt; the DataStax C/C++ driver for Cassandra, which
# Ubuntu does not package, from source. These are the client libraries
# ta_nosql links on the agent.
FROM ubuntu:24.04

ARG user
ARG uid
ARG gid
ARG docker_gid
ENV USER=${user}
ENV UID=${uid}
ENV GID=${gid}
ENV DOCKER_GID=${docker_gid}

RUN if [ "$DOCKER_GID" != "${GID}" ] ; then groupadd --gid ${DOCKER_GID} docker ; fi

RUN if [ ! -d /home/${USER} ] ; then mkdir -p /home/${USER} && chmod -R 777 /home/${USER} ; fi

# TE's own build dependencies, then the suite's:
#  - libhiredis-dev, libmongoc-dev + libbson-dev, libmemcached-dev,
#    libcurl4-openssl-dev, libjansson-dev: the client libraries of
#    ta_nosql's drivers (Redis, MongoDB, Memcached; Elasticsearch, Neo4j
#    and etcd over HTTP) and its JSON;
#  - libuv1-dev, zlib1g-dev, libssl-dev, cmake: to build the Cassandra
#    driver below;
#  - docker.io: the database servers run in sibling containers, started
#    by scripts/nosql-servers.sh through the mounted Docker socket;
#    openssl makes their test certificates.
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y \
        autoconf \
        automake \
        bison \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        curl \
        docker.io \
        file \
        flex \
        gawk \
        git \
        libbsd-dev \
        libjansson-dev \
        libexpat1 \
        libexpat1-dev \
        libcurl4-openssl-dev \
        libglib2.0-dev \
        libnetconf2-dev \
        libncurses6 \
        libncurses-dev \
        libpam0g \
        libpam0g-dev \
        libpcap0.8-dev \
        libpcre2-dev \
        libpopt-dev \
        libreadline-dev \
        libssh-dev \
        libssh2-1-dev \
        libssl-dev \
        libusb-1.0-0 \
        libusb-1.0-0-dev \
        libxml2 \
        libxml2-dev \
        libxslt1-dev \
        libxslt1.1 \
        libpcre3-dev \
        libyaml-dev \
        libtirpc-dev \
        m4 \
        meson \
        openssh-server \
        pkg-config \
        perl \
        python3 \
        python3-pip \
        rsync \
        ssh \
        sudo \
        tshark \
        tcl-dev \
        wget \
        libhiredis-dev \
        libmongoc-dev \
        libbson-dev \
        libmemcached-dev \
        libuv1-dev \
        zlib1g-dev \
        openssl


# The DataStax C/C++ driver for Apache Cassandra (cassandra.h,
# libcassandra, cassandra.pc).
ARG CASS_DRIVER_VERSION=2.17.1
RUN curl -fsSL -o /tmp/cpp-driver.tar.gz \
        "https://github.com/datastax/cpp-driver/archive/refs/tags/${CASS_DRIVER_VERSION}.tar.gz" && \
    mkdir -p /tmp/cpp-driver && \
    tar -C /tmp/cpp-driver --strip-components=1 -xzf /tmp/cpp-driver.tar.gz && \
    cmake -S /tmp/cpp-driver -B /tmp/cpp-driver/build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCASS_BUILD_STATIC=OFF -DCASS_BUILD_SHARED=ON \
        -DCASS_BUILD_EXAMPLES=OFF -DCASS_BUILD_TESTS=OFF && \
    cmake --build /tmp/cpp-driver/build -j"$(nproc)" && \
    cmake --install /tmp/cpp-driver/build && \
    ldconfig && rm -rf /tmp/cpp-driver /tmp/cpp-driver.tar.gz

# The build tree lives in /tmp/aibuild; a named volume mounted there
# (scripts/run.sh, TE_DOCKER_CACHE) starts as a copy of this directory,
# so it has to exist and be writable by the container user.
RUN mkdir -p /tmp/aibuild && chmod 1777 /tmp/aibuild

RUN echo "${USER} ALL=NOPASSWD: ALL" >> /etc/sudoers

RUN if [ "$DOCKER_GID" != "${GID}" ] ; then groupadd --gid ${GID} ${USER} ; fi

RUN useradd --system \
            --gid ${GID} --gid ${DOCKER_GID} \
            --uid ${UID} \
            --shell /bin/bash \
            ${USER}
RUN usermod -a -G ${DOCKER_GID} ${USER}
RUN usermod -a -G docker ${USER}
