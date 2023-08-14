ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER} as build

# Declare argument for SNMP version
ENV SNMP_VERSION=5.9.3
ENV LD_LIBRARY_PATH /usr/local/lib

RUN apt-get update && apt-get upgrade && \
    apt-get install -y build-essential libssl-dev libwrap0-dev libc6-dev wget file libperl-dev && \
    cd /tmp && \
    wget http://sourceforge.net/projects/net-snmp/files/net-snmp/${SNMP_VERSION}/net-snmp-${SNMP_VERSION}.tar.gz && \
    tar -xvzf net-snmp-${SNMP_VERSION}.tar.gz && \
    cd /tmp/net-snmp-${SNMP_VERSION} && \
    ./configure --with-defaults --with-persistent-directory=/var/net-snmp --with-logfile=/var/log/snmpd.log --with-openssl --enable-privacy && \
    make && \
    make install

# Define the final stage
FROM ${OS}:${OS_VER}

ENV LD_LIBRARY_PATH /usr/local/lib

RUN apt-get update && apt-get upgrade && \
    apt-get install -y libpci3 libwrap0 libperl5.32 libssl1.1

# Copy files from the build stage
COPY --from=build /usr/bin/net-snmp-create-v3-user /usr/bin/
COPY --from=build /usr/sbin/snmpd /usr/sbin/
COPY --from=build /usr/share/lintian/overrides/snmpd /usr/share/lintian/overrides/
COPY --from=build /usr/share/man/man1/net-snmp-create-v3-user.1.gz /usr/share/man/man1/
COPY --from=build /usr/share/man/man5/snmpd.conf.5.gz /usr/share/man/man5/
COPY --from=build /usr/share/man/man8/snmpd.8.gz /usr/share/man/man8/
COPY --from=build /usr/share/snmp/ /usr/share/
