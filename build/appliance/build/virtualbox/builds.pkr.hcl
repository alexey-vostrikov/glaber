build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]

  provisioner "shell" {
    name = "Install clickhouse"
    inline = [
      "export DEBIAN_FRONTEND=noninteractive",
      "sudo apt-get update -y",
      "sudo apt-get upgrade -y",
      "sudo apt-get install -y gnupg2",
      "export GNUPGHOME=$(mktemp -d)",
      "sudo gpg --no-default-keyring --keyring /usr/share/keyrings/clickhouse-keyring.gpg --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 8919F6BD2B48D754",
      "sudo chmod +r /usr/share/keyrings/clickhouse-keyring.gpg",
      "echo 'deb [signed-by=/usr/share/keyrings/clickhouse-keyring.gpg] https://packages.clickhouse.com/deb lts main' | sudo tee /etc/apt/sources.list.d/clickhouse.list",
      "sudo apt-get update",
      "echo clickhouse-server clickhouse-server/default-password password ${var.zbx_ch_pass} | sudo debconf-set-selections",
      "sudo apt-get install -y clickhouse-common-static=${var.clickhouse_version}*",
      "sudo apt-get install -y clickhouse-server=${var.clickhouse_version}* clickhouse-client=${var.clickhouse_version}*"
    ]
  }
# try su - $(id -nu 1000)
# add to builder image debian user with id 1000 and ability su - 1000;
# with additional group root group
# maybe set default USER?
# any restiction for the docker build?
# packer init ...
# copy...
  provisioner "shell" {
  name = "Set permissions to copy config"
  inline = [
    "sudo chmod -R 700 /etc/clickhouse-server",
    // run on builder host `id -u`
    // set number in next step  ## prepare set 1000 with env
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
    "journalctl -u clickhouse-server| tail -50",
    "while ! ss -ltn 'sport = :9000' | grep -q ':9000'; do echo 'Waiting for port 9000 to be ready...' && sleep 2; done",
  ]
  }

provisioner "shell" {
  name = "Import glaber schema to the clickhouse database"
  inline = [
    "if ! clickhouse-client --user ${var.zbx_ch_user} --password ${var.zbx_ch_pass} --database ${var.zbx_ch_db} --query 'select count(*) from history_str;'; then",
    "echo 'Download glaber clickhouse schema'",
    "wget -q https://gitlab.com/mikler/glaber/-/raw/${var.glaber_tag}/database/clickhouse/history.sql",
    "echo 'Make custom retention period'",
    "sed -i -e 's/glaber/${var.zbx_ch_db}/g' -e 's/6 MONTH/${var.zbx_ch_retention}/g' history.sql",
    "echo 'Import clickhouse.sql to the ${var.zbx_ch_db} database'",
    "clickhouse-client --user ${var.zbx_ch_user} --password ${var.zbx_ch_pass} --multiquery < history.sql",
    "else",
    "echo 'Glaber clickhouse schema already installed'",
    "fi"
  ]
}

}
