ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER} as build

# Declare argument for SNMP version
ENV SNMP_VERSION=5.9.3
ENV LD_LIBRARY_PATH /usr/local/lib

RUN apt-get update -y && apt-get upgrade -y && \
    apt-get install -y build-essential libssl-dev libwrap0-dev libc6-dev wget file libperl-dev && \
    cd /tmp && \
    wget http://sourceforge.net/projects/net-snmp/files/net-snmp/${SNMP_VERSION}/net-snmp-${SNMP_VERSION}.tar.gz && \
    tar -xvzf net-snmp-${SNMP_VERSION}.tar.gz && \
    cd /tmp/net-snmp-${SNMP_VERSION} && \
    ./configure --with-defaults --with-persistent-directory=/var/net-snmp --with-logfile=/var/log/snmpd.log --with-openssl --enable-privacy --enable-blumenthal-aes && \
    make && \
    make install

# Define the final stage
FROM ${OS}:${OS_VER}

ENV LD_LIBRARY_PATH /usr/local/lib

RUN apt-get update -y && apt-get upgrade -y && \
    apt-get install -y libpci3 libwrap0 libperl5.32 libssl1.1

# Copy files from the build stage
COPY --from=build /usr/local/share/man /usr/local/share/
COPY --from=build /usr/local/sbin/snmpd /usr/local/sbin/
COPY --from=build /usr/local/sbin/snmptrapd /usr/local/sbin/
COPY --from=build /usr/local/include/net-snmp /usr/local/include/
COPY --from=build /usr/local/share/snmp/ /usr/local/share/
COPY --from=build /usr/local/lib/x86_64-linux-gnu/perl/5.32.1 /usr/local/lib/x86_64-linux-gnu/perl/
COPY --from=build /usr/local/bin/ /usr/local/
