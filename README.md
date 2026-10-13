# ATS Mini SI4732 Receiver

An ESP32-S3 based portable receiver for the Sunnygold SI4732 Mini Radio (PCB 1.2). The firmware provides continuous tuning, AM/FM and SSB reception, seek, RDS display, and persistent user settings.

## Hardware and documentation

- [Hardware overview, schematic, and photos](docs/hardware.md)
- [Radio capabilities and implementation notes](docs/radio.md)
- [User manual](docs/user-manual.md)

The receiver exposes LW (153–279 kHz), MW (520–1710 kHz), SW (2.3–26.1 MHz), and VHF/FM (64–108 MHz). AIR is listed in the interface as unavailable on this hardware. LW, MW, and SW support AM, LSB, and USB; VHF supports FM.

## Controls

Press the encoder to cycle focus through **Band → Frequency → Mode → BW → AGC → SM → Step → Volume → Brightness**. Rotate to change the focused value. At the frequency focus, rotation tunes continuously; crossing a band edge continues at the adjacent band. The global range stops at 153 kHz and 108 MHz.

Long-press Frequency to enter or leave seek mode. Rotate to search forward or backward; press to stop at the current frequency and return to manual tuning. Long-press Volume to mute or unmute.

Shortwave step options include **10 kHz and 100 kHz** in both AM and SSB. Other available steps depend on band and mode; see the [user manual](docs/user-manual.md) for the full list. Band memories, mode, step, volume, brightness, mute, and reception settings are saved to ESP32 NVS after two seconds of inactivity and restored at startup.

## Build and upload

Install PlatformIO, then run:

```sh
pio run -e ats-mini
pio run -e ats-mini -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

Replace `/dev/ttyACM0` with the port shown by `pio device list` (for example, `/dev/cu.usbmodem21101` on macOS). Uploading the firmware preserves the NVS settings partition.

## Source layout

- `src/drivers/si473x` — SI4732 commands, status handling, and SSB patch.
- `src/hal` — display, encoder, buttons, battery, backlight, audio controls, and board resources.
- `src/radio` — bands, modes, tuning steps, frequency formatting, and saved radio state.
- `src/app` — receiver UI and tuning/seek coordination.
- `docs/` — hardware references, radio implementation notes, and the user manual.

The battery indicator may show `--%` while USB-powered because the board's ADC does not reliably measure the cell voltage in that condition. See [hardware notes](docs/hardware.md).
