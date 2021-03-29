#
# zabbix server
#

update_conf_server:
	cat conf/zabbix_server.conf | sed \
		-e '/^# PidFile=/a \\nPidFile=/var/run/zabbix/zabbix_server.pid' \
		-e 's|^LogFile=.*|LogFile=/var/log/zabbix/zabbix_server.log|g' \
		-e '/^# LogFileSize=/a \\nLogFileSize=0' \
		-e 's:^# AlertScriptsPath=.*:# AlertScriptsPath=/usr/lib/zabbix/alertscripts:' \
		-e 's:^# ExternalScripts=.*:# ExternalScripts=/usr/lib/zabbix/externalscripts:' \
		-e '/^# FpingLocation=/a \\nFpingLocation=/usr/bin/fping' \
		-e '/^# Fping6Location=/a \\nFping6Location=/usr/bin/fping6' \
		-e 's|^DBUser=root|DBUser=zabbix|g' \
		-e '/^# SNMPTrapperFile=.*/a \\nSNMPTrapperFile=/var/log/snmptrap/snmptrap.log' \
		-e '/^# SocketDir=/a \\nSocketDir=/var/run/zabbix' \
		> debian/conf/zabbix_server.conf

install_frontend: run_dh_install
	find debian/zabbix-frontend-php/usr/share/zabbix -name .htaccess | xargs rm -f
	find debian/zabbix-frontend-php/usr/share/zabbix/locale -name '*.po' | xargs rm -f
	find debian/zabbix-frontend-php/usr/share/zabbix/locale -name '*.sh' | xargs rm -f
	rm -f debian/zabbix-frontend-php/usr/share/zabbix/assets/fonts/DejaVuSans.ttf
	sed -i -r "s/(define\(.*_FONT_NAME.*)DejaVuSans/\1graphfont/" \
		debian/zabbix-frontend-php/usr/share/zabbix/include/defines.inc.php

installinit_server:
	dh_installinit --no-start -p zabbix-server-mysql --name=zabbix-server
	dh_installinit --no-start -p zabbix-server-pgsql --name=zabbix-server

clean_server: run_dh_clean
	rm -f debian/conf/zabbix_server.conf

BUILD_TARGETS += update_conf_server
INSTALL_TARGETS += install_frontend
INSTALLINIT_TARGETS += installinit_server
CLEAN_TARGETS += clean_server
