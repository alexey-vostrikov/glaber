build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    name = "Install base apt"
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "export LD_LIBRARY_PATH=/usr/local/lib",
      "sudo apt-get update",
      "sudo apt-get upgrade -y",
      "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales",
      "sudo apt-get install -y software-properties-common openssl dirmngr net-tools",
      "sudo apt-get install -y ca-certificates supervisor default-mysql-client iputils-ping",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' | sudo tee /etc/apt/sources.list.d/nonfree.list",
      "sudo apt-get update",
      "sudo apt-get install -y snmp-mibs-downloader",
      "sudo chmod +s /usr/bin/nmap",
      "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo locale-gen",
      "sudo download-mibs",
      "sudo apt-get install -y build-essential libssl-dev libwrap0-dev libc6-dev wget file libperl-dev",
      "cd /tmp",
      "sudo wget -q http://sourceforge.net/projects/net-snmp/files/net-snmp/${var.snmp_version}/net-snmp-${var.snmp_version}.tar.gz",
      "sudo tar -xvzf net-snmp-${var.snmp_version}.tar.gz",
      "cd /tmp/net-snmp-${var.snmp_version}",
      "sudo ./configure --with-defaults --with-persistent-directory=/var/net-snmp --with-logfile=/var/log/snmpd.log --with-openssl --enable-privacy",
      "sudo make",
      "sudo make install",
      "sudo apt-get autoremove -y",
      "sudo apt-get clean",
      "sudo rm -rf /var/lib/apt/lists/* /tmp/net-snmp-${var.snmp_version}.tar.gz /tmp/net-snmp-${var.snmp_version}"
    ]
  }
  provisioner "shell" {
    name = "Install clickhouse"
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "export GNUPGHOME=$(mktemp -d)",
      "sudo GNUPGHOME=\"$GNUPGHOME\" gpg --no-default-keyring --keyring /usr/share/keyrings/clickhouse-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 8919F6BD2B48D754",
      "sudo chmod +r /usr/share/keyrings/clickhouse-keyring.gpg",
      "echo \"deb [signed-by=/usr/share/keyrings/clickhouse-keyring.gpg] https://packages.clickhouse.com/deb lts main\" | sudo tee /etc/apt/sources.list.d/clickhouse.list",
      "sudo apt-get update",
      "echo clickhouse-server clickhouse-server/default-password password ${var.zbx_ch_pass} | sudo debconf-set-selections",
      "sudo apt-get install -y clickhouse-common-static=${var.clickhouse_version}*",
      "sudo apt-get install -y clickhouse-server=${var.clickhouse_version}* clickhouse-client=${var.clickhouse_version}*"
    ]
  }

  provisioner "shell" {
  name = "Set permissions to copy config"
  inline = [
    "sudo chmod -R 700 /etc/clickhouse-server",
    // ru on host id
    // set  
    "packer_user=$(id -nu 1000)", # debian
    "sudo chown -R $packer_user:$packer_user /etc/clickhouse-server",
  ]
  }

  provisioner "file" {
    source      = "clickhouse/config.d"
    destination = "/etc/clickhouse-server"
  }

  provisioner "file" {
    source      = "clickhouse/users.xml"
    destination = "/etc/clickhouse-server/users.xml"
  }

  provisioner "shell" {
  name = "Start clickhouse server"
  inline = [
    "sudo chown -R clickhouse:clickhouse /etc/clickhouse-server /etc/clickhouse-server /var/log/clickhouse-server /var/lib/clickhouse",
    "sudo systemctl enable --now clickhouse-server",
    "journalctl -u clickhouse-server| tail -20"
  ]
  }

# provisioner "shell" {
#   inline = [
#     "sudo chown -R clickhouse:clickhouse /etc/clickhouse-server /etc/clickhouse-server /var/log/clickhouse-server /var/lib/clickhouse",
#     "sudo systemctl restart clickhouse-server",
#     "sudo systemctl status clickhouse-server",
#       "journalctl -u clickhouse-server| tail -20",
#     "if ! clickhouse-client --user ${var.zbx_ch_user} --password ${var.zbx_ch_pass} --database ${var.zbx_ch_db} --query 'select count(*) from history_str;'; then",
#     "echo 'Install glaber clickhouse schema'",
#     "wget -q https://gitlab.com/mikler/glaber/-/raw/${var.glaber_tag}/database/clickhouse/history.sql",
#     "sed -i -e 's/glaber/${var.zbx_ch_db}/g' -e 's/6 MONTH/${var.zbx_ch_retention}/g' history.sql",
#     "clickhouse-client --user ${var.zbx_ch_user} --password ${var.zbx_ch_pass} --multiquery < history.sql",
#     "else",
#     "echo 'Glaber clickhouse schema already installed'",
#     "fi"
#   ]
# }

# provisioner "shell" {
#   inline = [
#      "sudo chown -R clickhouse:clickhouse /etc/clickhouse-server /etc/clickhouse-server /var/log/clickhouse-server /var/lib/clickhouse",
#      "sudo systemctl restart clickhouse-server",
#      "sudo systemctl status clickhouse-server",
#      "while true; do",
#      "  echo 'This will print indefinitely. Press Ctrl+C to stop.'",
#      "  sleep 60",
#      "done"
#    ]
#  }


  # set random password
  # add ? script? ... to apply galber database
  # make schema
  # start systemd

}
