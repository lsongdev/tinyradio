# Development Guide

Build, flash, test, diagnose, and package the firmware from this guide. Implementation decisions are in [Design](design.md), board evidence in [Hardware facts](hardware-ats.md), and receiver operation in the [User manual](user-manual.md).

- [Source layout](#source-layout)
- [Build and flash](#build-and-flash)
- [Validation](#validation)
- [Serial diagnostics](#serial-diagnostics)
- [Screen capture and ruler preview](#screen-capture-and-ruler-preview)
- [Battery calibration](#battery-calibration)
- [Continuous integration and daily releases](#continuous-integration-and-daily-releases)
- [Third-party notices](#third-party-notices)

## Source layout

| Path | Content |
| --- | --- |
| `src/main.cpp` | Arduino entry point; forwards `setup()`/`loop()` to the receiver |
| `src/app/` | Application coordination, RDS clock adapter, and serial diagnostics |
| `src/ui/` | Screen layout, drawing, and screenshot export behind a C API |
| `src/boards/` | Board selector and one directory per supported board |
| `src/drivers/si473x/` | Chip driver, RDS decoders, and bundled SSB patch |
| `src/hal/` | Board-independent ESP32 button, encoder, I2C, NVS, serial, and timer |
| `src/services/` | Shared device clock, independent of RDS or future network sources |
| `src/radio/` | Band model, frequency formatting, and settings serialization |
| `tools/` | Host tests, screenshot capture, ruler preview, and firmware packaging |
| `test/native/` | Host C99 tests and Arduino stubs |
| `test/` | Python capture tests and opt-in device smoke scripts |
| `docs/` | Four guides, hardware references, screenshots, and license texts |

### Languages

The firmware is C. C++ appears only where a dependency requires it: LovyanGFX drawing (`src/ui/ui.cpp`, each board's `display.cpp`), Arduino's `Serial` object (`src/hal/serial.cpp`), and `src/main.cpp`, because Arduino declares `setup()`/`loop()` with C++ linkage. Each of these files implements or calls a C interface and includes C headers inside one `extern "C" { ... }` block; C headers carry no `__cplusplus` guards. Headers ending in `.h` are C; `src/boards/display.hpp` is the only C++ header.

### Adding a board

Each board lives in `src/boards/<name>/` and is selected at compile time; there are no virtual interfaces. A new board needs:

1. A PlatformIO environment that defines `BOARD_<NAME>` and sets `build_src_filter = +<*> -<boards/*/> +<boards/<name>/>`.
2. A branch for that macro in [`src/boards/board.h`](../src/boards/board.h).
3. `board.h` declaring the C API of [`ats_mini/board.h`](../src/boards/ats_mini/board.h): the `BOARD_*` constants and every `board_*()` function. Use a distinct `BOARD_STORAGE_NAMESPACE`; it must not change once units have saved settings.
4. `display.cpp` configuring the LovyanGFX panel and defining `board_display_begin()`, `board_display_set_brightness()`, and `board_display_device()` from [`src/boards/display.hpp`](../src/boards/display.hpp).

Reuse `src/hal/` pieces where the hardware matches. Keep board-only parts (I/O expanders, RF front ends, clocks) inside the board directory, behind `board_*()` functions, so the driver and application stay unchanged.

## Build and flash

Run commands from the repository root. Python 3.11 and PlatformIO 6.1.18 match CI. The [`Makefile`](../Makefile) holds the build steps CI uses:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.1.18
make                                         # build every board
make test                                    # host tests
pio device list
make flash BOARD=ats-mini PORT=/dev/ttyACM0  # build and flash one board
make monitor BOARD=ats-mini PORT=/dev/ttyACM0
make package PYTHON=python                   # build and package every board into dist/
```

`BOARD` is `ats-mini` or `v5a` and has no default, so one board's image is never flashed to the other. `PORT` is optional when only one radio is connected. `BOARDS` limits `make`/`make package` to some boards. `PYTHON` must be able to import PlatformIO (the venv above, or `~/.platformio/penv/bin/python` with the installer's PlatformIO).

The GeekNest V5A port is in bring-up and builds as `v5a` (classic ESP32, USB-UART bridge, upload at 115200). It latches power, switches the amplifier through the XL9555, starts the SI4732's 32.768 kHz reference on Si5351 CLK1 (`BOARD_RADIO_EXTERNAL_CLOCK`; the driver then powers up without XOSCEN and sets REFCLK), and draws the shared receiver UI in the top 320 × 170 of the 320 × 240 panel. The 70 rows below are a separately buffered board-extras area with the date, time, and SHT30 temperature/humidity; two buffers keep the whole panel flicker-free without PSRAM, which stays off. Side keys report only positions A/B/C (`board_keys_read()`); the receiver binds A to a short press and B/C to volume. The PCF8523 keeps UTC (`BOARD_HAS_RTC`): it seeds the device clock at boot, RDS CT writes it back, and the UTC offset is stored under the NVS key `utcoffset`. AIR and RF routing go through `board_frontend_set()` (see [Design](design.md#frequency-model)); tuning and LO lock at 118/118.001/137 MHz are verified, aviation reception is not. Board facts and constraints are in [V5A hardware facts](hardware-v5a.md).

Replace `/dev/ttyACM0` with the receiver's port: macOS commonly uses `/dev/cu.usbmodem…`, and Windows uses `COM…`. Close other serial consumers before uploading. Normal PlatformIO uploads preserve compatible NVS settings. Prebuilt installation choices are in the [user manual](user-manual.md#installing-or-updating-firmware).

[`platformio.ini`](../platformio.ini) pins `espressif32@6.12.0` and `LovyanGFX@1.2.7`, selects the board with `-D BOARD_ATS_MINI` and `build_src_filter`, uses Arduino and the S3 build configuration, and selects 16 MB flash with `default_16MB.csv`. USB CDC is enabled at boot. PSRAM is not required by the firmware; the board ID does not identify the physical radio as an Espressif dev kit.


## Validation

Run the host checks from the repository root:

```sh
sh tools/test.sh
```

These cover band/mode constraints, tuning boundaries/BFO, corrected RDS blocks, PS/RT/eRT assembly, CT fields, retained time across tuning/seek, a simulated NTP update through the shared API, local-time rollover and 64-bit uptime, UTF-8 boundaries and malformed input, simulated command/FIFO behavior, persistence validation, and screenshot framing/PNG encoding. Simulated tests do not establish RF sensitivity or audio quality.

Device smoke scripts in `test/*_smoke.py` exercise tuning, seek, reception controls, and persistence. They change settings; inspect each script before running. Tests originally written for default settings may need that configuration; rebooting now preserves settings. Do not use them merely to obtain screenshots.

## Serial diagnostics

At 115200 baud, commands are single characters without a required newline. `?` and `S` only read state; the other commands can change tuning or saved settings.

| Command | Action |
| --- | --- |
| `?` | Report receiver, RDS, focus, and storage state |
| `S` | Export full-screen RGB888 snapshot |
| `0`, `1`, `2`, `3` | Select LW, MW, SW, VHF |
| `a`, `l`, `u`, `f` | Select AM, LSB, USB, FM where allowed |
| `+`, `-` | Adjust frequency, or request seek in seek mode |
| `[`, `]` | Go to current band's lower/upper endpoint |
| `e`, `h` | Short/long encoder press |
| `>`, `<` | Adjust focused item |
| `m` | Toggle mute |
| `r` | Reset chip and retry |

| Status fields | Meaning |
| --- | --- |
| `requested`, `confirmed` | Target frequency and completed tune result, in Hz |
| `ps`, `rt`, `ert` | Independently confirmed station name, ordinary RT, and eRT |
| `ertgroup`, `ertflags` | Five-bit type/version code (-1 when unannounced), and 3A configuration flags |
| `clock`, `clocksource`, `clockutc`, `clockoffset` | Shared local clock (or --:--), last synchronization source, running Unix UTC seconds, and retained offset in minutes |
| `ctmjd`, `ctutc`, `ctoffset` | Latest raw CT sample: UTC MJD, UTC minutes since midnight, and signed local offset; these reset when the radio invalidates station data |
| `rdssync`, `rdsfifo` | Current synchronization and last queried FIFO depth |
| `rdsgroups`, `rdsoverruns` | Groups read and overruns observed since the last RDS invalidation |
| `stored`, `save`, `writes` | Valid stored settings, pending save, and successful writes this boot |
| `supply`, `adc` | Calibrated supply estimate in volts, and averaged pin reading in millivolts |

A successful tune or `error=OK` does not establish that a station exists or that its audio is intelligible.

## Screen capture and ruler preview

For a full device screenshot, connect USB and close the serial monitor:

```sh
python -m pip install -r tools/requirements.txt
python tools/capture_screen.py --port /dev/ttyACM0 --output screenshot.png
```

Add `--wait 5` to allow the screen to settle before capture. This exports the last presented 320 × 170 frame without tuning or changing settings. It briefly pauses the UI during transfer. Python's standard library writes the lossless PNG; pyserial handles the serial connection. The transfer format is described in [Design](design.md#full-screen-capture).

The offline ruler renderer needs only Python 3:

```sh
python tools/render_ruler.py --band SW --frequency 26.1MHz --step 100k --output ruler.svg
```

It previews the ruler geometry, not a live full screen. Its band ranges and geometry mirror the firmware and need updating together when those change. Test both sides of a boundary, including the five blank minor divisions between each endpoint and its band marker. Public screenshots should use suitable received text rather than phone numbers or other personal details.

## Battery calibration

Disconnect USB and measure `VCC_BAT` with a meter. Read the firmware's `supply` (volts) and `adc` (millivolts at the pin) from the serial `?` report. Set `BATTERY_CALIBRATION` in [`src/boards/ats_mini/pins.h`](../src/boards/ats_mini/pins.h) to measured voltage divided by that `supply` reading, then rebuild and compare. The calibration factor defaults to 1.0. The measurement is taken with the receiver running, so it includes the radio and audio load.

The 1.71/1.702 factors found in some other firmware convert raw ADC codes; applying them to this HAL's calibrated millivolt readings would double-convert the measurement. Calibration does not turn the switched supply measurement into a dedicated cell gauge. See [Hardware facts](hardware-ats.md#battery-indication) and the [battery interpretation](design.md#battery-interpretation).

## Continuous integration and daily releases

[`.github/workflows/firmware.yml`](../.github/workflows/firmware.yml) coordinates build and publication for the `master` branch. Pushes, pull requests, and manual dispatch run `make test`, `make build` and `make package` with pinned PlatformIO 6.1.18, producing firmware for every board. Actions are pinned to verified commit hashes; PlatformIO platforms/packages are cached against `platformio.ini`.

There is no scheduled cron trigger: publication happens after a successful build, not automatically on a day with no build. Successful `master` builds update one prerelease per Shanghai calendar day, tagged `daily-YYYY-MM-DD`. PR builds produce downloadable artifacts only. Publication is serialized and checks that its commit is still the head of `master`, preventing a superseded build from replacing a newer one.

`tools/package_firmware.py` obtains installed dependency paths from PlatformIO, including version-suffixed package directories. Its `BOARDS` table holds each board's file prefix (`ats-mini`, `geeknest-v5a`), chip, flash size and frequency, bootloader offset, and flashing notes. For each board it exports:

- The application image for updates at `0x10000`.
- A filled full-size image (16 MiB ATS Mini, 4 MiB V5A) for complete installation at `0x0`, which resets NVS/data.
- An install ZIP with bootloader (`0x0` on the ESP32-S3, `0x1000` on the ESP32), partitions (`0x8000`), boot-app marker (`0xe000`), application (`0x10000`), instructions, source snapshot, the guides and reference assets, and notices.
- Build metadata and SHA-256 checksums.

ROM images use DIO upload parameters (80 MHz ATS Mini, 40 MHz V5A), matching PlatformIO's Arduino handling; the ATS Mini application then configures its QIO flash mode. Multi-file or application-only updates preserve compatible NVS; the filled full image overwrites it.

Packaging requires a clean checkout so the archived commit corresponds to the release source. Local validation may use `--allow-dirty`; its metadata marks that result as dirty and its source archive still represents HEAD. Run the tool with the Python environment where PlatformIO is installed:

```sh
make package PYTHON=python                    # or: python tools/package_firmware.py --output dist
make package PYTHON=python ALLOW_DIRTY=1      # local check of uncommitted work
```

## Third-party notices

The [MIT License](../LICENSE) covers this project's original firmware code, tools, tests, and authored documentation. Third-party files and dependencies retain their own terms. Including them here does not grant additional rights or apply MIT to those files.

### SSB patch

`src/drivers/si473x/patches/ssb.h` is an unchanged copy of the [PU2CLR compressed patch file](https://github.com/pu2clr/SI4735/blob/123eccc730dc03c0ccc28526cbea6e305da7d2fc/src/patch_ssb_compressed.h), derived from `amrx_6_0_1_ssbrx_patch_init_0xA902.csg` published by Vadim Afonkin and adapted/compressed by Ricardo Caratti.

Its full upstream notice is preserved in the file. The notice states that the patch is not part of the SI4735 library and discourages commercial use. The library's license must not be taken as a license for this patch. This repository makes no additional claim about patch usage permissions.

### Hardware references and photographs

`docs/SCH_SI4732_1.2.pdf` and `docs/PCB_SI4732_1.2.pdf` are Sunnygold design attachments, copied unchanged from the [hardware mirror](https://github.com/esp32-si4732/esp32-si4732-oshwhub/tree/6105532703f85a0275950600ff4f040d1f831b24/FILES/2025-01-22). The mirror identifies Sunnygold hardware/software as CC BY-NC-SA 3.0. They are excluded from this project's MIT grant; preserve Sunnygold attribution and upstream terms.

`docs/images/radio-front.jpg`, `radio-chinese-ui.png`, and `pcb-buttons.jpg` come from the [ATS Mini community documentation](https://github.com/esp32-si4732/ats-mini/tree/ab4f5dc8120767cbb134b70096929f74c49df3e7/docs/source/_static). These photographs retain their upstream ownership; a uniform image license has not been established here. Their provenance and original filenames are recorded in [Hardware facts](hardware-ats.md#sources-and-reference-assets). They are excluded from MIT.

`docs/images/screen-frequency.png` and `screen-rds.png` are framebuffer exports of this project's firmware, produced by `tools/capture_screen.py`. RDS strings in a screenshot are received broadcast content, not project-authored text or an endorsement of the broadcaster.

### Display library and fonts

The build downloads [LovyanGFX 1.2.7](https://github.com/lovyan03/LovyanGFX/tree/1.2.7). Its upstream license file includes FreeBSD/BSD and MIT notices for LovyanGFX and incorporated library portions. The license text is reproduced as [LovyanGFX notices](licenses/LovyanGFX.txt), with line endings normalized.

The efont Chinese font comes through LovyanGFX. Copyright 2000–2001 /efont/ The Electronic Font Open Laboratory. Its redistribution conditions and disclaimer are reproduced unchanged in [efont notices](licenses/efont.txt).

PlatformIO also downloads the Arduino-ESP32 framework and Espressif build tools. Those components retain their upstream licenses; distributing a firmware binary does not replace dependency notices with this project's MIT license.

### Reference projects

Thanks to Sunnygold for the hardware, the ATS Mini community and Chinese firmware authors for board references, Vadim Afonkin and Ricardo Caratti/PU2CLR for SSB resources, and lovyan03 and font contributors for the display library. This project's application and driver logic are original implementations; only the SSB patch is bundled from PU2CLR. Reference-project terms do not imply a blanket license for their individual assets.
