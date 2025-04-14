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
The firmware image starts with a magic 0x55 0xaa 0x55 0xaa, followed by 8 (yet) unknown bytes, followed by 4 bytes giving the length of the image. One of the unknowns might be a CRC over the image. 

## Fun facts

The magic numbers used by VanMoof have been used previously by others, this gives some funny output when using the unix `file` command:

```
$ file packfile.bin
packfile.bin: Quake I or II world or extension, 340 entries

$ file mainware.bin
mainware.bin: BIOS (ia32) ROM Ext. (85*512)
```
