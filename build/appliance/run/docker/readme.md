# Glaber docker appliance

This repo can be used as starting point to **migrate from your current zabbix installation to glaber**.

The main reasons for this may be:

- The performance of zabbix proxies is constantly not enough, you have to add new ones.
- Restarting the monitoring system takes a long time.
- You are faced with an architecturally unsolvable problem where increasing the performance of the zabbix server, zabbix proxy server, database tuning does not improve the performance of the monitoring system as a whole.


# Hardware requirements:

- CPU:  1 cpu like `cpu `xeon e5` performance enough.
- RAM:  8G RAM.
- DISK: SSD preferred. Depend on your zabbix setup.
This setup is enough for zabbix setup with 20k nvps performance.

# Software requirements:

Linux operation system with internet access and software installed:
- docker >=20.10.22 
- docker-compose >=1.29.2

## Components:

- glaber server
- glaber nginx
- clickhouse as history storage backend
- mysql as the main database backend

# Migration guide (under development):

- Feel free to ask for a [solution](https://glaber.ru/pricing.html) according to your infrastructure
- As glaber and zabbix have backward compatibility you can try to replace one zabbix component with glaber component one by one (for example change zabbix proxy to glaber proxy)
- You can install glaber in parallel to zabbix and redirect all zabbix agents and zabbix proxy to the glaber server


# Installation:

```bash
cd /opt/
git clone https://gitlab.com/mikler/glaber.git
cd glaber/build/appliance/run/docker
./glaber.sh start (needs root access)
```

This action creates passwords for clickhouse and mysql and docker-compose up all glaber components.

This action needs sudo access.

# After success  build and start:
- Zabbix web Admin password located in `.zbxweb` and displayed to stdout
- All passwords variables are updated in `.env` file

# Default credentials:

- Zabbix web. http://127.0.0.1 - Admin,`<random generated>`
- Mysql server. Db,User,Pass   - zabbix,zabbix,`<random generated>`
- Clickhouse. Db,User,Pass     - zabbix,defaultuser,`<random generated>`


# Default ENV variables:

All variables, their default values and their default behavior are described in `.env` file

# Usage:
```bash
./glaber.sh <action>

./glaber.sh build    - Build docker images
./glaber.sh start    - Build docker images and start glaber
./glaber.sh stop     - Stop glaber containers
./glaber.sh recreate - Completely remove glaber and start it again
./glaber.sh remove   - Completely remove glaber installation
./glaber.sh diag     - Collect glaber start logs and some base system info to the file
```
