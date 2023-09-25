build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]

#   provisioner "shell" {
#     name = "Install clickhouse"
#     inline = [
#       "export DEBIAN_FRONTEND=noninteractive",
#       "sudo apt-get update",
#       "sudo apt-get upgrade -y",
#       "sudo apt-get install -y gnupg2 debconf-utils lsb-release", # some prereq for click and mysql
#       "sudo mkdir /root/.gnupg/",
#       "sudo gpg --no-default-keyring --keyring /usr/share/keyrings/clickhouse-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys ${var.ch_pgp}",
#       "sudo chmod +r /usr/share/keyrings/clickhouse-keyring.gpg",
#       "echo 'deb [signed-by=/usr/share/keyrings/clickhouse-keyring.gpg] https://packages.clickhouse.com/deb lts main' | sudo tee /etc/apt/sources.list.d/clickhouse.list",
#       "sudo apt-get update",
#       "echo clickhouse-server clickhouse-server/default-password password ${var.zbx_ch_pass} | sudo debconf-set-selections",
#       "sudo apt-get install -y clickhouse-common-static=${var.clickhouse_version}*",
#       "sudo apt-get install -y clickhouse-server=${var.clickhouse_version}* clickhouse-client=${var.clickhouse_version}*"
#     ]
#   }

#   provisioner "shell" {
#   name = "Set permissions to copy config"
#   inline = [
#     "sudo chmod -R 700 /etc/clickhouse-server",
#     "packer_user=$(id -nu 1000)", # debian inside the kvm
#     "sudo chown -R $packer_user:$packer_user /etc/clickhouse-server",
#   ]
#   }

#   provisioner "file" {
#     name = "Copy clickhouse server config"
#     source      = "clickhouse/config.d"
#     destination = "/etc/clickhouse-server"
#   }

#   provisioner "file" {
#     name = "Copy clickhouse server users config"
#     source      = "clickhouse/users.xml"
#     destination = "/etc/clickhouse-server/users.xml"
#   }

#   provisioner "shell" {
#   name = "Start clickhouse server"
#   inline = [
#     "sudo chown -R clickhouse:clickhouse /etc/clickhouse-server /etc/clickhouse-server /var/log/clickhouse-server /var/lib/clickhouse",
#     "sudo systemctl enable --now clickhouse-server"
#   ]
#   }

#   provisioner "shell" {
#   name = "Install percona Mysql 8 server"
#   inline = [
#     "curl -O https://repo.percona.com/apt/percona-release_latest.generic_all.deb",
#     "sudo apt install -y ./percona-release_latest.generic_all.deb",
#     "sudo apt update",
#     "sudo percona-release setup ${var.percona_release}",
#     "echo percona-server-server	percona-server-server/root-pass password ${var.mysql_pass} | sudo debconf-set-selections",
#     "echo percona-server-server	percona-server-server/re-root-pass password ${var.mysql_pass} | sudo debconf-set-selections",
#     "echo percona-server-server	percona-server-server/default-auth-override select 'Use Legacy Authentication Method (Retain MySQL 5.x Compatibility)' | sudo debconf-set-selections",
#     "sudo apt install -y percona-server-server=${var.mysql_version} percona-toolkit",
#     //"sudo mysql_secure_installation",
#     "sudo systemctl enable --now mysql"
#   ]
#   }

  provisioner "shell" {
    name = "Install glaber server"
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "sudo dpkg --configure -a",
      "sudo apt-get update",
      "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales net-tools iputils-ping",
      "wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | sudo apt-key add -",
      "echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" >> /etc/apt/sources.list.d/glaber.list",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' > /etc/apt/sources.list.d/nonfree.list",
      "sudo apt-get update",
      "sudo apt-get install --no-install-recommends -y glaber-server-mysql=1:${var.glaber_build_version}* glaber-workers fping",
      "sudo apt-get install -y snmp-mibs-downloader",
      "sudo mkdir -p /var/lib/mysql/vcdump/ /run/zabbix",
      "sudo chown zabbix:zabbix /run/zabbix /var/lib/mysql/vcdump/",
      "sudo chmod +s /usr/bin/nmap",
      "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "locale-gen",
      "download-mibs",
      "sudo rm /etc/zabbix/zabbix_server.conf"
    ]
  }
  provisioner "file" {
    source      = "glaber-server/etc/zabbix/zabbix_server.conf"
    destination = "/etc/zabbix/zabbix_server.conf"
  }
  provisioner "file" {
    source      = "glaber-server/prepare-zabbix-vars.sh"
    destination = "/tmp/prepare-zabbix-vars.sh"
  }

  provisioner "shell" {
  name = "Prepare and start glaber server"
  inline = [
    "source .env",
    "bash /tmp/prepare-zabbix-vars.sh",
    "sudo systemctl enable --now zabbix-server",
    "sleep 10",
    "sudo systemctl status zabbix-server"
  ]
  }
  

# # install glaber-web(php-fpm), nginx as docker
# # prepare mysql schema

# # Configure databases after glaber binary installed
# provisioner "shell" {
#   name = "Import glaber schema to the clickhouse database"
#   inline = [
#     "wget -q https://gitlab.com/mikler/glaber/-/raw/${var.glaber_tag}/database/clickhouse/history.sql",
#     "echo 'Make custom clickhouse retention period'",
#     "sed -i -e 's/glaber/${var.zbx_ch_db}/g' -e 's/6 MONTH/${var.zbx_ch_retention}/g' history.sql",
#     "echo 'Import clickhouse.sql to the ${var.zbx_ch_db} database'",
#     "clickhouse-client --user ${var.zbx_ch_user} --password ${var.zbx_ch_pass} --multiquery < history.sql"
#   ]
# }

}
