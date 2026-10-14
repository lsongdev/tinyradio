# ATS Mini

[![Firmware](https://github.com/lsongdev/ats-mini/actions/workflows/firmware.yml/badge.svg)](https://github.com/lsongdev/ats-mini/actions/workflows/firmware.yml)

**AM/FM/SSB receiver firmware for the Sunnygold SI4732 Mini Radio.**

Tune LW, MW, SW, and VHF/FM with one encoder. Adjust reception, search for stations, and resume your saved settings when you switch the radio back on.

<table>
  <tr>
    <td><img src="docs/images/screen-frequency.png" width="320" alt="Full receiver screen showing frequency, reception controls, signal status, and tuning scale"></td>
    <td><img src="docs/images/screen-rds.png" width="320" alt="Full receiver screen showing the received RDS station name and radio text"></td>
  </tr>
</table>

*Actual receiver screenshots: frequency view and RDS view.*

## Features

- Continuous tuning across bands, with selectable modes and steps.
- Seek by rotating the encoder; short-press to return to manual tuning.
- Adjustable bandwidth, gain, soft mute, volume, and brightness.
- RDS station names, scrolling radio text, and broadcast clock, including UTF-8 eRT support.
- Saved tuning and reception settings, plus full-screen screenshot export.

## Get started

Download firmware from the [releases page](https://github.com/lsongdev/ats-mini/releases) and follow the [installation guide](docs/user-manual.md#installing-or-updating-firmware). Successful builds update the day's prerelease. Confirm that your receiver matches the [supported hardware](docs/hardware.md).

After switching on, rotate to tune and short-press to select a control. Long-press Frequency to enter seek mode; long-press Volume to toggle mute. The [user manual](docs/user-manual.md) explains the controls and display.

## Documentation

| Guide | What it covers |
| --- | --- |
| [User manual](docs/user-manual.md) | Installation, everyday controls, screen indicators, and troubleshooting |
| [Design](docs/design.md) | Architecture, tuning and seek, UI, RDS/eRT, and saved settings |
| [Development](docs/development.md) | Building, flashing, diagnostics, tests, tools, releases, and third-party notices |
| [Hardware facts](docs/hardware.md) | Schematics, GPIOs, power, reference assets, and verified observations |

## License and credits

Original code and authored documentation are [MIT licensed](LICENSE). The SSB patch, imported hardware assets, and dependencies retain their own terms; see [third-party notices and credits](docs/development.md#third-party-notices).
