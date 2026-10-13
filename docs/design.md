# Firmware Design

This document describes the current implementation. Board-level evidence belongs in [Hardware facts](hardware.md); everyday operation belongs in the [User manual](user-manual.md).

## Architecture

| Layer | Responsibility |
| --- | --- |
| `src/hal` | GPIO, radio power/reset, I2C resource, display/backlight, encoder/button, audio gating, battery ADC, and NVS access |
| `src/drivers/si473x` | Chip commands and responses, AM/FM/SSB tuning, SSB patch loading, signal quality, reception settings, seek, and RDS decoding |
| `src/radio` | Bands, mode constraints, tuning increments, per-band memories, frequency formatting, and versioned settings bytes |
| `src/app` | Encoder focus, seek interaction, radio coordination, drawing, RDS presentation, and save scheduling |

The SI473x driver receives a `TwoWire&`; it does not own board pins or power rails. The HAL powers and resets the chip before `begin()`. Encoder edges are accumulated in an interrupt; I2C and display work happen in the main loop.

## Frequency model

The application uses the broadcast ranges documented in the [SI4732-A10 data short](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-shorts/Si4732-A10-short.pdf): LW 153–279 kHz, MW 520–1710 kHz, SW 2.3–26.1 MHz, and VHF/FM 64–108 MHz. AM/LSB/USB are available on LW/MW/SW; FM is available on VHF. AIR 118–137 MHz is a disabled reference entry, skipped during selection and tuning. Loading an SSB patch does not extend the VHF front end into AIR.

Public frequency APIs use Hz. FM commands encode 10 kHz units; AM/SSB commands encode kHz carriers. The AM/SSB encoder can express 150 kHz–30 MHz, but the application deliberately uses the data-short ranges. Encodable frequencies are not a guarantee of RF performance outside those ranges.

Each band remembers its frequency, mode, and step. Manual tuning follows the currently selected step without snapping to a channel grid. Crossing an edge enters the next available band's endpoint and continues remaining turns using that band's remembered step. The global endpoints stop at 153 kHz and 108 MHz. Selecting a band directly restores its remembered frequency; crossing into a band by tuning enters its endpoint. Returning from SSB to AM removes sub-kHz precision.

### SSB

On entry, the driver uses `carrier_kHz = floor(hz / 1000)` and `BFO = -(hz % 1000)`. During continuous tuning, it keeps the RF carrier while the target is within ±8 kHz, adjusting only BFO; outside that window it retunes the carrier. Fine steps do not establish absolute oscillator accuracy.

The compressed patch is loaded when entering SSB from another receiver engine, and GET_REV must report patch ID `0xA902`. LSB/USB changes retain the existing patch. Reset, power loss, or leaving SSB requires a reload. See [third-party notices](../THIRD_PARTY_NOTICES.md#ssb-patch) for its provenance and terms.

## Receiver coordination

Band/mode edits are combined for 180 ms before configuration. Chip commands check CTS and ERR with a 500 ms deadline; individual I2C transactions have a 20 ms timeout. Every crystal-enabled `POWER_UP` waits at least 500 ms before tuning, as required by [AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf). CTS indicates command completion, not oscillator stability.

Tuning polls STC without blocking the application for the entire operation. A tune is confirmed only when the returned carrier matches the request; the timeout is 3 seconds. Inputs received during tuning update the target, which is sent once the active tune completes. Audio stays muted during initial configuration and is enabled after confirmed tuning. Errors mute audio and disable the amplifier; short-press retries after resetting the radio chip.

### Seek

A 650 ms long-press on Frequency toggles seek mode, indicated in magenta. Rotation starts a search in the corresponding direction. Seek stays within the current band, with wrap disabled; repeated turns in the same direction do not queue station skips. Short-press cancels search, adopts the chip's reported frequency, and returns to manual tuning. SSB seek is unavailable. Seek status is polled every 100 ms, with a 30-second timeout.

## UI and display

The application draws a 320 × 170 RGB565 sprite and presents the complete frame to reduce flicker. The confirmed board uses rotation 3. If framebuffer allocation fails, the UI falls back to direct drawing; screenshot export then reports that its framebuffer is unavailable.

| Region | Content |
| --- | --- |
| Top status bar | Band choices on the left; brightness, volume, and battery on the right |
| Main area, x=8…311, y=22…67 | Frequency and unit, or RDS PS/RT text |
| Controls at y=70 | Mode, BW, AGC/manual attenuation, and SM |
| Signal row at y=91…107 | Equal-height RSSI bars, RSSI/SNR, VALID, AFC, RF gain, FM pilot, and Step |
| Bottom scale | Continuous ruler, band-boundary labels, red cursor, and current frequency |

Yellow indicates encoder focus; cyan indicates active choices; disabled options are gray. Seek uses magenta. Soft mute engagement changes the SM text to orange while retaining the normal/focused border color.

The volume icon is gray for mono, cyan for stereo blend, dark gray while quality is unknown, and red with a cross when muted. Its zero-to-three wave lines represent volume. Volume focus turns the percentage yellow without hiding the mono/stereo color.

The ruler advances in 12-pixel minor divisions. Between adjacent bands, decorative positions reserve five minor divisions from the last frequency to the departing band name, one division between band names, and five divisions to the next frequency. Only the two name markers and actual frequency ticks are drawn. Decorative positions have no tunable frequency: crossing bands remains one encoder step. The red cursor's label always shows the actual frequency, including when RDS occupies the main area.

### RDS decoding and presentation

PS is decoded from groups 0A/0B and RT from 2A/2B. `FM_RDS_CONFIG=0xAA01` admits corrected blocks with BLE levels 0–2; required blocks with BLE=3 are rejected. Station PI changes invalidate previous text. Up to eight FIFO groups are drained every 100 ms. Temporary synchronization loss or FIFO overrun does not flush surviving groups or erase already confirmed text.

PS and RT are published after two matching complete messages. RT's A/B flag or 2A/2B version change immediately clears the old RT and starts a new assembly. The decoder also accepts a confirmed empty message. There is no display expiry timer: a reception gap alone does not clear RT. With slow or intermittent delivery, the two-message confirmation can leave RT blank for several seconds. In a three-minute observation of 95.9 MHz, PS `LOVE959` stayed stable while two RT messages alternated; blank intervals were usually 15–19 seconds, once about 45 seconds, with RDS synchronization maintained and no FIFO overruns.

The display reuses space: a single PS or RT uses bold `efontCN_24_b` scaled to approximately 42 pixels high; when both exist, it uses two approximately 21-pixel rows. Each row has its own scrolling state. A long string pauses 1.2 seconds at the start, advances one pixel per 35 ms, pauses 1.2 seconds at the end, and repeats. Confirmed content changes restart that row's scroll. There is no timed alternation between PS and RT.

The font can draw Chinese, but the current RDS decoder handles printable ASCII only; other bytes become `?`. UTF-8 eRT and broadcaster-specific Chinese encodings are not implemented. The [RDS Forum glossary](https://www.rds.org.uk/pages/glossary-of-terms.html) distinguishes ordinary RT from enhanced RT.

### Reception settings

| Control | Values |
| --- | --- |
| FM BW | AUTO, 40, 60, 84, 110 kHz |
| AM BW | 1, 1.8, 2, 2.5, 3, 4, 6 kHz |
| SSB BW | 0.5, 1, 1.2, 2.2, 3, 4 kHz |
| AGC / ATT | AUTO, or chip gain index 0–26 for FM / 0–37 for AM/SSB |
| SM | 0–31 dB soft-mute attenuation; zero is OFF |

Manual gain indices represent attenuation settings, not dB. SM controls attenuation under poor reception; it is not a squelch threshold. FM uses 50 μs de-emphasis. SW uses antenna capacitor value 1; LW/MW use automatic value 0. Reception settings are remembered per demodulation mode.

## Persistence

After two seconds without changes, and when tuning/seek has completed, the application saves current band, per-band frequency/mode/step, per-mode BW/AGC/SM, volume, brightness, and mute. Focus and seek state are not persisted: startup focuses Frequency in manual tuning mode.

NVS access lives in `src/hal/settings_storage.*`. `src/radio/saved_settings.*` encodes a 96-byte record with explicit little-endian fields, version, band count, and CRC32. It validates values before applying them; unsupported or corrupt records fall back to defaults. No C++ struct padding or internal step indices are stored. Unchanged settings are not rewritten; failed writes retry after ten seconds. Updates preserve compatible NVS records.

Defaults are VHF/FM 100 MHz, 100 kHz step, volume 18/63, brightness 128/255, unmuted, BW AUTO, AGC AUTO, and FM SM 16 dB. Changes made within the final two seconds before power removal may be lost.

## Full-screen capture

`tools/capture_screen.py` sends uppercase `S` over USB serial. The application exports the last presented RGB565 framebuffer converted to RGB888, without rerendering or tuning. The packet is:

```text
\nATS_SCREENSHOT RGB888 320 170 163200\n
<163200 binary bytes: R, G, B per pixel; rows top to bottom>
\nATS_SCREENSHOT_END\n
```

This is a binary transfer, so other serial consumers must be closed. Requests received before the initial frame is drawn wait until a complete frame has been presented. Firmware sends one row at a time with bounded write retries; it briefly pauses the main loop. The host validates dimensions, payload length, and the closing marker before writing an RGB PNG. PNG encoding uses `struct` and `zlib`; only serial access needs pyserial. Failure leaves the requested output untouched. The framebuffer capture reflects UI pixels, not LCD optics or panel gamma.

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/capture_screen.py --port /dev/ttyACM0 --output screenshot.png
```

The separate `tools/render_ruler.py` creates SVG previews of the ruler geometry offline; it does not capture a device.

## Validation

Build and flash commands are in the [README](../README.md#quick-start). Run native tests from the repository root:

```sh
sh tools/test.sh
```

These cover band/mode constraints, tuning boundaries/BFO, corrected RDS blocks and assembly, simulated command/FIFO behavior, persistence validation, and screenshot framing/PNG encoding. Simulated tests do not establish RF sensitivity or audio quality.

Device smoke scripts in `test/*_smoke.py` exercise tuning, seek, reception controls, and persistence. They change settings; inspect each script before running. Tests originally written for default settings may need that configuration; rebooting now preserves settings. Do not use them merely to obtain screenshots.

USB diagnostics:

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

`requested` is the target; `confirmed` is the completed tune result. `ps`/`rt` contain confirmed text; `rdssync` reports current synchronization; `rdsfifo` is the last queried FIFO depth; `rdsgroups` and `rdsoverruns` count received groups and observed overruns since invalidation. `stored`, `save`, and `writes` describe persistence. A successful tune or `error=OK` does not imply that a station exists or that its audio is intelligible.

## Continuous integration and daily releases

[`.github/workflows/firmware.yml`](../.github/workflows/firmware.yml) follows the GeekNest Radio build/publication pattern, adapted to this board and the `master` branch. Pushes, pull requests, and manual dispatch run `tools/test.sh`, build with pinned PlatformIO 6.1.18, and package firmware. Actions are pinned to verified commit hashes; PlatformIO platforms/packages are cached against `platformio.ini`.

Successful `master` builds update one prerelease per Shanghai calendar day, tagged `daily-YYYY-MM-DD`. PR builds produce downloadable artifacts only. Publication is serialized and checks that its commit is still the head of `master`, preventing a superseded build from replacing a newer one.

`tools/package_firmware.py` obtains installed dependency paths from PlatformIO, including version-suffixed package directories. It exports:

- The application image for updates at `0x10000`.
- A filled 16 MiB image for complete installation at `0x0`, which resets NVS/data.
- An install ZIP with bootloader (`0x0`), partitions (`0x8000`), boot-app marker (`0xe000`), application (`0x10000`), instructions, source snapshot, and notices.
- Build metadata and SHA-256 checksums.

The ESP32-S3 ROM image uses DIO/80 MHz upload parameters, matching PlatformIO's Arduino handling of the QIO board configuration. The application configures the board's QIO flash mode. Multi-file or application-only updates preserve compatible NVS; the filled full image overwrites it.

Packaging requires a clean checkout so the archived commit corresponds to the release source. Local validation may use `--allow-dirty`; its metadata marks that result as dirty and its source archive still represents HEAD. Run the tool with the Python environment where PlatformIO is installed:

```sh
python tools/package_firmware.py --output dist
```

No repository visibility change is made by the workflow. A private repository's prereleases remain private until the repository is made public.

## References

- [Skyworks AN332 programming guide](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf)
- [PU2CLR SSB interfaces](https://pu2clr.github.io/SI4735/extras/apidoc/html/group__group17.html)
- [Community BFO reference](https://github.com/esp32-si4732/ats-mini/blob/ab4f5dc8120767cbb134b70096929f74c49df3e7/ats-mini/ats-mini.ino)
- [Third-party notices and patch provenance](../THIRD_PARTY_NOTICES.md)
