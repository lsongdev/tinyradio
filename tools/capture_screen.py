#!/usr/bin/env python3
"""Export the receiver's complete displayed framebuffer to a lossless PNG.

Install pyserial, close other serial monitors, then run:
  python3 tools/capture_screen.py --port /dev/ttyACM0 --output screenshot.png

The firmware's S command returns a framed RGB888 snapshot. PNG encoding uses
the Python standard library; no Pillow, graphics runtime, or display readback
is needed. Capturing briefly pauses UI updates and leaves settings unchanged.
"""
import argparse
from pathlib import Path
import re
import struct
import time
import zlib

HEADER = re.compile(rb"ATS_SCREENSHOT RGB888 (\d+) (\d+) (\d+)\n")
END = b"\nATS_SCREENSHOT_END\n"


def encode_png(width: int, height: int, pixels: bytes) -> bytes:
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError("invalid RGB888 image dimensions or payload")

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    stride = width * 3
    scanlines = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride]
                         for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def read_snapshot(port, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline()
        if line.startswith(b"SCREENSHOT_ERROR"):
            raise RuntimeError(line.decode(errors="replace").strip())
        match = HEADER.fullmatch(line)
        if match:
            break
    else:
        raise TimeoutError("no screenshot header; flash current firmware and close other serial monitors")
    width, height, size = map(int, match.groups())
    if not (0 < width <= 1024 and 0 < height <= 1024 and size == width * height * 3):
        raise ValueError("invalid screenshot header")
    payload = bytearray()
    expected = size + len(END)
    while len(payload) < expected and time.monotonic() < deadline:
        payload.extend(port.read(expected - len(payload)))
    if len(payload) != expected or payload[size:] != END:
        raise RuntimeError("incomplete screenshot transfer; no output image written")
    return width, height, bytes(payload[:size])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial port from pio device list")
    parser.add_argument("--output", type=Path, default=Path("screenshot.png"))
    parser.add_argument("--wait", type=float, default=0, help="seconds to wait before capturing, e.g. for RDS")
    parser.add_argument("--timeout", type=float, default=20, help="maximum transfer time in seconds")
    args = parser.parse_args()
    if args.wait < 0 or args.timeout <= 0:
        parser.error("--wait must be nonnegative and --timeout must be positive")
    try:
        import serial
        with serial.Serial(args.port, baudrate=115200, timeout=0.25, write_timeout=2) as port:
            time.sleep(args.wait)
            port.reset_input_buffer()
            port.write(b"S")
            width, height, pixels = read_snapshot(port, args.timeout)
        png = encode_png(width, height, pixels)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(png)
        print(f"Captured {width}x{height} RGB888 -> {args.output}")
    except ImportError:
        parser.exit(1, "Install the serial dependency: python3 -m pip install -r tools/requirements.txt\n")
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        parser.exit(1, f"Screenshot failed: {error}\n")


if __name__ == "__main__":
    main()
