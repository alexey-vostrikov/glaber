#!/usr/bin/env bash
set -e

if ! clickhouse-client --user $ZBX_CH_USER --password $ZBX_CH_PASS \
    --database $ZBX_CH_DB --query "select count(*) from history_str;"; then
  echo "Install glaber clickhouse schema"
  wget -q https://gitlab.com/mikler/glaber/-/raw/${GLABER_TAG}/database/clickhouse/history.sql

  sed -i -e "s/glaber/${ZBX_CH_DB}/g" \
         -e "s/6 MONTH/${ZBX_CH_RETENTION}/g" \
         history.sql
  
# File users.xml should be defined before init.sh script
# Redefine with .env file works with .glaber.sh script at prebuild stage
# May be use bitnami image https://hub.docker.com/r/bitnami/clickhouse/

  clickhouse-client \
    --user ${ZBX_CH_USER} --password ${ZBX_CH_PASS} \
    --multiquery < history.sql
else
  echo "Glaber clickhouse schema already installed"
fi

## for debug
# tail -f /var/log/clickhouse-server/clickhouse-server.log &
# tail -f /var/log/clickhouse-server/clickhouse-server.err.log &
