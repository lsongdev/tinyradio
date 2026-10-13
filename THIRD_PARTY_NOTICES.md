# Third-party Notices

The [MIT License](LICENSE) covers this project's original firmware code, tools, tests, and authored documentation. Third-party files and dependencies retain their own terms. Including them here does not grant additional rights or apply MIT to those files.

## SSB patch

`src/drivers/si473x/patches/ssb.h` is an unchanged copy of the [PU2CLR compressed patch file](https://github.com/pu2clr/SI4735/blob/123eccc730dc03c0ccc28526cbea6e305da7d2fc/src/patch_ssb_compressed.h), derived from `amrx_6_0_1_ssbrx_patch_init_0xA902.csg` published by Vadim Afonkin and adapted/compressed by Ricardo Caratti.

Its full upstream notice is preserved in the file. The notice states that the patch is not part of the SI4735 library and discourages commercial use. The library's license must not be taken as a license for this patch. This repository makes no additional claim about patch usage permissions.

## Hardware references and photographs

`docs/SCH_SI4732_1.2.pdf` and `docs/PCB_SI4732_1.2.pdf` are Sunnygold design attachments, copied unchanged from the [hardware mirror](https://github.com/esp32-si4732/esp32-si4732-oshwhub/tree/6105532703f85a0275950600ff4f040d1f831b24/FILES/2025-01-22). The mirror identifies Sunnygold hardware/software as CC BY-NC-SA 3.0. They are excluded from this project's MIT grant; preserve Sunnygold attribution and upstream terms.

`docs/images/radio-front.jpg`, `radio-chinese-ui.png`, and `pcb-buttons.jpg` come from the [ATS Mini community documentation](https://github.com/esp32-si4732/ats-mini/tree/ab4f5dc8120767cbb134b70096929f74c49df3e7/docs/source/_static). These photographs retain their upstream ownership; a uniform image license has not been established here. Their provenance and original filenames are recorded in [Hardware facts](docs/hardware.md#sources-and-reference-assets). They are excluded from MIT.

`docs/images/screen-frequency.png` and `screen-rds.png` are framebuffer exports of this project's firmware, produced by `tools/capture_screen.py`. RDS strings in a screenshot are received broadcast content, not project-authored text or an endorsement of the broadcaster.

## Display library and fonts

The build downloads [LovyanGFX 1.2.7](https://github.com/lovyan03/LovyanGFX/tree/1.2.7). Its upstream license file includes FreeBSD/BSD and MIT notices for LovyanGFX and incorporated library portions. The license text is reproduced as [LovyanGFX notices](docs/licenses/LovyanGFX.txt), with line endings normalized.

The efont Chinese font comes through LovyanGFX. Copyright 2000–2001 /efont/ The Electronic Font Open Laboratory. Its redistribution conditions and disclaimer are reproduced unchanged in [efont notices](docs/licenses/efont.txt).

PlatformIO also downloads the Arduino-ESP32 framework and Espressif build tools. Those components retain their upstream licenses; distributing a firmware binary does not replace dependency notices with this project's MIT license.

## Reference projects

Thanks to Sunnygold for the hardware, the ATS Mini community and Chinese firmware authors for board references, Vadim Afonkin and Ricardo Caratti/PU2CLR for SSB resources, and lovyan03 and font contributors for the display library. This project's application and driver logic are original implementations; only the SSB patch is bundled from PU2CLR. Reference-project terms do not imply a blanket license for their individual assets.
