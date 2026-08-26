# Required game files

Provide these files locally from your **legally obtained** copy of *Dragon Ball
Z: Budokai 3 HD Collection* (Xbox 360). **None of them are distributed by this
project.** This document lets you verify that you have the exact files the
recompile expects, the same way other static-recompilation projects do
(e.g. `baserom.md` in mstan's recomp projects).

## Game executable (`.xex`)

The Xbox 360 game executable. **The USA and EU (PAL) builds are different**
(the recompiled port includes a recompilation of each one inside the same dual
core; the launcher picks the matching code by checksum), so each region has its
own expected hash:

- Expected path: `default.xex` (next to `dbz3.exe`)
- Size: **4,890,624 bytes** (4.66 MB)

| Region | SHA-256 |
|---|---|
| USA / NA (`yae3_xenon.xex`) | `B40BBA40CFD6C90CB269EBF5020818924F43109BCC86827A3CC37124C052A26B` |
| EU / PAL (`yae3_xenon_eu.xex`) | `7193803AEE6124C8D0782EF2C37FF2E8D41DB8A11286D198C23352EA7622E924` |

> Either copy works, but only the matching one is used by the game; using the
> other region's `.xex` is detected by the launcher, which warns and blocks Play
> instead of crashing.

## Game data archives (`.afs`)

These are the model/audio/text data archives. You only need the ones for the
region you play. They all share the same internal bin numbering.

### USA region (`us/`)

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `data_cmn.afs` | 293,423,104 | `FC73A1DC9FBF1B13953C0347A121C65FCB2DEECA4A7C823F138177E60FF8C0DF` |
| `data_eng.afs` | 64,284,672 | `AC51F1D273EB7A2A50F685282723651AAFDD54159F4BF2237BECD5E484EA8958` |
| `data_fra.afs` | 64,727,040 | `0136FFA2C62CE0ED82AD22C07058566A081AED3EF4EC6915D35786752A2C5983` |
| `data_ger.afs` | 66,117,632 | `83C51CB4D6DB408868AF6487C0AA0CDDEAC20C32BB28324F08E6215E0B32A328` |
| `data_ita.afs` | 64,315,392 | `EFECE6723C2D7859FCB773E446CEA9489394837E632BC68C79A9066B2FFC7E4A` |
| `data_spn.afs` | 65,036,288 | `AEB97105E3F3B0A105D41B77F0A4F966184298381519F3B96EFAB210C43EF4D4` |
| `data_usi.afs` | 62,898,176 | `7DD517A4EB1462CBD5C104C5CF9748D8CD34719B10B6FACC22F0107181A65851` |
| `data_yah.afs` | 1,632,256 | `6E55D160F6C29EE21CC255AE966AB17016143FB52ADE4E84409D0493BAC7EFCF` |
| `adx_jpn.afs` | 728,424,448 | `ECF59DB76B2B04720505DDEBC4AC931A45EE2597D21AD65693DA2DF86E27A822` |
| `adx_usa.afs` | 681,572,352 | `702168D52C1DD93DFE5E1AB0F625C433EC9B7AFD7E07930C3A9C0FB3E82A4BA5` |
| `lang_jpn.afs` | 19,107,840 | `E3421DB3848D93E457EE632BF14D1CD94D3D0165C69A6AA4FBDF48C13D38B4E9` |
| `lang_usa.afs` | 17,618,944 | `9F4C947634B368ACBE594F2BC773745312B23BD14B7394E02435AE9432880A0E` |
| `opening.sfd` | 73,623,552 | `773D9391BE0CD2EFA4351BE76FB1D4C054A69B2A624D94A882C65CF0EE83DC08` |
| `Ending00.sfd` | 88,363,008 | `65180E8C4E9D3C16095447A285018A0E5492D1966DB009E6092585CCB995786B` |
| `Ending01.sfd` | 169,752,576 | `C243C28EAE615F161DAC2CC67709507AD60CE0CEF7BCB4F372A413EA837ED4F2` |

### EU (PAL) region (`eu/`)

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `data_cmn.afs` | 293,423,104 | `C963502B00AF6B85C4C12A8AB28ACAE86B2EF57559682174D194DF78021B2F49` |
| `data_eng.afs` | 64,284,672 | `AC51F1D273EB7A2A50F685282723651AAFDD54159F4BF2237BECD5E484EA8958` |
| `data_fra.afs` | 64,727,040 | `E742E911F6656EE0C12DA77E29519FF089E22E2771790FD4797016FC5F230EDA` |
| `data_ger.afs` | 66,117,632 | `83C51CB4D6DB408868AF6487C0AA0CDDEAC20C32BB28324F08E6215E0B32A328` |
| `data_ita.afs` | 64,315,392 | `EFECE6723C2D7859FCB773E446CEA9489394837E632BC68C79A9066B2FFC7E4A` |
| `data_spn.afs` | 65,036,288 | `FAA80E60E16536FC43365762253A59770EF2D7DD897A49DCCDFBC334BD9A12A5` |
| `data_yah.afs` | 1,632,256 | `ECACA9299CA76A642A716B1B5A0B34C8E74E08AAEE5ADBA4E1F15A5E61B845A9` |
| `adx_jpn.afs` | 727,304,192 | `308EE1FF10E273D4A984504E8412B41401C58D01CC09A1AB0EEA2DC9EE758077` |
| `adx_usa.afs` | 681,025,536 | `D742C2A65CDC338E5643D3240B77EF52298A64538910FEB871E9F2AAB023AEF6` |
| `lang_jpn.afs` | 19,107,840 | `B76B558E2737E3C0E171540F6C31B1FDF3423D7F94D998F227E18F0BFBE344DE` |
| `lang_usa.afs` | 17,618,944 | `DF5CBF644F0754DC0A07D7477E3020473F34A1AF9167F51EA68FF3D9D9D10D77` |

> The `.xex` and the `.afs`/`.sfd` archives are **copyrighted** and are **not
> distributed** by this project. You must extract them yourself from the
> original Xbox 360 disc (ISO) or digital copy you own.

## How to extract the files from the ISO

The Xbox 360 disc image (`.iso` / `.xiso`) contains the game data in an Xbox
FATX filesystem. Tools to read it (all third-party, not affiliated with this
project):

1. **`extract-xiso`** — reads Xbox/Xbox 360 ISO images on Windows/Linux/macOS.
2. **Xbox 360 HDD/DVD extraction tools** from the modding community.

Once the FATX volume is mounted/extracted, the files you need live under the
game's `Partition1\` layout:
- The `.xex` (executable) at the game root.
- The `us/` and `eu/` folders containing the `data_*.afs`, `adx_*.afs`,
  `lang_*.afs` archives and the `.sfd` movie files.

Copy those into the `us/`/`eu/` layout described above. You do **not** need the
whole ISO — only the executable and the data archives.

## Why this matters

This project follows the standard "copyright-friendly" convention of the
static-recompilation community (see `mstan/DragonBallZBuusFuryRecomp`): the
recompile logic ships as source and as a runnable launcher, but **no game
content is distributed**. You bring your own legally obtained files, verify
them against this document, and play.
