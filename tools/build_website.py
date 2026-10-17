#!/usr/bin/env python3
"""Build a self-contained GitHub Pages tree with verified release firmware.

Requires gh authenticated via GH_TOKEN. No browser calls GitHub's asset endpoints:
release binaries are served from the same origin to avoid release-download CORS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOARDS = {"ats-mini": ("ats-mini", "esp32s3", 16 * 1024 * 1024),
          "v5a": ("geeknest-v5a", "esp32", 4 * 1024 * 1024)}


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True)


def build(output, repo, limit):
    if output == ROOT or output == ROOT / "www" or ROOT in output.parents and output.name in ("src", "docs", "tools"):
        raise ValueError("output must be a dedicated build directory")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / "www", output, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("test", "package.json", "README.md"))
    shutil.rmtree(output / "firmware", ignore_errors=True)
    (output / ".nojekyll").touch()
    data = json.loads(gh("api", f"repos/{repo}/releases?per_page=100"))
    releases = []
    for release in sorted(data, key=lambda r: r["published_at"] or "", reverse=True):
        if release["draft"] or not re.fullmatch(r"[A-Za-z0-9._-]+", release["tag_name"]):
            continue
        assets = {a["name"] for a in release["assets"]}
        tag = release["tag_name"]
        entry = {"tag": tag, "url": release["html_url"], "prerelease": release["prerelease"], "boards": {}}
        for board, (prefix, chip, size) in BOARDS.items():
            names = [f"{prefix}-{suffix}" for suffix in ("build-info.json", "firmware.bin", "full.bin")]
            if not set(names) <= assets:
                continue
            destination = output / "firmware" / tag
            destination.mkdir(parents=True, exist_ok=True)
            gh("release", "download", tag, "--repo", repo, "--dir", str(destination),
               *[arg for name in names for arg in ("--pattern", name)])
            info = json.loads((destination / names[0]).read_text())
            if info["chip"] != chip or info["environment"] != board or info["flash_size"] != size or info["dirty"]:
                raise ValueError(f"{tag}/{board}: incompatible release metadata")
            board_entry = {"commit": info["commit"]}
            for name, kind, address in [(names[1], "update", 0x10000), (names[2], "full", 0)]:
                image = destination / name
                digest = hashlib.sha256(image.read_bytes()).hexdigest()
                length = image.stat().st_size
                if digest != info["sha256"][name]:
                    raise ValueError(f"{tag}/{name}: SHA-256 mismatch (release may be updating; retry later)")
                if not 0 < length <= size - address or kind == "full" and length != size:
                    raise ValueError(f"{tag}/{name}: invalid image size")
                board_entry[kind] = {"path": image.relative_to(output).as_posix(), "sha256": digest,
                                     "size": length, "address": address}
            entry["boards"][board] = board_entry
        if entry["boards"]:
            releases.append(entry)
        if len(releases) >= limit:
            break
    (output / "releases.json").write_text(json.dumps({"releases": releases}, indent=2) + "\n")
    print(f"Website ready: {output} ({len(releases)} releases)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist" / "www")
    parser.add_argument("--repo", default="lsongdev/tinyradio")
    parser.add_argument("--limit", type=int, default=8)
    args = parser.parse_args()
    if args.limit < 1:
        parser.error("--limit must be positive")
    build(args.output.resolve(), args.repo, args.limit)
