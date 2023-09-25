source "qemu" "debian" {
  # iso_url      = "${var.source_qcow}"
  iso_url        = "/opt/vm-output/glaber.qcow2"
  # iso_checksum = "${var.source_checksum_url}"
  iso_checksum = "none"
  disk_image = true

  accelerator       = "kvm"
  
  ssh_username     = "${var.username}"
  ssh_password     = "${var.password}"
  ssh_timeout      = "5m"
  shutdown_command = "sudo -S /sbin/shutdown -hP now"
  vm_name          = "${var.output_name}"
  
  cpus = 1
  memory = 2048
  disk_size = 300960
  headless = true

  # Builds a compact image
  disk_compression   = true
  disk_discard       = "unmap"
  skip_compaction    = false
  disk_detect_zeroes = "unmap"

  output_directory = "${var.output_dir}"
  format             = "qcow2"

  boot_wait    = "10s"
  qemuargs = [
        ["-cpu", "host,+ssse3"],
        ["-m", "2048M"],
        ["-smp", "1"],
        ["-cdrom", "cloud-init/seed.img"]
      ]
}
