all: unpack crc32 patch

unpack: unpack.o
crc32: crc32.o
patch: patch.o

unpack.o: unpack.c pack.h
crc32.o: crc32.c ware.h
patch.o: patch.c ware.h

clean:
	rm -f *.o unpack crc32 patch
