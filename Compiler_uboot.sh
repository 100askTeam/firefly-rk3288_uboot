#!/bin/bash
CPU=$(cat /proc/cpuinfo |grep 'processor' |wc -l)
#GET=$(dirname $(readlink -f 'pwd'))
#export PATH=$PATH:${GET%uboot2016.06}/ToolChain/gcc-linaro-6.2.1-2016.11-x86_64_arm-linux-gnueabihf/bin/
echo $PATH
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

#step 1:clean kernel
#make distclean
#make clean 

#step 2:copy uboot config file 
make 100ask_firefly-rk3288_defconfig


#step 3:compiler uboot

make -j$CPU

echo -e "\033[31m ----------------------- \033[0m"
./make.sh loader

./make.sh uboot

./make.sh trust


ls -la uboot.img idbloader.img trust.img

echo -e "\033[31m ----------------------- \033[0m"

