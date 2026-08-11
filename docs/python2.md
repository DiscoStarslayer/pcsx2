# Konami Python 2 Config

## Config

A descriptor is an INI file with a single `[Game]` section.

```ini
[Game]
Name=DDR SuperNova 2
GameId=GDJJA
Region=NTSC-J

HddImagePath=gdj_jaa_2007100800.chd
HddIdPath=ps2_hdd_id.bin
NvRamPath=ps2_nvram.nvm
; IlinkIdPath=ilink_id.bin

DongleBlackPath=ds2430_black_gqgdjjaa.bin
DongleWhitePath=ds2430_white_gqfdhjaa.bin

InputType=2
DIPSW1=false
DIPSW2=false
DIPSW3=false
DIPSW4=false
Force31kHz=false

Player1Card=card1.txt
; Player2Card=card2.txt
PatchFile=ddrsn2j.pnach
```

## Field reference

| Field                     | Required                     | Description                                              |
|---------------------------|------------------------------|----------------------------------------------------------|
| `Name`                    | Yes                          | Friendly title shown in the game list.                   |
| `GameId`                  | No                           | Game identifier used as the serial.                      |
| `UniqueId`                | No                           | Per-game settings id.                                    |
| `Region`                  | No                           | Game-list region string. Defaults to `NTSC-J`.           |
| `HddImagePath`            | Yes                          | Raw or CHD-compressed HDD image.                         |
| `HddIdPath`               | Yes                          | HDD identity paired image.                               |
| `IlinkIdPath`             | Yes                          | Explicit i.Link ID data.                                 |
| `NvRamPath`               | One identity source required | Console NVRAM. Prefered over i.Link ID file.             |
| `DongleBlackPath`         | Authentication-dependent     | Black Dallas dongle data.                                |
| `DongleWhitePath`         | Authentication-dependent     | White Dallas dongle data.                                |
| `InputType`               | Yes for useful controls      | Input profile. See the mapping below. Defaults to `0`.   |
| `DIPSW1` through `DIPSW4` | No                           | Boolean DIP-switch states. Each defaults to `false`.     |
| `Force31kHz`              | No                           | Reports 31 kHz output through P2IO. Defaults to `false`. |
| `Player1Card`             | No                           | Text file containing a 16-character player card ID.      |
| `Player2Card`             | No                           | Text file containing a 16-character player card ID.      |
| `PatchFile`               | No                           | Extended PNACH patch file loaded.                        |

### Dongle file layouts

The Python 2 dongle loader accepts the layouts commonly found in preservation sets:

- Legacy layout: 8-byte serial ID followed by the 32-byte encrypted payload.
- MAME-style layout: 32-byte encrypted payload followed by the 8-byte serial ID.

Keep the black and white devices associated with the correct title and hardware set.

## Input types

`InputType` selects the P2IO profile and controller bindings:

| Value | Profile |
|---|---|
| `0` | DrumMania |
| `1` | GuitarFreaks |
| `2` | Dance Dance Revolution |
| `3` | Toy's March |
| `4` | Thrill Drive 3 |
| `5` | Dance 86.4 Funky Radio Station |

P2IO is forced onto USB port 1 when a Python 2 game boots. Configure its bindings from that port's device settings.

![Python 2 I/O configuration](p2io-config.png)
