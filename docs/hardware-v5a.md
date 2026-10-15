# GeekNest V5A Hardware Facts

This page records what is known about the GeekNest Radio V5A main board used for the `v5a` build: resources, wiring, and constraints confirmed on hardware. Chip capabilities are not proof of reception on this board. Recheck wiring on other PCB revisions. The ATS Mini is described in [Hardware facts](hardware-ats.md); building and the board API are in [Development](development.md).

- [Board resources](#board-resources)
- [GPIO](#gpio)
- [I2C devices and expander bits](#i2c-devices-and-expander-bits)
- [Clocks and the AIR path](#clocks-and-the-air-path)
- [Power latch and cold start](#power-latch-and-cold-start)
- [GPIO12, PSRAM and the display bus](#gpio12-psram-and-the-display-bus)
- [Audio](#audio)
- [Battery and charging](#battery-and-charging)
- [Original firmware backup](#original-firmware-backup)
- [Sources](#sources)

## Board resources

| Resource | Known facts | Evidence and limits |
| --- | --- | --- |
| MCU | ESP32-D0WDQ6-V3, revision 3, 40 MHz crystal | ROM query and runtime information agree |
| Storage | 4 MiB flash, about 4 MiB PSRAM | Full flash read-out; PSRAM 1 MiB two-pattern check passed in the previous firmware, no long-term stress test |
| Display | ST7789, 320 × 240 landscape | Colors and orientation confirmed with LovyanGFX rotation 1, SPI 40 MHz |
| Receiver | SI4732-A10 at I2C 0x11 | GET_REV PN 0x20 (32), FW 1.0 (`31 30`); no crystal, RCLK from Si5351 |
| I/O expander | XL9555 at 0x20 | Receiver reset, amplifier, analog audio route, LNA and mixer routing |
| Clock generator | Si5351A at 0x60, 25 MHz crystal | CLK1 = SI4732 reference; CLK0 = AIR local oscillator |
| Mixer | SA602 | AIR conversion to a fixed 10.7 MHz IF |
| RTC | PCF8523T at 0x68 (schematic U7) | Calendar advances; date was never set (read 2024-01-01) |
| Temperature/humidity | SHT30-DIS-B2.5KS at 0x44 (schematic U1) | Both CRCs pass; on-board reading affected by the radio's own heat, not calibrated |
| Unknown | 0x1C acknowledged once | Not seen in later scans; model unknown |
| Charger | TP4056 | CHRG/STDBY only drive LEDs; no software detection |

Published SI4732 ranges are FM 64–108 MHz, AM 520–1710 kHz, SW 2.3–26.1 MHz and LW 153–279 kHz ([data short](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-shorts/Si4732-A10-short.pdf)). AIR is a board-level conversion path, not a chip band.

## GPIO

The firmware mapping is [`src/boards/v5a/pins.h`](../src/boards/v5a/pins.h).

| Use | GPIO | Notes |
| --- | --- | --- |
| Power latch | 4 | Must go high first; see [power](#power-latch-and-cold-start) |
| Display SCLK / MOSI / CS / DC | 18 / 19 / 22 / 23 | Write-only, no reset line, VSPI |
| Backlight | 21 | PWM, active high |
| I2C SDA / SCL | 13 / 15 | Shared by all I2C devices, always powered |
| Side keys A / B / C | 35 / 32 / 12 | Active low; A has a board pull-up; GPIO12 is also a strapping pin. Labelled select / volume up / volume down in the vendor firmware |
| Encoder A / B / push | 38 / 37 / 36 | Push also switches the power circuit on and is the EXT0 wake input |
| Battery | 39 | 100 kΩ / 100 kΩ divider |
| Analog audio input | 34 | Left audio channel to ADC1 channel 6, sampled by I2S0 DMA for the spectrum; shares ADC1 with the battery |
| Digital audio BCLK / DATA / LRCK | 25 / 26 / 27 | Reserved; never produced sound in tests |

GPIO34–39 have no internal pull-ups; the board supplies them for the encoder, encoder push and SELECT.

## I2C devices and expander bits

XL9555 (PCA9555 register map). Write the output latch before switching a pin to output, and update only the bits a function owns.

| Bit | Function | Polarity |
| --- | --- | --- |
| P0.0 | SI4732 RST | Low resets; pulse low 10 ms, then wait 100 ms |
| P0.5 | RF switch to the mixer loop (AIR) | High selects the mixer |
| P0.6 | RF switch to the LNA antenna route | High selects the LNA (SW, VHF, AIR) |
| P1.2 | SA602 mixer supply | High on |
| P1.4 | LNA supply | High on |
| P1.5 | Amplifier SHUTDOWN | High shuts the amplifier down |
| P1.6 | Analog audio route | High routes SI4732 analog audio to the amplifier |

LW and MW use the direct path. P1.1 is an input; do not configure every pin as an output. A register snapshot taken after the original firmware (outputs 0x01/0x20, directions 0x00/0x02) reflects that firmware's state, not cold-start defaults.

## Clocks and the AIR path

- Si5351 registers survive an ESP32 reset while the board stays powered, so a working receiver after reflashing does not prove the new firmware configured the clock. Only a cold start does.
- **CLK1 → SI4732 RCLK, 32768 Hz**: PLLB = 25 MHz × 36 = 900 MHz (integer); MS1 = 214 + 37796/65536 with R1 = 128. The SI4732 then powers up without XOSCEN and gets `REFCLK_FREQ = 32768`, `REFCLK_PRESCALE = 1`. With the SSB patch, those properties are accepted only after the patch download.
- **CLK0 → SA602 LO for AIR**: PLLA from the crystal with an integer MS0 = 6; LO = RF − 10.7 MHz (low-side injection). The SI4732 stays tuned to 10.7 MHz in AM. Verified by PLL lock and readback at 118, 125, 125.025 and 137 MHz; one 125 MHz reading had RSSI 46 / SNR 6 / VALID, which is not proof of aviation reception.
- Lock checks: SYS_INIT plus the LOL bit of the PLL in use. The reserved LOS bit reads 1 on this Si5351A and is not a loss of lock.
- Crystal load register 183 = 0x92 (10 pF).

## Power latch and cold start

Pressing the encoder powers the board through hardware; the firmware must then drive GPIO4 high to keep power after the key is released.

- **Disable the brownout detector first.** The rail sags while the key is held at power-on, and a brownout reset drops the latch before it is taken. The vendor firmware does the same.
- Wake from deep sleep leaves GPIO4 and GPIO36 in RTC mode; release the hold and return them to digital GPIO before use.
- **Power off: release GPIO4 and keep running at full speed for about 2 s.** On battery this drains the rail to a clean cutoff, and the next key press is a cold start. Entering deep sleep immediately leaves the chip half-powered at µA current, after which the key often fails to start the board.
- If the firmware is still running after 2 s, external power is present: hold GPIO4 low through RTC hold and deep-sleep with EXT0 wake on GPIO36 low. The key must be released first, or the board wakes at once.
- With the brownout detector off, writing NVS at a nearly empty battery carries a small corruption risk, the same trade-off as the vendor firmware.
- Verified in this firmware: power off by holding the encoder, battery cold start with the Si5351 reference reconfigured, settings restored.

## GPIO12, PSRAM and the display bus

GPIO12 is the volume-down key with a pull-up and also the ESP32 MTDI strap that selects the VDD_SDIO voltage. eFuse `EFUSE_BLK0_RDATA4_REG` read 0x00001238: SDIO_FORCE is not set, so the strap level decides. Flash and PSRAM run from the 3.3 V rail on the schematic.

- With the key's pull-up, VDD_SDIO can come up at 1.8 V; enabling PSRAM in that state stalled or watchdog-reset the board (`boot:0x33`).
- The previous firmware made PSRAM reliable by wrapping `psram_enable` and forcing VDD_SDIO to 3.3 V in RTC (`rtc_vddsdio_set_config`, force, TIEH 3.3 V, regulator enabled) before calling the SDK, and by delaying `psramInit` until after the power latch. No eFuse was burned. PSRAM then reported 4192123 bytes.
- The SDK's 80 MHz PSRAM mode can use HSPI for its clock, so the display uses VSPI.
- Arduino's SPI begin with MISO = −1 attaches HSPI's default MISO, GPIO12, as a plain input and removes its pull-up. Restore `INPUT_PULLUP` on GPIO12 after the display bus starts.
- Without PSRAM, internal RAM has a largest free block of about 110 KB after the 320 × 170 frame buffer; a full 320 × 240 frame (150 KB) does not fit.

## Audio

- Set the speaker's Analog/Digital switch to Analog; the firmware cannot read it.
- P1.5 low enables the amplifier and P1.6 high selects analog audio; with these, FM was heard.
- The amplifier pops every time it is enabled, regardless of how long it was off, and an SI4732 reset makes a DC step that routing, volume zero or POWER_DOWN cannot hide. Listening tests concluded that power-on and band changes keep one pop and power-off has none; removing it needs hardware changes (amplifier part, SHUTDOWN default, coupling). Do not keep trying firmware timing.
- The MW ferrite coil has its own switch and may not be fitted. Headphone insertion is not detected.

## Battery and charging

GPIO39 measures BAT_IN / 2 through 100 kΩ / 100 kΩ. The percentage is a voltage estimate, not calibrated. USB presence, charging and full charge cannot be detected in software.

## Original firmware backup

Before the first test firmware, the complete 4 MiB flash of this unit was read and verified (`esptool verify_flash` matched). SHA-256 `88464781f1e12d7fe3b6a608ec9e2c052ff598aa00b95cbd7d64c9473556f38d`, file `v5a-original-20261008.bin` in the previous project's ignored `backups/` directory. Keep that file elsewhere before deleting the old project; it is the only way back to the vendor firmware.

## Sources

- [V5A hardware project by the author](https://oshwhub.com/alec_cy/geek-nest-full-band-radio-v5a-op) (MCU and radio board schematics, V1.0, 2024-08-26)
- [V5A user manual](https://github.com/LuBiBi98/User-manual-for-V5A-radio/blob/main/V5A%20User%20Manual-Reading%20version-EN-20241030.pdf); its ranges and switch positions disagree in places, so check against the board and schematics
- [Vendor yoRadio pin configuration](https://github.com/aleccy/yoradio/blob/b4b1a00cde7dbc1c55a957a72f501a77c160a358/yoRadio/src/myoptions.h) and its power handling
- [Hardware investigation notes](https://lsong.org/notes/geek-nest-radio/)
- Datasheets: [Skyworks AN332](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf) (SI47xx, external RCLK), [Skyworks AN619](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf) (Si5351 registers), [pSemi PE4259](https://psemi.com/pdf/datasheets/pe4259ds.pdf) (RF switch), [NXP PCF8523](https://www.nxp.com/docs/en/data-sheet/PCF8523.pdf), [Sensirion SHT3x](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf)
