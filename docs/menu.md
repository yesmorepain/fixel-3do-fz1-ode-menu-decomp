# The menu disc: contents, ODE access, controls

How Fixel's menu disc (`boot.iso` V5) behaves, as found while reconstructing it. Function names refer to `src/`
and `tools/names/`; addresses are offsets in the original program files.

## What is on the disc

| File | What it is |
|---|---|
| `LaunchMe` | the menu (Fixel, "(c) 2022 Felix Lazarev"), Norcroft ARM C + 3DO SDK 2.5 libs |
| `System/Drivers/ode` | Fixel's privileged Portfolio driver "odedrv": `ode` device (xbus proxy) + `ramp` device (memory / ram-device proxy) |
| `mplay` | small music player (":mplay.c"): loops `assets/bgm44.aifc` (SDX2 stereo 22,050 Hz) |
| `assets/storagetuner` | SDK 2.5 "Storage Tuner" example (st.c); its cels and font are byte-identical to the SDK's `Apps_Data` |
| `System/Programs/MAUS`, `WALKER`, `Drivers/TUNERS/access.tuner` | not programs: 153,636-B IMAG pictures shown by the secret codes |
| rest | stock SDK 2.5 runtime (kernel, folios, DSP, FMV microcode, eventbroker, shell, lmadm, fscheck ...) |

Boot: `STARTOPERA` -> `AppStartup`: `$drivers/ode&` then `$boot/LaunchMe`; LaunchMe starts `$boot/mplay`.

## How the menu reaches the ODE

```
LaunchMe --IOReq "odecmd0"/"odecmd1"/"oderw"--> ode device (driver) --xbus IOReqs--> xbus device --> ODE firmware
LaunchMe --IOReq "ramprq"--> ramp device --> unit 0: supervisor memcpy at ioi_Offset; units 2-5: Portfolio "ram" device
```

- `ode_cmd(cmd7, reply, n)`: ode-device cmd 3 (xbus CmdCommand), 7 command bytes out, n status bytes back.
- `ode_cmd_data(cmd7, reply, n, buf, len)`: ode-device cmd 12: data phase (xbus CMD_READ into buf) + command.
  `ioi_User` -> `{bit31 write | unit, buf, len}`; the driver takes the xbus unit from the low byte.
- ode-device cmd 13: returns xbus `dev_MaxUnitNum`. `ode_init`: 2 xbus units -> ODE = unit 1, else unit 0.
- ODE commands sent: 93, C0, C3, C4, C5, D1, E1, E3, E4, E5, E6, E7, F0. Wrappers exist but are never called for
  C1 and E2. E0 is never sent (E1 with the write flag creates files).
- ramp units (Portfolio `ram` device): 2 = SYSROM, 3 = NVRAM (32 KB), 4 = FMV ROM, 5 = FONT ROM; 0 = raw memory
  (supervisor memcpy, used for RAM dumps, chip ID registers and RAM patches).

## Screen and controls

Left pane (`side_pane_update`): help lines, `<ODE name>:r5` (name from EXT_ID, e.g. FZ1ODE), `v a.b.c` = EXT_ID
reply bytes 8.9.10 = firmware 1.0.<Eject press count>, optional status line ("Queue n").

Browser (`browser_input`, D1 pages of 14 rows, entries in the order the ODE returns them: **no sorting**;
directories shown as `[name]`, names truncated to 30 characters):

| Button | Action |
|---|---|
| Up / Down | move; past the end/start loads the next/previous page |
| R shoulder / L shoulder | jump to page end / start (and so to the next / previous page) |
| A | directory: C0 enter it, reload page 0. File: C4 add to queue, row turns 0x7bc0, "Queue n" |
| B | only while a file row is selected: C3 clear queue |
| C | file: C3 + C4 (this file) + C5 launch, "Starting the game", wait 7 s |
| P (Start) | queue not empty: C5, "Starting the queue", wait 7 s |
| X | open / close the X menu |

X menu (table @0x9518, `{label, handler}`):

| Entry | What it does |
|---|---|
| Storage Manager | LoadProgram `assets/storagetuner`; returns on X |
| Save NVRam to current dir | ramp unit 3 read 32 KB -> E1 `nvram.nvm` (write) -> 16 x (E6 fill 2 KB, E4) -> E5 |
| Load NVRam from current dir | `lmadm -u ram 3 0 nvram`, E1 `nvram.nvm` (read) -> 16 x (E3 2 KB, E7) -> E5 -> ramp unit 3 write -> `fscheck` |
| Dump SYSRom / FontRom / FMVRom | 1 MB from ramp unit 2 / 5 / 4 -> `sysrom.nvm` / `fontrom.nvm` / `fmvrom.nvm` |
| Dump RAM 3mb | memory 0..0x2fffff (ramp unit 0) -> `ram3b.nvm` |
| Update ODE | F0; reply ok -> "Updating. Don't turn off power!", 15 s, re-read EXT_ID; else "Update not found" |
| Device info | PATCHES n, MADAM/CLIO ID registers (0x03300000/0x03400000), "DIPIR" = CRC32 of RAM 0x200-0x3ff, CRC32 of SYSRom/FONTRom/FMVRom ("not present" if absent / mirrored) |
| About | (ODE name), "(c) 2022 Felix Lazarev", "Special thanks to:", "1." .. "4." (no names), "many more ..." |

All files go to the ODE's current directory (the one browsed to). Debug output goes to kprintf/printf only.

## Hidden behaviour

- Konami code (Up Up Down Down Left Right Left Right B A) shows `$c/maus`; the reverse (Down Down Up Up Right Left
  Right Left A B) shows `access.tuner`; Eject press count 69 at boot shows `$c/walker`. Any button returns.
- XPort only (ODE name starting with 'X'): `apply_ram_patches` writes 4 words at RAM 0x5dd8-0x5e00 when the RAM CRC
  at 0x200 is 0x00984c6a (one OS version); two of them make a routine `mov r0,#1; mov pc,lr`. Target routine unknown.
- EXT_ID byte 1 bit 0x20 (media source USB, from the firmware) -> an extra DrawCels and a 2 s delay at startup.

## Checked against outside documents

The byte-identical rebuild proves the code; these checks (2026-10-26) cover the names and explanations on this page.
Sources: **SDK** = 3DO Portfolio 2.5 headers (`io.h`, `device.h`, `event.h`) and examples; **PF** = Portfolio OS
source `xbusdevice.c`; **QS** = Fixel's FZ1 ODE quick-start guide (written for menu r3 and firmware 0.20); **FW** =
the firmware project's `docs/commands.md`.

| Finding | Document evidence | Status |
|---|---|---|
| ode-device cmd 3 = xbus CmdCommand, 7 command bytes | PF: command table entry 3 is `CmdCommand`; commands are sent as 7 bytes | match |
| Data phase with CMD_READ; unit count from `dev_MaxUnitNum` | SDK `io.h` (`CMD_READ` = 1), `device.h`; PF sets `dev_MaxUnitNum` | match |
| ramp unit 3 = NVRAM | SDK example Storage Tuner: `lmadm -a ram 3 0 NVRAM` | match. Units 2, 4, 5 (SYSROM, FMV ROM, FONT ROM): no document found |
| Every ODE command the menu sends | FW command table | match (all documented there) |
| Up/Down, A, B, C, P, X, page buttons | QS; SDK `event.h` button bits | match. Two quirks of the original: the on-screen help says "L page ->" and "R page <-", but L goes to the previous page and R to the next (as QS says); the help says "B back/clr", but B only clears the queue (going up is the `[..]` entry) |
| `[..]` returns to the parent, or refreshes at the top | QS; FW (entry 0 is always `..`) | match |
| Version line `<name>:rN` and `v a.b.c` | QS (`FZ1ODE:r3`, `V 0.20.0`, an older release) | consistent; current firmware puts the Eject press count in the last digit (FW) |
| NVRAM backup into the current directory; Storage Manager exits with X; Update ODE in the X menu | QS | match |
