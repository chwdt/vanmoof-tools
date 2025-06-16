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

This tool patches the VanMoof bleware file, so the command `rtos-statistics` is replaced with a command `dump`. Using `dump keys` inside the bledebug console will show the stored keys. These keys are two factory default keys used during bringup of the bike in factory, your API key, and the VanMoof manufacturer key. The latter is used to encrypt firmware images (when sending updates to the APP).

You can update the patched bleware on the bike by creating a pack containing this bleware and using the command `pack-upload` inside the bledebug console. You need to send the created pack using ymodem.

Output looks like:

```
> dump keys
Key 0x00: UKEY 52XXXXXXXXXXXXXXXXXXXXXXXXXXXX2f 00000001 000003fe CRC 81XXXX92
Key 0x01: UKEY 98XXXXXXXXXXXXXXXXXXXXXXXXXXXX02 00000002 000001f4 CRC 72XXXX94
Key 0x7c: M-ID 00000000000000000000000000000000 00000008 00000000 CRC 7062da7e
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

There is a special command which shows the values of `CCFG_TI_OPTIONS` and `CCFG_TAP_DAP_*`, this command will patch to boot loader of the BLE chip to enable the JTAG debug port of the BLE chip for further debugging. The first time the command is called, it will output values like:

```
> dump ccfg
CCFG_TI_OPTIONS: 0xffffff00
CCFG_TAP_DAP_0:  0xff000000
CCFG_TAP_DAP_1:  0xff000000
JTAGCFG:         0x00000000
```

When these values are found, the last sector of flash (including the CCFG) is copied down from 0x56000 to 0x46000 while patching the CCFG values and the version string, and then copying the sector back to 0x56000. After a `reset` these new CCFG values take effect and the JTAG port is enabled:

```
> dump ccfg
CCFG_TI_OPTIONS: 0xffffffc5
CCFG_TAP_DAP_0:  0xffc5c5c5
CCFG_TAP_DAP_1:  0xffc5c5c5
JTAGCFG:         0x00000003
```

The boot loader is not touched again once these CCFG values are present.

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

## Offsets in BLE controller internal ROM:

```
10000000: TI ROM boot loader
10007000: TI ROM ble5stack
1002b400: TI ROM tirtos7

50001000: FCFG1
500012e8: FCFG1::MAC_BLE_0
500012ec: FCFG1::MAC_BLE_1

50003000: CCFG (mirrored from FLASH 56000)
```

## Offsets in BLE controller internal FLASH:

```
00000000: bleware.bin

00056000: boot loader
00057f38: boot loader version: "BVERApr 23 2020"
00057f48: boot loader version: "14:10:12"

00057fa8: CCFG (Customer configuration) Mirrored at CCFG 50004fa8
00057fec: CCFG::IMAGE_VALID_CONF -> 56000 (boot loader entry)
```

## Offsets in BLE controller internal SRAM:

```
2000a3dc: Memory location of UKEY, when used in BLE protocol (1.4.1)
2000a3fc: Memory location of MKEY, when used in BLE protocol (1.4.1)

2000cec8: Memory location of UKEY, when used in BLE protocol (2.4.1)
2000cee8: Memory location of MKEY, when used in BLE protocol (2.4.1)
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
- BLE MCU control: UART5 (SSP, [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol) encoded packets)
- BLE MCU debug: UART8 (passthru in bledebug mode)
- GSM uBlox G350: USART2 (AT commands, passthru in gsmdebug mode)
- Battery MCU: UART4 ([Modbus](https://en.wikipedia.org/wiki/Modbus))
- Shifter MCU: USART3 ([Modbus](https://en.wikipedia.org/wiki/Modbus))
- Motor MCU: USART6 (SSP, [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol) encoded packets)
- Main MCU alternative debug: USART1  (unknown how this is accessed, maybe through GSM?)

The [SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol) encoded packets contain one byte sender address, a command byte, a sequence byte,
a little endian 16 bit word which is an offset or a function code, a 16 bit data length word, data, and a 16 bit CRC, which is the same as the [Modbus](https://en.wikipedia.org/wiki/Modbus) CRC.

The command byte is 06 for READ, 07 for WRITE, and 05 for ACK packets. The CRC is calculated over the packet without the C0 framing characters.

```
C0 01 06 56 1A 01 33 F8 C0                              MCU -> BLE READ req 56: 0x011a
C0 02 05 56 53 6E C0                                    BLE -> MCU ACK 56

C0 02 07 79 1A 01 06 00 F8 8A 5E XX XX XX YY YY C0      BLE -> MCU WRITE req 79: 0x011a: 0x0006 bytes: 0xF8 8A 5E XX XX XX
C0 01 05 79 E2 B2 C0                                    MCU -> BLE ACK 79
```

The MCU handles both packet streams from the BLE and the Motor MCU inside the same packet handler, so the offsets/function codes of the BLE and the Motor need to be disjunct.

## Debug console

The `Login:` prompt on the debug console knows two passwords, one fixed password hardcoded in the firmware `vEVjGF!paYsM2EBV8SoDT8*T0eB&#T6xevaoxCaO` and one password containing the last three bytes of the bikes MAC address followed by the word "DeBug", as output by `printf("%02X%02X%02XDeBug", MAC[3], MAC[4], MAC[5])`.

The debug console has a `help` command. The BLE chip and the GSM modem can also be accessed from the debug port, by using the commands `bledebug` and `gsmdebug` respectively.

```
Login: ***********
Welcome to ES3

help
Available commands:
help              This tekst
reboot            reboot CPU
login             Login shell
logout            Logout shell
ver               Software version
distance          Manual set dst
gear              set gear
region            Region 0..3
model             model
blereset          hard reset BLE
bledebug          redirect uart8
show              Parameters
motorupdate       Update F2806 CPU
vollow            Audio volume
volmid            Audio volume
volhigh           Audio volume
speed             override speed
loop              main loop time
shipping          Shipping mode
factory-shipping   Factory shipping mode (ignores BMS)
logprn            Print log
logclr            Clear log 6
logapp            1/ 0
powerchange       1/ 0
factory           Load factory defaults
battery           Show battery
batware           Battery update
batboot           BatteryBL update
batreset          Battery reset
shiftware         Battery update
shifterstatus     Show shifter
shiftdebug        Show Modbus
shiftresetcounter   Reset shift counter
motorstatus
gsminfo           Info from Ublox
gsmstart          start GSM function
gsmdebug          redirect uart2
bmsdebug          Show Modbus
sound             sample,volume,times
adc               read adc
bwritereg         Modbus Bat write register
bwritedata        Modbus Bat write data
breadreg          Modbus Bat read register
swritereg         Modbus Shift write register
swritedata        Modbus Shift write data
sreadreg          Modbus Shift read register
stc               read lipo monitor
stcreset
setoad            test
setgear           save muco shifter
soc               overrule soc
customsoc         sound soc
hwrev             hardware revision
error             set errorcode

ver
ES3.0 Main  1.09.03 (10:30:52 Apr 30 2025)
ES3 boot    1.9
Motorware   S.0.00.22
BMSWare     BL:007 FW:1.17 RSOC:100 Cycles:42 HW:3.10 ESN:XXXXXXXXXXXXXX
Shifterware 0.237 stored: 0.237
BLEWare     1.4.01
GSMWare     08.90
CMD_BLE_MAC F8:8A:5E:XX:XX:XX
```

The BLE console also has a `help` command and one can return to the MCU console with the command `exit`.

```
Login: ***********
Welcome to ES3
bledebug
Connect to UART8

> help
The following commands are available:

    firmware-update                   - update a new image of firmware to the external flash
    extflash-verify                   - verify the current flashchip
    log-count                         - get log-count statistic
    log-dump <start-index> <n>        - print <n> blocks starting at address <start-index>
    log-flush                         - flush all log-entries
    log-inject <n>                    - Create <n> fake-logs
    audio-play <index>                - play audio bound to the specified index
    audio-stop                        - stop playing the current audio file
    audio-dump                        - dump all audio files in external memory
    audio-upload <index>              - upload audio binary using Y-Modem at the address linked to the specified index
    audio-volume-set-all <level>      - set audio level of all audio-clips (0-3)
    pack-upload                       - upload a PACK file by Y-Modem
    pack-list                         - list the contents of a PACK file
    pack-delete                       - delete a PACK file
    pack-process                      - process pack files in external flash memory
    ble-info                          - dump current BLE connection info / statistics
    ble-disconnect                    - force a disconnect of all connected devices
    ble-erase-all-bonds               - erase all bonds
    shutdown                          - shutdown the system
    rtos-statistics                   - dump memory stats every 500ms
    rtos-nvm-compact                  - Compact the non-volatile storage
    dump                              - dump keys/memory/extflash
    info/ver                          - show basic firmware info
    exit                              - exit from shell
    help                              - show all monitor commands

> info
BLE MAC Address: "f8:8a:5e:xx:xx:xx"

Device name ................ : ES3-F88A5EXXXXXX
Firmware version ........... : 1.04.01
Compile date / time ........ : May 12 2025 / 09:03:35
BIM firmware version ....... : 1.00.00
BIM compile date / time .... : Apr 23 2020 / 14:10:12
reset type ................. : pin reset
systick .................... : -117259002

> exit
```

The GSM console talks extended ublox [AT](https://en.wikipedia.org/wiki/Hayes_AT_command_set) commands. It can be exited with the sequence `<ESC>[14~`.

```
Login: ***********
Welcome to ES3
gsmdebug
Modem powering on..

ATI
SARA-G350-02S-01

OK
AT+UGSRV?
+UGSRV: "ublox1.vanmoof.com","ublox1.vanmoof.com","PBNjh0V46Eev8CcfS4LPJg",14,4,1,65,0,15

OK
AT+UPSDA=0,3
OK
AT+UPSND=0,8
+UPSND: 0,8,1

OK
AT+ULOCIND=1
OK
AT+ULOC=2,2,1,180,1,10
OK

+UULOCIND: 0,0

+UULOCIND: 1,0

+UULOCIND: 2,0

+UULOCIND: 3,0

+UULOC: 16/05/2025,08:27:54.000,<latitude>,<longitude>,0,601,0,0,0,2,0,0,0
```

## External resources

- [Wiring harness](https://www.moofrepair.nl/wiring-harness/)
- [Debug console](https://www.reddit.com/r/vanmoofbicycle/comments/17744l7/comment/k4z0wyi/?utm_source=share&utm_medium=web3x&utm_name=web3xcss&utm_term=1&utm_content=share_button)
- [Hardware info](https://github.com/ciborg971/VanmoofX3RE/tree/master/OG)
- [Hardware info](https://github.com/dtngx/VMBattery)
- [pymoof](https://github.com/quantsini/pymoof)


## Special thanks!

- [Tobias](https://github.com/Knight1)
- [Quinten](https://github.com/quintenadema)


## Help needed

If you want to support these efforts, please contact me with your offer. I could really use a smart cartridge to hook up a debugger to the TI bluetooth controller and debug some ideas I have...

I see a crash of the controller when sending firmware update packets > 256 bytes, so there might be the chance to use this as an exploit to read out the MKEY over bluetooth. We need the MKEY to be able to:
- decode the update packages received from Vanmoof
- send our own update package (patched with features like offroad) to the bike

## Fun facts

The magic numbers used by VanMoof have been used previously by others, this gives some funny output when using the unix `file` command:

```
$ file packfile.bin
packfile.bin: Quake I or II world or extension, 340 entries

$ file mainware.bin
mainware.bin: BIOS (ia32) ROM Ext. (85*512)
```
