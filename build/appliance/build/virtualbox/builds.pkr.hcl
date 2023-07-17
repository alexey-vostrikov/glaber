build {
  name = "Build base glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "sudo apt-get update",
      "sudo apt-get upgrade -y",
      "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales",
      "sudo apt-get install -y software-properties-common openssl dirmngr net-tools",
      "sudo apt-get install -y ca-certificates supervisor default-mysql-client iputils-ping",
      "wget -qO - https://nginx.org/keys/nginx_signing.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/nginx.gpg >/dev/null",
      //"wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/glaber.gpg >/dev/null",
      //"echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" | sudo tee /etc/apt/sources.list.d/glaber.list",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' | sudo tee /etc/apt/sources.list.d/nonfree.list",
      "export GNUPGHOME=$(mktemp -d)",
      "sudo GNUPGHOME=\"$GNUPGHOME\" gpg --no-default-keyring --keyring /usr/share/keyrings/clickhouse-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 8919F6BD2B48D754",
      "sudo chmod +r /usr/share/keyrings/clickhouse-keyring.gpg",
      "echo \"deb [signed-by=/usr/share/keyrings/clickhouse-keyring.gpg] https://packages.clickhouse.com/deb lts main\" | sudo tee /etc/apt/sources.list.d/clickhouse.list",
      "sudo apt-get update",
      //"sudo apt-get install -y glaber-server-mysql=1:${var.glaber_build_version}* snmp-mibs-downloader",
      "sudo apt-get install -y snmp-mibs-downloader",
      //"sudo apt-get install -y glaber-nginx-conf=1:${var.glaber_build_version}*",
      //"sudo rm -rf /var/lib/{apt,dpkg,cache,log}/",
      //"sudo apt-get autoremove --yes",
      //"sudo apt-get clean autoclean",
      //"sudo mkdir -p /var/lib/mysql/vcdump/ /run/zabbix",
      //"sudo chown zabbix:zabbix /run/zabbix /var/lib/mysql/vcdump/",
      "sudo chmod +s /usr/bin/nmap",
      //"sudo chmod +s /usr/sbin/glbmap",
      "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo locale-gen",
      "sudo download-mibs",
      "timeout 30m sudo apt-get install -y clickhouse-common-static=21.3*",
      "timeout 30m sudo apt-get install -y clickhouse-server=21.3* clickhouse-client=21.3*"
      # interactive too ((
    ]
  }
}
  # provisioner "shell" {
  #   inline = [
  #     "sudo groupadd -r clickhouse",
  #     "sudo useradd -r --shell /bin/false --home-dir /nonexistent -g clickhouse clickhouse",
  #     "sudo mkdir /etc/clickhouse-server",
  #     "sudo chown -R debian:debian /etc/clickhouse-server",
  #   ]
  # }
  # provisioner "file" {
  #   source      = "clickhouse/users.xml"
  #   destination = "/etc/clickhouse-server/users.xml"
  # }
  # provisioner "file" {
  #   source      = "clickhouse/config.d"
  #   destination = "/etc/clickhouse-server/config.d"
  # }

#   provisioner "shell" {
#     inline = [
#       "export DEBIAN_FRONTEND=noninteractive",
#       //"sudo chown -R clickhouse:clickhouse /etc/clickhouse-server/",
#       "sudo sed -i 's/stable/lts/g' /etc/apt/sources.list.d/clickhouse.list",
#       "sudo apt-get update",
#       "timeout 30m sudo apt-get install -y clickhouse-server=21.3* clickhouse-client=21.3*"
#     ]
#   }
# }

# apt-get download
# copy click config (users)
# install
# copy other configs
# run it
      # "export DEBIAN_FRONTEND=noninteractive",
      # "sudo apt-get install -y clickhouse-server=21.3.20 clickhouse-client=21.3.20"

  # https://clickhouse.com/docs/en/install#quick-install
  # copy config files glaber-server
  # copy config files glaber-web
  # Install mysql (percona 8)
  # configure mysql
  # Install clickhouse
  # Configure clickhouse
  # at the end enable all systemd
  # - click
  # - mysql
  # - nginx
  # - php-fpm
  # - glaber server
  #    // Start and enable clickhouse-server
  #    "sudo systemctl start clickhouse-server",
  #    "sudo systemctl enable clickhouse-server"

