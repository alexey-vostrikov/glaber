variable "glaber_build_version" {
  type    = string
  default = "3.1.8" ## prepare
}

variable "glaber_repo" {
  type    = string
  default = "repo" ## prepare
}

variable "source_checksum_url" {
  type    = string
  default = "file:https://cloud.debian.org/images/cloud/bullseye/20210814-734/SHA512SUMS"
}

variable "source_qcow" {
  type    = string
  default = "https://cloud.debian.org/images/cloud/bullseye/20210814-734/debian-11-genericcloud-amd64-20210814-734.qcow2"
}

variable "password" {
  type    = string
  default = "debian" ## prepare
  sensitive = true
}

variable "username" {
  type    = string
  default = "debian"
}

variable "output_dir" {
  type    = string
  default = "vm-output"
}


variable "output_name" {
  type    = string
  default = "glaber.qcow2"
}

variable "clickhouse_version" {
  default     = "21.3"
}

variable "percona_release" {
  default     = "ps80"
}

variable "mysql_version" {
  default     = "8.0.33-25-1.bullseye"
}

variable "zbx_ch_user" {
  type        = string
  description = "The ClickHouse username"
  default     = "defaultuser" ## prepare
}

variable "zbx_ch_pass" {
  type        = string
  description = "The ClickHouse password"
  default     = "password" ## prepare
  sensitive   = true
}

variable "ch_pgp" {
  type        = string
  default     = "8919F6BD2B48D754"
}

variable "mysql_pass" {
  type        = string
  default     = "p@SSW0RDing74@r" ## prepare
  sensitive   = true
}

variable "zbx_ch_db" {
  type        = string
  description = "The ClickHouse database"
  default     = "glaber" ## prepare
}

variable "glaber_tag" {
  type        = string
  description = "The Glaber tag"
  default     = "3.2.16" ## prepare
}

variable "zbx_ch_retention" {
  type        = string
  description = "The retention period for ClickHouse"
  default     = "30 DAY" ## prepare
}
