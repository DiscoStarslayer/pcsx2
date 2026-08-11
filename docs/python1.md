# Konami Python 1 Config

## Descriptor syntax

A descriptor is an INI file with a single `[Game]` section.

```ini
[Game]
Name=Pop'n Music 14
GameId=GNF14JAB
Region=NTSC-J

HddImagePath=popn14/popn14.chd
BbsRamPath=popn14/m48t58y.u48
IoBootRomPath=popn14/b22a01.u42
IoConfigRomPath=popn14/d72872gc.crom
InternalDonglePath=popn14/ds2430.u3
ExternalDonglePath=popn14/ds2430_black_gnf14jab.u3
MemoryCardDonglePath=popn14/kn00002.ps2
MemoryCardIdPath=popn14/kn00002.id
IoMode=POPN
```

Use either `HddImagePath` or `CfImagePath` for normal game media. A descriptor may contain both, such as an update kit.

## Field reference

| Field                  | Required | Description                                                                                                                                                                                        |
|------------------------|----------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Name`                 | Yes      | Friendly title shown in the game list. Defaults to the descriptor path.                                                                                                                            |
| `GameId`               | No       | Game identifier used as the serial. Reliquary attempts to extract it from the P1IO boot ROM.                                                                                                       |
| `UniqueId`             | No       | Per-game settings id.                                                                                  |
| `Region`               | No       | Game-list region string. Defaults to `NTSC-J`.                                                                                                                                                     |
| `HddImagePath`         | Yes      | Raw or CHD-compressed HDD image.                                                                                                                                                                   |
| `CfImagePath`          | Yes      | Raw or CHD-compressed CompactFlash image. PythonFS inside the MBR/FAT16 container is handled by the emulated platform path.                                                                        |
| `BbsRamPath`           | Yes      | Writable path for the P1IO's 8 KiB battery-backed SRAM. A zero-filled file is created if the path does not exist. Some titles can initialize blank state; others require matching dumped contents. |
| `IoBootRomPath`        | Yes      | P1IO boot ROM.                                                                                                                                                                                     |
| `IoConfigRomPath`      | Yes      | FireWire configuration ROM, board OUI.                                                                                                                                                             |
| `InternalDonglePath`   | Maybe    | Internal DS2430 data.                                                                                                                                                                              |
| `ExternalDonglePath`   | Maybe    | External round black dongle.                                                                                                                                                                       |
| `MemoryCardDonglePath` | Yes      | Raw PS2 memory-card dongle image including ECC/spare data. It is assigned to memory-card slot 1.                                                                                                   |
| `MemoryCardIdPath`     | Yes      | Card ID/key data used for card-bound KELF auth.                                                                                                                                                    |
| `IoMode`               | No       | P1IO protocol/input profile. Defaults to `JVS`.                                                                                                                                                    |

## I/O modes

`IoMode` is case-insensitive when the descriptor is scanned and is normalized into one of these values:

| Value          | Profile                    |
|----------------|----------------------------|
| `JVS`          | General JVS-oriented P1IO  |
| `EXTIO`        | Dancing Stage Fusion       |
| `POPN`         | Pop'n Music                |
| `PPOOL`        | Perfect Pool               |
| `B22`          | Dogstation B22 I/O profile |
| `DOGSTATIONDX` | DogStation DX I/O profile  |

Select the mode matching the board firmware and game. The setting affects P1IO protocol responses as well as which automatic controller bindings are exposed.

## P1IO controls and persistent data

Configure Python 1 inputs under the FireWire section of the controller settings.

![Python 1 I/O configuration](p1io-config.png)
