variable "glaber_build_version" {
  type    = string
  default = "${env("GLABER_VERSION")}"
}

variable "glaber_repo" {
  type    = string
  default = "${env("REPO_DIR")}"
}

variable "registry" {
  type    = string
  default = "${env("CI_REGISTRY")}"
}

variable "registry_user" {
  type    = string
  default = "${env("CI_REGISTRY_USER")}"
}

variable "registry_password" {
  type      = string
  default   = "${env("DOCKER_PASSWORD")}"
  sensitive = true
}

variable "gitlab_repo" {
  type    = string
  default = "${env("CI_REGISTRY_IMAGE")}"
}
