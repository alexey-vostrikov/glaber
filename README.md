# Please go to [Glaber docs](https://docs.glaber.io)

# glaber quick start
```bash
 # needs root access
sudo su
cd /opt/
git clone --depth 1 --branch master https://gitlab.com/mikler/glaber.git
# docker folder is an appliance type, 
# possible variants virtualbox,yandex-cloud-vm,vagrant,helm-chart etc
cd glaber/build/appliance/run/docker
# result of command is ready to use 
# glaber application with all needed components
# deployed in docker-compose
./glaber.sh start
```

Available options to install glaber:
- `./glaber.sh start stable`,   - install latest stable version
- `./glaber.sh start latest`,   - install latest version
- `./glaber.sh start 3.0.50`,   - install a certain glaber version
- `./glaber.sh upgrade 3.0.50`, - upgrade to the certain glaber version
- `./glaber.sh upgrade`       , - upgrade to the latest glaber version

By default, glaber install a latest stable version
