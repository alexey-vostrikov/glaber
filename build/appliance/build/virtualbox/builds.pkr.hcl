build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    inline = [
      "apt-get update",
      "apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales net-tools iputils-ping",
      "apt-get install -y software-properties-common openssl",
      "apt-get install -y ca-certificates supervisor default-mysql-client",
      "wget -qO - https://nginx.org/keys/nginx_signing.key | apt-key add -",
      "wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | apt-key add -",
      "echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" >> /etc/apt/sources.list.d/glaber.list",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' > /etc/apt/sources.list.d/nonfree.list",
      "apt-get update",
      "apt-get install -y glaber-server-mysql=1:${var.glaber_build_version}* snmp-mibs-downloader",
      "apt-get install -y glaber-nginx-conf=1:${var.glaber_build_version}*",
      "rm -rf /var/lib/{apt,dpkg,cache,log}/",
      "apt-get autoremove --yes",
      "apt-get clean autoclean",
      "mkdir -p /var/lib/mysql/vcdump/ /run/zabbix",
      "chown zabbix:zabbix /run/zabbix /var/lib/mysql/vcdump/",
      "chmod +s /usr/bin/nmap",
      "chmod +s /usr/sbin/glbmap",
      "sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "locale-gen",
      "download-mibs"
    ]
  }
  provisioner "file" {
    source      = "glaber-server/etc/zabbix/zabbix_server.conf"
    destination = "/etc/zabbix/zabbix_server.conf"
  }
  provisioner "file" {
    source      = "glaber-server/docker-entrypoint.sh"
    destination = "/root/docker-entrypoint.sh"
  }
}

