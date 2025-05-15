# vanmoof-tools

Tools to support VanMoof S3/X3 reverse engineering efforts

## unpack

usage: `unpack <packfile>`

This tool extracts the contents of a VanMoof update file, also known as PACK file. A PACK file starts with a header containing the magic "PACK", an offset to a directory structure and the length of the directory structure. The directory structure (at the end of the file) contains one or more entries containing a filename, an offset, and the length of the data. See pack.h for details of these structures.

The tool will overwrite any file present in the current directory if this is contained in the PACK file. Run this in a separate directory to be shure not to loose any data.

## Known firmware images:

- mainware.bin (MCU: ST STM32F413VGT6)
- bleware.bin (MCU: TI CC2642R1F)
- motorware.bin (MCU: TI TMS320F28054F)
- shifterware.bin (MCU: MindMotion MM32F031F6U6)
- batteryware.bin (MCU: ST STM32L072CZT6)
- bmsboot.bin (MCU: see batteryware.bin)

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

## ble-patch

usage: `ble-patch <bleware>`

This tool patches the VanMoof bleware file, so the command `reset` is replaced with a command `dump`. Using `dump keys` inside the bledebug console will show the stored keys. These keys are two factory default keys used during bringup of the bike in factory, your API key, and the VanMoof manufacturer key. The latter is used to encrypt firmware images (when sending updates to the APP).

You can update the patched bleware on the bike by creating a pack containing this bleware and using the command `pack-upload` inside the bledebug console. You need to send the created pack using ymodem.

Output looks like:

```
> dump keys
Key 0x00: UKEY 52XXXXXXXXXXXXXXXXXXXXXXXXXXXX2f 00000001 000003fe CRC 81XXXX92
Key 0x7d: UKEY 5f5f5f5f5f4f574e45525f5045524d53 00000000 ffffffff CRC 4f25ee68
Key 0x7e: MKEY 710b2ea0dc8568b7b5e5ec0b8a39dae9 00000000 00000000 CRC a69429b6
Key 0x7f: MKEY 46383841XXXXXXXXXXXXXXXX4d4f4f46 00000000 ffffffff CRC 4aXXXX7e
```

The keys 0x7d and 0x7f seem to be the factory bring-up keys, translated to ASCII they read:

```
Key 0x7d: UKEY _____OWNER_PERMS 00000000 ffffffff CRC 4f25ee68
Key 0x7f: MKEY F88AXXXXXXXXMOOF 00000000 ffffffff CRC 4aXXXX7e
               ^- Bike MAC Address
```

This can also dump memory (i.e. ROM, internal FLASH, or external FLASH):

```
> dump mem 10000000 40000
10000000        00 20 00 11 b1 19 00 10   bf 20 00 10 c1 20 00 10       . ...... . ... ..
10000010        c3 20 00 10 c3 20 00 10   c3 20 00 10 00 00 00 00       . ... .. . ......
...

> dump extflash 5afa0 20
0005afa0        5f 5f 5f 5f 5f 4f 57 4e   45 52 5f 50 45 52 4d 53       _____OWN ER_PERMS
0005afb0        00 00 00 00 ff ff ff ff   55 4b 45 59 68 ee 25 4f       ........ UKEYh.%O
```

## patch-dump

This tool patches a modern VanMoof mainware as `patch` above, but adds a function to dump FLASH or memory to the console. This function is patched into the `help` command and will output FLASH or memory as hexdump.  Use as `help <addr> <count>`.

An older version would output the whole FLASH as S-Records, the source is still provided in the repo, edit the Makefile if you want to use this function.

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

## update.py

A simple cheasy update tool to send firmware packed with `pack` to the bike. This needs [pymoof](https://github.com/quantsini/pymoof) to run.

You need to insert your bikes API key and manufacturer key before using the tool.

Use as a reference for the firmware update over BLE.

## read_logs.py

A simple cheasy tool to read the internal debug logs from the bike using BLE. This needs [pymoof](https://github.com/quantsini/pymoof) to run.

You need to insert your bikes API key before using the tool.

Use as a reference howto read logs over BLE.

## Internal communication

Main MCU communicates with the other MCUs via:

- Main MCU debug: UART7 (port behind rear light)
- BLE MCU control: UART5 (SSP, SLIP encoded packets)
- BLE MCU debug: UART8 (passthru in bledebug mode)
- GSM uBlox G350: USART2 (AT commands, passthru in gsmdebug mode)
- Battery MCU: UART4 (Modbus)
- Shifter MCU: USART3 (Modbus)
- Motor MCU: USART6 (SSP, SLIP encoded packets)
- Main MCU alternative debug: USART1  (unknown how this is accessed, maybe through GSM?)

## External resources

- [Wiring harness](https://www.moofrepair.nl/wiring-harness/)
- [Debug console](https://www.reddit.com/r/vanmoofbicycle/comments/17744l7/comment/k4z0wyi/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button) Login: "%02X%02X%02xDeBug", BLE_MAC[3], BLE_MAC[4], BLE_MAC[5]

## Fun facts

The magic numbers used by VanMoof have been used previously by others, this gives some funny output when using the unix `file` command:

```
$ file packfile.bin
packfile.bin: Quake I or II world or extension, 340 entries

$ file mainware.bin
mainware.bin: BIOS (ia32) ROM Ext. (85*512)
```
