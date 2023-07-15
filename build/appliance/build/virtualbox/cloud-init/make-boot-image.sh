#/bin/bash

echo "instance-id: {}; local-hostname: glaber" > meta-data

cat <<EOF> user-data
#cloud-config
password: debian
ssh_pwauth: true
chpasswd:
  expire: false

locale: en_US.UTF-8
locale_configfile: /etc/default/locale
EOF

genisoimage -output seed.img -volid cidata -joliet -rock user-data meta-data
