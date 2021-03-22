

rm heaptrack.zabbix_server.*
ps ax | grep zabbix_server: | grep -v grep | awk '{print "heaptrack -p " $1 "&"}' | sh