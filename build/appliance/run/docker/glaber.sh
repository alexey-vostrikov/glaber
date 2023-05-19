#!/usr/bin/env bash
set -e

# functions
mysql-schema-package-url() {
  local name=$1
  local version=$2
  URL="${GITLAB_PROJECT_URL}/${PROJECT_ID}/packages?per_page=1000"
  PACKAGE_ID=$(curl -s "$URL" | jq --arg name "$name" --arg version "$version" '.[] | select(.name == $name) | select(.version == $version) | .id')
  if [[ ! -z "$VERSION" ]]; then
    info "Wrong or not existing glaber version: $version"
    exit 1
  else
    FILES_URL="${GITLAB_PROJECT_URL}/${PROJECT_ID}/packages/${PACKAGE_ID}/package_files"
    FILE_ID=$(curl -s "$FILES_URL" | jq '.[] | .id')
    echo "${GIT_BASE}/-/package_files/${FILE_ID}/download/"
  fi
}
glaber-version() {
  if [[ -f .version ]]; then
    export GLABER_TAG=$(cat .version)
    else
      MAIN_BRANCH=$(git ls-remote --symref $GIT_REPO HEAD | \
                    head -1 | awk '{print $2}' | cut -d'/' -f3)
      LATEST_TAG=$(git ls-remote --refs --sort='version:refname' --tags \
                  $GIT_REPO | tail --lines=1 | cut -d'/' -f3)
      local VERSION=$1
      if [[ ! -z "$VERSION" ]]; then
        if [[ "$VERSION" == "stable" ]]; then
          export GLABER_TAG=$LATEST_TAG
        elif [[ "$VERSION" == "latest" ]]; then
          export GLABER_TAG=$MAIN_BRANCH
        elif git ls-remote --refs --tags $GIT_REPO| grep $VERSION| cut -d'/' -f3; then
          export GLABER_TAG=$VERSION
        else
          echo "Wrong glaber version. Avaliable versions:"
          git ls-remote --refs --sort='version:refname' --tags \
              $GIT_REPO origin 2* | tail --lines=1 | cut -d'/' -f3
          git ls-remote --refs --sort='version:refname' --tags \
              $GIT_REPO origin 3* | tail --lines=3 | cut -d'/' -f3
          exit 1
        fi
      else
        export GLABER_TAG=$LATEST_TAG
      fi
  fi
  export GLABER_VERSION=$(curl -s ${GIT_BASE}/-/raw/${GLABER_TAG}/include/version.h | \
                  grep GLABER_VERSION | tr -dc 0-9.)
}
apitest () {
  info "Install hurl for testing glaber"
  [ -d ".tmp/hurl-$HURL_VERSION" ] || \
  curl -sL https://github.com/Orange-OpenSource/hurl/releases/download/\
$HURL_VERSION/hurl-$HURL_VERSION-x86_64-linux.tar.gz | \
  tar xvz -C .tmp/ 1>/dev/null
  info "Testing that glaber-server is runing"
  .tmp/hurl-$HURL_VERSION/hurl  -o .tmp/hurl.log \
    --variables-file=../../test/.hurl \
    --retry --retry-max-count 20 --retry-interval 15000 \
    ../../test/glaber-runing.hurl
    if [ ! -f .version ]; then
      echo $GLABER_VERSION > .version
    fi
}
diag () {
  info "Collect glaber logs"
  docker-compose logs --no-color clickhouse > .tmp/diag/clickhouse.log || true
  docker-compose logs --no-color mysql > .tmp/diag/mysql.log || true
  docker-compose logs --no-color glaber-nginx > .tmp/diag/glaber-nginx.log || true
  docker-compose logs --no-color glaber-server > .tmp/diag/glaber-server.log || true
  docker-compose ps > .tmp/diag/ps.log
  info "Collect geneal information about system and docker"
  uname -a > .tmp/diag/uname.log
  git log -1 --stat > .tmp/diag/last-commit.log
  cat /etc/os-release > .tmp/diag/os-release
  free -m > .tmp/diag/mem.log
  df -h   > .tmp/diag/disk.log
  docker-compose --version > .tmp/diag/docker-compose-version.log
  docker --version > .tmp/diag/docker-version.log
  docker info > .tmp/diag/docker-info.log
  curl http://127.0.1.1:${ZBX_PORT:-80} > .tmp/diag/curl.log
  command -v zip >/dev/null 2>&1 || \
  { echo >&2 "zip is required, please install it and start over. Aborting."; exit 1; }
  info "Add diagnostic information to .tmp/diag/diag.zip"
  zip -r .tmp/diag/diag.zip .tmp/diag/ 1>/dev/null
  info "Fill free to create issue https://gitlab.com/mikler/glaber/-/issues"
  info "And attach .tmp/diag/diag.zip to it"
}
git-reset-variables-files () {
  git checkout HEAD -- clickhouse/users.xml
  git checkout HEAD -- .env
}
info () {
  local message=$1
  echo $(date --rfc-3339=seconds) $message
}
wait () {
  info "Waiting zabbix to start..."
  apitest && info "Success" && info "$(cat .zbxweb)" && exit 0 || \
  docker-compose logs --no-color && \
  curl http://127.0.1.1:${ZBX_PORT:-80} || true && \
  info "Please try to open zabbix url with credentials:" && \
  info "$(cat .zbxweb)"  && \
  info "If not success, please run diagnostics ./glaber.sh diag" && \
  info "Zabbix start failed.Timeout 5 minutes reached" && \
  exit 1
}
set-passwords() {
  gen-password() {
    < /dev/urandom tr -dc _A-Z-a-z-0-9 | head -c12
  }
  make-bcrypt-hash() {
    htpasswd -bnBC 10 "" $1 | tail -c 55
  }
  if [ ! -f .passwords.created ]; then
    git-reset-variables-files
    echo "GLABER_TAG=$GLABER_TAG" >> .env
    source .env
    ZBX_CH_PASS=$(gen-password)
    ZBX_WEB_ADMIN_PASS=$(gen-password)
    sed -i -e "s/MYSQL_PASSWORD=.*/MYSQL_PASSWORD=$(gen-password)/" \
           -e "s/ZBX_CH_PASS=.*/ZBX_CH_PASS=$ZBX_CH_PASS/" \
           -e "s/MYSQL_ROOT_PASSWORD=.*/MYSQL_ROOT_PASSWORD=$(gen-password)/" \
    .env
    [ -d ".mysql/docker-entrypoint-initdb.d/" ] || \
    mkdir mysql/docker-entrypoint-initdb.d/  
    [[ ! -f mysql/docker-entrypoint-initdb.d/create.sql ]] && \
    curl -sSR $(mysql-schema-package-url "mysql" $GLABER_VERSION) --output - | tar -xz && \
    mv create.sql mysql/docker-entrypoint-initdb.d/create.sql
    echo "use MYSQL_DATABASE;" >> mysql/docker-entrypoint-initdb.d/create.sql
    echo "update users set passwd='\$2y\$10\$ZBX_WEB_ADMIN_PASS' where username='Admin';" >> mysql/docker-entrypoint-initdb.d/create.sql
    ZBX_WEB_ADMIN_PASS_HASH=$(make-bcrypt-hash $ZBX_WEB_ADMIN_PASS)
    sed -i -e "s#MYSQL_DATABASE#$MYSQL_DATABASE#" \
           -e "s#ZBX_WEB_ADMIN_PASS#$ZBX_WEB_ADMIN_PASS_HASH#" \
    mysql/docker-entrypoint-initdb.d/create.sql
    sudo chown -R 1001:1001 mysql/docker-entrypoint-initdb.d/
    sed -i -e "s/<password>.*<\/password>/<password>$ZBX_CH_PASS<\/password>/" \
           -e "s/10000000000/$ZBX_CH_CONFIG_MAX_MEMORY_USAGE/" \
           -e "s/defaultuser/$ZBX_CH_USER/" \
    clickhouse/users.xml
    sed -i -e "s/3G/$MYSQL_CONFIG_INNODB_BUFFER_POOL_SIZE/" \
    mysql/etc/my.cnf.d/innodb.cnf
    echo "user=Admin" > ../../test/.hurl
    echo "pass=$ZBX_WEB_ADMIN_PASS" >> ../../test/.hurl
    echo "port=${ZBX_PORT:-80}" >> ../../test/.hurl
    touch .passwords.created
    echo "Zabbix web access http://$(hostname -I | awk '{print $1}'):${ZBX_PORT:-80} Admin $ZBX_WEB_ADMIN_PASS" > .zbxweb
  fi
}
usage() {
  echo "Usage: $0 <action>"
  echo
  echo "$0 build                          - Build docker images for mysql and clickhouse"
  echo "$0 start   (latest,stable,3.0.50) - Build docker images and start glaber"
  echo "$0 upgrade (latest,stable,3.0.50) - Upgrade docker images and restart glaber"
  echo "$0 stop                           - Stop glaber containers"
  echo "$0 diag                           - Collect glaber start and some base system info to the file"
}
build() {
  [ -d "glaber-server/workers_script/" ] || mkdir -p glaber-server/workers_script/
  [ -d ".tmp/diag/" ] || mkdir -p .tmp/diag/
  [ -d ".mysql/mysql_data/" ] || \
  sudo install -d -o 1001 -g 1001 mysql/mysql_data/
  [ -d ".clickhouse/clickhouse_data/" ] || \
  sudo install -d -o 101 -g 103 clickhouse/clickhouse_data
  docker-compose pull 1>.tmp/diag/docker-pull.log
}
start() {
  set-passwords
  build
  docker-compose up -d
  wait
}
stop() {
  docker-compose down
}
remove() {
  docker-compose down
  read -p "Are you sure to completely remove glaber with database [y/n] ? " -n 1 -r
  echo
  if [[ $REPLY =~ ^[Yy]$ ]]
  then
    rm .passwords.created .zbxweb ../../test/.hurl .version || true
    sudo rm -rf mysql/docker-entrypoint-initdb.d mysql/mysql_data/ clickhouse/clickhouse_data
    git-reset-variables-files
  fi
}
force-remove() {
  docker-compose down
  rm .passwords.created .zbxweb ../../test/.hurl .version || true
  sudo rm -rf mysql/docker-entrypoint-initdb.d mysql/mysql_data/ clickhouse/clickhouse_data
  git-reset-variables-files
}
recreate() {
  remove
  start
}
upgrade() {
  docker-compose pull
  docker-compose up -d
  wait
}

HURL_VERSION="1.8.0"
# export ZBX_PORT=8050
GIT_BASE="https://gitlab.com/mikler/glaber"
GIT_REPO="${GIT_BASE}.git"
PROJECT_ID="11936575"
GITLAB_PROJECT_URL="https://gitlab.com/api/v4/projects"

# main part
if [ $# -gt 0 ] && [ $# -lt 3 ]; then
  echo ""
else
  echo "Invalid number of arguments"
  usage
  exit 1
fi

# Check whether docker-compose, jq and apache2-utils is installed
command -v docker-compose >/dev/null 2>&1 || \
{ echo >&2 "docker-compose is required, please install it and start over. Aborting."; exit 1; }

command -v jq >/dev/null 2>&1 || \
{ echo >&2 "jq is required, please install it and start over. Aborting."; exit 1; }

command -v htpasswd >/dev/null 2>&1 || \
{ echo >&2 "htpasswd is required, please install it apt-get install apache2-utils. And start over. Aborting."; exit 1; }

if [ "$1" == "build" ]; then
  build
elif [[ "$1" == "start" ]]; then
  glaber-version $2
  start
elif [ "$1" == "stop" ]; then
  stop
elif [ "$1" == "recreate" ]; then
  recreate
elif [ "$1" == "remove" ]; then
  glaber-version $2
  remove
elif [ "$1" == "force-remove" ]; then
  glaber-version $2
  force-remove
elif [ "$1" == "diag" ]; then
  diag
elif [ "$1" == "test" ]; then
  apitest
elif [ "$1" == "upgrade" ]; then
  rm .version
  glaber-version $2
  upgrade
else
  echo "unknown command"
fi
