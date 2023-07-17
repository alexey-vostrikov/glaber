build {
  name = "Build glaber server mysql"
  sources = [
    "source.docker.glaber-server"
  ]
  provisioner "shell" {
    inline = [
      "apt-get update",
      "apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales net-tools iputils-ping",
      "wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | apt-key add -",
      "echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" >> /etc/apt/sources.list.d/glaber.list",
      "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' > /etc/apt/sources.list.d/nonfree.list",
      "apt-get update",
      "apt-get install -y glaber-server-mysql=1:${var.glaber_build_version}* snmp-mibs-downloader",
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
      "download-mibs",
      "rm /etc/zabbix/zabbix_server.conf"
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
  post-processors {
    post-processor "docker-tag" {
      repository = "${var.gitlab_repo}/glaber-server"
      tags       = ["${var.glaber_build_version}"]
    }
    post-processor "docker-push" {
      login          = true
      login_server   = "${var.registry}"
      login_username = "${var.registry_user}"
      login_password = "${var.registry_password}"
    }
  }
}

build {
  name = "Build glaber web nginx php-fpm"
  sources = [
    "source.docker.glaber-web-nginx"
  ]
  provisioner "shell" {
    inline = [
      "apt-get update",
      "apt-get install -y wget software-properties-common nmap gnupg2 openssl",
      "apt-get install -y ca-certificates supervisor default-mysql-client locales",
      "apt-get install -y lsb-release apt-transport-https",
      "wget -qO - https://glaber.io/${var.glaber_repo}/key/repo.gpg | apt-key add -",
      "wget -qO - https://nginx.org/keys/nginx_signing.key | apt-key add -",
      "echo \"deb [arch=amd64] https://glaber.io/${var.glaber_repo}/debian $(lsb_release -sc) main\" >> /etc/apt/sources.list.d/glaber.list",
      "apt-get update",
      "apt-get install -y glaber-nginx-conf=1:${var.glaber_build_version}*",
      "rm -rf /var/lib/{apt,dpkg,cache,log}",
      "apt-get autoremove --yes",
      "apt-get clean autoclean",
      "sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "locale-gen"
    ]
  }
  provisioner "file" {
    source      = "glaber-nginx/etc/"
    destination = "/etc/"
  }
  provisioner "shell" {
    inline = [
      "mkdir /run/php && chown www-data:www-data /run/php",
      "chown www-data:www-data /etc/zabbix/web/zabbix.conf.php",
      "mv /etc/docker-entrypoint.sh /usr/bin",
      "sed -i \"s/#        listen          80;/    listen          80;/g\" /etc/nginx/conf.d/zabbix.conf",
      "sed -i \"s/#        server_name     example.com;/    server_name     _;/g\" /etc/nginx/conf.d/zabbix.conf"
    ]
  }
  post-processors {
    post-processor "docker-tag" {
      repository = "${var.gitlab_repo}/glaber-nginx"
      tags       = ["${var.glaber_build_version}"]
    }
    post-processor "docker-push" {
      login          = true
      login_server   = "${var.registry}"
      login_username = "${var.registry_user}"
      login_password = "${var.registry_password}"
    }
  }
}
