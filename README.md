# ATS Mini

[![Firmware](https://github.com/lsongdev/ats-mini/actions/workflows/firmware.yml/badge.svg)](https://github.com/lsongdev/ats-mini/actions/workflows/firmware.yml)

**A compact AM/FM/SSB receiver firmware for the Sunnygold SI4732 Mini Radio.**

Continuous tuning across LW, MW, SW, and VHF/FM, controlled by one encoder. Built for the ESP32-S3, with RDS station names and radio text, adjustable reception settings, and persistent band memories.

<p align="center">
  <img src="docs/images/screen-frequency.png" width="640" alt="Full receiver screen showing frequency, reception controls, signal status, and tuning scale">
  <br>
  <img src="docs/images/screen-rds.png" width="640" alt="Full receiver screen showing the received RDS station name in the shared main area">
</p>

*Actual 320 × 170 framebuffer captures exported from the receiver over USB.*

## Features

- **AM, FM, LSB, and USB:** SSB patch loaded and verified when required.
- **Continuous tuning:** cross adjacent bands; stop at the overall limits without wrapping.
- **Seek:** long-press Frequency, then rotate to search; short-press to stop and tune manually.
- **RDS:** PS station names and RT messages share the main display, with independent scrolling when both are present.
- **Reception controls:** bandwidth, automatic gain/manual attenuation, soft mute, and tuning step.
- **Remembered settings:** band, per-band frequency/mode/step, reception controls, volume, brightness, and mute.
- **Device screenshots:** export the entire displayed framebuffer as a PNG.

| Band | Range | Modes |
| --- | --- | --- |
| LW | 153–279 kHz | AM / LSB / USB |
| MW | 520–1710 kHz | AM / LSB / USB |
| SW | 2.3–26.1 MHz | AM / LSB / USB |
| VHF | 64–108 MHz | FM |

AIR is displayed as unavailable: this hardware does not cover VHF aviation reception. See [hardware facts](docs/hardware.md) for the supported board and reference schematics.

## Download firmware

Successful `master` builds publish a [daily prerelease](https://github.com/lsongdev/ats-mini/releases), dated in **Asia/Shanghai**. New successful builds replace that day's downloads. Pull requests build and upload Actions artifacts without publishing a release.

| Download | Use |
| --- | --- |
| `ats-mini-firmware.bin` | Application update at `0x10000`, preserving compatible settings |
| `ats-mini-install.zip` | Flashing files, instructions, commit source snapshot, and dependency notices |
| `ats-mini-full.bin` | Complete 16 MiB installation at `0x0`; resets settings and data |
| `ats-mini-build-info.json` / `ats-mini-sha256sums.txt` | Commit, dependency versions, and SHA-256 hashes |

Follow `FLASHING.txt` inside the install package for esptool commands. Confirm that your hardware matches the [reference board](docs/hardware.md).

## Quick start

Install [PlatformIO](https://platformio.org/), connect the receiver by USB, and locate its serial port:

```sh
pio device list
pio run -e ats-mini
pio run -e ats-mini -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Use your device's port in place of `/dev/ttyACM0`; macOS commonly uses `/dev/cu.usbmodem…`, and Windows uses `COM…`. Normal uploads preserve saved settings in NVS.

Rotate the encoder to adjust the focused item. Short-press to cycle:

**Band → Frequency → Mode → BW → AGC → SM → Step → Volume → Brightness**

Focus starts on Frequency. Long-press Volume to mute. Read the [user manual](docs/user-manual.md) for steps, signal indicators, RDS behavior, and troubleshooting.

## Export a screenshot

Close the serial monitor first. The capture tool requires Python 3 and pyserial:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/capture_screen.py --port /dev/ttyACM0 --output screenshot.png
```

Add `--wait 5` to let the screen settle before capture. This reads the last displayed frame without changing tuning or saved settings. The UI briefly pauses during transfer; the tool writes a lossless, native-resolution PNG without requiring Pillow.

An offline [ruler renderer](tools/render_ruler.py) is also available for inspecting band boundaries:

```sh
python3 tools/render_ruler.py --band SW --frequency 26.1MHz --step 100k --output ruler.svg
```

## Documentation

| Document | Purpose |
| --- | --- |
| [Hardware facts](docs/hardware.md) | Schematics, GPIO mapping, power, display, battery measurement, and confirmed hardware observations |
| [Design](docs/design.md) | Architecture, tuning and seek state, UI layout, RDS decoding, persistence, screenshot protocol, and validation |
| [User manual](docs/user-manual.md) | Everyday controls, display meanings, saved settings, and troubleshooting |

## Source layout

```text
src/app/              Receiver UI and application coordination
src/drivers/si473x/   Chip commands, tuning, reception settings, RDS, SSB patch
src/hal/             Board resources: display, encoder, button, audio, battery, I2C, NVS
src/radio/           Band model, frequency formatting, settings serialization
src/examples/        Battery-screen example
tools/               USB screenshot capture and offline ruler rendering
test/                Native tests and opt-in device smoke tests
docs/                Hardware facts, design, user manual, and reference assets
```

Run `sh tools/test.sh` for native and screenshot-tool checks. Development and test commands are documented in [Design](docs/design.md#validation).

## License and credits

Original firmware code, tools, tests, and documentation are available under the [MIT License](LICENSE), copyright Lsong and contributors.

The bundled SSB patch, imported hardware PDFs, and reference photographs retain their upstream notices and terms. They are **not relicensed under MIT**. See [third-party notices](THIRD_PARTY_NOTICES.md) for Sunnygold, Vadim Afonkin, PU2CLR, hardware/community references, and dependency credits.
