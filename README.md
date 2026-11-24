<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/eternal-logo-dark.png">
    <img alt="Prusa Firmware Eternal" src="docs/images/eternal-logo-light.png" width="420">
  </picture>
</p>

> ## ⚠️ Unofficial community fork. Not made, reviewed, endorsed or supported by Prusa Research.
> **Prusa Firmware Eternal** is an independent hobby project by a printer owner (with AI assistance). Prusa Research
> has no involvement in it. "Prusa" is a trademark of Prusa Research a.s. and is used here only to say which printers
> and which upstream firmware this is based on. Do not contact Prusa support about problems with this firmware, and
> expect that flashing it may affect your warranty and support eligibility. You flash it at your own risk.
>
> It adds extra MK3S/MK3S+ features (SD-card EEPROM backup, junction deviation, experimental input shaping, a
> no-MMU build and more) on top of upstream [Prusa-Firmware](https://github.com/prusa3d/Prusa-Firmware).
> Read [CUSTOM_CHANGES.md](CUSTOM_CHANGES.md) before flashing. For official firmware, use
> [Prusa Drivers](https://www.prusa3d.com/drivers/).

# Prusa Firmware Eternal

Prusa Firmware Eternal is a community-modified version of the firmware for the **Original Prusa i3 MK3S / MK3S+**,
made by an MK3S+ owner who likes these printers and wanted to keep improving them.

It is based on [Prusa-Firmware](https://github.com/prusa3d/Prusa-Firmware) (upstream commit `f3e0dfd`: the 3.14.1
release plus 59 later upstream commits), which is in turn based on [Marlin 1.0.x](https://github.com/MarlinFirmware/Marlin/)
by Scott Lahteine (@thinkyhead) et al. Like them it is distributed under the [GNU GPL 3 license](LICENSE).

**Target printer:** MK3S / MK3S+ (the `MK3S` variant, English only). Other variants in the tree (MK3, MK2.5/S, ...)
come from upstream and have not been built or tested here.

## What is different from Prusa's firmware

- SD-card **EEPROM backup / restore** and compatibility with OctoPrint's Marlin EEPROM Editor
- **Junction deviation** (`M205 J`), `M290` live Z babystep, settings that stick through `M500`/`M501`/`M502`
- Optimisations in the planner and elsewhere to free flash and CPU time
- A **`NO_MMU` build** for printers without an MMU (about 20 KB smaller)
- **Experimental** ZV input shaping (`M593`), off unless you turn it on
- English only, honest version numbers (3.14.1-*commit count*), extra `M115` capability lines

[CUSTOM_CHANGES.md](CUSTOM_CHANGES.md) has the full list, which hex file to pick, what has and has not been tested
on a real printer, and credits. Read it before flashing.

## Getting the firmware

Pre-built `.hex` files are attached to the [Releases](../../releases) page of this repository. They are built by the
maintainer, not by Prusa, and not signed. If you would rather not trust a binary, build it yourself (below).

To flash a `.hex` file, use the firmware flashing tool in PrusaSlicer (Configuration, then Flash Printer Firmware) or
any other method you already use for custom firmware. Run `M503` first and keep the output so you can restore your
settings if anything goes wrong.

## Support and bug reports

- This project is **not** supported by Prusa Research. Please do not report its problems to Prusa.
- Report bugs and ask questions in this repository's issues. Say which hex file or commit you used, and whether you
  have an MMU.
- It is a hobby project, so expect slow and best-effort answers.
- If a bug also exists in stock firmware, reporting it upstream at
  [prusa3d/Prusa-Firmware](https://github.com/prusa3d/Prusa-Firmware) is the right place for that part.

For official, supported firmware go to [Prusa Drivers](https://www.prusa3d.com/drivers/).


# Build
## Linux
Build with [CMake](#cmake) (recommended). [PF-build](#pf-build) is a helper script inherited from upstream and is
less tested with this fork.

### CMake
#### Quick-start
After installing git and a recent version of python 3 all you have to do is:

    # clone the repository (keep the full history: the version number is the commit count)
    git clone https://github.com/thornygravy/Prusa-Firmware-Eternal
    cd Prusa-Firmware-Eternal

    # automatically setup dependencies
    ./utils/bootstrap.py

    # configure and build the MK3S English firmware
    mkdir build
    cd build
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/AvrGcc.cmake -DFW_VARIANTS=MK3S
    ninja MK3S_ENGLISH

Fork-specific options:

- `-DNO_MMU=ON`: build without MMU2/MMU3 code (only for printers without an MMU).
- Input shaping is compiled in by default. To leave it out completely, comment out `#define INPUT_SHAPING` in
  `Firmware/Configuration_adv.h`.
- Diagnostic build: `-DCUSTOM_COMPILE_OPTIONS="-DDEBUG_ISR_LOAD -DDEBUG_STACK_MONITOR"`.

#### Detailed CMake guide
Building with cmake requires:

- cmake >= 3.22.5
- ninja >= 1.12.1 (optional, but recommended)

Python >= 3.8 is also required with the following modules:

- pyelftools (package `python3-pyelftools`)
- polib (package `python3-polib`)
- regex (package `python3-regex`)

Additionally `gettext` is required for translators.

Assuming a recent Debian/Ubuntu distribution, install the dependencies globally with:

    sudo apt-get install cmake ninja python3-pyelftools python3-polib python3-regex gettext

When using a recent Fedora(non-atomic)/RHEL distribution, install the dependencies globally with:

    sudo dnf install cmake ninja-build python3-pyelftools python3-polib python3-regex gettext

When using a Fedora Atomic/UBlue distribution use `rpm-ostree install --allow-inactive` instead of `sudo dnf install`

The firmware depends on a pinned version of `avr-gcc` and the external `prusa3dboards` package. These can be setup using `./utils/bootstrap.py`:

    # automatically setup dependencies
    ./utils/bootstrap.py

which will download and unpack them inside the `.dependencies` directory. `./utils/bootstrap.py` will also install `cmake`, `ninja` and the required python packages if missing, although installing those through the system's package manager is usually preferred.

You can then proceed by creating a build directory, configure for AVR and build:

    # configure
    mkdir build
    cd build
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/AvrGcc.cmake

    # build
    ninja

By default all variants are built. There are several ways to restrict the build for development. During configuration you can set:

- `cmake -DFW_VARIANTS=variant`: comma-separated list of variants to build. This is the file name as present in `Firmware/variants` without the final `.h`.
- `cmake -DMAIN_LANGUAGES=languages`: comma-separated list of ISO language codes to include as main translations.
- `cmake -DCOMMUNITY_LANGUAGES=languages`: comma-separated list of ISO language codes to include as community translations.

When building the following targets are available:

- `ninja ALL_MULTILANG`: build all multi-language targets (default)
- `ninja ALL_ENGLISH`: build all single-language targets
- `ninja ALL_FIRMWARE`: build all single and multi-language targets
- `ninja VARIANT_ENGLISH`: build the single-language version of `VARIANT`
- `ninja VARIANT_MULTILANG`: build the multi-language version of `VARIANT`
- `ninja check_lang`: build and check all language translations
- `ninja check_lang_ISO`: build and check all variants with language `ISO`
- `ninja check_lang_VARIANT`: build and check all languages for `VARIANT`
- `ninja check_lang_VARIANT_ISO`: build and check language `ISO` for `VARIANT`


#### Automated tests
Automated tests are built with cmake by configuring for the current host:

    # clone the repository
    git clone https://github.com/thornygravy/Prusa-Firmware-Eternal
    cd Prusa-Firmware-Eternal

    # automatically setup dependencies
    ./utils/bootstrap.py

    # configure and build
    mkdir build
    cd build
    cmake .. -G Ninja
    ninja

    # run the tests
    ctest


### PF-build
PF-build is recommended for users without development experience. Download or clone the repository,
then run PF-build and simply follow the instructions:

    cd Prusa-Firmware-Eternal
    ./PF-build.sh

PF-build currently assumes a Debian/Ubuntu (or derivative) distribution.


## Windows
### Visual Studio Code (VSCode)
#### Prerequisites

* [Visual Studio Code](https://code.visualstudio.com/)
* [CMake Tools plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
* [Python](https://www.python.org/)
* [Git Bash](https://git-scm.com/downloads)

#### First time setup

Start by cloning this repository

    git clone https://github.com/thornygravy/Prusa-Firmware-Eternal

Open the `Prusa-Firmware-Eternal` folder in VScode.

Open a new terminal in VScode (Terminal→New Terminal) and run

    python .\utils\bootstrap.py

This will download all dependencies required to build the firmware. You should see a `.dependencies` folder in the repository folder.

Reload VScode. If all works correctly you should see the VScode automatically configuring the CMake project for you. If this doesn't happen you likely need to set the CMake kit; This can be done in two ways:

1. Type `Ctrl+Shift+P` and search for `CMake: Select a Kit`. Select `avr-gcc`. If none appear, Scan for kits first.
2. If 1) does not work for some reason, as a last resort you can edit the CMake Tools settings. Search for "Additional Kits" and add `.vscode/cmake-kits.json` to the list.

After updating the kit, you may need to reload VScode.

#### Building

To start building a firmware, click the CMake Tools plugin icon on the far left side. You will get a very large list of targets to build. Find the firmware you'd like to build (like `MK3S_ENGLISH`) and select the small icon which shows "Build" when hovered over.

The built .hex file can then be found in folder `build`


# Credits and license

GPL-3.0, same as upstream. Built on the work of Prusa Research and its contributors, Marlin, GRBL, vdidon (SD EEPROM
backup code) and cp2004's OctoPrint-EEPROM-Marlin plugin. Much of the code and testing of this fork was done with an
AI assistant (Claude), so review it like any other unreviewed patch set. Details are in
[CUSTOM_CHANGES.md](CUSTOM_CHANGES.md).
