build {
  name = "Build glaber vm appliance"
  sources = [
    "source.qemu.debian"
  ]
  provisioner "shell" {
    inline = [
      "apt-get update"
    ]
  }
}

