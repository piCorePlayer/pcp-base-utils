CC = gcc
CFLAGS = -Os -pipe -march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp
DEPS =
OBJ = pcpfetch.o

LIBS=-lcurl

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

pcpfetch: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o pcpfetch
