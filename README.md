# vanmoof-tools

Tools to support VanMoof S3/X3 reverse engineering efforts

## unpack

usage: `unpack <packfile>`

This tool extracts the contents of a VanMoof update file, also known as PACK file. A PACK file starts with a header containing the magic "PACK", an offset to a directory structure and the length of the directory structure. The directory structure (at the end of the file) contains one or more entries containing a filename, an offset, and the length of the data. See pack.h for details of these structures.

The tool will overwrite any file present in the current directory if this is contained in the PACK file. Run this in a separate directory to be shure not to loose any data.

## Known firmware images:

- mainware.bin
- motorware.bin
- shifterware.bin
- batteryware.bin
- bleware.bin
- bmsboot.bin

## Some observations about firmware images:

Most components of the bike contain a split boot loader and firmware image, the Bluetooth firmware might differ from this scheme.

The boot loader starts with the typical ARM vector table and is 20Kb (0x5000 bytes) long.
The last 8 bytes of the boot loader binary contain:
- 4 bytes version encoded as ASCII characters
- 4 bytes CRC32.

The CRC is calculated using the STM32 CRC algorithm over the whole boot loader data without the last 4 bytes

The firmware image starts with a magic 0xaa55aa55, followed by these header items:
- 4 bytes version info
- 4 bytes CRC32
- 4 bytes length
- 12 bytes date in ASCII
- 12 bytes time in ASCII

All 4 byte values are encoded little endian

The CRC is calculated using the STM32 CRC algorithm over the whole image data with the header CRC and length fields both set to 0xffffffff.

## crc32

usage: `crc32 <warefile>`

This tool calculates and verifies the CRC of both boot loader and firmware images.

## patch

usage: `patch <mainware>`

This tool patches a modern VanMoof mainware file, so the region OFFROAD is not reset to region US during boot. The power assistance level can be configured to 5 again. When cycling through power assistance levels using the handle bar, the bike will cycle through power level 5 as well if region is OFFROAD.

The file given on the command line is overwritten with the patched version of the file, so please make a backup of your mainware before using the tool.

Currently the tool only works for mainware version 1.9.3.


## patch-dump

This tool patches a modern VanMoof mainware as `patch` above, but adds a function to dump the whole FLASH to the console. This function is patched into the `help` command and will output the FLASH as S-Records.

Capture the terminal output to a logfile and clip out the S-Records to a file `vanmoof.srec`. To convert this dump to the different binaries used inside the bike, use these shell commands:

```
objcopy -I srec -O binary vanmoof.srec vanmoof.bin
dd if=vanmoof.bin of=muco-boot.bin bs=4096 count=8
dd if=vanmoof.bin of=vanmoof-config-a.bin bs=4096 skip=8 count=4
dd if=vanmoof.bin of=vanmoof-config-b.bin bs=4096 skip=12 count=4
dd if=vanmoof.bin of=shifterware.bin bs=4096 skip=16 count=16
dd if=vanmoof.bin of=mainware.bin bs=4096 skip=32 count=64
dd if=vanmoof.bin of=shadowware.bin bs=4096 skip=96 count=64
dd if=vanmoof.bin of=motorware.bin bs=4096 skip=160 count=32
dd if=vanmoof.bin of=batteryware.bin bs=4096 skip=192 count=32
dd if=vanmoof.bin of=bmsboot.bin bs=4096 skip=224 count=32
```

## Offsets in smart controller internal flash:

```
0x08000000: stm32 boot loader
0x08008000: bike config A
0x0800c000: bike config B
0x08010000: shifterware image
0x08020000: mainware image
0x08060000: shadow image
0x080a0000: motorware image
0x080c0000: batteryware image
0x080e0000: bmsboot image
```

## Offsets in smart controller internal SRAM:

```
0x20000a00: Bike configuration
   + 0x109: Region

   + 0x310: EEPROM copy 0x3c bytes
   + 0x310: Alarm state
   + 0x311: Play lock sound
   + 0x312: remote locked
   + 0x313: Logging APP/Serial
   + 0x314: Shipping
   + 0x316: Power level + high bit?
   + 0x317: Alarm enable/disable
   + 0x318: Horn file
   + 0x31c: km * 10
   + 0x334: Shifter tries
   + 0x336: Shifter version
   + 0x344: Wake counter

   + 0x3d1: Power level (init from 0x316 & 0x7f)
   + 0x3d2: Power level (copy, init from 0x316 & 0x7f)
   + 0x3d3: Power level high bit? (init from 0x316 >> 7)
```

## Fun facts

The magic numbers used by VanMoof have been used previously by others, this gives some funny output when using the unix `file` command:

```
$ file packfile.bin
packfile.bin: Quake I or II world or extension, 340 entries

$ file mainware.bin
mainware.bin: BIOS (ia32) ROM Ext. (85*512)
```
