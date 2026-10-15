# Firmware Design

This document explains how the current firmware works. Build, diagnostics, tools, tests, and release procedures are in [Development](development.md). Board evidence is in [Hardware facts](hardware-ats.md); everyday operation is in the [User manual](user-manual.md).

- [Architecture](#architecture)
- [Frequency model](#frequency-model)
- [Receiver coordination](#receiver-coordination)
- [UI and display](#ui-and-display)
- [RDS decoding and presentation](#rds-decoding-and-presentation)
- [Persistence](#persistence)
- [Battery interpretation](#battery-interpretation)
- [Full-screen capture](#full-screen-capture)
- [Implementation references](#implementation-references)

## Architecture

| Layer | Responsibility |
| --- | --- |
| `src/hal` | Board-independent ESP32 pieces: debounced button, interrupt encoder, I2C master port, NVS settings blob, serial console, 64-bit monotonic time |
| `src/boards/<name>` | One directory per board: GPIO map, audio mute/amplifier, battery ADC, radio rail/reset/I2C, input wiring, and the LovyanGFX panel |
| `src/drivers/si473x` | Chip commands and responses, AM/FM/SSB tuning, SSB patch loading, signal quality, reception settings, seek, and RDS/eRT/CT decoding |
| `src/radio` | Bands, mode constraints, tuning increments, per-band memories, frequency formatting, and versioned settings bytes |
| `src/services` | Shared UTC device clock and local-time readings, independent of radio protocols |
| `src/ui` | Screen layout and drawing from a plain `ui_view_t` snapshot; framebuffer and screenshot export |
| `src/app` | Encoder focus, seek interaction, radio coordination, RDS presentation/clock adapter, save scheduling, and serial diagnostics |

Every layer is C with plain structs and functions. C++ is confined to the files that use C++-only dependencies (LovyanGFX, Arduino `Serial`, Arduino's `setup`/`loop`); see [Development](development.md#languages). Lower layers never include upper ones: `hal` and `drivers` know nothing about boards, radio policy, or UI, and `ui` reads state only through `ui_view_t`.

The SI473x driver receives an `si473x_bus_t` (write/read callbacks plus context); it does not own board pins or power rails. The board powers and resets the chip before `si473x_begin()` and supplies its `hal_i2c_t` as the bus. Encoder edges are accumulated in an interrupt; I2C and display work happen in the main loop.

## Frequency model

The application uses the broadcast ranges documented in the [SI4732-A10 data short](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-shorts/Si4732-A10-short.pdf): LW 153–279 kHz, MW 520–1710 kHz, and VHF/FM 64–108 MHz. SW is opened to what the chip accepts: 1.711–30 MHz, continuing directly from MW (the data short lists 2.3–26.1 MHz). AM/LSB/USB are available on LW/MW/SW; FM is available on VHF. AIR 118–137 MHz is a converted band (`radio_band_t.converted`): boards without a frequency converter leave it disabled, so it is hidden from the band bar and skipped during selection and tuning; the V5A enables it (`BOARD_HAS_AIR`). AIR is AM only, with 1/5/25 kHz steps and frequencies on the 1 kHz grid; 8.33 kHz channels are not supported. Loading an SSB patch does not extend the SI4732's VHF front end into AIR.

Before each tune the receiver calls `board_frontend_set(rf)` and tunes the chip to `board_frontend_receiver_hz(rf)`. Direct boards return the RF itself. On the V5A, LW/MW use the direct antenna path, SW/VHF the LNA, and AIR the LNA and SA602 mixer with the Si5351 CLK0 local oscillator at RF − 10.7 MHz while the SI4732 stays at 10.7 MHz in AM; changing AIR frequency only retunes the LO. A tune counts as complete when the front end is set for the RF and the chip has confirmed its frequency. The chip's seek cannot follow a converter, so seek is unavailable on AIR, as in SSB.

Public frequency APIs use Hz. FM commands encode 10 kHz units; AM/SSB commands encode kHz carriers. The AM/SSB encoder can express 150 kHz–30 MHz; SW uses that upper limit. Tuning and seek to 30 MHz in AM and USB, and from 1.711 MHz, were verified on the V5A. Encodable frequencies are not a guarantee of RF performance outside those ranges.

Each band remembers its frequency, mode, and step. Manual tuning follows the currently selected step without snapping to a channel grid. Crossing an edge enters the next available band's endpoint and continues remaining turns using that band's remembered step. The global endpoints stop at 153 kHz and 108 MHz. Selecting a band directly restores its remembered frequency; crossing into a band by tuning enters its endpoint. Returning from SSB to AM removes sub-kHz precision.

### SSB

On entry, the driver uses `carrier_kHz = floor(hz / 1000)` and `BFO = -(hz % 1000)`. During continuous tuning, it keeps the RF carrier while the target is within ±8 kHz, adjusting only BFO; outside that window it retunes the carrier. Fine steps do not establish absolute oscillator accuracy.

The compressed patch is loaded when entering SSB from another receiver engine, and GET_REV must report patch ID `0xA902`. LSB/USB changes retain the existing patch. Reset, power loss, or leaving SSB requires a reload. See [third-party notices](development.md#ssb-patch) for its provenance and terms.

## Receiver coordination

Band/mode edits are combined for 180 ms before configuration. Chip commands check CTS and ERR with a 500 ms deadline; individual I2C transactions have a 20 ms timeout. Every crystal-enabled `POWER_UP` waits at least 500 ms before tuning, as required by [AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf). CTS indicates command completion, not oscillator stability.

Tuning polls STC without blocking the application for the entire operation. A tune is confirmed only when the returned carrier matches the request; the timeout is 3 seconds. Inputs received during tuning update the target, which is sent once the active tune completes. Audio stays muted during initial configuration and is enabled after confirmed tuning. Errors mute audio and disable the amplifier; short-press retries after resetting the radio chip.

### Seek

A 650 ms long-press on Frequency toggles seek mode, indicated by the theme's active-state color. Rotation starts a search in the corresponding direction. Seek stays within the current band, with wrap disabled; repeated turns in the same direction do not queue station skips. Short-press cancels search, adopts the chip's reported frequency, and returns to manual tuning. RDS text remains visible while seek mode is idle; active tuning/search hides previous-station text. SSB seek is unavailable. Seek status is polled every 100 ms, with a 30-second timeout.

### Reception settings

[`settings.h`](../src/drivers/si473x/settings.h) maps the mode-specific bandwidth choices to SI4732 property codes. The [user manual](user-manual.md#reception-controls) lists their displayed values. AGC can be automatic or overridden with a chip attenuation index; these indices are not dB. SM sets maximum attenuation under poor reception, rather than a squelch threshold. FM uses 50 μs de-emphasis. SW uses antenna capacitor value 1; LW/MW use automatic value 0. Reception settings are remembered per demodulation mode.

## UI and display

The application draws a 320 × 170 RGB565 sprite and presents the complete frame to reduce flicker. The confirmed board uses rotation 3. If framebuffer allocation fails, the UI falls back to direct drawing; screenshot export then reports that its framebuffer is unavailable.

| Region | Content |
| --- | --- |
| Top status bar | Band choices on the left; device clock, brightness, volume, and battery on the right |
| Main area, x=8…311, y=22…67 | Frequency and unit, or RDS PS and RT/eRT text |
| Controls at y=70 | Mode, BW, AGC/manual attenuation, and SM |
| Signal row at y=91…107 | RSSI bars and measured-width readings share the space with even gaps; Step aligns with SM above at x=258, width 59 |
| Bottom scale | Continuous ruler, band-boundary labels, red cursor, and current frequency |

The 16 semantic color tokens and both RGB565 palettes live in [`src/ui/theme.c`](../src/ui/theme.c), with their C interface in [`theme.h`](../src/ui/theme.h). The shared `accent` token sets the brand color of selected controls, normal enabled control borders, and spectrum bars. Unselected control text stays neutral; unselected band borders stay gray. Focused and disabled controls retain their state colors. Dark mode uses yellow for focus, cyan for the accent, and gray for disabled options. Light mode uses dark slate text, a deep teal accent, and darker status colors on a cool near-white background. All text colors, including disabled labels, have at least 4.5:1 contrast after RGB565 quantization. The shared `active` token (magenta in dark mode, purple in light mode) represents seek mode/direction, band lock, engaged soft mute, and external power. Band lock and soft mute override only text color, retaining the focus/accent border. Long-pressing Brightness switches themes without changing backlight brightness. Each view carries the theme; a theme change invalidates the extras cache even when telemetry and spectrum values have not changed. The UI paints the initial theme background before the application enables the backlight.

Detected USB power is shown as a full battery icon in the active-state color with `USB` instead of a percentage. Implausible voltage readings retain `--%`; battery-powered percentage is an estimate.

In dark mode, the volume icon is light gray for mono, cyan for stereo blend, dark gray while quality is unknown, and red with a cross when muted. Light mode uses the corresponding theme tokens. Mono retains the speaker outline. Stereo uses a center dot and symmetric waves on both sides within the same 15 × 11 pixel bounds. Muted or unknown audio status uses the original speaker outline. Zero to three waves per side represent volume. Volume focus highlights the percentage without hiding the mono/stereo color.

The ruler advances in 12-pixel minor divisions. Between adjacent bands, decorative positions reserve five minor divisions from the last frequency to the departing band name, one division between band names, and five divisions to the next frequency. Only the two name markers and actual frequency ticks are drawn. Decorative positions have no tunable frequency: crossing bands remains one encoder step. The red cursor's label always shows the actual frequency, including when RDS occupies the main area.

## RDS decoding and presentation

### PS and ordinary RT

PS is decoded from groups 0A/0B and RT from 2A/2B. `FM_RDS_CONFIG=0xAA01` admits corrected blocks with BLE levels 0–2; required blocks with BLE=3 are rejected. Station PI changes invalidate previous text. Up to eight FIFO groups are drained every 100 ms. Temporary synchronization loss or FIFO overrun does not flush surviving groups or erase already confirmed text.

PS and RT are published after two matching complete messages. RT's A/B flag or 2A/2B version change immediately clears the old RT and starts a new assembly. The decoder also accepts a confirmed empty message. There is no display expiry timer: a reception gap alone does not clear RT. With slow or intermittent delivery, the two-message confirmation can leave RT blank for several seconds.

Ordinary PS/RT decode printable ASCII; other bytes become `?`.

### Clock time

CT is decoded only from group 4A. Its 17-bit UTC Modified Julian Day comes from B[1:0] and C[15:1]; the UTC hour comes from C[0] and D[15:12], the minute from D[11:6], and the signed half-hour local offset from D[5:0]. All required blocks must have BLE ≤ 2. Hours above 23, minutes above 59, and MJD outside 1–99999 are rejected; all-zero CT codes withdraw the clock. One valid group supplies a complete update, unlike multi-group PS/RT confirmation. Format reference: [NRSC-4-B, Figure 20](https://www.nrscstandards.org/standards-and-guidelines/documents/archive/nrsc-4-b-standard-2005.pdf).

The driver exposes the raw broadcast value; [`rds_clock_sync_update()`](../src/app/rds_clock.c) converts MJD/minute to Unix UTC seconds before updating the [device clock](../src/services/device_clock.c). Protocol details and station PI/sequence fields stay outside the shared clock service. Identical UTC timestamps are ignored across retransmissions and station changes; an offset-only update changes local time without resetting seconds. Invalid or withdrawn CT does not unset device time.

The shared clock stores a UTC anchor, the monotonic synchronization instant, a separate offset in minutes, and the last synchronization source. `hal::monotonicMilliseconds()` uses ESP32's 64-bit timer, so the running time crosses 32-bit `millis()` boundaries without expiry. The UI reads only `Clock::read()` for local `HH:MM` at x=152 beside brightness. There is no radio-state gate: tuning, seek, mode/PI changes, reception loss, or errors leave the clock running indefinitely. The display remains blank only until the first synchronization after boot. Time is kept in RAM, not NVS, and is lost on restart/power loss.

The common API is `synchronize(utcSeconds, source, monotonicMs)` plus `setUtcOffsetMinutes(offset)`. Future Wi-Fi NTP synchronization uses this same UTC field and UI reading; it does not require a second display clock. NTP input changes UTC without implicitly changing the offset. Wi-Fi/NTP networking is not implemented yet; tests simulate an NTP caller. Fresh updates currently use the same API with the most recent source recorded, without a source-priority policy. Synchronization calls belong to the application loop; future asynchronous network callbacks must deliver their updates to that owner.

### Enhanced RadioText

Enhanced RadioText is an independent ODA service with AID `0x6552`. A valid 3A announcement assigns a five-bit carrier group code and flags: bit 0 selects UTF-8 instead of legacy UCS-2; bit 1 selects right-to-left layout. The implementation accepts allocatable A groups 5A–13A and supports UTF-8 left-to-right text. Unsupported encoding/layout leaves ordinary RT available. Reassignment, changed announcement flags, tuning, and PI changes reset eRT state.

Each carrier group contributes four raw bytes from blocks C/D at segment index B[4:0]. Segments 0–31 form at most 128 bytes; `0x0D` ends a shorter message. Assembly starts at segment zero and requires consecutive segments. Identical retransmitted segments are harmless; a gap or conflicting duplicate discards the partial cycle and waits for another segment zero. UTF-8 validation occurs after assembly, so multibyte characters may cross segment boundaries. Overlong sequences, surrogates, invalid continuations, truncated characters, and out-of-range code points are rejected. Display control bytes are rejected too.

eRT has no ordinary RT A/B flag. A changed valid segment overlapping the previous message clears published eRT, and two matching complete cycles confirm replacement text. Empty confirmed eRT falls back to RT. Already confirmed text has no reception timeout. Both decoders remain independent; display policy chooses nonempty confirmed eRT before ordinary RT. The scrolling buffer holds 128 bytes plus a terminator. Serial `?` reports separate `rt`, `ert`, `ertgroup` (five-bit group code, or -1 when unannounced), and `ertflags` fields.

This uses the SI4732's ordinary RDS group FIFO; additional RDS2 subcarriers, UCS-2, right-to-left shaping, eRT+ tagging, and broadcaster-specific Chinese encodings are outside this implementation. Protocol references: [RDS Forum glossary](https://www.rds.org.uk/pages/glossary-of-terms.html), [NRSC ODA AID registry](https://www.nrscstandards.org/committees/dsm/archive/rds-oda-aids.pdf), and the [redsea receiver's eRT field parsing](https://github.com/windytan/redsea/blob/master/src/station.cc).

### Text layout

Frequency and its adjacent unit use the same bold `efontCN_24_b` as RDS; the number is approximately 42 pixels high and the unit is smaller, aligned to the same Latin glyph baseline as the number, compensating the font's native three-pixel inset at different scales. The display reuses space: a single PS or RT uses bold `efontCN_24_b` scaled to approximately 42 pixels high; when both exist, it uses two approximately 21-pixel rows. Each row has its own scrolling state. A long string pauses 1.2 seconds at the start, advances one pixel per 35 ms, pauses 1.2 seconds at the end, and repeats. Confirmed content changes restart that row's scroll. There is no timed alternation between PS and RT.

## Persistence

After two seconds without changes, and when tuning/seek has completed, the application saves current band, per-band frequency/mode/step, per-mode BW/AGC/SM, volume, brightness, mute, and theme. Focus and seek state are not persisted: startup focuses Frequency in manual tuning mode.

NVS access lives in `src/hal/settings_storage.*`, under the board's namespace (`ats-mini` for this board). `src/radio/saved_settings.*` encodes a 96-byte record with explicit little-endian fields, version, band count, and CRC32. Version 2 stores the theme in header byte 11; version 1 remains readable and defaults to dark. It validates values before applying them; unsupported or corrupt records fall back to defaults. No C struct padding or internal step indices are stored. Unchanged settings are not rewritten; failed writes retry after ten seconds. Updates preserve compatible NVS records.

Defaults are VHF/FM 100 MHz, 100 kHz step, volume 18/63, brightness 128/255, dark theme, unmuted, BW AUTO, AGC AUTO, and FM SM 16 dB. AM defaults to 3 kHz BW with SM off; LSB/USB default to 2.2 kHz BW with SM off. Changes made within the final two seconds before power removal may be lost.

## Battery interpretation

The board averages 32 calibrated `analogReadMilliVolts()` readings with 11 dB attenuation, then computes `volts = ADC_millivolts × 2 × BATTERY_CALIBRATION / 1000`. The software defaults to a calibration factor of 1.0. Readings outside 2.5–5.5 V are implausible; values above 4.35 V indicate suspected external power. This threshold is a heuristic, not USB connection detection. Battery percentage is `int((volts - 3.2) × 100)`, clamped to 0–100.

The UI shows `--%` for an implausible reading and a full battery in the active-state color with `USB` for suspected external power. The full icon does not mean the cell is charged. The measured supply node is described in [Hardware facts](hardware-ats.md#battery-indication); calibration instructions are in [Development](development.md#battery-calibration).

## Full-screen capture

`tools/capture_screen.py` sends uppercase `S` over USB serial. The application exports the last presented RGB565 framebuffer converted to RGB888, without rerendering or tuning. The packet is:

```text
\nATS_SCREENSHOT RGB888 320 170 163200\n
<163200 binary bytes: R, G, B per pixel; rows top to bottom>
\nATS_SCREENSHOT_END\n
```

This is a binary transfer, so other serial consumers must be closed. Requests received before the initial frame is drawn wait until a complete frame has been presented. Firmware sends one row at a time with bounded write retries; it briefly pauses the main loop. The host validates dimensions, payload length, and the closing marker before writing an RGB PNG. PNG encoding uses `struct` and `zlib`; only serial access needs pyserial. Failure leaves the requested output untouched. The framebuffer capture reflects UI pixels, not LCD optics or panel gamma.

Host capture commands and the offline ruler renderer are described in [Development](development.md#screen-capture-and-ruler-preview).

## Implementation references

| Topic | Code |
| --- | --- |
| Band constraints, memories, and tuning transitions | [`model.h`](../src/radio/model.h), [`model.c`](../src/radio/model.c), [`tuning.h`](../src/drivers/si473x/tuning.h) |
| Receiver state, focus, and save scheduling | [`receiver.c`](../src/app/receiver.c) |
| Screen layout and drawing | [`ui.h`](../src/ui/ui.h), [`ui.cpp`](../src/ui/ui.cpp) |
| Chip commands, patch, seek, FIFO, and signal readback | [`si473x.c`](../src/drivers/si473x/si473x.c), [`settings.h`](../src/drivers/si473x/settings.h) |
| CT parsing and device clock | [`clock_time.h`](../src/drivers/si473x/clock_time.h), [`rds_clock.c`](../src/app/rds_clock.c), [`device_clock.c`](../src/services/device_clock.c), [HAL timer](../src/hal/clock.h) |
| PS/RT and eRT assembly | [`rds.c`](../src/drivers/si473x/rds.c), [`enhanced_radiotext.c`](../src/drivers/si473x/enhanced_radiotext.c) |
| Settings format and storage | [`saved_settings.c`](../src/radio/saved_settings.c), [`settings_storage.c`](../src/hal/settings_storage.c) |
| Battery interpretation | [`board.c`](../src/boards/ats_mini/board.c) |

Protocol references: [Skyworks AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf), [PU2CLR SSB interfaces](https://pu2clr.github.io/SI4735/extras/apidoc/html/group__group17.html), and [community BFO implementation](https://github.com/esp32-si4732/ats-mini/blob/ab4f5dc8120767cbb134b70096929f74c49df3e7/ats-mini/ats-mini.ino).
