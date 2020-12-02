#!/bin/sh
while :
do
    zabbix_sender -z 127.0.0.1 -i trapper_data100k
done