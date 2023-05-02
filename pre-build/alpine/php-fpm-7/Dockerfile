ARG OS
ARG OS_VER
FROM ${OS}:${OS_VER}

RUN apk update && \
    apk add --no-cache --clean-protected \
    curl openssl ca-certificates \
    mariadb-client mariadb-connector-c postgresql-client \ 
    php7 php7-fpm  \
    php7-gd php7-bcmath php7-ctype \
    php7-xmlreader php7-xmlwriter \
    php7-session php7-sockets php7-mbstring \
    php7-gettext php7-ldap php7-json \
    php7-mysqli php7-pgsql php7-simplexml \
    php7-fileinfo php7-openssl && \
    adduser -u 1101 -g 1101 -D -H glaber && \
    rm /etc/php7/php-fpm.d/www.conf && \
    mkdir -p /var/www/ui && \
    chown glaber:glaber /var/www/ui

COPY etc/ /etc

EXPOSE 9000

ENTRYPOINT ["/usr/sbin/php-fpm7", "-F", "-c", "/etc/php7/php-fpm.conf"]



