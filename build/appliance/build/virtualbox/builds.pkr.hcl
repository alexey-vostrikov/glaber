build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "sudo apt-get update",
      "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales net-tools iputils-ping",
      "sudo apt-get install -y software-properties-common openssl",
      "sudo apt-get install -y ca-certificates supervisor default-mysql-client",
      "wget -qO - https://nginx.org/keys/nginx_signing.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/nginx.gpg >/dev/null",
      "wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/glaber.gpg >/dev/null",
      "echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" | sudo tee /etc/apt/sources.list.d/glaber.list",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' | sudo tee /etc/apt/sources.list.d/nonfree.list",
      "sudo apt-get update",
      "sudo apt-get install -y glaber-server-mysql=1:${var.glaber_build_version}* snmp-mibs-downloader",
      "sudo apt-get install -y glaber-nginx-conf=1:${var.glaber_build_version}*",
      "sudo rm -rf /var/lib/{apt,dpkg,cache,log}/",
      "sudo apt-get autoremove --yes",
      "sudo apt-get clean autoclean",
      "sudo mkdir -p /var/lib/mysql/vcdump/ /run/zabbix",
      "sudo chown zabbix:zabbix /run/zabbix /var/lib/mysql/vcdump/",
      "sudo chmod +s /usr/bin/nmap",
      "sudo chmod +s /usr/sbin/glbmap",
      "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo locale-gen",
      "sudo download-mibs"
    ]
  }
}

