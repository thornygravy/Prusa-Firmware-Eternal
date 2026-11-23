# Prusa MK3S/MK3S+ community firmware: extra features

An **unofficial** fork of [prusa3d/Prusa-Firmware](https://github.com/prusa3d/Prusa-Firmware) (`MK3` branch) for the
Original Prusa i3 MK3S/MK3S+. It adds a handful of features and optimizations that Prusa has not shipped.
Everything is optional or off by default, and the diff against upstream is kept small and reviewable
(one commit per change).

> **Use at your own risk.** This is not Prusa Research firmware, Prusa does not support it, and nobody has audited
> it. Read "Status and what is NOT tested" before you flash anything.

Base: upstream commit `f3e0dfd` (the 3.14.1 release plus 59 later upstream commits; every functional fix of the
3.14.1 release is included). It reports itself as `3.14.1-<commit count>_<hash>`.

## Which file should I flash?

| File | What it is |
|---|---|
| `MK3S+_custom_no_shaping.hex` | Everything below **except** input shaping. Start here. |
| `MK3S+_custom_SHAPING_experimental.hex` | The same plus experimental input shaping (off at boot). |
| `MK3S+_custom_DIAGNOSTIC.hex` | Shaping build plus the ISR load meter and stack monitor, for testing. |
| `..._noMMU.hex` variants | The same builds with all MMU2/MMU3 code compiled out (about 20 KB smaller). Only for printers **without** an MMU. |

English only (these are the single-language builds). Settings are kept in the printer's EEPROM and flashing does
not normally erase them, but run an `M503` and note your values first.

## What is in it

Flash and RAM costs were measured on the MK3S English build (about 236.3 KB of the 248 KiB limit without shaping).

| Feature | Default | Cost |
|---|---|---|
| **EEPROM backup / restore to SD** (`Settings -> EEPROM`, last entry): Backup, Restore, Undo restore, Verify. CRC32-checked, never restores the serial number, refuses backups from another printer type, snapshots before a restore and rolls back if it fails, will not run while printing. | on | +3.6 KB |
| **Junction deviation**, `M205 J<mm>` (GRBL/Marlin cornering model instead of the X/Y jerk check). `J0` = stock jerk logic, bit for bit. `M500` stores it (see "Settings that stick" below). Typical 0.008 to 0.025. | off (`J0`) | +1.3 KB |
| **Linear Advance K in the Tune menu** (`LA_LIVE_K`, already in upstream but disabled). | on | +0.5 KB |
| **"Print from host"** replaces "Set Ready" and sends `//action:start`, which OctoPrint 1.5+ handles (`REPLACE_SETREADY`). | on | about 0 |
| **PETG preheat 250 C** (was 230) to match the Prusament PETG profile. | on | 0 |
| **`M503` fixes for OctoPrint's "Marlin EEPROM Editor"**: stealth tables are printed before normal ones so saving cannot overwrite normal-mode limits; junction deviation, Linear Advance K and shaping are reported. | on | +0.2 KB |
| **Input shaping (ZV)**, `M593 F<hz> [D<zeta>] [X\|Y]`, `F0` = off. Experimental, see below. | off at boot | +5.1 KB flash, +0.6 KB RAM |
| **Planner: skip unchanged trapezoid recalculation.** The planner redid slow 32-bit divisions for queued blocks whose result had not changed. Same output, about 11 to 21 % less time per G-code line when many moves are queued; only matters for dense/curvy G-code. | on | +76 B |
| Smaller fixes: `chdir()` could overflow a 9-byte buffer; `strlen()` hoisted out of a loop; PINDA temperature cubic evaluated with Horner's method instead of two `pow()` calls; `-flto-partition=none` (-234 B). | on | about -200 B |
| **Settings that stick**: `M500` now also stores junction deviation and input shaping, `M501` reads them back (they are applied at start-up), `M502` resets them. They live in 11 of the 20 EEPROM bytes the stock map lists as free (0x0F73), so the stock layout and `EEPROM_VERSION` are untouched and stock firmware ignores them. While everything is at its default nothing is written, so EEPROM backups of an unmodified setup stay identical. | on (stores only when you use `M500` with these features changed) | +1.2 KB |
| **`M290 Z<mm>`** (babystep): the G-code form of Live Adjust Z for OctoPrint plugins and macros. Same stored offset and limits as the menu (per sheet, -9.99 mm .. 0, only below Z 2 mm, not while homing or paused); a change outside the range is rejected whole; every change writes the EEPROM once. `M290` alone reports the offset. | on | +0.65 KB |
| **`M115` capability lines**: `Cap:BABYSTEPPING`, `JUNCTION_DEVIATION`, `INPUT_SHAPING`, `EEPROM_BACKUP` (only when compiled in), and `PRUSA_MMU2:0` in the NO_MMU build. | on | +0.05 KB |
| **`NO_MMU` build option** (`-DNO_MMU=ON`): removes the MMU2/MMU3 protocol, error screens and tool-change code and replaces the `MMU2` class with an inline stub (`Firmware/mmu2_stub.h`). Behaves like the stock firmware with "MMU Mode: Off"; the Settings entries "MMU" and "Reset MMU" are hidden so nothing can write the MMU-enabled flag. EEPROM layout is untouched, so backups stay compatible. | off (normal build unchanged, byte for byte) | **-20.3 KB flash, -164 B RAM** |
| `DEBUG_ISR_LOAD` (off by default): stepper ISR average/peak load, longest ISR, missed deadlines, printed by `M503`. | off | +0.8 KB when on |

### Input shaping

Every X/Y step is also made once more, half a ringing period later, at a weighted amplitude. The two halves
cancel the frame's resonance. The idea and fixed-point parameters come from Marlin 2.1.2's integrated ZV shaper; the
implementation is reworked for Prusa's stepper interrupt (`Firmware/input_shaping.h`).

* Off at boot unless you stored it with `M500`. `M593 X F40 D0.1` turns it on for X, `M593 F0` turns it off. Nothing is
  stored while it is off, so a printer that never used it is unchanged. You can also keep it in the start G-code.
  Find your frequency with a ringing tower; matching it within about 10 % matters.
* Never applied while homing or probing, and unshaped moves wait for pending echoes.
* **To leave it out of the firmware completely**, comment out `#define INPUT_SHAPING` in `Firmware/Configuration_adv.h`.
  That build was checked to be byte-identical to a build without the code.
* CPU: it adds roughly 15 to 20 points of stepper-interrupt load. In the emulator, 200 mm/s moves went from about 36 %
  to 48 % average load, with a few missed 8 us deadlines at top speed. Use the diagnostic build and read the numbers on
  your own printer before relying on it.
* Known limitations: extrusion runs about 5 ms ahead of the shaped X/Y motion, so Linear Advance K may need a nudge;
  a `G92` within about 12 ms of a move can leave a sub-millimeter offset; crash detection with shaped pulses is untested.

## Status and what is NOT tested

* Everything compiles with zero warnings (avr-gcc 7.3.0 from Ubuntu with `cmake`, not Prusa's pinned toolchain, so
  byte counts may differ slightly from an official build).
* The input shaper has host unit tests (step conservation, reversals, overflow, purge) and an AVR cycle benchmark
  (`tools/input_shaping/`).
* The real firmware was booted in the simavr AVR emulator, and step/direction pin streams were compared with and
  without the changes (`tools/input_shaping/emulator/`). The emulator cannot home (no stallGuard model), so the
  "never shaped while homing" path is **not** verified.
* The planner shortcut has a self-test build (`-DPLANNER_TRAPEZOID_SELFTEST`): 327 skipped recalculations over a
  random stress, zero mismatches.
* **Real-printer testing: add your results here.** At the time of writing the author had just started a first long
  print on the no-shaping build. Nothing here should be called "proven on hardware" until that is filled in.
* Settings that stick, `M290` and the capability lines: tested in the emulator only. Stored values survive a restart
  and come back at boot; `M502` resets, `M501` restores, and `M502` + `M500` erases the entry (EEPROM then equals a
  printer that never used it, apart from the stock thermal-model defaults that `M502` + `M500` always write).
  `M290`: report, relative moves, both out-of-range rejections and the stored offset were checked, and the Z motor made
  exactly the expected 20, 8 and 4 steps. The G-code stream of the earlier test still gives the identical step
  sequence. Not run on a printer.
* `NO_MMU`: the default build's hex is byte-identical with and without these changes. In the emulator, the NO_MMU build
  boots to the same state and produces the same step/direction stream (34,753 step edges, identical sequence, per-step
  timing within about 20 us) for a script that includes `T0`, `T1` and `M702`. Differences in serial output: no
  "MMU is Off" line at boot, more free RAM. Not run on a printer.
* EEPROM backup on a real printer and SD card (reported by the fork owner, MK3S+, on the `no_shaping` build before
  the `NO_MMU` option existed; the NO_MMU builds share the same backup code but were not tested): **Backup** wrote `EEPROM.BAK` and
  **Verify** passed. A **Restore** was run and the settings were correct afterwards, but nothing had been changed
  before the backup, and restore only writes bytes that differ, so that run did not prove it wrote anything.
  A restore that changes a setting back, and **Undo restore**, are still to be tested on hardware.
* Not checked on a printer: crash detection, Power Panic, MMU (the MMU is not a goal of this fork), other printer
  variants (only `MK3S.h` was edited).

## Building

Add `-DNO_MMU=ON` to the CMake configure step for the build without MMU code. Without it the build is identical
to before (checked by comparing hex files).

Follow the upstream instructions in `README.md` (`utils/bootstrap.py`, then CMake). For the diagnostic build add
`-DCUSTOM_COMPILE_OPTIONS="-DDEBUG_ISR_LOAD -DDEBUG_STACK_MONITOR"` to the CMake configure step.
A full git history is needed so the commit-count version number is right (a shallow clone prints a too-small number).

## Credits and license

GPL-3.0, like upstream (see `LICENSE`). Built on:

* **Prusa Research** and contributors: the firmware itself.
* **vdidon** ([vdidon/Prusa-Firmware](https://github.com/vdidon/Prusa-Firmware)): the SD-card EEPROM backup/restore code,
  the `chdir()` overflow fix and the Horner's-method rewrite were taken from or found in that fork.
* **Marlin** ([MarlinFirmware/Marlin](https://github.com/MarlinFirmware/Marlin), GPL-3.0) and GRBL: the junction
  deviation model and the ZV input shaping method and constants.
* **cp2004's OctoPrint-EEPROM-Marlin** plugin: not included, but its parser and save logic were run against the
  firmware's `M503` output to find the `M203`/`M201` ordering problem fixed here.

Much of the code and testing was written with the help of an AI assistant (Claude). Review it like any other
unreviewed patch set.
