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

## Offsets in smart controller internal flash:

0x08000000: stm32 boot loader
0x08010000: shifterware image
0x08020000: mainware image
0x08060000: shadow image
0x080a0000: motorware image
0x080c0000: batteryware image
0x080e0000: bmsboot image

## Fun facts

The magic numbers used by VanMoof have been used previously by others, this gives some funny output when using the unix `file` command:

```
$ file packfile.bin
packfile.bin: Quake I or II world or extension, 340 entries

$ file mainware.bin
mainware.bin: BIOS (ia32) ROM Ext. (85*512)
```
