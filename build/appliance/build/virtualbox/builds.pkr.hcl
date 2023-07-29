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
}
