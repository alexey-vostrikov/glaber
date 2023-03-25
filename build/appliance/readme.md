# Glaber appliances:

Based on the last *stable* glaber version
Used for glaber quick start and may be used as starting point for migration from zabbix to glaber.

- [docker](./run/docker/)
- vagrant         # under develop
- virtualbox      # under develop
- yandex-cloud-vm # under develop
- aws-vm          # under develop
- helm-chart      # under develop

Fill free to create an issue https://gitlab.com/mikler/glaber/-/issues if you need some specialized type of appliance.

# Installation of any type of appliance:

```bash
sudo su # needs root access
cd /opt/
git clone --depth 1 --branch master https://gitlab.com/mikler/glaber.git
# docker folder is an appliance type, possible variants virtualbox,yandex-cloud-vm,vagrant,helm-chart etc
cd glaber/build/appliance/run/docker
./glaber.sh start
```

# Update of any type of appliance:

```bash
sudo su # needs root access
cd /opt/glaber
git pull
cd glaber/build/appliance/run/docker
./glaber.sh start
```