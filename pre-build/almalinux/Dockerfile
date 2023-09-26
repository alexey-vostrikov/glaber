ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

RUN dnf update -y && \
    dnf -y install dnf-plugins-core epel-release

RUN rpm -q --qf "%{VERSION}" -f /etc/almalinux-release | grep 8  && \
    dnf config-manager --set-enabled powertools || true

RUN rpm -q --qf "%{VERSION}" -f /etc/almalinux-release | grep 9  && \
    dnf config-manager --set-enabled crb || true

RUN dnf -y install OpenIPMI-devel libarchive jq

RUN yum groupinstall "Development Tools" -y

RUN yum install -y ccache rpm-build yum-utils xz wget tar curl cmake json-c-devel gengetopt libunistring-devel libpcap-devel gmp-devel sshpass --allowerasing

RUN GO_VER=1.16.2 && \
    wget -q https://golang.org/dl/go${GO_VER}.linux-amd64.tar.gz && \
    rm -rf /usr/local/go && \
    tar -C /usr/local -xzf go${GO_VER}.linux-amd64.tar.gz && \
    rm go${GO_VER}.linux-amd64.tar.gz

# Set environment variables
ENV PATH="/usr/local/go/bin:${PATH}"