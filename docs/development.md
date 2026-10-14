# Development Guide

Build, flash, test, diagnose, and package the firmware from this guide. Implementation decisions are in [Design](design.md), board evidence in [Hardware facts](hardware.md), and receiver operation in the [User manual](user-manual.md).

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
| `src/main.cpp` | Receiver entry point; optional battery demo |
| `src/app/` | Application coordination and drawing |
| `src/drivers/si473x/` | Chip driver, RDS decoders, and bundled SSB patch |
| `src/hal/` | Board resources, GPIO mapping, and NVS access |
| `src/radio/` | Band model, frequency formatting, and settings serialization |
| `src/examples/` | Battery-screen example |
| `tools/` | Host tests, screenshot capture, ruler preview, and firmware packaging |
| `test/native/` | Host C++ tests and Arduino/I2C stubs |
| `test/` | Python capture tests and opt-in device smoke scripts |
| `docs/` | Four guides, hardware references, screenshots, and license texts |

## Build and flash

Run commands from the repository root. Python 3.11 and PlatformIO 6.1.18 match CI:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.1.18
pio run -e ats-mini
pio device list
pio run -e ats-mini -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Replace `/dev/ttyACM0` with the receiver's port: macOS commonly uses `/dev/cu.usbmodem…`, and Windows uses `COM…`. Close other serial consumers before uploading. Normal PlatformIO uploads preserve compatible NVS settings. Prebuilt installation choices are in the [user manual](user-manual.md#installing-or-updating-firmware).

[`platformio.ini`](../platformio.ini) pins `espressif32@6.12.0` and `LovyanGFX@1.2.7`, uses Arduino and the S3 build configuration, and selects 16 MB flash with `default_16MB.csv`. USB CDC is enabled at boot. PSRAM is not required by the firmware; the board ID does not identify the physical radio as an Espressif dev kit.

For the battery-screen example, add `-D BATTERY_DEMO` to `build_flags` temporarily and rebuild. [`src/main.cpp`](../src/main.cpp) selects `src/examples/battery_screen.cpp` instead of the receiver application. Remove the macro to return to normal firmware.

## Validation

Run the host checks from the repository root:

```sh
sh tools/test.sh
```

These cover band/mode constraints, tuning boundaries/BFO, corrected RDS blocks, PS/RT/eRT assembly, CT fields and local-time/holdover rollover, UTF-8 boundaries and malformed input, simulated command/FIFO behavior, persistence validation, and screenshot framing/PNG encoding. Simulated tests do not establish RF sensitivity or audio quality.

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
| `ct`, `ctmjd`, `ctutc`, `ctoffset` | Displayed local clock (or --:--), raw UTC MJD, UTC minutes since midnight, and signed local offset in minutes |
| `rdssync`, `rdsfifo` | Current synchronization and last queried FIFO depth |
| `rdsgroups`, `rdsoverruns` | Groups read and overruns observed since the last RDS invalidation |
| `stored`, `save`, `writes` | Valid stored settings, pending save, and successful writes this boot |

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

Disconnect USB and measure `VCC_BAT` with a meter. Set `batteryCalibration` in [`src/hal/board.h`](../src/hal/board.h) to measured voltage divided by the firmware reading, then rebuild and compare. The battery demo displays supply voltage and, on battery power, estimated percentage. Push the encoder to show ADC details; rotate to adjust the backlight. Audio is muted and the radio rail stays off in this example. The calibration factor defaults to 1.0.

The 1.71/1.702 factors found in some other firmware convert raw ADC codes; applying them to this HAL's calibrated millivolt readings would double-convert the measurement. Calibration does not turn the switched supply measurement into a dedicated cell gauge. See [Hardware facts](hardware.md#battery-indication) and the [battery interpretation](design.md#battery-interpretation).

## Continuous integration and daily releases

[`.github/workflows/firmware.yml`](../.github/workflows/firmware.yml) coordinates build and publication for the `master` branch. Pushes, pull requests, and manual dispatch run `tools/test.sh`, build with pinned PlatformIO 6.1.18, and package firmware. Actions are pinned to verified commit hashes; PlatformIO platforms/packages are cached against `platformio.ini`.

There is no scheduled cron trigger: publication happens after a successful build, not automatically on a day with no build. Successful `master` builds update one prerelease per Shanghai calendar day, tagged `daily-YYYY-MM-DD`. PR builds produce downloadable artifacts only. Publication is serialized and checks that its commit is still the head of `master`, preventing a superseded build from replacing a newer one.

`tools/package_firmware.py` obtains installed dependency paths from PlatformIO, including version-suffixed package directories. It exports:

- The application image for updates at `0x10000`.
- A filled 16 MiB image for complete installation at `0x0`, which resets NVS/data.
- An install ZIP with bootloader (`0x0`), partitions (`0x8000`), boot-app marker (`0xe000`), application (`0x10000`), instructions, source snapshot, the four guides and reference assets, and notices.
- Build metadata and SHA-256 checksums.

The ESP32-S3 ROM image uses DIO/80 MHz upload parameters, matching PlatformIO's Arduino handling of the QIO board configuration. The application configures the board's QIO flash mode. Multi-file or application-only updates preserve compatible NVS; the filled full image overwrites it.

Packaging requires a clean checkout so the archived commit corresponds to the release source. Local validation may use `--allow-dirty`; its metadata marks that result as dirty and its source archive still represents HEAD. Run the tool with the Python environment where PlatformIO is installed:

```sh
python tools/package_firmware.py --output dist
```

## Third-party notices

The [MIT License](../LICENSE) covers this project's original firmware code, tools, tests, and authored documentation. Third-party files and dependencies retain their own terms. Including them here does not grant additional rights or apply MIT to those files.

### SSB patch

`src/drivers/si473x/patches/ssb.h` is an unchanged copy of the [PU2CLR compressed patch file](https://github.com/pu2clr/SI4735/blob/123eccc730dc03c0ccc28526cbea6e305da7d2fc/src/patch_ssb_compressed.h), derived from `amrx_6_0_1_ssbrx_patch_init_0xA902.csg` published by Vadim Afonkin and adapted/compressed by Ricardo Caratti.

Its full upstream notice is preserved in the file. The notice states that the patch is not part of the SI4735 library and discourages commercial use. The library's license must not be taken as a license for this patch. This repository makes no additional claim about patch usage permissions.

### Hardware references and photographs

`docs/SCH_SI4732_1.2.pdf` and `docs/PCB_SI4732_1.2.pdf` are Sunnygold design attachments, copied unchanged from the [hardware mirror](https://github.com/esp32-si4732/esp32-si4732-oshwhub/tree/6105532703f85a0275950600ff4f040d1f831b24/FILES/2025-01-22). The mirror identifies Sunnygold hardware/software as CC BY-NC-SA 3.0. They are excluded from this project's MIT grant; preserve Sunnygold attribution and upstream terms.

`docs/images/radio-front.jpg`, `radio-chinese-ui.png`, and `pcb-buttons.jpg` come from the [ATS Mini community documentation](https://github.com/esp32-si4732/ats-mini/tree/ab4f5dc8120767cbb134b70096929f74c49df3e7/docs/source/_static). These photographs retain their upstream ownership; a uniform image license has not been established here. Their provenance and original filenames are recorded in [Hardware facts](hardware.md#sources-and-reference-assets). They are excluded from MIT.

`docs/images/screen-frequency.png` and `screen-rds.png` are framebuffer exports of this project's firmware, produced by `tools/capture_screen.py`. RDS strings in a screenshot are received broadcast content, not project-authored text or an endorsement of the broadcaster.

### Display library and fonts

The build downloads [LovyanGFX 1.2.7](https://github.com/lovyan03/LovyanGFX/tree/1.2.7). Its upstream license file includes FreeBSD/BSD and MIT notices for LovyanGFX and incorporated library portions. The license text is reproduced as [LovyanGFX notices](licenses/LovyanGFX.txt), with line endings normalized.

The efont Chinese font comes through LovyanGFX. Copyright 2000–2001 /efont/ The Electronic Font Open Laboratory. Its redistribution conditions and disclaimer are reproduced unchanged in [efont notices](licenses/efont.txt).

PlatformIO also downloads the Arduino-ESP32 framework and Espressif build tools. Those components retain their upstream licenses; distributing a firmware binary does not replace dependency notices with this project's MIT license.

### Reference projects

Thanks to Sunnygold for the hardware, the ATS Mini community and Chinese firmware authors for board references, Vadim Afonkin and Ricardo Caratti/PU2CLR for SSB resources, and lovyan03 and font contributors for the display library. This project's application and driver logic are original implementations; only the SSB patch is bundled from PU2CLR. Reference-project terms do not imply a blanket license for their individual assets.
