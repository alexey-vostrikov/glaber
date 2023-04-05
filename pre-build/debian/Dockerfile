ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

ENV DEBIAN_FRONTEND noninteractive

RUN echo "Install common apt" && \
    apt-get -qq update && \
    apt-get --ignore-missing install -yqq sshpass \
    build-essential cmake libgmp3-dev gengetopt  \
    libpcap-dev flex byacc libjson-c-dev \
    libunistring-dev dpkg-dev devscripts wget git gcc \
    automake dh-make build-essential autoconf autotools-dev \
    quilt pkg-config libsnmp-dev libpq-dev libsqlite3-dev \
    libcurl4-openssl-dev libldap2-dev libiksemel-dev \
    libopenipmi-dev libssh2-1-dev unixodbc-dev default-jdk \
    libxml2-dev libpcre3-dev libevent-dev curl libssl-dev

RUN echo "Install ${OS}:${OS_VER} specific apt" && \
    apt-get install -yqq default-libmysqlclient-dev libssh-dev sass

ENV GO_VER 1.16.2

# Download and extract Go
RUN wget -q https://golang.org/dl/go${GO_VER}.linux-amd64.tar.gz && \
    rm -rf /usr/local/go && \
    tar -C /usr/local -xzf go${GO_VER}.linux-amd64.tar.gz && \
    rm go${GO_VER}.linux-amd64.tar.gz

# Set environment variables
ENV PATH="/usr/local/go/bin:${PATH}"
