#!/usr/bin/env bash
systemctl status mysql
systemctl status clickhouse-server
echo "${var.tmate_ssh_pubkeys}" >> ~/.ssh/authorized_keys
echo "set tmate-authorized-keys ~/.ssh/authorized_keys" > ~/.tmate.conf
echo "set tmate-api-key ${var.tmate_api_key}" >> ~/.tmate.conf
echo "set tmate-session-name virtualbox" >>  ~/.tmate.conf
echo "set -g tmate-server-host nyc1.tmate.io" >> ~/.tmate.conf
echo "set -g tmate-server-host sgp1.tmate.io" >> ~/.tmate.conf
wget -q https://github.com/tmate-io/tmate/releases/download/2.4.0/tmate-2.4.0-static-linux-amd64.tar.xz;
tar xvf tmate-2.4.0-static-linux-amd64.tar.xz
mv tmate-2.4.0-static-linux-amd64/tmate .
rm -rf tmate-2.4.0-static-linux-amd64
./tmate -F;