source "qemu" "debian" {
  iso_url      = "${var.source_qcow}"
  iso_checksum = "${var.source_checksum_url}"
  disk_image = true
  ssh_username = "debian"
  ssh_private_key_file = "/home/bakaut/.ssh/id_ed25519_nopass"
  ssh_timeout = "30m"
  shutdown_command = "echo 'packer' | sudo -S shutdown -P now"
  vm_name = "glaber"
  cpus = 1
  memory = 2048
  disk_size = 300960
  headless = true
}

