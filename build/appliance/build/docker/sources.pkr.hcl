source "docker" "glaber-server" {
  image  = "registry.gitlab.com/mikler/glaber/debian-bullseye-net-snmp-ssl:1.0.2"
  commit = true
  changes = [
    "ENV DEBIAN_FRONTEND noninteractive",
    "ENV LANG en_US.UTF-8",
    "ENV LANGUAGE en_US:en",
    "ENV LC_ALL en_US.UTF-8",
    "ENTRYPOINT [\"/bin/bash\", \"-c\", \"/root/docker-entrypoint.sh\"]"
  ]
}

source "docker" "glaber-web-nginx" {
  image  = "debian:bullseye"
  commit = true
  changes = [
    "ENV DEBIAN_FRONTEND noninteractive",
    "ENV LANG en_US.UTF-8",
    "ENV LANGUAGE en_US:en",
    "ENV LC_ALL en_US.UTF-8",
    "VOLUME /etc/ssl/nginx",
    "WORKDIR /usr/share/zabbix",
    "EXPOSE 80",
    "ENTRYPOINT [\"docker-entrypoint.sh\"]"
  ]
}
