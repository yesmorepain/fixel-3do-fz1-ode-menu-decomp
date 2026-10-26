# Object layout of the rebuilt programs

Where each source file's object sits in the original program files, and the link and post-processing steps
that reproduce them. Object file names are not in the binaries; the names used here are guesses.

## LaunchMe

Link: `armlink -aif -r -b 0x00` main controlpad getvideoinfo ode ui crc rampatch, then the SDK 2.5 libraries
lib3do, filesystem, graphics, input, clib, swi, and cstartup.o. Post-processing: `modbin -stack 4096 -debug`, then
`stripaif` (reproduced by `tools/aifpost.py --stack 4096 --debug`). No signature.

| Object | Source | Code | RW data | Zero-init | Evidence |
|---|---|---|---|---|---|
| cstartup.o | SDK | 0x80-0x16c | 0x93c0-0x93c8 | - | SDK object |
| main | `src/main.c` | 0x498-0x1154 | 0x93c8-0x9448 | - | shared literal pool; Konami tables at the end (0x1104-0x1154) |
| controlpad | SDK ExamplesLib | 0x1154-0x16fc | 0x9448-0x94a0 | - | byte-exact compile |
| getvideoinfo | SDK ExamplesLib | 0x16fc-0x18d0 | - | - | byte-exact compile |
| ode | `src/ode.c` | 0x18d0-0x2c4c | 0x94a0-0x94d4 | 0x978c-0x9f8c | shared literal pool; 16 zero 8-byte command templates at the end |
| ui | `src/ui.c` | 0x2c4c-0x4db4 | 0x94d4-0x9728 | 0x9f8c-0x129f4 | X-menu and About strings at the start; const data at the end |
| crc | `src/crc.c` | 0x4db4-0x5200 | - | - | CRC table at the end (0x4e00) |
| rampatch | `src/rampatch.c` | 0x5200-0x5388 | - | 0x129f4-0x12bf4 | patch table at the end (0x5338) |

Library data follows: RW 0x9728-0x978c, zero-init 0x12bf4-0x12e40.

## System/Drivers/ode

Link: `armlink -aif -r -b 0x00` finditem CreateItem odedrv superdo supervectors, then clib, swi, cstartup.o.
Post-processing: `modbin -stack 0x1000`, flags 0x02 (privileged), name "odedrv", no `-debug`; then `stripaif`
(`tools/aifpost.py --stack 0x1000 --flags 0x02 --name odedrv`). The file ends with a 64-byte signature at 0x1c0c
(header 0xb0/0xb4 = offset/size); these tools do not regenerate it (re-signing with the published 3DO keys has not been tried), so it is copied from the original (`--sig-from`).

| Object | Source | Code | RW data |
|---|---|---|---|
| finditem | portfolio_os kernellib `finditem.c` | 0x174-0x1c0 | - |
| CreateItem | portfolio_os kernellib `CreateItem.c` | 0x1c0-0x1f4 | - |
| odedrv | `src/odedrv.c` | 0x1f4-0x1060 | 0x19b4-0x1a90 |
| superdo | generated (`tools/mkkernellib.py`) | 0x1060-0x108c | - |
| supervectors | generated, 60 STUB areas | interleaved with the clib stubs, 0x1614-0x19ac | - |

The driver object starts with the data-initialiser strings "ode", "ode", "ramp", "ramp" (0x1f4-0x20c). Its data
holds five tag arrays (driver, device, ramp command table, ramp driver, ramp device) followed by eight scalars.

## mplay

Fixel's code is only `play_sound_file_loop` and `main` (0x2fc-0x4a8, `":mplay.c"`); everything else is SDK 2.5
library code. `src/mplay.c` matches both functions; `tools/build_mplay.sh` links it with SDK 2.5 lib3do, audio,
music, clib, swi and cstartup (in that order; it reproduces the original member layout) and applies the same
post-processing as LaunchMe (`modbin -stack 4096 -debug`, `stripaif`). The result is byte-identical; mplay is not
signed. `play_sound_file_loop` has three locals that are never used (an 8-byte array and two zeroed scalars); they
are needed for the original stack frame and registers.

## storagetuner

The SDK 2.5 Storage Tuner example (`st.c`, `stlists.c`, `stmem.c`, from ext.3dodev.com), built as its
`storagetuner.make` does: debug flags, objects then `cstartup.o`, then Lib3DO, audio, music, operamath, filesystem,
graphics, input and clib; post-processing as LaunchMe. All functions match the SDK sources except
`CreateTunerScreen` in `st.c`, which has PAL support added: it reads `QUERYGRAF_TAG_DEFAULTDISPLAYTYPE` with
`QueryGraphics` (returns 0 if that fails) and passes `CSG_TAG_DISPLAYTYPE` (`DI_TYPE_PAL1` for PAL1/PAL2, otherwise
`DI_TYPE_NTSC`) to `CreateScreenGroup`. `src/storagetuner_st.c.patch` holds that change; with it the rebuild is
byte-identical (unsigned). Who made the change is not known (guess: Fixel). Local names in the patch are guesses.

Size: the program's RO is 0x116c larger than the SDK's prebuilt `Apps_Data/storagetuner`. The prebuilt was built
without `-g` (the debug build adds 0x114c here), the PAL change adds 0x6c (0x5c in `st.c`, 0x10 for the `QueryGraphics`
library stub); the prebuilt is still 0x4c larger than an unmodified build without `-g` (not investigated).
