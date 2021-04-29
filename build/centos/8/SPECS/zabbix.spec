Name:		glaber
Version:	2.0.7
Release:	%{?alphatag:0.}1%{?alphatag}%{?dist}
Summary:	The Enterprise-class open source monitoring solution
Group:		Applications/Internet
License:	GPLv2+
URL:		http://glaber.io
Source0:	%{name}-%{version}%{?alphatag}.tar.gz
Source1:	zabbix-web22.conf
Source2:	zabbix-web24.conf
Source3:	zabbix-logrotate.in
Source6:	zabbix-server.init
Source7:	zabbix-proxy.init
Source10:	zabbix-agent.service
Source11:	zabbix-server.service
Source12:	zabbix-proxy.service
Source15:	zabbix-tmpfiles.conf
Source16:	zabbix-php-fpm.conf
Source17:	zabbix-web-fcgi.conf
Source18:	zabbix-nginx.conf
Patch0:		config.patch
Patch1:		fping3-sourceip-option.patch


Buildroot:	%{_tmppath}/zabbix-%{version}-%{release}-root-%(%{__id_u} -n)

%{!?build_agent: %global build_agent 1}

%if 0%{?rhel} >= 7
%{!?build_proxy: %global build_proxy 1}
%{!?build_java_gateway: %global build_java_gateway 1}
%endif

%if 0%{?rhel} >= 8
%{!?build_server: %global build_server 1}
%{!?build_frontend: %global build_frontend 1}
%endif

%{!?build_with_mysql: %global build_with_mysql 1}
%{!?build_with_pgsql: %global build_with_pgsql 1}
%{!?build_with_sqlite: %global build_with_sqlite 1}

%if 0%{build_with_mysql} == 0 && 0%{build_with_pgsql} == 0
%global build_server 0
%if 0%{build_with_sqlite} == 0
%global build_proxy 0
%endif
%endif

# FIXME: Building debuginfo is broken on RHEL-8. Disabled for now.
# Enable hardening
%if 0%{?rhel} >= 8
%define debug_package %{nil}
%global _hardened_build 1
%endif

BuildRequires:	make
%if 0%{?rhel} >= 8
BuildRequires:	mariadb-connector-c-devel
BuildRequires:	postgresql-devel >= 12.0
BuildRequires:	sqlite-devel
BuildRequires:	net-snmp-devel
BuildRequires:	openldap-devel
BuildRequires:	gnutls-devel
BuildRequires:	unixODBC-devel
BuildRequires:	curl-devel >= 7.13.1
BuildRequires:	OpenIPMI-devel >= 2
BuildRequires:	libssh-devel >= 0.9.0
BuildRequires:	java-devel >= 1.6.0
BuildRequires:	libxml2-devel
BuildRequires:	libevent-devel
%endif
BuildRequires:	pcre-devel
%if 0%{?rhel} >= 6
BuildRequires:	openssl-devel >= 1.0.1
%endif
%if 0%{?rhel} >= 7
BuildRequires:	systemd
%endif

%description
Glaber is a fork of Zabbix with efficiency and speed improvements.
Zabbix is the ultimate enterprise-level software designed for
real-time monitoring of millions of metrics collected from tens of
thousands of servers, virtual machines and network devices.

%package proxy-mysql
Summary:		Glaber proxy for MySQL or MariaDB database
Group:			Applications/Internet
Requires:		fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Provides:		glaber-proxy = %{version}-%{release}
Provides:		glaber-proxy-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-proxy
Obsoletes:		glaber-proxy

%description proxy-mysql
Glaber proxy with MySQL or MariaDB database support.

%package proxy-pgsql
Summary:		Glaber proxy for PostgreSQL database
Group:			Applications/Internet
Requires:		fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Provides:		glaber-proxy = %{version}-%{release}
Provides:		glaber-proxy-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-proxy
Obsoletes:		glaber-proxy

%description proxy-pgsql
Glaber proxy with PostgreSQL database support.

%package proxy-sqlite3
Summary:		Glaber proxy for SQLite3 database
Group:			Applications/Internet
Requires:		fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Provides:		glaber-proxy = %{version}-%{release}
Provides:		glaber-proxy-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-proxy
Obsoletes:		glaber-proxy

%description proxy-sqlite3
Zabbix proxy with SQLite3 database support.

%if 0%{?rhel} >= 8
%package server-mysql
Summary:		Glaber server for MySQL or MariaDB database
Group:			Applications/Internet
Requires:		fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Provides:		glaber-server = %{version}-%{release}
Provides:		glaber-server-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-server
Obsoletes:		glaber-server

%description server-mysql
Zabbix server with MySQL or MariaDB database support.

%package server-pgsql
Summary:		Glaber server for PostgresSQL database
Group:			Applications/Internet
Requires:		fping
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
Provides:		glaber-server = %{version}-%{release}
Provides:		glaber-server-implementation = %{version}-%{release}
Obsoletes:		zabbix
Obsoletes:		zabbix-server
Obsoletes:		glaber-server
%description server-pgsql
Glaber server with PostgresSQL database support.

%package web
Summary:		Glaber web frontend common package
Group:			Application/Internet
BuildArch:		noarch
Requires:		dejavu-sans-fonts
Requires(post):		%{_sbindir}/update-alternatives
Requires(preun):	%{_sbindir}/update-alternatives

%description web
Glaber web frontend common package

%package web-deps
Summary:		PHP dependencies metapackage for frontend
Group:			Application/Internet
BuildArch:		noarch
Requires:		glaber-web = %{version}-%{release}
Requires:		php-gd >= 7.2
Requires:		php-bcmath >= 7.2
Requires:		php-mbstring >= 7.2
Requires:		php-xml >= 7.2
Requires:		php-ldap >= 7.2
Requires:		php-json >= 7.2
Requires:		php-fpm >= 7.2
Requires:		glaber-web = %{version}-%{release}
Requires:		glaber-web-database = %{version}-%{release}

%description web-deps
PHP dependencies metapackage for frontend

%package web-mysql
Summary:		Glaber web frontend for MySQL
Group:			Applications/Internet
BuildArch:		noarch
Requires:		glaber-web = %{version}-%{release}
Requires:		glaber-web-deps = %{version}-%{release}
Requires:		php-mysqlnd
Provides:		glaber-web-database = %{version}-%{release}

%description web-mysql
Glaber web frontend for MySQL

%package web-pgsql
Summary:		Glaber web frontend for PostgreSQL
Group:			Applications/Internet
BuildArch:		noarch
Requires:		glaber-web = %{version}-%{release}
Requires:		glaber-web-deps = %{version}-%{release}
Requires:		php-pgsql
Provides:		glaber-web-database = %{version}-%{release}

%description web-pgsql
Glaber web frontend for PostgreSQL

%package apache-conf
Summary:		Automatic zabbix frontend configuration with apache
Group:			Applications/Internet
BuildArch:		noarch
Requires:		glaber-web-deps = %{version}-%{release}
Requires:		httpd

%description apache-conf
Zabbix frontend configuration for apache

%package nginx-conf
Summary:		Glaber frontend configuration for nginx and php-fpm
Group:			Applications/Internet
BuildArch:		noarch
Requires:		glaber-web-deps = %{version}-%{release}
Requires:		nginx

%description nginx-conf
Glaber frontend configuration for nginx and php-fpm

%package web-japanese
Summary:		Japanese font settings for Glaber frontend
Group:			Applications/Internet
BuildArch:		noarch
Requires:		google-noto-sans-cjk-ttc-fonts
Requires:		glibc-langpack-ja
Requires:		glaber-web = %{version}-%{release}
Requires(post):		%{_sbindir}/update-alternatives
Requires(preun):	%{_sbindir}/update-alternatives

%description web-japanese
Japanese font configuration for Glaber web frontend
%endif

#
# prep
#

%prep
%setup0 -q -n %{name}-%{version}%{?alphatag}

%if 0%{?build_frontend}
%patch0 -p1

## remove font file
rm -f ui/assets/fonts/DejaVuSans.ttf

# replace font in defines.inc.php
sed -i -r "s/(define\(.*_FONT_NAME.*)DejaVuSans/\1graphfont/" \
	ui/include/defines.inc.php

# remove .htaccess files
rm -f ui/app/.htaccess
rm -f ui/conf/.htaccess
rm -f ui/include/.htaccess
rm -f ui/local/.htaccess

# remove translation source files and scripts
find ui/locale -name '*.po' | xargs rm -f
find ui/locale -name '*.sh' | xargs rm -f
%endif

%if 0%{?build_server} || 0%{?build_proxy} || 0%{?build_agent} || 0%{?build_agent2}
%patch1 -p1
%endif

%if 0%{?build_server} || 0%{?build_proxy}
# traceroute command path for global script
sed -i -e 's|/usr/bin/traceroute|/bin/traceroute|' database/mysql/data.sql
sed -i -e 's|/usr/bin/traceroute|/bin/traceroute|' database/postgresql/data.sql
sed -i -e 's|/usr/bin/traceroute|/bin/traceroute|' database/sqlite3/data.sql
%endif

%if 0%{?build_server}
# sql files for servers
cat database/mysql/schema.sql > database/mysql/create.sql
cat database/mysql/images.sql >> database/mysql/create.sql
cat database/mysql/data.sql >> database/mysql/create.sql
gzip database/mysql/create.sql

cat database/postgresql/schema.sql > database/postgresql/create.sql
cat database/postgresql/images.sql >> database/postgresql/create.sql
cat database/postgresql/data.sql >> database/postgresql/create.sql
gzip database/postgresql/create.sql
gzip database/postgresql/timescaledb.sql
%endif

%if 0%{?build_proxy}
# sql files for proxies
gzip database/mysql/schema.sql
gzip database/postgresql/schema.sql
gzip database/sqlite3/schema.sql
%endif

# Build consists of 1-3 configure/make passes, one for each database.
# pass 1: is sqlite proxy, may be omitted.
# pass 2: is pqsql server/proxy, may be omitted.
# pass 3: If only one database is enabled, then it must occur with pass 3.

%build
build_conf_common="
	--enable-dependency-tracking
	--sysconfdir=/etc/zabbix
	--libdir=%{_libdir}/zabbix
	--enable-ipv6
	--with-net-snmp
	--with-ldap
	--with-libcurl
	--with-openipmi
	--with-unixodbc
%if 0%{?rhel} >= 8
	--with-ssh
%else
	--with-ssh2
%endif
	--with-libxml2
	--with-libevent
	--with-libpcre
%if 0%{?rhel} >= 6
	--with-openssl
%endif
"

# setup pass 3
%if 0%{?build_with_mysql} && ( 0%{?build_server} || 0%{?build_proxy} )
build_conf_3="
%if 0%{?build_server}
	--enable-server
%endif
%if 0%{?build_proxy}
	--enable-proxy
%endif
	--with-mysql
"

build_db_3=mysql
%endif


# setup pass 2
%if 0%{?build_with_pgsql} && ( 0%{?build_server} || 0%{?build_proxy} )
build_conf_2="
%if 0%{?build_server}
	--enable-server
%endif
%if 0%{?build_proxy}
	--enable-proxy
%endif
	--with-postgresql
"

if [ -z "$build_conf_3" ]; then
	build_conf_3="$build_conf_2"
	build_conf_2=""
	build_db_3="pgsql"
fi
%endif


# setup pass 1
%if 0%{?build_with_sqlite} && 0%{?build_proxy}
build_conf_1="--enable-proxy --with-sqlite3"

if [ -z "$build_conf_3" ]; then
	build_conf_3="$build_conf_1"
	build_conf_1=""
	build_db_3=sqlite3
fi
%endif


# pass 1
if [ -n "$build_conf_1" ]; then
	%configure $build_conf_common $build_conf_1
	make $make_flags
	mv src/zabbix_proxy/zabbix_proxy src/zabbix_proxy/zabbix_proxy_sqlite3
fi


# pass 2
if [ -n "$build_conf_2" ]; then
	%configure $build_conf_common $build_conf_2
	make $make_flags
%if 0%{?build_server}
	mv src/zabbix_server/zabbix_server src/zabbix_server/zabbix_server_pgsql
%endif
%if 0%{?build_proxy}
	mv src/zabbix_proxy/zabbix_proxy src/zabbix_proxy/zabbix_proxy_pgsql
%endif
fi


# pass 3
if [ -n "$build_conf_3" ]; then
	%configure $build_conf_common $build_conf_3
	make $make_flags
%if 0%{?build_server}
	mv src/zabbix_server/zabbix_server "src/zabbix_server/zabbix_server_$build_db_3"
%endif
%if 0%{?build_proxy}
	mv src/zabbix_proxy/zabbix_proxy "src/zabbix_proxy/zabbix_proxy_$build_db_3"
%endif
fi


#
# install
#

%install

rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/zabbix
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/run/zabbix
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zabbix
mkdir -p $RPM_BUILD_ROOT%{_datadir}
mkdir -p $RPM_BUILD_ROOT%{_datadir}/man/man8

%if 0%{?build_server} || 0%{?build_proxy}
mkdir -p $RPM_BUILD_ROOT/usr/lib/zabbix
mkdir -p $RPM_BUILD_ROOT/usr/sbin
mkdir -p $RPM_BUILD_ROOT/usr/share/zabbix
mkdir -p $RPM_BUILD_ROOT/usr/lib/zabbix/externalscripts
mkdir -p $RPM_BUILD_ROOT/usr/lib/zabbix/workerscripts
mkdir -p $RPM_BUILD_ROOT/usr/lib/zabbix/alertscripts
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man8
#mv $RPM_BUILD_ROOT%{_datadir}/zabbix/externalscripts $RPM_BUILD_ROOT/usr/lib/zabbix
%endif

%if 0%{?build_proxy}
#mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.d
install -m 0755 -p src/zabbix_proxy/zabbix_proxy_* $RPM_BUILD_ROOT%{_sbindir}/
#rm $RPM_BUILD_ROOT%{_sbindir}/zabbix_proxy
cat conf/zabbix_proxy.conf | sed \
	-e '/^# PidFile=/a \\nPidFile=%{_localstatedir}/run/zabbix/zabbix_proxy.pid' \
	-e 's|^LogFile=.*|LogFile=%{_localstatedir}/log/zabbix/zabbix_proxy.log|g' \
	-e '/^# LogFileSize=/a \\nLogFileSize=0' \
	-e 's:^# ExternalScripts=${datadir}/zabbix/externalscripts:# ExternalScripts=/usr/lib/zabbix/externalscripts:' \
	-e 's|^DBUser=root|DBUser=zabbix|g' \
	-e '/^# SNMPTrapperFile=.*/a \\nSNMPTrapperFile=/var/log/snmptrap/snmptrap.log' \
	-e '/^# SocketDir=.*/a \\nSocketDir=/var/run/zabbix' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_proxy.conf
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|proxy|g' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-proxy
install -Dm 0644 -p %{SOURCE12} $RPM_BUILD_ROOT%{_unitdir}/zabbix-proxy.service
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf
%endif


%if 0%{?build_server}
#mv $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.conf.d $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.d
install -m 0755 -p src/zabbix_server/zabbix_server_* $RPM_BUILD_ROOT%{_sbindir}/
install -m 0755 -p src/glapi/glb_hist_clickhouse $RPM_BUILD_ROOT%{_sbindir}/
install -m 0755 -p ./glbmap $RPM_BUILD_ROOT%{_sbindir}/
setcap cap_net_raw,cap_net_admin=eip /usr/sbin/glbmap
#rm $RPM_BUILD_ROOT%{_sbindir}/zabbix_server
#mv $RPM_BUILD_ROOT%{_datadir}/zabbix/alertscripts $RPM_BUILD_ROOT/usr/lib/zabbix
cat conf/zabbix_server.conf | sed \
	-e '/^# PidFile=/a \\nPidFile=%{_localstatedir}/run/zabbix/zabbix_server.pid' \
	-e 's|^LogFile=.*|LogFile=%{_localstatedir}/log/zabbix/zabbix_server.log|g' \
	-e '/^# LogFileSize=/a \\nLogFileSize=0' \
	-e 's:^# AlertScriptsPath=${datadir}/zabbix/alertscripts:# AlertScriptsPath=/usr/lib/zabbix/alertscripts:' \
	-e 's:^# ExternalScripts=${datadir}/zabbix/externalscripts:# ExternalScripts=/usr/lib/zabbix/externalscripts:' \
	-e 's:^# WorkerScripts=${datadir}/zabbix/workerscripts:# WorkerScripts=/usr/lib/zabbix/workerscripts:' \
	-e 's|^DBUser=root|DBUser=zabbix|g' \
	-e '/^# SNMPTrapperFile=.*/a \\nSNMPTrapperFile=/var/log/snmptrap/snmptrap.log' \
	-e '/^# SocketDir=.*/a \\nSocketDir=/var/run/zabbix' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/zabbix_server.conf
cat %{SOURCE3} | sed \
	-e 's|COMPONENT|server|g' \
	> $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/zabbix-server
install -Dm 0644 -p %{SOURCE11} $RPM_BUILD_ROOT%{_unitdir}/zabbix-server.service
install -Dm 0644 -p %{SOURCE15} $RPM_BUILD_ROOT%{_prefix}/lib/tmpfiles.d/zabbix-server.conf
%endif


%if 0%{?build_frontend}
find ui -name '*.orig' | xargs rm -f
cp -a ui/* $RPM_BUILD_ROOT%{_datadir}/zabbix
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web
touch $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/zabbix.conf.php
mv $RPM_BUILD_ROOT%{_datadir}/zabbix/conf/maintenance.inc.php $RPM_BUILD_ROOT%{_sysconfdir}/zabbix/web/
install -Dm 0644 -p %{SOURCE16} $RPM_BUILD_ROOT%{_sysconfdir}/php-fpm.d/zabbix.conf
install -Dm 0644 -p %{SOURCE17} $RPM_BUILD_ROOT%{_sysconfdir}/httpd/conf.d/zabbix.conf
install -Dm 0644 -p %{SOURCE18} $RPM_BUILD_ROOT%{_sysconfdir}/nginx/conf.d/zabbix.conf
%endif



%clean
rm -rf $RPM_BUILD_ROOT


#
# files & scriptlets
#


%if 0%{?build_proxy}
%if 0%{?build_with_mysql}
%files proxy-mysql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/mysql/schema.sql.gz
%attr(0600,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_proxy.conf
%dir /usr/lib/zabbix/externalscripts
%dir /usr/lib/zabbix/workerscripts
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-proxy
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_unitdir}/zabbix-proxy.service
%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf
%{_sbindir}/zabbix_proxy_mysql

%pre proxy-mysql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post proxy-mysql
%systemd_post zabbix-proxy.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_proxy \
	zabbix-proxy %{_sbindir}/zabbix_proxy_mysql 10
:

%preun proxy-mysql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-proxy.service
/usr/sbin/update-alternatives --remove zabbix-proxy \
%{_sbindir}/zabbix_proxy_mysql
fi
:

%postun proxy-mysql
%systemd_postun_with_restart zabbix-proxy.service
:
%endif


%if 0%{?build_with_pgsql}
%files proxy-pgsql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/postgresql/schema.sql.gz
%attr(0600,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_proxy.conf
%dir /usr/lib/zabbix/externalscripts
%dir /usr/lib/zabbix/workerscripts
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-proxy
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_unitdir}/zabbix-proxy.service
%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf
%{_sbindir}/zabbix_proxy_pgsql

%pre proxy-pgsql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post proxy-pgsql
%systemd_post zabbix-proxy.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_proxy \
	zabbix-proxy %{_sbindir}/zabbix_proxy_pgsql 10
:

%preun proxy-pgsql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-proxy.service
/usr/sbin/update-alternatives --remove zabbix-proxy \
	%{_sbindir}/zabbix_proxy_pgsql
fi
:

%postun proxy-pgsql
%systemd_postun_with_restart zabbix-proxy.service
:
%endif


%if 0%{?build_with_sqlite}
%files proxy-sqlite3
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/sqlite3/schema.sql.gz
%attr(0640,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_proxy.conf
%dir /usr/lib/zabbix/externalscripts
%dir /usr/lib/zabbix/workerscripts
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-proxy
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_unitdir}/zabbix-proxy.service
%{_prefix}/lib/tmpfiles.d/zabbix-proxy.conf
%{_sbindir}/zabbix_proxy_sqlite3

%pre proxy-sqlite3
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post proxy-sqlite3
%systemd_post zabbix-proxy.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_proxy \
	zabbix-proxy %{_sbindir}/zabbix_proxy_sqlite3 10
:

%preun proxy-sqlite3
if [ "$1" = 0 ]; then
%systemd_preun zabbix-proxy.service
/usr/sbin/update-alternatives --remove zabbix-proxy \
	%{_sbindir}/zabbix_proxy_sqlite3
fi
:

%postun proxy-sqlite3
%systemd_postun_with_restart zabbix-proxy.service
:
%endif
%endif


%if 0%{?build_server}
%if 0%{?build_with_mysql}
%files server-mysql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/mysql/create.sql.gz
%doc database/mysql/double.sql
%attr(0600,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_server.conf
%dir /usr/lib/zabbix/alertscripts
%dir /usr/lib/zabbix/externalscripts
%dir /usr/lib/zabbix/workerscripts
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-server
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_unitdir}/zabbix-server.service
%{_prefix}/lib/tmpfiles.d/zabbix-server.conf
%{_sbindir}/zabbix_server_mysql
%{_sbindir}/glb_hist_clickhouse
%{_sbindir}/glbmap

%pre server-mysql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post server-mysql
%systemd_post zabbix-server.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_server \
	zabbix-server %{_sbindir}/zabbix_server_mysql 10
:

%preun server-mysql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-server.service
/usr/sbin/update-alternatives --remove zabbix-server \
	%{_sbindir}/zabbix_server_mysql
fi
:

%postun server-mysql
%systemd_postun_with_restart zabbix-server.service
:
%endif


%if 0%{?build_with_pgsql}
%files server-pgsql
%defattr(-,root,root,-)
%doc AUTHORS ChangeLog COPYING NEWS README
%doc database/postgresql/create.sql.gz
%doc database/postgresql/double.sql
%doc database/postgresql/timescaledb.sql.gz
%attr(0600,root,zabbix) %config(noreplace) %{_sysconfdir}/zabbix/zabbix_server.conf
%dir /usr/lib/zabbix/alertscripts
%dir /usr/lib/zabbix/workerscripts
%config(noreplace) %{_sysconfdir}/logrotate.d/zabbix-server
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/log/zabbix
%attr(0755,zabbix,zabbix) %dir %{_localstatedir}/run/zabbix
%{_unitdir}/zabbix-server.service
%{_prefix}/lib/tmpfiles.d/zabbix-server.conf
%{_sbindir}/zabbix_server_pgsql
%{_sbindir}/glb_hist_clickhouse
%{_sbindir}/glbmap

%pre server-pgsql
getent group zabbix > /dev/null || groupadd -r zabbix
getent passwd zabbix > /dev/null || \
	useradd -r -g zabbix -d %{_localstatedir}/lib/zabbix -s /sbin/nologin \
	-c "Zabbix Monitoring System" zabbix
:

%post server-pgsql
%systemd_post zabbix-server.service
/usr/sbin/update-alternatives --install %{_sbindir}/zabbix_server \
	zabbix-server %{_sbindir}/zabbix_server_pgsql 10
:

%preun server-pgsql
if [ "$1" = 0 ]; then
%systemd_preun zabbix-server.service
/usr/sbin/update-alternatives --remove zabbix-server \
	%{_sbindir}/zabbix_server_pgsql
fi
:

%postun server-pgsql
%systemd_postun_with_restart zabbix-server.service
:
%endif
%endif


%if 0%{?build_frontend}
%files web
%defattr(-,root,root,-)
%dir %{_sysconfdir}/zabbix/web
%ghost %config(noreplace) %{_sysconfdir}/zabbix/web/zabbix.conf.php
%doc AUTHORS ChangeLog COPYING NEWS README
%config(noreplace) %{_sysconfdir}/zabbix/web/maintenance.inc.php
%{_datadir}/zabbix

%files web-deps
%config(noreplace) %{_sysconfdir}/php-fpm.d/zabbix.conf

%files web-japanese
%defattr(-,root,root,-)

%files web-mysql
%defattr(-,root,root,-)

%files web-pgsql
%defattr(-,root,root,-)

%files apache-conf
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/httpd/conf.d/zabbix.conf

%files nginx-conf
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/nginx/conf.d/zabbix.conf

%post web
# The fonts directory was moved into assets subdirectory at one point.
#
# This broke invocation of update-alternatives command below, because the target link for zabbix-web-font changed
# from zabbix/fonts/graphfont.ttf to zabbix/assets/fonts/graphfont.ttf
#
# We handle this movement by deleting /var/lib/alternatives/zabbix-web-font file if it contains the old target link.
# We also remove symlink at zabbix/fonts/graphfont.ttf to have the old fonts directory be deleted during update.
if [ -f /var/lib/alternatives/zabbix-web-font ] && \
	[ -z "$(grep %{_datadir}/zabbix/assets/fonts/graphfont.ttf /var/lib/alternatives/zabbix-web-font)" ]
then
	rm /var/lib/alternatives/zabbix-web-font
	if [ -h %{_datadir}/zabbix/fonts/graphfont.ttf ]; then
		rm %{_datadir}/zabbix/fonts/graphfont.ttf
	fi
fi
/usr/sbin/update-alternatives --install %{_datadir}/zabbix/assets/fonts/graphfont.ttf \
	zabbix-web-font %{_datadir}/fonts/dejavu/DejaVuSans.ttf 10
:

%post web-japanese
/usr/sbin/update-alternatives --install %{_datadir}/zabbix/assets/fonts/graphfont.ttf zabbix-web-font \
	%{_datadir}/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc 20
:

# The user apache must be available for these to work.
# It is provided by httpd or php-fpm packages.
%post apache-conf
if [ -d /etc/zabbix/web ]; then
	chown apache:apache /etc/zabbix/web/
fi
:

%post nginx-conf
if [ -d /etc/zabbix/web ]; then
	chown apache:apache /etc/zabbix/web/
fi
:

%preun web
if [ "$1" = 0 ]; then
/usr/sbin/update-alternatives --remove zabbix-web-font \
	%{_datadir}/fonts/dejavu/DejaVuSans.ttf
fi
:

%preun web-japanese
if [ "$1" = 0 ]; then
/usr/sbin/update-alternatives --remove zabbix-web-font \
	%{_datadir}/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc
fi
:
%endif


#
# changelog
#

%changelog
* Thu Jan 07 2021 Zabbix Packager <info@zabbix.com> - 5.2.4-1
- update to 5.2.4
- reworked spec file to allow selecting which packages are being built via macros (ZBX-18826)

* Mon Dec 21 2020 Zabbix Packager <info@zabbix.com> - 5.2.3-1
- update to 5.2.3

* Mon Nov 30 2020 Zabbix Packager <info@zabbix.com> - 5.2.2-1
- update to 5.2.2
- added proxy and java-gateway to rhel-7

* Fri Oct 30 2020 Zabbix Packager <info@zabbix.com> - 5.2.1-1
- update to 5.2.1

* Mon Oct 26 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-1
- update to 5.2.0

* Thu Oct 22 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.7rc2
- update to 5.2.0rc2

* Tue Oct 20 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.6rc1
- update to 5.2.0rc1

* Mon Oct 12 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.5beta2
- update to 5.2.0beta2

* Mon Sep 28 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.4beta1
- update to 5.2.0beta1
- added User=zabbix & Group=zabbix to all service files

* Mon Sep 14 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.3alpha3
- update to 5.2.0alpha3
- added separate zabbix-web-deps package
- doing hardened builds on rhel >= 8
- removed libyaml.patch
- overriding ExternalScripts & AlertScriptsPath in binaries instead of config files (ZBX-17983)

* Mon Aug 31 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.2alpha2
- update to 5.2.0alpha2
- building only agent, sender & get packages on rhel <= 7
- creating empty log file for agent2 (ZBX-18243)

* Mon Aug 17 2020 Zabbix Packager <info@zabbix.com> - 5.2.0-0.1alpha1
- update to 5.2.0alpha1
- building server and proxy with mysql 8 & postgresql 12 on rhel/centos 7 (ZBX-18221)
- added various After=postgresql* directives to server & proxy service files (ZBX-17492)

* Mon Jul 13 2020 Zabbix Packager <info@zabbix.com> - 5.0.2-1
- update to 5.0.2
- removed ZBX-17801 patch
- added "if build_agent2" around zabbix_agent2.conf installation (ZBX-17818)

* Thu May 28 2020 Zabbix Packager <info@zabbix.com> - 5.0.1-1
- update to 5.0.1
- changed mysql build dependency on rhel/centos-8 from mysql-devel to mariadb-connector-c-devel (ZBX-17738)
- added patch that fixes (ZBX-17801)

* Mon May 11 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-1
- update to 5.0.0

* Tue May 05 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.7rc1
- update to 5.0.0rc1
- moved frontends/php to ui directory

* Mon Apr 27 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.6beta2
- update to 5.0.0beta2

* Tue Apr 14 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.5beta1
- update to 5.0.0beta1
- added agent2 on rhel/centos 7

* Mon Mar 30 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.4alpha4
- update to 5.0.0alpha4
- removed proxy, java-gateway & js packages on rhel 5 & 6 due to minimum supported database version increase

* Mon Mar 16 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.3alpha3
- update to 5.0.0alpha3
- using libssh instead of libssh2 (rhel/centos 8)
- removed explicit dependency on php from zabbix-web (rhel/centos 8)
- removed explicit dependency on httpd from zabbix-web (rhel/centos 7)
- added zabbix-apache-conf (rhel/centos 7)
- using zabbix-web-database-scl as zabbix-(apache/nginx)-conf package dependency (rhel/centos 7)

* Mon Feb 17 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.2alpha2
- update to 5.0.0alpha2
- fixed font configuration in pre/post scriptlets on rhel-8

* Wed Feb 05 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.2alpha1
- added *-scl packages to help with resolving php7.2+ and nginx dependencies of zabbix frontend on rhel/centos 7
- added posttrans script that preserves /etc/zabbix/zabbix_agentd.d/userparameter_mysql.conf file
- added config(noreplace) to /etc/sysconfig/zabbix-agent
- added explicit version to php-module dependencies in zabbix-web package on rhel/centos 8

* Mon Jan 27 2020 Zabbix Packager <info@zabbix.com> - 5.0.0-0.1alpha1
- update to 5.0.0alpha1

* Tue Jan 07 2020 Zabbix Packager <info@zabbix.com> - 4.4.4-2
- build of rhel-5 packages to be resigned with gpg version 3

* Thu Dec 19 2019 Zabbix Packager <info@zabbix.com> - 4.4.4-1
- update to 4.4.4
- added After=<database>.service directives to server and proxy service files

* Wed Nov 27 2019 Zabbix Packager <info@zabbix.com> - 4.4.3-1
- update to 4.4.3
- added User=zabbix and Group=zabbix directives to agent service file

* Mon Nov 25 2019 Zabbix Packager <info@zabbix.com> - 4.4.2-1
- update to 4.4.2

* Mon Oct 28 2019 Zabbix Packager <info@zabbix.com> - 4.4.1-1
- update to 4.4.1

* Mon Oct 07 2019 Zabbix Packager <info@zabbix.com> 4.4.0-1
- update to 4.4.0

* Thu Oct 03 2019 Zabbix Packager <info@zabbix.com> - 4.4.0-0.5rc1
- update to 4.4.0rc1

* Tue Sep 24 2019 Zabbix Packager <info@zabbix.com> - 4.4.0-0.4beta1
- update to 4.4.0beta1
- added zabbix-agent2 package

* Wed Sep 18 2019 Zabbix Packager <info@zabbix.com> - 4.4.0-0.3alpha3
- update to 4.4.0alpha3

* Thu Aug 15 2019 Zabbix Packager <info@zabbix.com> - 4.4.0-0.2alpha2
- update to 4.4.0alpha2
- using google-noto-sans-cjk-ttc-fonts for graphfont in web-japanese package on rhel-8
- added php-fpm as dependency of zabbix-web packages on rhel-8

* Wed Jul 17 2019 Zabbix Packager <info@zabbix.com> - 4.4.0-0.1alpha1
- update to 4.4.0alpha1
- removed apache config from zabbix-web package
- added dedicated zabbix-apache-conf and zabbix-nginx-conf packages

* Fri Mar 29 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-1
- update to 4.2.0
- removed jabber notifications support and dependency on iksemel library

* Tue Mar 26 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.6rc2
- update to 4.2.0rc2

* Mon Mar 18 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.5rc1
- update to 4.2.0rc1

* Mon Mar 04 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.4beta2
- update to 4.2.0beta2

* Mon Feb 18 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.1beta1
- update to 4.2.0beta1

* Tue Feb 05 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.3alpha3
- build of 4.2.0alpha3 with *.mo files

* Wed Jan 30 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.2alpha3
- added timescaledb.sql.gz to zabbix-server-pgsql package

* Mon Jan 28 2019 Zabbix Packager <info@zabbix.com> - 4.2.0-0.1alpha3
- update to 4.2.0alpha3

* Fri Dec 21 2018 Zabbix Packager <info@zabbix.com> - 4.2.0-0.2alpha2
- update to 4.2.0alpha2

* Tue Nov 27 2018 Zabbix Packager <info@zabbix.com> - 4.2.0-0.1alpha1
- update to 4.2.0alpha1

* Mon Oct 29 2018 Zabbix Packager <info@zabbix.com> - 4.0.1-1
- update to 4.0.1

* Mon Oct 01 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-2
- update to 4.0.0

* Fri Sep 28 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1rc3
- update to 4.0.0rc3

* Tue Sep 25 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1rc2
- update to 4.0.0rc2

* Wed Sep 19 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1rc1
- update to 4.0.0rc1

* Mon Sep 10 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1beta2
- update to 4.0.0beta2

* Tue Aug 28 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1beta1
- update to 4.0.0beta1

* Mon Jul 23 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1alpha9
- update to 4.0.0alpha9
- add PHP variable max_input_vars = 10000, overriding default 1000

* Mon Jun 18 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1alpha8
- update to 4.0.0alpha8

* Wed May 30 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1alpha7
- update to 4.0.0alpha7

* Fri Apr 27 2018 Zabbix Packager <info@zabbix.com> - 4.0.0-1.1alpha6
- update to 4.0.0alpha6
- add support for Ubuntu 18.04 (Bionic)
- move enabling JMX interface on Zabbix java gateway to zabbix_java_gateway.conf

* Mon Mar 26 2018 Vladimir Levijev <vladimir.levijev@zabbix.com> - 4.0.0-1.1alpha5
- update to 4.0.0alpha5

* Tue Feb 27 2018 Vladimir Levijev <vladimir.levijev@zabbix.com> - 4.0.0-1.1alpha4
- update to 4.0.0alpha4

* Mon Feb 05 2018 Vladimir Levijev <vladimir.levijev@zabbix.com> - 4.0.0-1.1alpha3
- update to 4.0.0alpha3

* Tue Jan 09 2018 Vladimir Levijev <vladimir.levijev@zabbix.com> - 4.0.0-1.1alpha2
- update to 4.0.0alpha2

* Tue Dec 19 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 4.0.0-1alpha1
- update to 4.0.0alpha1

* Thu Nov 09 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.4-2
- add missing translation (.mo) files

* Tue Nov 07 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.4-1
- update to 3.4.4
- fix issue with new line character in pid file that resulted in failure when shutting down daemons on RHEL 5

* Tue Oct 17 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.3-1
- update to 3.4.3

* Mon Sep 25 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.2-1
- update to 3.4.2

* Mon Aug 28 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.1-1
- update to 3.4.1
- change SocketDir to /var/run/zabbix

* Mon Aug 21 2017 Vladimir Levijev <vladimir.levijev@zabbix.com> - 3.4.0-1
- update to 3.4.0

* Wed Apr 26 2017 Kodai Terashima <kodai.terashima@zabbix.com> - 3.4.0-1alpha1
- update to 3.4.0alpla1 r68116
- add libpcre and libevent for compile option

* Sun Apr 23 2017 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.5-1
- update to 3.2.5
- add TimeoutSec=0 to systemd service file

* Thu Mar 02 2017 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.4-2
- remove TimeoutSec for systemd

* Mon Feb 27 2017 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.4-1
- update to 3.2.4
- add TimeoutSec for systemd service file

* Wed Dec 21 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.3-1
- update to 3.2.3

* Thu Dec 08 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.2-1
- update to 3.2.2

* Sun Oct 02 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.1-1
- update to 3.2.1
- use zabbix user and group for Java Gateway
- add SuccessExitStatus=143 for Java Gateway servie file

* Tue Sep 13 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0-1
- update to 3.2.0
- add *.conf for Include parameter in agent configuration file

* Mon Sep 12 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0rc2-1
- update to 3.2.0rc2

* Fri Sep 09 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0rc1-1
- update to 3.2.0rc1

* Thu Sep 01 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0beta2-1
- update to 3.2.0beta2

* Fri Aug 26 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0beta1-1
- update to 3.2.0beta1

* Fri Aug 12 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.2.0alpha1-1
- update to 3.2.0alpha1

* Sun Jul 24 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.4-1
- update to 3.0.4

* Sun May 22 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.3-1
- update to 3.0.3
- fix java gateway systemd script to use java options

* Wed Apr 20 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.2-1
- update to 3.0.2
- remove ZBX-10459.patch

* Sat Apr 02 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.1-2
- fix proxy packges doesn't have schema.sql.gz
- add server and web packages for RHEL6
- add ZBX-10459.patch

* Sun Feb 28 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.1-1
- update to 3.0.1
- remove DBSocker parameter

* Sat Feb 20 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0-2
- agent, proxy and java-gateway for RHEL 5 and 6

* Mon Feb 15 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0-1
- update to 3.0.0

* Thu Feb 11 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0rc2
- update to 3.0.0rc2
- add TIMEOUT parameter for java gateway conf

* Thu Feb 04 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0rc1
- update to 3.0.0rc1

* Sat Jan 30 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0beta2
- update to 3.0.0beta2

* Thu Jan 21 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0beta1
- update to 3.0.0beta1

* Thu Jan 14 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha6
- update to 3.0.0alpla6
- remove zabbix_agent conf and binary

* Wed Jan 13 2016 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha5
- update to 3.0.0alpha5

* Fri Nov 13 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha4-1
- update to 3.0.0alpha4

* Thu Oct 29 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha3-2
- fix web-pgsql package dependency
- add --with-openssl option

* Mon Oct 19 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha3-1
- update to 3.0.0alpha3

* Tue Sep 29 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-3
- add IfModule for mod_php5 in apache configuration file
- fix missing proxy_mysql alternatives symlink
- chagne snmptrap log filename
- remove include dir from server and proxy conf

* Fri Sep 18 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-2
- fix create.sql doesn't contain schema.sql & images.sql

* Tue Sep 15 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 3.0.0alpha2-1
- update to 3.0.0alpha2

* Sat Aug 22 2015 Kodai Terashima <kodai.terashima@zabbix.com> - 2.5.0-1
- create spec file from scratch
- update to 2.5.0
