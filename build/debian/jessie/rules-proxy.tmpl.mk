#
# zabbix-proxy
#

update_conf_proxy:
	cat conf/zabbix_proxy.conf | sed \
		-e '/^# PidFile=/a \\nPidFile=/var/run/zabbix/zabbix_proxy.pid' \
		-e 's|^LogFile=.*|LogFile=/var/log/zabbix/zabbix_proxy.log|g' \
		-e '/^# LogFileSize=/a \\nLogFileSize=0' \
		-e 's:^# ExternalScripts=.*:# ExternalScripts=/usr/lib/zabbix/externalscripts:' \
		-e '/^# FpingLocation=/a \\nFpingLocation=/usr/bin/fping' \
		-e '/^# Fping6Location=/a \\nFping6Location=/usr/bin/fping6' \
		-e 's|^DBUser=root|DBUser=zabbix|g' \
		-e '/^# SNMPTrapperFile=.*/a \\nSNMPTrapperFile=/var/log/snmptrap/snmptrap.log' \
		-e '/^# SocketDir=/a \\nSocketDir=/var/run/zabbix' \
		> debian/conf/zabbix_proxy.conf

installinit_proxy:
	dh_installinit --no-start -p glaber-proxy-mysql --name=zabbix-proxy
	dh_installinit --no-start -p glaber-proxy-pgsql --name=zabbix-proxy
	dh_installinit --no-start -p glaber-proxy-sqlite3 --name=zabbix-proxy

clean_proxy: run_dh_clean
	rm -f debian/conf/zabbix_proxy.conf

BUILD_TARGETS += update_conf_proxy
INSTALLINIT_TARGETS += installinit_proxy
CLEAN_TARGETS += clean_proxy
