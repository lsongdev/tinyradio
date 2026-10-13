# User Manual

## Getting started

Switch on the receiver. It restores saved settings and starts with Frequency focused, ready for manual tuning. Rotate the encoder to tune, short-press to select another control, and rotate to change that control.

Prebuilt firmware is available in [daily prereleases](https://github.com/lsongdev/ats-mini/releases). Use the application image for updates that preserve settings; the complete 16 MiB image resets settings/data. Flashing instructions are included in the install ZIP. For source builds, see the [README](../README.md#quick-start). For board-specific information, see [Hardware facts](hardware.md).

## Bands and modes

| Band | Frequency range | Available modes |
| --- | --- | --- |
| LW | 153–279 kHz | AM, LSB, USB |
| MW | 520–1710 kHz | AM, LSB, USB |
| SW | 2.3–26.1 MHz | AM, LSB, USB |
| VHF | 64–108 MHz | FM |
| AIR | 118–137 MHz, reference only | Unavailable |

Unavailable bands and modes are gray and skipped during selection. The SSB patch enables LSB/USB; it does not enable VHF aviation reception.

Manual tuning crosses from one band to the next at its endpoint. It stops at the overall limits of 153 kHz and 108 MHz, without wrapping. Directly selecting a band restores its remembered frequency, mode, and step.

## Encoder controls

| Gesture | Result |
| --- | --- |
| Rotate | Adjust the focused value |
| Short-press | Cycle focus through Band → Frequency → Mode → BW → AGC → SM → Step → Volume → Brightness |
| Long-press Frequency | Toggle seek mode in AM/FM |
| Rotate in seek mode | Search up/down within the current band |
| Short-press in seek mode | Cancel search at the current frequency, return to manual tuning, and keep Frequency focused |
| Long-press Volume | Toggle mute while retaining the chosen volume |
| Short-press on receiver error | Reset the radio chip and retry |

A long press lasts approximately 650 ms. Releasing it does not generate a second short press. Seek is unavailable in LSB/USB; seek mode is magenta, while ordinary focus is yellow.

## Reading the screen

The top left lists bands. The top right shows brightness, volume, and battery estimate. The main area shows frequency and unit, or RDS text. Modes and reception controls occupy the next row; signal indicators and Step sit below them. The bottom ruler's red pointer always carries the current frequency.

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

**eRT (Enhanced RadioText)** is a separate broadcast service that can coexist with ordinary RT. This firmware supports up to 128 bytes of left-to-right UTF-8 eRT, including Chinese supported by the screen font. Confirmed nonempty eRT replaces RT in the same scrolling row; ordinary RT remains the fallback. PS stays in its own row.

eRT has no A/B flag. A changed received segment clears the previous eRT; two matching complete transmissions are needed to show the replacement. Missing segments delay confirmation. Ordinary PS/RT still use printable ASCII; legacy UCS-2 eRT, right-to-left layout, and broadcaster-specific Chinese encodings are not supported.

## Reception controls

| Control | What you adjust |
| --- | --- |
| BW | Channel/audio bandwidth: FM AUTO or 40–110 kHz; AM 1–6 kHz; SSB 0.5–4 kHz |
| AGC / ATT | Automatic gain control, or manual attenuation index |
| SM | Soft-mute attenuation from OFF to 31 dB |
| Step | Manual tuning increment |
| Volume | Audio level, 0–100% |
| Brightness | Display backlight level |

SM is soft mute, not a squelch threshold. BW, AGC, and SM are remembered separately for each demodulation mode. Available bandwidth values are listed in [Design](design.md#reception-settings).

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

The board's ADC measures a supply node that may follow USB voltage, so battery percentage is unreliable while USB-powered. When external power is detected, the status bar shows a full cyan battery icon and `USB`; the full icon indicates USB power rather than measured cell charge. An implausible voltage reading shows `--%`. On battery power, the percentage is a simple voltage estimate. See [Hardware facts](hardware.md#battery-indication) for calibration and measurement limits.

## Screenshots

Connect USB, close the serial monitor, then use the host tool:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/capture_screen.py --port /dev/ttyACM0 --output screenshot.png
```

Replace the port with your receiver's port from `pio device list`. The image includes the whole 320 × 170 screen. Capture does not change settings; the UI briefly pauses while sending it. Add `--wait 5` to wait before taking the snapshot.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No RDS or temporarily blank RT | Extend/reorient the antenna and allow time for confirmation. RT may be changing; audible FM does not guarantee RDS reception. |
| Restored frequency produces noise | Current firmware waits for crystal stabilization before its first tune. Check the station/antenna and update from an older version. Report the frequency, mode, and serial `?` output if it persists. |
| SI4732 error on screen | Short-press to retry. Record the error and serial status; confirm the supported board wiring. |
| Screenshot times out | Flash the current firmware, select the correct USB port, and close other serial monitors. |
| Screenshot reports no framebuffer | Restart the receiver; capture requires successful framebuffer allocation. |
| Display orientation or colors differ | Board/display variants exist. Verify the controller and configuration against [Hardware facts](hardware.md). |

For bug reports, include board markings if known, firmware commit, frequency/mode, reproduction steps, a screenshot, and serial status. Native and device validation procedures are in [Design](design.md#validation).
