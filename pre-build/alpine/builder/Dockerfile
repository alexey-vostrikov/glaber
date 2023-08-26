ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

ENV PACKER_VER=1.8.5

RUN apk add --no-cache wget unzip docker-cli openssl openssh jq bash curl && \
    wget -q https://glaber.io/misc/packer_${PACKER_VER}_linux_amd64.zip && \
    unzip packer_${PACKER_VER}_linux_amd64.zip && \
    mv packer /usr/local/bin/

