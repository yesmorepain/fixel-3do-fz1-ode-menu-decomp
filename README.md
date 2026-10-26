# FZ-1 ODE menu: reconstruction of the 3DO-side programs on boot.iso

This project reconstructs the programs on the menu disc (`boot.iso`, release V5) that Fixel wrote for his 3DO optical
drive emulator (ODE), the FZ-1 internal ODE and the XPort (one image for both [doc: Fixel's release page; the
XPort-only code path is in the program, [code]]). It covers only the code that runs on the 3DO itself. The
ODE firmware ([fixel-3do-fz1-ode-decomp](https://github.com/yesmorepain/fixel-3do-fz1-ode-decomp)) and the ODE's FPGA gateware
([fixel-3do-fz1-ode-fpga-decomp](https://github.com/yesmorepain/fixel-3do-fz1-ode-fpga-decomp)) are separate projects.

The aim is a faithful, documented reconstruction: C source plus build tools that turn it back into the original
binaries, byte for byte, with the original 1994 toolchain. Because the source rebuilds exactly, it is a reliable
starting point for anyone who wants to study the menu, interact with it or modify it. Hardware check: a menu disc
whose LaunchMe was rebuilt from `src/` (with a small visible test change, then re-signed) booted and ran on an FZ-1
through the ODE [hardware]. The unmodified rebuilds are byte-identical to the originals [rebuild], so they behave
exactly like them.

> The menu software is Fixel's work and its licence is "TBD". This repository
> contains the reconstructed source but no binaries and no files from the disc. See
> [What is not included](#what-is-not-included).

## How sure is each statement?

Statements in this README carry a label where it matters:

| Label | Meaning |
|---|---|
| **[hardware]** | Verified on a real FZ-1 with the ODE |
| **[rebuild]** | Verified by rebuilding: the reconstructed source and the original tools produce the original file byte for byte |
| **[code]** | Read from the original program, which the rebuild matches. Exactly what the original does, but not every case was tried on hardware |
| **[doc]** | Taken from a document, the SDK, or another tool or source |
| **[inference]** | Derived from facts, not shown directly |
| **[guess]** | Plausible, not verified |

## One device, three projects

Fixel's FZ-1 ODE has three programmed parts, and each is covered by its own project. Together they describe
everything Fixel programmed into the device, layer by layer, from the menu on the TV screen down to the timing on
the console's CD-ROM bus.

```
 3DO console                                ODE board (Rev B)
┌──────────────────────┐   CD-ROM bus   ┌──────────────┐   SPI + GPIO   ┌──────────────────┐   microSD / USB
│ Menu disc (boot.iso) │ ─────────────> │ FPGA         │ ─────────────> │ RP2040 firmware  │ ─────────────> disc images
│ LaunchMe, ode driver │ <───────────── │ (gateware)   │ <───────────── │ (update.ode)     │
└──────────────────────┘  7-byte cmds,  └──────────────┘  packets,      └──────────────────┘
      menu project        status, data    FPGA project    handshakes      firmware project
```

| Project | Layer | What it explains | State |
|---|---|---|---|
| **[Menu](https://github.com/yesmorepain/fixel-3do-fz1-ode-menu-decomp)** | Software on the 3DO | What the user sees and does: browsing, the play queue, saves, updates. It reaches the ODE only through 7-byte commands sent via its `ode` driver | Rebuilds byte for byte from reconstructed C; a menu disc with a rebuilt LaunchMe has booted on an FZ-1 |
| **[FPGA](https://github.com/yesmorepain/fixel-3do-fz1-ode-fpga-decomp)** | Gateware between the console and the RP2040 | How bytes cross the 3DO bus: buffers, handshakes, status, 75 Hz pacing, timing | Decoded and documented; a clean HDL version is proved equivalent on a zero-delay model. **Not tested on hardware** |
| **[Firmware](https://github.com/yesmorepain/fixel-3do-fz1-ode-decomp)** | Software on the RP2040 | What each command means: reading disc images, answering the console, update and recovery | Rebuilds from reconstructed C, mostly byte-identical; tested on a Rev B board |

This repository is the **menu** project.

How they fit together:

- **Menu and firmware** share the ODE command set: the menu sends the commands, the firmware's
  `docs/commands.md` defines them. Every command the menu uses is documented on the firmware side.
- **Firmware and FPGA** share the SPI packet protocol and the GPIO handshake lines. The FPGA analysis explains what
  the firmware's FPGA accesses actually do, and corrected the firmware documentation in places (GPIO20 in CD-audio
  mode, what the GPIO22 reset clears, that the reset after configuration is required).
- **FPGA and console** share the 3DO drive bus. The FPGA's behaviour was checked against the 3DO technical guide,
  the FZ-1 service manual and the open SataTo3DO drive emulator.

What the whole adds up to: every part can be rebuilt from Fixel's official, freely available files, and each
finding is labelled by how well it is proven. That makes it a record for preserving and repairing an
out-of-production device, a reference for anyone building a replacement drive emulator or new ODE firmware, and a
safe starting point for changes.

What is still missing: the hardware design. Fixel's hardware repository is empty, so what is known about the board
comes from the bitstream, the firmware and the part markings (for example the two 12 MHz clocks). The FPGA side
also still needs confirmation on real hardware.

## The programs on the disc

| File | What it is | Rebuild status |
|---|---|---|
| `LaunchMe` | The menu: file browser, game queue, NVRAM save/load, ODE update | byte-identical [rebuild]; a modified rebuild ran on an FZ-1 [hardware] |
| `System/Drivers/ode` | Privileged Portfolio driver: the `ode` device (proxy to the `xbus` device) and the `ramp` device (memory and `ram`-device access) | byte-identical [rebuild] (signature copied, see below) |
| `mplay` | Background music player; loops `assets/bgm44.aifc` | byte-identical [rebuild] |
| `assets/storagetuner` | 3DO SDK 2.5 "Storage Tuner" example, the X menu's "Storage Manager" | byte-identical [rebuild] (SDK source + a small patch) |

Everything else on the disc is the stock 3DO SDK 2.5 runtime [inference: names and contents match the SDK's; not
compared file by file].

Boot sequence: Portfolio starts `$drivers/ode` and then `$boot/LaunchMe`, and LaunchMe starts `mplay`. The menu
talks to the ODE only through the `ode` driver, which sends 7-byte commands over the 3DO expansion bus (xbus)
[code; the xbus calls match Portfolio's own bus driver source, [doc]].

## How the rebuild works

- **Compiler.** The programs were built with Norcroft ARM C against the 3DO SDK 2.5 libraries. The exact compiler
  is the MPW-hosted ARM C 4.62 (Toolkit 1.5, a PowerPC Mac tool) [rebuild: it reproduces the original code]. `tools/mpwrun.py` runs these MPW tools (armcc,
  armlink) on a modern machine: it emulates the PowerPC code with Unicorn and provides the parts of the Mac
  libraries the tools use.
- **Flags.** `-fa -zps0 -za1 -g -d DEBUG=1` [rebuild], i.e. the SDK example makefile's debug build [doc], with sources compiled
  from their own folder (`__FILE__` is `":name.c"`).
- **Matching.** Each object is compared with the original, function by function (`tools/cmp_obj.py`) and as
  whole areas (`tools/cmp_area.py`). The source was adjusted until the compiler output matches exactly:
  statement order, declaration order, line numbers of the debug macros, and so on.
- **Post-link steps.** The SDK's `modbin` and `stripaif` are not available (not found on ext.3dodev.com). `tools/aifpost.py` reproduces their
  effect on the file: the 3DO header (stack size, flags, name, debug marker) and removal of the debug area.
- **Assembler objects.** No ARM assembler is among the MPW tools, so `tools/mkaof.py` writes the few small
  assembler objects (kernel library supervisor stubs) directly as AOF files.
- **Signature.** The ode driver carries a 64-byte signature at the end of the file [code], which these tools do
  not regenerate. (The 3DO signing keys were published in 2010, but re-signing the driver has not been tried.)
  `tools/aifpost.py --sig-from <original>` copies it from your copy of the original file, and only if every byte
  before it already matches. The build also writes an unsigned version.

## Requirements

- Python 3 with a virtual environment at `.venv` (the build scripts call `.venv/bin/python`):
  `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt` (unicorn 2.1.0, capstone 5.0.1,
  `setuptools<81` for capstone's import).
- A POSIX shell for the build scripts.
- Your own copies of the following (not included, see below). The `->` targets are folders under `references/`:
  - `boot.iso` from the official V5 release page: <https://github.com/fixelsan/3do-ode-firmware/releases/tag/V5>
    (6,619,136 bytes, sha256 `ef0787a3b06845f767be852ef6d5b986021c3def4b458e8e53bddd8c1640595f`), placed at
    `original/boot.iso` in the repo root
  - 3DO SDK 2.5 headers and libraries: `ext.3dodev.com/3DO/Portfolio_2.5/Interfaces/2p5/` (`includes/`, `libs/`)
    -> `sdk25_interfaces/`
  - 3DO SDK 2.5 ExamplesLib: `ext.3dodev.com/3DO/Portfolio_2.5/Examples/ExamplesLib/` -> `sdk25_examples/ExamplesLib/`.
    The copy in trapexit/3do-devkit's "original/Portfolio 2.5" examples is a different revision and does not match
  - 3DO SDK 2.5 Storage Tuner example: `ext.3dodev.com/3DO/Portfolio_2.5/Examples/NVRAM/Storage_Tuner/`
    -> `sdk25_examples/NVRAM/Storage_Tuner/`
  - MPW ARM tools from 3DO Toolkit 1.5 (`armcc` 4.62, `armlink` 4.49): `ext.3dodev.com/3DO/Toolkit_1.5/Tools/ArmTools/`
    -> `sdk_mpw_armtools/`. Other compiler versions were not tried for a byte-identical rebuild; Norcroft's output differs between
    versions [inference]
  - Kernel library sources (`finditem.c`, `CreateItem.c`, `supervectors.s`) from
    <https://github.com/trapexit/portfolio_os> -> `portfolio_os/kernellib/`
- `references.sha256` lists the sha256 of every external file the builds were verified with. Check your copies
  with `sha256sum -c references.sha256` from the repo root: a different version will not rebuild byte-identical
  (for example, the devkit's `clib.lib` and `codec.lib` differ from the 2.5 originals [rebuild: compared]).
- Optional: Ghidra (11.x, Java 21) for the analysis scripts in `ghidra/scripts/`.
- Optional: <https://github.com/trapexit/3do-devkit> in `references/3do-devkit/` (its `lib/` folder, extra libraries
  besides the 2.5 ones) to regenerate the library names in `tools/names/matched/` and `tools/names/linked/` with
  `tools/match_all.sh` (run the four build scripts first: they write the link maps it reads).

## Usage

```sh
# 1. Extract the disc (pure-Python Opera file system reader)
python3 tools/opera.py extract original/boot.iso extracted/

# 2. Rebuild and compare
tools/build_launchme.sh      # -> build/LaunchMe, compare with extracted/LaunchMe
tools/build_ode.sh           # -> build/ode (+ build/ode.unsigned), compare with extracted/System/Drivers/ode
tools/build_mplay.sh         # -> build/mplay, compare with extracted/mplay
tools/build_storagetuner.sh  # -> build/storagetuner, compare with extracted/assets/storagetuner

# 3. Optional: put the rebuilt programs into a copy of the disc (identical to the original when all four match)
python3 tools/make_iso.py original/boot.iso build/boot.iso LaunchMe=build/LaunchMe System/Drivers/ode=build/ode \
    mplay=build/mplay assets/storagetuner=build/storagetuner
```

## Known gap: the XPort RAM patch

On an XPort only (ODE name starting with 'X'), LaunchMe patches the console's RAM at startup: `src/rampatch.c`
(`apply_ram_patches`) writes 4 words to RAM 0x5dd8-0x5e00, but only if the CRC32 of RAM 0x200-0x3ff is 0x00984c6a,
i.e. on one particular console memory layout [code], and each word only if RAM still holds the expected original value
(stored in the patch table) [code]. Two of the new words form the routine `mov r0,#1; mov pc,lr` (always return 1) at
0x5dfc, a third is the pointer 0x5dfc at 0x5df8. The fourth, 0xf4840010 written at 0x5dd8, is unexplained [code]. The
menu's Device info screen shows the same CRC as "DIPIR", so you can see whether a console matches (`00984C6A`).

The four expected original words are not stored as-is in any file on `boot.iso`. They do occur, at the same
relative offsets, inside a 64-byte data block in trapexit's `3dt` tool [doc: checked]; what that block is has not
been checked.

The XPort ODE firmware (`xpode.xpu` v0.57) includes 3DO console-side code: several `operator.rom` versions, an LCCD
driver and dipir. It also has RSA code whose key constant appears in `3dt`, the same tool that holds the patch's
original words. The patch table values are not stored as-is in the firmware. Decompiling the firmware may show which
console or OS version the patch targets, but confirming it still needs a RAM dump from a matching console.

**Which routine this is, and why it is patched, is unknown.** Finding out needs a RAM dump (and boot ROM dump) from
a console whose Device info shows `00984C6A`; this project had no such dump. The reconstructed code reproduces the original bytes exactly,
so a rebuilt menu behaves like the original, but the names and comments around the patch are guesses. Be careful
when changing `rampatch.c` or the code that calls it: the effect on an XPort is untested, and patching the wrong
RAM can crash the console. If you can identify the routine, that information would close this gap.

## Repository contents

| Path | What |
|---|---|
| `src/` | Reconstructed C: `main.c`, `ode.c`, `ui.c`, `crc.c`, `rampatch.c` (LaunchMe), `odedrv.c` (ode driver), `mplay.c` (mplay), `storagetuner_st.c.patch` (changes to the SDK's `st.c` for storagetuner). File and identifier names are guesses [guess]; the code is matched to the original [rebuild] |
| `tools/names/` | Function names for the original binaries: `<prog>.tsv` (address, name, confidence, comment), `matched/<prog>.tsv` (SDK library functions found by byte matching), `linked/<prog>.tsv` (library names from the rebuilds' link maps), `common.tsv`. Used by the Ghidra scripts and the compare tools |
| `tools/opera.py` | Opera file system lister and extractor |
| `tools/make_iso.py` | Write rebuilt programs into a copy of `boot.iso` (same-size, in-place replacement) |
| `tools/mpwrun.py`, `tools/pef.py` | MPW tool runner (PEF loader, PowerPC emulation, StdCLib/InterfaceLib subset) |
| `tools/cc.sh`, `tools/build_*.sh` | Compile and link scripts with the pinned flags |
| `tools/cmp_obj.py`, `tools/cmp_area.py` | Compare rebuilt objects with the original program |
| `tools/aifpost.py` | `modbin` / `stripaif` equivalent, signature copy |
| `tools/mkaof.py`, `tools/mkkernellib.py` | AOF object writer, kernel library stub objects |
| `tools/match_libs.py`, `tools/match_all.sh`, `tools/linkmap_names.py` | Byte-match SDK library code in a program to name library functions; take library names from the rebuilds' link maps |
| `tools/swi_table.py` | Build the Portfolio SWI number table from the SDK headers |
| `requirements.txt` | Python packages for `.venv` |
| `references.sha256` | Checksums of the external files (SDK, MPW tools, portfolio_os) the builds were verified with |
| `tools/kernellib/super.h` | Two-prototype stand-in for a Portfolio-internal header |
| `ghidra/scripts/`, `ghidra/run_import.sh` | Ghidra scripts (AIF layout, Portfolio function/SWI discovery, naming, exports) and the headless import of all four programs (needs `GHIDRA_HOME`) |
| `docs/menu.md` | What is on the disc, how the menu reaches the ODE, screen, controls, X menu, hidden behaviour |
| `docs/objects.md` | Object layout of each rebuilt program, link order and post-processing |
| `docs/matching.md` | How the sources were matched to the original compiler output; rules for editing them |
| `original/`, `references/` | Placeholders: where to put `boot.iso` and the external material |

## What is not included

- `boot.iso`, the files extracted from it, and anything derived from them (disassembly, decompiler output,
  disassembly-derived symbol tables other than `tools/names/`). These are Fixel's work and the licence is "TBD".
- 3DO SDK headers, libraries and examples, the MPW tools, and the portfolio_os sources. These belong to their
  respective owners.

## Credits

- **Fixel** (Felix Lazarev) wrote the menu, the driver and the ODE.
- The 3DO Company: Portfolio OS, SDK 2.5, Toolkit 1.5 tools.
- trapexit: portfolio_os sources, and modbin, whose documentation of the 3DO binary header these tools follow.
- ext.3dodev.com and 3dodev.com for preserving the SDK and its documentation.

## Licence

The tools in `tools/` and `ghidra/scripts/` are original work of this project and are released under the MIT
licence (see `LICENSE`). The sources in `src/` and the name lists in `tools/names/` are a reconstruction of, and
symbols for, Fixel's programs, whose licence is "TBD"; the MIT licence does not cover them. Nothing in this
repository grants any rights to Fixel's software or to the 3DO SDK.
