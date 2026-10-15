# User Manual

- [Getting started](#getting-started)
- [Installing or updating firmware](#installing-or-updating-firmware)
- [Bands and modes](#bands-and-modes)
- [Encoder controls](#encoder-controls)
- [Reading the screen](#reading-the-screen)
- [RDS station names and text](#rds-station-names-and-text)
- [Reception controls](#reception-controls)
- [Saved settings](#saved-settings)
- [Battery estimate](#battery-estimate)
- [Screenshots](#screenshots)
- [Troubleshooting](#troubleshooting)

## Getting started

Switch on the receiver. It restores saved settings and starts with Frequency focused, ready for manual tuning. Rotate the encoder to tune, short-press to select another control, and rotate to change that control.

For board-specific information, see [Hardware facts](hardware-ats.md). Source build commands are in [Development](development.md#build-and-flash).

## Installing or updating firmware

Download the install ZIP from the [releases page](https://github.com/lsongdev/ats-mini/releases). Successful `master` builds update the prerelease for that Shanghai calendar day. Check your board against the [supported hardware](hardware-ats.md) and follow `FLASHING.txt` in the ZIP.

| File | Purpose |
| --- | --- |
| `ats-mini-firmware.bin` | Application update at `0x10000`; preserves compatible saved settings |
| `ats-mini-full.bin` | Complete 16 MiB installation at `0x0`; resets saved settings/data |
| `ats-mini-install.zip` | Flashing files, instructions, source snapshot, documentation, and notices |
| `ats-mini-build-info.json` | Source commit, dependency versions, and build identity |
| `ats-mini-sha256sums.txt` | SHA-256 hashes for the downloads |

For example, after extracting the ZIP and installing Python:

```sh
python -m pip install esptool==4.9.0
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x10000 ats-mini-firmware.bin
```

GeekNest V5A files use the prefix `geeknest-v5a-` instead, with a 4 MiB full image, `--chip esp32` and `--baud 115200`; its `FLASHING.txt` has the exact commands. Back up the V5A's original flash before the first installation.

Replace `PORT` with the receiver's USB serial port. Close other serial monitors first. The command above updates only the application. Use the complete-installation or multi-file commands in `FLASHING.txt` for those installation types, and use files from the same package.

## Bands and modes

| Band | Frequency range | Available modes |
| --- | --- | --- |
| LW | 153–279 kHz | AM, LSB, USB |
| MW | 520–1710 kHz | AM, LSB, USB |
| SW | 1.711–30 MHz (beyond the chip's 2.3–26.1 MHz specification at both ends) | AM, LSB, USB |
| VHF | 64–108 MHz | FM |
| AIR | 118–137 MHz, GeekNest V5A only; grayed out elsewhere | AM (no seek) |

Unavailable bands and modes are gray and skipped during selection. The SSB patch enables LSB/USB; it does not enable VHF aviation reception.

Manual tuning crosses from one band to the next at its endpoint. It stops at the overall limits of 153 kHz and 108 MHz, without wrapping. Directly selecting a band restores its remembered frequency, mode, and step.

## Encoder controls

| Gesture | Result |
| --- | --- |
| Rotate | Adjust the focused value |
| Short-press | Cycle focus around the screen: Band → Frequency → Mode → BW → AGC → SM → Step → Battery → Volume → Brightness → Band. Power-on starts at Frequency |
| Long-press Frequency | Toggle seek mode in AM/FM |
| Rotate in seek mode | Search up/down within the current band |
| Short-press in seek mode | Cancel search at the current frequency, return to manual tuning, and keep Frequency focused |
| Long-press Volume | Toggle mute while retaining the chosen volume |
| Long-press Band | Lock or unlock the current band: manual tuning stops at its edges instead of crossing |
| Short-press on receiver error | Reset the radio chip and retry |
| Hold 3 s (V5A) | `Release to power off` appears; releasing saves settings and powers off. Press the encoder to switch on. On the V5A, long-press actions run on release so a longer hold can power off |

A long press lasts approximately 650 ms. Releasing it does not generate a second short press. A locked band's name turns orange; the lock follows band changes and is cleared at power-on. A key held while the radio starts must be released before it acts. Seek is unavailable in LSB/USB; seek mode is magenta, while ordinary focus is yellow. Station text remains visible in idle seek mode; an active search temporarily shows frequency instead.

## Reading the screen

The top left lists bands. The top right shows the device clock beside brightness, followed by volume and battery estimate. The main area shows frequency and unit, or RDS text. Modes and reception controls occupy the next row; signal indicators and Step sit below them. The bottom ruler's red pointer always carries the current frequency.

At band boundaries, the ruler reserves space for names such as SW and VHF. Blank positions between frequency endpoints and band-name markers are visual spacing, not frequencies you can tune.

### Colors and volume

- **Yellow:** focused value controlled by the encoder.
- **Cyan:** active option; a cyan speaker indicates stereo blend.
- **Gray:** unavailable option; a light-gray speaker indicates mono, and a dark-gray speaker means quality status is not yet known.
- **Magenta:** seek mode and search direction.
- **Orange SM text:** soft mute is currently attenuating audio; its border retains focus/selection color.
- **Red crossed speaker:** muted.

The speaker's wave lines represent volume: none at zero, one through 33%, two through 67%, and three above 67%. When Volume is focused, its percentage becomes yellow while the icon keeps its mono/stereo color.

### Signal indicators

| Indicator | Meaning |
| --- | --- |
| Signal bars / RSSI | Received signal strength in dBµV |
| SN | Signal-to-noise ratio in dB |
| VALID / INVALID | Whether the chip considers the channel valid for its seek criteria |
| TUNING / SEEK | A tune or search is in progress |
| AFC OK / AFC! | Frequency correction is within range / has reached its limit |
| RF | Current chip gain index, not a value in dB |
| P0 / P1 | FM stereo pilot absent / detected |
| `--` | A stable measurement is not available |

VALID does not guarantee an intelligible station or successful RDS decoding. SSB has AFC disabled by patch configuration.

## RDS station names and text

**PS (Programme Service)** is the station name, such as `LOVE959`. **RT (RadioText)** is extra broadcast text, such as programme details, a song title, an advertisement, or a contact number.

With only PS or RT available, the main area uses large text, approximately 42 pixels high. When both are available, it shows PS above RT in two approximately 21-pixel rows. Long strings scroll independently, pausing at their beginning and end. The display grows the remaining text again when the other message is absent, reusing the same space.

RT has no broadcast-defined display duration. When the station starts a new message by changing its A/B flag, this firmware clears the old RT and waits for two matching complete new messages. Consequently, RT can disappear temporarily and the PS name can become larger. A reception gap alone does not clear confirmed RT; a confirmed empty message does. Updates depend on the broadcaster and reception quality, not on a timer that alternates PS and RT.

**eRT (Enhanced RadioText)** is a separate broadcast service that can coexist with ordinary RT. This firmware supports up to 128 bytes of left-to-right UTF-8 eRT, including Chinese characters available in the screen font. This parsing is tested with constructed data; reception from an eRT broadcaster has not yet been verified on this unit. Confirmed nonempty eRT replaces RT in the same scrolling row; ordinary RT remains the fallback. PS stays in its own row.

eRT has no A/B flag. A changed received segment clears the previous eRT; two matching complete transmissions are needed to show the replacement. Missing segments delay confirmation. Ordinary PS/RT still use printable ASCII; legacy UCS-2 eRT, right-to-left layout, and broadcaster-specific Chinese encodings are not supported.

### Device clock

The device clock appears as `HH:MM` beside brightness after its first synchronization. Currently it is set by an FM station broadcasting **CT (Clock Time)**, which supplies UTC time and a local offset. There is no manual timezone setting. It can take a minute or longer to receive the first update; until then, no clock or placeholder appears.

Once set, the clock keeps running when you tune, change band/mode, search, lose RDS reception, or receive a CT withdrawal. It has no five-minute timeout. A fresh CT can correct its time and offset; repeated copies do not rewind it. Restarting or switching off loses the time, so it must be synchronized again. Wi-Fi NTP synchronization is planned to use this same clock; network synchronization is not available yet.

## Reception controls

| Control | What you adjust |
| --- | --- |
| BW | FM: AUTO, 40, 60, 84, 110 kHz; AM: 1, 1.8, 2, 2.5, 3, 4, 6 kHz; SSB: 0.5, 1, 1.2, 2.2, 3, 4 kHz |
| AGC / ATT | AUTO, or manual attenuation index 0–26 for FM / 0–37 for AM/SSB; the index is not dB |
| SM | Soft-mute attenuation from OFF to 31 dB |
| Step | Manual tuning increment |
| Volume | Audio level, 0–100% |
| Brightness | Display backlight level |
| Battery | Rotate to switch between the percentage estimate and the supply voltage (session only) |

SM is soft mute, not a squelch threshold. BW, AGC, and SM are remembered separately for each demodulation mode.

### Tuning steps

| Band / mode | Available steps |
| --- | --- |
| LW/MW AM | 1, 5, 9, 10 kHz |
| SW AM | 1, 5, 9, 10, 100 kHz |
| LW/MW SSB | 10, 50, 100, 500, 1000 Hz |
| SW SSB | 10, 50, 100, 500, 1000 Hz; 10, 100 kHz |
| VHF FM | 10, 50, 100, 200 kHz |

A fine step does not improve absolute frequency accuracy or enforce a broadcast channel grid.

## Saved settings

After two seconds without adjustments, once tuning/search has completed, the receiver saves current band, each band's frequency/mode/step, BW/AGC/SM, volume, brightness, and mute to NVS. Startup restores them; focus starts on Frequency and seek starts off.

Normal firmware updates preserve NVS. If you switch off within the two-second save delay, the latest adjustment may not be retained. Invalid or incompatible saved data falls back to defaults.

## Battery estimate

The board's ADC measures a supply node that may follow USB voltage, so battery percentage is unreliable while USB-powered. When external power is detected, the status bar shows a full cyan battery icon and `USB`; the full icon indicates USB power rather than measured cell charge. An implausible voltage reading shows `--%`. On battery power, the percentage is a simple voltage estimate. See [Hardware facts](hardware-ats.md#battery-indication) for calibration and measurement limits.

## Screenshots

Connect USB and use the [capture procedure](development.md#screen-capture-and-ruler-preview). The export contains the entire 320 × 170 display and does not change settings; the UI briefly pauses during transfer.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No RDS or temporarily blank RT | Extend/reorient the antenna and allow time for confirmation. RT may be changing; audible FM does not guarantee RDS reception. |
| Restored frequency produces noise | Current firmware waits for crystal stabilization before its first tune. Check the station/antenna and update from an older version. Report the frequency, mode, and serial `?` output if it persists. |
| SI4732 error on screen | Short-press to retry. Record the error and serial status; confirm the supported board wiring. |
| Screenshot times out | Flash the current firmware, select the correct USB port, and close other serial monitors. |
| Screenshot reports no framebuffer | Restart the receiver; capture requires successful framebuffer allocation. |
| Display orientation or colors differ | Board/display variants exist. Verify the controller and configuration against [Hardware facts](hardware-ats.md). |

For bug reports, include board markings if known, firmware commit, frequency/mode, reproduction steps, a screenshot, and serial status. Native and device validation procedures are in [Development](development.md#validation).
