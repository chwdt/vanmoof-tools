all: unpack

unpack: unpack.o

unpack.o: unpack.c pack.h

clean:
	rm -f unpack.o unpack
