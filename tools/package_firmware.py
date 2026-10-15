#!/usr/bin/env python3
"""Package each board's build with flash instructions, source snapshot, and notices.

Run using a Python environment with PlatformIO installed, after building:
  pio run -e ats-mini -e v5a
  python tools/package_firmware.py --output dist
Use --environment to package only some boards.

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
MIB = 1024 * 1024
# Per-board flash layout. Offsets follow PlatformIO's upload layout for each
# chip; Arduino QIO builds use a DIO ROM-loader header and switch after boot.
BOARDS = {
    "ats-mini": {
        "prefix": "ats-mini", "name": "ATS Mini",
        "title": "ATS Mini - Sunnygold SI4732 Mini Radio firmware",
        "target": "ESP32-S3, 16 MB flash, PCB 1.2 reference wiring.",
        "chip": "esp32s3", "flash_size": 16, "flash_freq": "80m", "bootloader": 0x0, "baud": 460800,
        "notes": "",
    },
    "v5a": {
        "prefix": "geeknest-v5a", "name": "GeekNest V5A",
        "title": "GeekNest V5A firmware (experimental)",
        "target": "Classic ESP32, 4 MB flash, GeekNest Radio V5A main board.",
        "chip": "esp32", "flash_size": 4, "flash_freq": "40m", "bootloader": 0x1000, "baud": 115200,
        "notes": "Set the speaker switch to Analog. Back up the original flash before the first\n"
                 "installation (esptool read_flash 0x0 0x400000 v5a-backup.bin): it is the only\n"
                 "way back to the vendor firmware.\n",
    },
}


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--environment", action="append", choices=sorted(BOARDS),
                        help="board environment to package (repeatable; default: all)")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    dirty = bool(command("git", "status", "--porcelain"))
    if dirty and not args.allow_dirty:
        parser.error("release packaging requires a clean checkout; use --allow-dirty only for local checks")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for environment in args.environment or list(BOARDS):
        package(parser, environment, BOARDS[environment], output, dirty)


def package(parser, environment, board, output, dirty):
    from platformio.project.config import ProjectConfig
    from platformio.platform.factory import PlatformFactory
    prefix = board["prefix"]
    flash_bytes = board["flash_size"] * MIB
    flash_size = f"{board['flash_size']}MB"
    config = ProjectConfig(str(ROOT / "platformio.ini"))
    platform = PlatformFactory.new(config.get(f"env:{environment}", "platform"))
    framework = Path(platform.get_package_dir("framework-arduinoespressif32"))
    esptool = Path(platform.get_package_dir("tool-esptoolpy")) / "esptool.py"
    build = ROOT / ".pio" / "build" / environment
    inputs = {
        "bootloader.bin": build / "bootloader.bin",
        "partitions.bin": build / "partitions.bin",
        "boot_app0.bin": framework / "tools/partitions/boot_app0.bin",
        "firmware.bin": build / "firmware.bin",
    }
    for path in [*inputs.values(), esptool]:
        if not path.is_file():
            parser.error(f"missing build input: {path}; run pio run -e {environment} first")
    full = output / f"{prefix}-full.bin"
    # A filled full-size image overwrites NVS/data as well as code and is for
    # complete installation.
    offsets = {"bootloader.bin": board["bootloader"], "partitions.bin": 0x8000,
               "boot_app0.bin": 0xE000, "firmware.bin": 0x10000}
    merge = [sys.executable, str(esptool), "--chip", board["chip"], "merge_bin",
             "--output", str(full), "--flash_mode", "dio", "--flash_freq", board["flash_freq"],
             "--flash_size", flash_size, "--fill-flash-size", flash_size]
    for name, offset in offsets.items():
        merge.extend([hex(offset), str(inputs[name])])
    subprocess.run(merge, cwd=ROOT, check=True)
    sha = command("git", "rev-parse", "HEAD")
    app = output / f"{prefix}-firmware.bin"
    shutil.copyfile(inputs["firmware.bin"], app)
    info_path = output / f"{prefix}-build-info.json"
    info = {
        "project": "tinyradio", "board": board["name"], "environment": environment, "commit": sha,
        "dirty": dirty, "chip": board["chip"], "flash_size": flash_bytes,
        "flash_offsets": {name: hex(offset) for name, offset in offsets.items()},
        "platformio": command(sys.executable, "-m", "platformio", "--version"),
        "packages": command(sys.executable, "-m", "platformio", "pkg", "list", "-e", environment),
        "sha256": {f"{prefix}-{name}": digest(path) for name, path in inputs.items()},
    }
    info["sha256"][full.name] = digest(full)
    info_path.write_text(json.dumps(info, indent=2) + "\n")
    flash = f"python -m esptool --chip {board['chip']} --port PORT --baud {board['baud']} write_flash"
    instructions = f'''{board['title']}

Target: {board['target']}
{board['notes']}Install Python and esptool: python -m pip install esptool==4.9.0
Replace PORT with your receiver's serial port.

Application update (preserves compatible NVS settings):
  {flash} 0x10000 {prefix}-firmware.bin

Complete single-file installation ({board['flash_size']} MiB; RESETS saved settings/data):
  {flash} 0x0 {prefix}-full.bin

Multi-file installation (use all four files from this same package):
  {flash} --flash_mode dio --flash_freq {board['flash_freq']} --flash_size {flash_size} {hex(board['bootloader'])} {prefix}-bootloader.bin 0x8000 {prefix}-partitions.bin 0xe000 {prefix}-boot_app0.bin 0x10000 {prefix}-firmware.bin

No erase_flash command is required. Multi-file flashing preserves NVS where
compatible; the filled full image resets all settings/data partitions.
{prefix}-build-info.json identifies the source commit, package versions and hashes.
{prefix}-source.tar.gz contains the tracked project at that commit. Local packages
marked dirty may include firmware changes absent from that source snapshot.
Read docs/development.md (Third-party notices) and licenses/ for patch, hardware and
font/dependency terms. The SSB patch is separate from the project's MIT license.
'''
    install = output / f"{prefix}-install.zip"
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / f"{prefix}-source.tar.gz"
        subprocess.run(["git", "archive", "--format=tar.gz", "-o", str(source), sha], cwd=ROOT, check=True)
        with zipfile.ZipFile(install, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, path in inputs.items():
                archive.write(path, f"{prefix}-{name}")
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
                                 (ROOT / ".pio/libdeps" / environment, "libraries")]:
                for path in sorted(tree.rglob("*")):
                    if path.is_file() and (prefix == "project-dependencies" or path.name.lower().startswith(("license", "copying", "notice", "copyright"))):
                        archive.write(path, f"licenses/{prefix}/{path.relative_to(tree)}")
            # Display-library notices can also occur in source headers.
            libraries = ROOT / ".pio/libdeps" / environment
            for path in sorted(libraries.rglob("*")):
                if path.is_file() and path.suffix.lower() in (".h", ".hpp", ".c", ".cpp"):
                    archive.write(path, f"licenses/library-source/{path.relative_to(libraries)}")
    assets = [app, full, install, info_path]
    (output / f"{prefix}-sha256sums.txt").write_text("".join(f"{digest(path)}  {path.name}\n" for path in assets))
    print(f"Packaged {board['name']} {'local dirty build' if dirty else 'commit'} {sha} -> {output}")


if __name__ == "__main__":
    main()
