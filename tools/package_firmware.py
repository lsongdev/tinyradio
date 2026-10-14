#!/usr/bin/env python3
"""Package an ATS Mini build, flash instructions, source snapshot, and notices.

Run using a Python environment with PlatformIO installed:
  python tools/package_firmware.py --output dist

Release packaging requires a clean checkout. --allow-dirty is for local
validation only and marks the build metadata accordingly.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = "ats-mini"
ENVIRONMENT = "ats-mini"


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    dirty = bool(command("git", "status", "--porcelain"))
    if dirty and not args.allow_dirty:
        parser.error("release packaging requires a clean checkout; use --allow-dirty only for local checks")
    from platformio.project.config import ProjectConfig
    from platformio.platform.factory import PlatformFactory
    config = ProjectConfig(str(ROOT / "platformio.ini"))
    platform = PlatformFactory.new(config.get(f"env:{ENVIRONMENT}", "platform"))
    framework = Path(platform.get_package_dir("framework-arduinoespressif32"))
    esptool = Path(platform.get_package_dir("tool-esptoolpy")) / "esptool.py"
    build = ROOT / ".pio" / "build" / ENVIRONMENT
    inputs = {
        "bootloader.bin": build / "bootloader.bin",
        "partitions.bin": build / "partitions.bin",
        "boot_app0.bin": framework / "tools/partitions/boot_app0.bin",
        "firmware.bin": build / "firmware.bin",
    }
    for path in [*inputs.values(), esptool]:
        if not path.is_file():
            parser.error(f"missing build input: {path}; run pio run -e {ENVIRONMENT} first")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    full = output / f"{PREFIX}-full.bin"
    # Match PlatformIO's ESP32-S3 upload layout. QIO Arduino builds use a DIO
    # ROM-loader header; firmware configures QIO after boot. A filled 16 MiB
    # image overwrites NVS/data as well as code and is for full installation.
    offsets = {"bootloader.bin": 0, "partitions.bin": 0x8000,
               "boot_app0.bin": 0xE000, "firmware.bin": 0x10000}
    merge = [sys.executable, str(esptool), "--chip", "esp32s3", "merge_bin",
             "--output", str(full), "--flash_mode", "dio", "--flash_freq", "80m",
             "--flash_size", "16MB", "--fill-flash-size", "16MB"]
    for name, offset in offsets.items():
        merge.extend([hex(offset), str(inputs[name])])
    subprocess.run(merge, cwd=ROOT, check=True)
    sha = command("git", "rev-parse", "HEAD")
    app = output / f"{PREFIX}-firmware.bin"
    shutil.copyfile(inputs["firmware.bin"], app)
    info_path = output / f"{PREFIX}-build-info.json"
    info = {
        "project": "ATS Mini", "environment": ENVIRONMENT, "commit": sha,
        "dirty": dirty, "chip": "esp32s3", "flash_size": 16 * 1024 * 1024,
        "flash_offsets": {name: hex(offset) for name, offset in offsets.items()},
        "platformio": command(sys.executable, "-m", "platformio", "--version"),
        "packages": command(sys.executable, "-m", "platformio", "pkg", "list", "-e", ENVIRONMENT),
        "sha256": {f"{PREFIX}-{name}": digest(path) for name, path in inputs.items()},
    }
    info["sha256"][full.name] = digest(full)
    info_path.write_text(json.dumps(info, indent=2) + "\n")
    instructions = f'''ATS Mini — Sunnygold SI4732 Mini Radio firmware

Target: ESP32-S3, 16 MB flash, PCB 1.2 reference wiring.
Install Python and esptool: python -m pip install esptool==4.9.0
Replace PORT with your receiver's serial port.

Application update (preserves compatible NVS settings):
  python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x10000 {PREFIX}-firmware.bin

Complete single-file installation (16 MiB; RESETS saved settings/data):
  python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 {PREFIX}-full.bin

Multi-file installation (use all four files from this same package):
  python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 {PREFIX}-bootloader.bin 0x8000 {PREFIX}-partitions.bin 0xe000 {PREFIX}-boot_app0.bin 0x10000 {PREFIX}-firmware.bin

No erase_flash command is required. Multi-file flashing preserves NVS where
compatible; the filled full image resets all settings/data partitions.
{PREFIX}-build-info.json identifies the source commit, package versions and hashes.
{PREFIX}-source.tar.gz contains the tracked project at that commit. Local packages
marked dirty may include firmware changes absent from that source snapshot.
Read docs/development.md (Third-party notices) and licenses/ for patch, hardware and
font/dependency terms. The SSB patch is separate from the project's MIT license.
'''
    install = output / f"{PREFIX}-install.zip"
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / f"{PREFIX}-source.tar.gz"
        subprocess.run(["git", "archive", "--format=tar.gz", "-o", str(source), sha], cwd=ROOT, check=True)
        with zipfile.ZipFile(install, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, path in inputs.items():
                archive.write(path, f"{PREFIX}-{name}")
            archive.write(full, full.name)
            archive.write(info_path, info_path.name)
            archive.write(source, source.name)
            archive.writestr("FLASHING.txt", instructions)
            for name in ("LICENSE", "platformio.ini"):
                archive.write(ROOT / name, name)
            for path in sorted((ROOT / "docs").rglob("*")):
                if path.is_file():
                    archive.write(path, str(path.relative_to(ROOT)))
            archive.write(ROOT / "src/drivers/si473x/patches/ssb.h", "licenses/ssb_patch.h")
            for tree, prefix in [(ROOT / "docs/licenses", "project-dependencies"),
                                 (framework, "arduino-esp32"),
                                 (ROOT / ".pio/libdeps" / ENVIRONMENT, "libraries")]:
                for path in sorted(tree.rglob("*")):
                    if path.is_file() and (prefix == "project-dependencies" or path.name.lower().startswith(("license", "copying", "notice", "copyright"))):
                        archive.write(path, f"licenses/{prefix}/{path.relative_to(tree)}")
            # Display-library notices can also occur in source headers.
            libraries = ROOT / ".pio/libdeps" / ENVIRONMENT
            for path in sorted(libraries.rglob("*")):
                if path.is_file() and path.suffix.lower() in (".h", ".hpp", ".c", ".cpp"):
                    archive.write(path, f"licenses/library-source/{path.relative_to(libraries)}")
    assets = [app, full, install, info_path]
    (output / f"{PREFIX}-sha256sums.txt").write_text("".join(f"{digest(path)}  {path.name}\n" for path in assets))
    print(f"Packaged {'local dirty build' if dirty else 'commit'} {sha} -> {output}")


if __name__ == "__main__":
    main()
