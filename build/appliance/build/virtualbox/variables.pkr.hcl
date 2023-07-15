variable "glaber_build_version" {
  type    = string
  default = "3.1.8"
}

variable "glaber_repo" {
  type    = string
  default = "repo"
}

variable "source_checksum_url" {
  type    = string
  default = "file:https://cloud.debian.org/images/cloud/bullseye/20210814-734/SHA512SUMS"
}

variable "source_qcow" {
  type    = string
  default = "https://cloud.debian.org/images/cloud/bullseye/20210814-734/debian-11-genericcloud-amd64-20210814-734.qcow2"
}