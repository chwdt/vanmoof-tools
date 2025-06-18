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

ble-patch.o: ble-patch.c ware.h keys1.hex keys2.hex
	$(eval SYSTEM_PUTCHAR1=$(shell nm keys1 | grep System_putchar | cut -d' ' -f1))
	$(eval SYSTEM_PUTCHAR2=$(shell nm keys2 | grep System_putchar | cut -d' ' -f1))
	$(CC) $(CFLAGS) -DSYSTEM_PUTCHAR1=0x$(SYSTEM_PUTCHAR1) -DSYSTEM_PUTCHAR2=0x$(SYSTEM_PUTCHAR2) -o $@ -c $<

patch-dump.o: patch.c ware.h dump.hex
	$(CC) $(CFLAGS) -DDUMP -o $@ -c $<

dump.hex: dump.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

dump.bin: dump.o
	arm-none-eabi-objcopy -O binary $< $@

dump.o: dump.c
	arm-none-eabi-gcc $(ARM_FLAGS) -c $<

keys1.hex: keys1.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

keys2.hex: keys2.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

keys1.bin: keys1
	arm-none-eabi-objcopy -O binary $< $@

keys2.bin: keys2
	arm-none-eabi-objcopy -O binary $< $@

keys1: keys1.o keys1.ld
	arm-none-eabi-ld -T keys1.ld -e dump -o $@ $<

keys2: keys2.o keys2.ld
	arm-none-eabi-ld -T keys2.ld -e dump -o $@ $<

keys1.o: keys.c
	arm-none-eabi-gcc $(ARM_FLAGS) -DVERSION_1_4_1 -c $< -o $@

keys2.o: keys.c
	arm-none-eabi-gcc $(ARM_FLAGS) -DVERSION_2_4_1 -c $< -o $@

clean:
	rm -f *.o unpack crc32 patch patch-dump
