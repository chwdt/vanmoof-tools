all: unpack crc32

unpack: unpack.o
crc32: crc32.o

unpack.o: unpack.c pack.h
crc32.o: crc32.c ware.h

clean:
	rm -f *.o unpack crc32
