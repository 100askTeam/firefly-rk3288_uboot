#!/bin/bash
IMAGE_U=../../images

cd output/build/uboot-master

./make.sh loader

echo -e "\033[31m make idloader \033[0m"

./make.sh uboot
echo -e "\033[31m make uboot.img \033[0m"


./make.sh trust
echo -e "\033[31m make trust.img \033[0m"

cp  uboot.img  idbloader.img  trust.img  $IMAGE_U

cd $IMAGE_U

pwd

if [ -f "idbloader.img" -a -f "uboot.img" -a -f "trust.img" ]
then
        echo -e "\033[32m file is found \033[0m"
        ls -lah *.img
fi