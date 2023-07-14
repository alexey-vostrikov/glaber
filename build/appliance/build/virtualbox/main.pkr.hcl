packer {
  required_plugins {
    docker = {
      version = ">= 1.0.9"
      source  = "github.com/hashicorp/qemu"
    }
  }
}
