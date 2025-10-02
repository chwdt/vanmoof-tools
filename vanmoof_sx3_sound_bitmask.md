# VanMoof Sound Bitmask Documentation

## Table of Contents
- [Overview](#overview)
- [Configuration Partition and SRAM Mapping](#configuration-partition-and-sram-mapping)
- [Sound Indexes](#sound-indexes)
- [Special Cases](#special-cases)
- [SX3 Behavior](#sx3-behavior)
- [Tables](#tables)

## Overview
The firmware for SX3 and SX4 models manages sounds using three **volume group bitmasks**.

-   Bitmask for volume group **Low**
-   Bitmask for volume group **Medium**    
-   Bitmask for volume group **High**

Each bitmask is 32 bits wide, where each **bit position corresponds to one sound**.  
If a bit is set (`1`), that sound is assigned to the respective volume group.  
A sound can belong to **exactly one group** at a time – if it is assigned to Medium, it will not be set in Low or High.

## Configuration Partition and SRAM Mapping

Both the bitmasks and the volume levels are stored on the **configuration partition** in flash:
- Primary: `0x08008000`
- Backup: `0x0800C000`

At startup, the primary partition is copied into **SRAM** at offsets after base address `0x20000A00`.  
All offsets documented here (e.g., `+0x0F4`) are relative to this SRAM base.

Example: `0x20000A00 + 0x0F4` → bitmask for sounds in group _Low_.

The **volume group bitmasks** are stored  at following offsets:
-   **+0x0F4** → Bitmask for volume group **Low**
-   **+0x0F8** → Bitmask for volume group **Medium**
-   **+0x0FC** → Bitmask for volume group **High**

The actual **volume levels** for each group are stored separately:
-   **+0x105** → Volume level for **Group Low** (Default: 20)
-   **+0x106** → Volume level for **Group Medium** (Default: 30)
-   **+0x107** → Volume level for **Group High** (Default: 38)

## Sound Indexes

Two different index identifiers exist:

-   **BLEware index** → used by BLEware commands, e.g.
    `audio-play 23`
    plays the sound at index `23` (hex 0x17, Mainware index `N`)
    
-   **Mainware index** → used by Mainware commands, e.g.
    `sound N` 
    plays the sound at index `N` (hex 0x17, BLEware index `23`).
    
Both indexes are shown in the tables for reference.

## Special Cases

-   **No Sound Entries** → Positions **0** and **31** have no assigned sound.
    -   In firmware ≤ 1.8.2, they are not set in any bitmask.
    -   In firmware ≥ 1.9.1, they are set in the **Medium** group. The reason is unknown; setting these bits has no audible effect.
        
-   **Loud Noise** → Position **26** has changed
    -   Firmware ≤ 1.8.2: correctly assigned to **High** (intended as a loud anti-theft locator sound for bike hunters).
    -   Firmware ≥ 1.9.1: moved to **Medium**, which contradicts its purpose as a loud sound.
        
-   **Firmware Update Success** → Position **25** not logical
    -   Present in both firmware generations.
    -   Currently mapped to **High**, but logically belongs to **Medium** like all other firmware-related sounds (Download, Update Failed, etc.).
        
-   **Flute (Horn sound)** → Position **11** exists in the sound table, but cannot be selected as the default bicycle bell tone in firmware.
    
-   **Find-My Sounds** → (e.g., **Pairing Loop**, **Pairing Success**, **Pairing Failed**, **Locate Sound**)
    -   Available only on bikes of the **2021 generation** equipped with **BLEware 2.4.1**.
    -   On older bikes with **BLEware 1.x** (2020 models), the corresponding index slots (**5, 8, 9, 27–30**) are present but completely **empty**.
    -   This suggests that VanMoof had already reserved these slots for Find-My on the 2020 generation, but did not enable them due to the missing Apple certification.

## SX3 Behavior

-   On SX3 models, **only Medium and High groups are effectively used**.
-   The **Low group bitmask is always `0x00000000`**, meaning no sounds are mapped to Low.
-   Volume levels (+0x105 … +0x107) still apply if Low were ever to be used.

## Tables
See below for the detailed sound mapping:

## SX3 – Firmware up to 1.8.2

| Bit | Sound Name              | BLE Idx | Main Idx | Low | Medium | High |
|-----|-------------------------|---------|----------|-----|--------|------|
|  0  | --- no sound ---        | 0       | 0        |     |        |      |
|  1  | Click                   | 1       | 1        |     | ✔️    |      |
|  2  | Error                   | 2       | 2        |     | ✔️    |      |
|  3  | Ding                    | 3       | 3        |     | ✔️    |      |
|  4  | Unlock Countdown        | 4       | 4        |     | ✔️    |      |
|  5  | Find-My Pairing Loop    | 5       | 5        |     | ✔️    |      |
|  6  | Enter PIN               | 6       | 6        |     | ✔️    |      |
|  7  | Reset                   | 7       | 7        |     | ✔️    |      |
|  8  | Find-My Pairing Success | 8       | 8        |     | ✔️    |      |
|  9  | Find-My Pairing Failed  | 9       | 9        |     | ✔️    |      |
| 10  | Horn: Submarine         | 10      | A        |     |       | ✔️   |
| 11  | Horn: Flute             | 11      | B        |     |       | ✔️   |
| 12  | Locked                  | 12      | C        |     | ✔️    |      |
| 13  | Unlock                  | 13      | D        |     | ✔️    |      |
| 14  | Alarm Stage 1           | 14      | E        |     |       | ✔️   |
| 15  | Alarm Stage 2           | 15      | F        |     |       | ✔️   |
| 16  | Startup                 | 16      | G        |     | ✔️    |      |
| 17  | Shutdown                | 17      | H        |     | ✔️    |      |
| 18  | Charging                | 18      | I        |     | ✔️    |      |
| 19  | Transfer Diag Log       | 19      | J        |     | ✔️    |      |
| 20  | Firmware Download       | 20      | K        |     | ✔️    |      |
| 21  | Firmware Update Success | 21      | L        |     | ✔️    |      |
| 22  | Horn: Ding Dong         | 22      | M        |     |       | ✔️   |
| 23  | Horn: Troot             | 23      | N        |     |       | ✔️   |
| 24  | Horn: Foghorn/Ping      | 24      | O        |     |       | ✔️   |
| 25  | Firmware Update Failed  | 25      | P        |     |       | ✔️   |
| 26  | Loud Noise              | 26      | Q        |     |       | ✔️   |
| 27  | Find-My Unpair          | 27      | R        |     | ✔️    |      |
| 28  | Find-My Disable         | 28      | S        |     | ✔️    |      |
| 29  | Find-My Enable          | 29      | T        |     | ✔️    |      |
| 30  | Find-My Locate Sound    | 30      | U        |     |       | ✔️   |
| 31  | --- no sound ---        | 31      | V        |     |       |      |

The default values result in following bitmasks:

    Volume Group Low: 0x00000000
    Volume Group Medium: 0x383F33FE
    Volume Group High: 0x47C0CC00

### SX3 – Firmware 1.9.1 and higher

| Bit | Sound Name              | BLE Idx | Main Idx | Low | Medium | High |
|-----|-------------------------|---------|----------|-----|--------|------|
|  0  | --- no sound ---        | 0       | 0        |     | ✔️    |      |
|  1  | Click                   | 1       | 1        |     | ✔️    |      |
|  2  | Error                   | 2       | 2        |     | ✔️    |      |
|  3  | Ding                    | 3       | 3        |     | ✔️    |      |
|  4  | Unlock Countdown        | 4       | 4        |     | ✔️    |      |
|  5  | Find-My Pairing Loop    | 5       | 5        |     | ✔️    |      |
|  6  | Enter PIN               | 6       | 6        |     | ✔️    |      |
|  7  | Reset                   | 7       | 7        |     | ✔️    |      |
|  8  | Find-My Pairing Success | 8       | 8        |     | ✔️    |      |
|  9  | Find-My Pairing Failed  | 9       | 9        |     | ✔️    |      |
| 10  | Horn: Submarine         | 10      | A        |     |       | ✔️   |
| 11  | Horn: Flute             | 11      | B        |     |       | ✔️   |
| 12  | Locked                  | 12      | C        |     | ✔️    |      |
| 13  | Unlock                  | 13      | D        |     | ✔️    |      |
| 14  | Alarm Stage 1           | 14      | E        |     |       | ✔️   |
| 15  | Alarm Stage 2           | 15      | F        |     |       | ✔️   |
| 16  | Startup                 | 16      | G        |     | ✔️    |      |
| 17  | Shutdown                | 17      | H        |     | ✔️    |      |
| 18  | Charging                | 18      | I        |     | ✔️    |      |
| 19  | Transfer Diag Log       | 19      | J        |     | ✔️    |      |
| 20  | Firmware Download       | 20      | K        |     | ✔️    |      |
| 21  | Firmware Update Success | 21      | L        |     | ✔️    |      |
| 22  | Horn: Ding Dong         | 22      | M        |     |       | ✔️   |
| 23  | Horn: Troot             | 23      | N        |     |       | ✔️   |
| 24  | Horn: Foghorn/Ping      | 24      | O        |     |       | ✔️   |
| 25  | Firmware Update Failed  | 25      | P        |     |       | ✔️   |
| 26  | Loud Noise              | 26      | Q        |     | ✔️    |      |
| 27  | Find-My Unpair          | 27      | R        |     | ✔️    |      |
| 28  | Find-My Disable         | 28      | S        |     | ✔️    |      |
| 29  | Find-My Enable          | 29      | T        |     | ✔️    |      |
| 30  | Find-My Locate Sound    | 30      | U        |     |       | ✔️   |
| 31  | --- no sound ---        | 31      | V        |     | ✔️    |      |

The default values result in following bitmasks:

    Volume Group Low: 0x00000000
    Volume Group Medium: 0xBC3F33FF
    Volume Group High: 0x43C0CC00
