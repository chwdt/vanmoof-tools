CC = gcc
LDLIBS = -lz -lcrypto

CFLAGS = -O1 -g

# On macOS, Homebrew's OpenSSL is keg-only: its headers and libraries are not on
# the default search paths, so a plain `-lcrypto` fails to link. Point the
# compiler and linker at the Homebrew prefix. No-op on Linux, where libssl-dev
# installs into the default paths.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null)
  ifneq ($(OPENSSL_PREFIX),)
    CFLAGS  += -I$(OPENSSL_PREFIX)/include
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib
  endif
endif

# ARM bare-metal cross toolchain used by the firmware targets (patch-dump,
# ble-patch, backupcode). Override CROSS to use a differently-named toolchain.
CROSS       ?= arm-none-eabi-
ARM_CC      = $(CROSS)gcc
ARM_LD      = $(CROSS)ld
ARM_OBJCOPY = $(CROSS)objcopy

ARM_FLAGS = -Os -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-ffreestanding -fno-toplevel-reorder

# backupcode: the three-digit handlebar unlock code to store (decimal 0..999).
BACKUP_CODE ?= 123
# post-build envelope crc/length stamper: reuse crc32's own ware_crc (crc32 -w).
STAMP ?= ./crc32 -w

all: pack unpack crc32 patch patch-dump ble-patch

pack: pack.o
unpack: unpack.o
crc32: crc32.o
patch: patch.o
patch-dump: patch-dump.o
ble-patch: ble-patch.o

pack.o: pack.c pack.h ware.h endian_compat.h
unpack.o: unpack.c pack.h ware.h endian_compat.h
crc32.o: crc32.c ware.h endian_compat.h
patch.o: patch.c ware.h endian_compat.h

ble-patch.o: ble-patch.c ware.h endian_compat.h keys1.hex keys2.hex
	$(eval SYSTEM_PUTCHAR1=$(shell $(CROSS)nm keys1 | grep System_putchar | cut -d' ' -f1))
	$(eval SYSTEM_PUTCHAR2=$(shell $(CROSS)nm keys2 | grep System_putchar | cut -d' ' -f1))
	$(CC) $(CFLAGS) -DSYSTEM_PUTCHAR1=0x$(SYSTEM_PUTCHAR1) -DSYSTEM_PUTCHAR2=0x$(SYSTEM_PUTCHAR2) -o $@ -c $<

patch-dump.o: patch.c ware.h dump.hex
	$(CC) $(CFLAGS) -DDUMP -o $@ -c $<

dump.hex: dump.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

dump.bin: dump.o
	$(ARM_OBJCOPY) -O binary $< $@

dump.o: dump.c | check-arm
	$(ARM_CC) $(ARM_FLAGS) -fPIC -c $<

keys1.hex: keys1.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

keys2.hex: keys2.bin
	od -v -An -tx2 $< | sed -e 's/\([0-9a-f][0-9a-f][0-9a-f][0-9a-f]\)/0x\1,/g' >$@

keys1.bin: keys1
	$(ARM_OBJCOPY) -O binary $< $@

keys2.bin: keys2
	$(ARM_OBJCOPY) -O binary $< $@

keys1: keys1.o keys1.ld
	$(ARM_LD) -T keys1.ld -e dump -o $@ $<

keys2: keys2.o keys2.ld
	$(ARM_LD) -T keys2.ld -e dump -o $@ $<

keys1.o: keys.c | check-arm
	$(ARM_CC) $(ARM_FLAGS) -DVERSION_1_4_1 -c $< -o $@

keys2.o: keys.c | check-arm
	$(ARM_CC) $(ARM_FLAGS) -DVERSION_2_4_1 -c $< -o $@

# One-shot mainware-slot payload that writes the owner backup code into config
# flash (BLE-dead recovery). Build: `make backupcode.bin BACKUP_CODE=123`, then
# upload backupcode.bin into the mainware slot over Y-modem.
backupcode.o: backupcode.c | check-arm
	$(ARM_CC) $(ARM_FLAGS) -DBACKUP_CODE=$(BACKUP_CODE) -c $< -o $@

backupcode.elf: backupcode.o backupcode.ld
	$(ARM_LD) -T backupcode.ld -o $@ backupcode.o

backupcode.bin: backupcode.elf crc32
	$(ARM_OBJCOPY) -O binary $< $@
	$(STAMP) $@

# Fail early with an actionable message instead of a raw "command not found"
# when the ARM cross toolchain is missing (only needed by the firmware targets).
check-arm:
	@command -v $(ARM_CC) >/dev/null 2>&1 || { echo "error: $(ARM_CC) not found - needed for the firmware targets (patch-dump, ble-patch, backupcode)."; echo "  macOS:         brew install --cask gcc-arm-embedded"; echo "  Debian/Ubuntu: apt install gcc-arm-none-eabi binutils-arm-none-eabi"; exit 1; }

.PHONY: all clean check-arm

clean:
	rm -f *.o unpack crc32 patch patch-dump backupcode.elf backupcode.bin
