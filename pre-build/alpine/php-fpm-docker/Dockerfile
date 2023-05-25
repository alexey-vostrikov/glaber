ARG OS
ARG OS_VER
FROM alpine:3.15

RUN apk update && \
    apk add --no-cache --clean-protected \
    curl openssl ca-certificates \
    mariadb-client mariadb-connector-c postgresql-client \ 
    php8 php8-fpm  \
    php8-gd php8-bcmath php8-ctype \
    php8-xmlreader php8-xmlwriter \
    php8-session php8-sockets php8-mbstring \
    php8-gettext php8-ldap php8-json \
    php8-mysqli php8-pgsql php8-simplexml \
    php8-fileinfo php8-openssl && \
    adduser -u 1101 -g 1101 -D -H glaber && \
    rm /etc/php8/php-fpm.d/www.conf && \
    mkdir -p /usr/share/zabbix && \
    chown glaber:glaber /usr/share/zabbix

COPY etc/ /etc

EXPOSE 8500

ENTRYPOINT ["/usr/sbin/php-fpm8", "-F", "-c", "/etc/php8/php-fpm.conf"]



