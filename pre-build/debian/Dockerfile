ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

RUN apt-get -qq update && \
    export DEBIAN_FRONTEND=noninteractive && \
    GO_VER=1.16.2 && \
    apt-get --ignore-missing install -yqq sshpass jq \
    build-essential cmake libgmp3-dev gengetopt  \
    libpcap-dev flex byacc libjson-c-dev \
    libunistring-dev dpkg-dev devscripts wget git gcc \
    automake dh-make build-essential autoconf autotools-dev \
    quilt pkg-config libsnmp-dev libpq-dev libsqlite3-dev \
    libcurl4-openssl-dev libldap2-dev libiksemel-dev \
    libopenipmi-dev libssh2-1-dev unixodbc-dev default-jdk \
    libxml2-dev libpcre3-dev libevent-dev curl libssl-dev \
    lsb-release default-libmysqlclient-dev libssh-dev sass && \
    wget -q https://golang.org/dl/go${GO_VER}.linux-amd64.tar.gz && \
    rm -rf /usr/local/go && \
    tar -C /usr/local -xzf go${GO_VER}.linux-amd64.tar.gz && \
    rm go${GO_VER}.linux-amd64.tar.gz && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN lsb_release -cs | grep bullseye && \
    apt-get install -y libmariadb-dev || true

# Set environment variables
ENV PATH="/usr/local/go/bin:${PATH}"
