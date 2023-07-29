build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    name = "Install base application"
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "sudo apt-get update",
      "sudo apt-get upgrade -y",
      "sudo apt-get install -y nmap wget gnupg2 lsb-release apt-transport-https locales",
      "sudo apt-get install -y software-properties-common openssl dirmngr net-tools",
      "sudo apt-get install -y ca-certificates supervisor default-mysql-client iputils-ping",
      "wget -qO - https://nginx.org/keys/nginx_signing.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/nginx.gpg >/dev/null",      "sudo apt-get update",
      "sudo apt-get install -y snmp-mibs-downloader",
      "sudo chmod +s /usr/bin/nmap",
      "sudo sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo sed -i '/ru_RU.UTF-8/s/^# //g' /etc/locale.gen",
      "sudo locale-gen",
      "sudo download-mibs",
    ]
  }
}
