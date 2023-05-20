CC = gcc
ARCH ?= $(shell uname -m)

ifeq ($(ARCH),aarch64)
CFLAGS = -Os -pipe -march=armv8-a+crc -mtune=cortex-a72
else
CFLAGS = -Os -pipe -march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp
endif

DEPS =
objects = pcpfetch.o pcpget.o
LIBS=-lcurl

all: pcpfetch pcpget

$(objects): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

pcpget: pcpget.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
	strip --strip-unneeded $@

pcpfetch: pcpfetch.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
	strip --strip-unneeded $@

.PHONY: clean

clean:
	rm -f *.o pcpfetch pcpget
