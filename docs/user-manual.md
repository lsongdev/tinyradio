# ATS Mini User Manual

## Receiver overview

The ATS Mini uses an ESP32-S3 and SI4732 receiver. Available bands and modes are:

| Band | Frequency range | Modes |
| --- | --- | --- |
| LW | 153–279 kHz | AM, LSB, USB |
| MW | 520–1710 kHz | AM, LSB, USB |
| SW | 2.3–26.1 MHz | AM, LSB, USB |
| VHF | 64–108 MHz | FM |
| AIR | 118–137 MHz | Unavailable on this hardware |

AIR is shown for reference but cannot be selected. The SI4732 SSB patch enables LSB/USB reception on LW, MW, and SW; it does not add VHF air-band reception.

## Encoder controls

- **Rotate:** Change the focused item.
- **Short press:** Move focus through Band → Frequency → Mode → BW → AGC → SM → Step → Volume → Brightness, then back to Band.
- **Long-press Frequency:** Toggle seek mode. The frequency display changes color while seeking.
- **Rotate while seeking:** Search forward or backward in the current band.
- **Short press while seeking:** Stop at the current receiver frequency and return to manual tuning, keeping Frequency focused.
- **Long-press Volume:** Toggle mute. The selected volume level is retained.

At the Frequency focus, ordinary rotation continuously tunes by the selected step. Crossing a band edge continues at the adjacent available band, carrying any remaining encoder movement forward. Tuning stops at the overall limits of 153 kHz and 108 MHz; the scale does not wrap around. Each band remembers its own frequency, mode, and step.

## Tuning steps

The Step focus cycles through the options for the current band and mode. Shortwave has coarse steps of **10 kHz** and **100 kHz** in both AM and SSB, in addition to the finer options.

| Band / mode | Available steps |
| --- | --- |
| LW/MW AM | 1, 5, 9, 10 kHz |
| SW AM | 1, 5, 9, 10, 100 kHz |
| LW/MW SSB | 10, 50, 100, 500, 1000 Hz |
| SW SSB | 10, 50, 100, 500, 1000 Hz; 10, 100 kHz |
| VHF FM | 10, 50, 100, 200 kHz |

Steps control manual tuning increments. They do not promise a matching channel plan or improve the receiver's absolute frequency accuracy.

## Radio modes and reception controls

Choose AM, LSB, or USB on LW/MW/SW, and FM on VHF. Band and mode choices that are unavailable are dimmed in the interface. Long-press Frequency to seek stations in AM or FM; seek is not available in SSB and stays within the current band.

The BW, AGC, and SM controls adjust channel bandwidth, automatic gain control/attenuation, and soft-mute attenuation. SM is a soft-mute setting, not a squelch threshold. For the available values and technical behavior, see [Radio capabilities and implementation notes](radio.md).

In FM, the main display can show confirmed RDS station name (PS) or radio text (RT) when received. The status area shows signal strength, SNR, channel validity, and compact receiver status. The volume icon indicates mono/stereo blend state where available.

## Saved settings

The firmware stores the selected band, each available band's frequency/mode/step, volume, brightness, mute state, and per-mode BW/AGC/SM settings in ESP32 non-volatile storage (NVS). Changes are saved after two seconds without adjustments and restored after a restart. Focus starts on Frequency at startup, and seek always starts in manual tuning mode.

Normal firmware uploads preserve NVS. If power is removed before the two-second save delay completes, the latest change may not be retained.

## Battery indicator

The battery percentage is not reliable while the receiver is connected to USB and may display `--%`. Refer to [hardware documentation](hardware.md) for the board's ADC and voltage-calibration notes.
