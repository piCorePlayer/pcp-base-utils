#!/bin/sh

make

# pcpget.c
/home/paul/git/picoreplayer-picoreplayer/build/kernel/x-tools/gcc-gcc10-aarch64-linux-gnu-_x64/bin/aarch64-linux-gnu-gcc -Os -s pcpget.c -o pcpget.aarch64 -lcurl
/home/paul/git/picoreplayer-picoreplayer/build/kernel/x-tools/gcc-gcc10-armv6-linux-gnueabihf_x64/bin/arm-bcm2708hf-linux-gnueabihf-gcc -Os -s pcpget.c -o pcpget.armhf -lcurl

# istty.c
/home/paul/git/picoreplayer-picoreplayer/build/kernel/x-tools/gcc-gcc10-aarch64-linux-gnu-_x64/bin/aarch64-linux-gnu-gcc -Os -s istty.c -o istty.aarch64
/home/paul/git/picoreplayer-picoreplayer/build/kernel/x-tools/gcc-gcc10-armv6-linux-gnueabihf_x64/bin/arm-bcm2708hf-linux-gnueabihf-gcc -Os -s istty.c -o istty.armhf

# powerbtn.c pi5 only
/home/paul/git/picoreplayer-picoreplayer/build/kernel/x-tools/gcc-gcc10-aarch64-linux-gnu-_x64/bin/aarch64-linux-gnu-gcc -Os -s powerbtn.c -o powerbtn.aarch64 -lpthread
