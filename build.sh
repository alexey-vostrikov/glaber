cd build/appliance/build/virtualbox
cd cloud-init
make-boot-image.sh
cd ..
packer build
packer init .
packer validate .
packer build -color=false -timestamp-ui -warn-on-undeclared-var .