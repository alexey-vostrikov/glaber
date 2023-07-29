build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  # provisioner "shell" {
  #   name = "Install base apt"
  #   inline = [
  #     "export DEBIAN_FRONTEND=noninteractive",
  #     "export LD_LIBRARY_PATH=/usr/local/lib",
  #     "sudo apt-get update",
  #     "sudo apt-get upgrade -y",
  #     "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales",
  #     "sudo apt-get install -y software-properties-common openssl dirmngr net-tools",
  #     "sudo apt-get install -y ca-certificates supervisor default-mysql-client iputils-ping",
  #     "echo 'deb http://ftp.de.debian.org/debian bullseye main non-free' | sudo tee /etc/apt/sources.list.d/nonfree.list",
  #     "sudo apt-get update",
  #     "sudo apt-get install -y snmp-mibs-downloader",
  #     "sudo chmod +s /usr/bin/nmap",
  #     "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
  #     "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
  #     "sudo locale-gen",
  #     "sudo download-mibs",
  #     "sudo apt-get install -y build-essential libssl-dev libwrap0-dev libc6-dev wget file libperl-dev",
  #     "cd /tmp",
  #     "sudo wget -q http://sourceforge.net/projects/net-snmp/files/net-snmp/${var.snmp_version}/net-snmp-${var.snmp_version}.tar.gz",
  #     "sudo tar -xvzf net-snmp-${var.snmp_version}.tar.gz",
  #     "cd /tmp/net-snmp-${var.snmp_version}",
  #     "sudo ./configure --with-defaults --with-persistent-directory=/var/net-snmp --with-logfile=/var/log/snmpd.log --with-openssl --enable-privacy",
  #     "sudo make",
  #     "sudo make install",
  #     "sudo apt-get autoremove -y",
  #     "sudo apt-get clean",
  #     "sudo rm -rf /var/lib/apt/lists/* /tmp/net-snmp-${var.snmp_version}.tar.gz /tmp/net-snmp-${var.snmp_version}"
  #   ]
  # }
  provisioner "shell" {
    name = "Install clickhouse"
    inline = [
      "export GNUPGHOME=$(mktemp -d)",
      "sudo GNUPGHOME=\"$GNUPGHOME\" gpg --no-default-keyring --keyring /usr/share/keyrings/clickhouse-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 8919F6BD2B48D754",
      "sudo chmod +r /usr/share/keyrings/clickhouse-keyring.gpg",
      "echo \"deb [signed-by=/usr/share/keyrings/clickhouse-keyring.gpg] https://packages.clickhouse.com/deb lts main\" | sudo tee /etc/apt/sources.list.d/clickhouse.list",
      "sudo apt-get update",
      "echo clickhouse-server clickhouse-server/default-password password mysecretpassword | sudo debconf-set-selections",
      "timeout 30m sudo apt-get install -y clickhouse-common-static=21.3*",
      "timeout 30m sudo apt-get install -y clickhouse-server=21.3* clickhouse-client=21.3*"
    ]
  }

}
