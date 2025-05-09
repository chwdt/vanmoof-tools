CC = gcc
LDLIBS = -lz

CFLAGS = -O1 -g

ARM_FLAGS = -Os -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-ffreestanding -fPIC -fno-toplevel-reorder

all: pack unpack crc32 patch patch-dump ble-patch

pack: pack.o
unpack: unpack.o
crc32: crc32.o
patch: patch.o
patch-dump: patch-dump.o
ble-patch: ble-patch.o

pack.o: pack.c pack.h
unpack.o: unpack.c pack.h
crc32.o: crc32.c ware.h
patch.o: patch.c ware.h
ble-patch.o: ble-patch.c ware.h keys.hex

patch-dump.o: patch.c ware.h dump.hex
	$(CC) $(CFLAGS) -DDUMP -o $@ -c $<

dump.hex: dump.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

dump.bin: dump.o
	arm-none-eabi-objcopy -O binary $< $@

dump.o: dump.c
	arm-none-eabi-gcc $(ARM_FLAGS) -c $<

keys.hex: keys.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

keys.bin: keys
	arm-none-eabi-objcopy -O binary $< $@

keys: keys.o keys.ld
	arm-none-eabi-ld -T keys.ld -e dump -o $@ $<

keys.o: keys.c
	arm-none-eabi-gcc $(ARM_FLAGS) -c $<

clean:
	rm -f *.o unpack crc32 patch patch-dump
