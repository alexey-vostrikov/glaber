ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

# Declare argument for SNMP version
ENV SNMP_VERSION=5.9.3
ENV LD_LIBRARY_PATH /usr/local/lib

RUN apt-get update && \
    apt-get install -y build-essential libssl-dev libwrap0-dev libc6-dev wget file libperl-dev && \
    cd /tmp && \
    wget http://sourceforge.net/projects/net-snmp/files/net-snmp/${SNMP_VERSION}/net-snmp-${SNMP_VERSION}.tar.gz && \
    tar -xvzf net-snmp-${SNMP_VERSION}.tar.gz && \
    cd /tmp/net-snmp-${SNMP_VERSION} && \
    ./configure --with-defaults --with-persistent-directory=/var/net-snmp --with-logfile=/var/log/snmpd.log --with-openssl --enable-privacy && \
    make && \
    make install && \
    apt-get remove -y build-essential file libperl-dev && \
    apt-get autoremove -y && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/net-snmp-${SNMP_VERSION}.tar.gz /tmp/net-snmp-${SNMP_VERSION}
