#!/usr/bin/env python3
"""Render the firmware's 320x170 frequency ruler as an SVG preview.

Example:
  python3 tools/render_ruler.py --band SW --frequency 26.1MHz --step 100k \
      --output /tmp/sw-ruler.svg

The ruler geometry, band limits, tuning-step transitions, endpoint clamping,
label selection, and major-division spacing mirror src/app/receiver.cpp and
src/radio/model.cpp. SVG output uses only the Python standard library.
"""

import argparse
import html
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Band:
    name: str
    low: int
    high: int
    default_step: int
    fm: bool = False


BANDS = [
    Band("LW", 153_000, 279_000, 1_000),
    Band("MW", 520_000, 1_710_000, 9_000),
    Band("SW", 2_300_000, 26_100_000, 1_000),
    Band("VHF", 64_000_000, 108_000_000, 100_000, True),
]
INDEX = {band.name: i for i, band in enumerate(BANDS)}
WIDTH, HEIGHT, CENTER, CENTER_INDEX, TICK_GAP = 320, 52, 160, 13, 12


def parse_frequency(value: str) -> int:
    match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*(hz|k|khz|m|mhz)?\s*", value, re.I)
    if not match:
        raise argparse.ArgumentTypeError(f"invalid frequency: {value!r}")
    number = float(match.group(1))
    unit = (match.group(2) or "hz").lower()
    multiplier = 1_000_000 if unit in ("m", "mhz") else 1_000 if unit in ("k", "khz") else 1
    return round(number * multiplier)


def parse_step(value: str) -> int:
    return parse_frequency(value)


def short_frequency(hz: int) -> str:
    divisor, suffix = (1_000_000, "M") if hz >= 1_000_000 else (1_000, "k")
    whole, remainder = divmod(hz, divisor)
    if remainder:
        digits = 6 if divisor == 1_000_000 else 3
        fraction = f"{remainder:0{digits}d}".rstrip("0")
        return f"{whole}.{fraction}{suffix}"
    return f"{whole}{suffix}"


class Tuning:
    def __init__(self, band_index: int, frequency: int, step: int):
        self.band_index = band_index
        self.frequency = frequency
        self.steps = [b.default_step for b in BANDS]
        self.steps[band_index] = step

    def turn(self, turns: int) -> None:
        direction = 1 if turns > 0 else -1
        remaining = abs(turns)
        while remaining:
            band = BANDS[self.band_index]
            step = self.steps[self.band_index]
            distance = band.high - self.frequency if direction > 0 else self.frequency - band.low
            room = distance // step
            if remaining <= room:
                self.frequency += direction * remaining * step
                return
            self.frequency += direction * room * step
            remaining -= room + 1
            next_index = self.band_index + direction
            if next_index < 0 or next_index >= len(BANDS):
                self.frequency = band.high if direction > 0 else band.low
                return
            self.band_index = next_index
            next_band = BANDS[self.band_index]
            self.frequency = next_band.low if direction > 0 else next_band.high


def make_ticks(band_index: int, frequency: int, step: int):
    ticks = [dict(hz=0, step=step, band=band_index, x=CENTER + (i-CENTER_INDEX)*TICK_GAP,
                  valid=False, name=None, endpoint=False) for i in range(27)]
    ticks[CENTER_INDEX].update(hz=frequency, valid=True)
    for direction in (-1, 1):
        preview = Tuning(band_index, frequency, step)
        index = CENTER_INDEX
        while 0 <= index + direction < 27:
            next_tick = Tuning(preview.band_index, preview.frequency, step)
            next_tick.steps = preview.steps.copy()
            next_tick.turn(direction)
            if (next_tick.band_index, next_tick.frequency) == (preview.band_index, preview.frequency):
                break
            transition = next_tick.band_index != preview.band_index
            if transition:
                ticks[index]["endpoint"] = True
                for division in range(1, 11):
                    index += direction
                    if not 0 <= index < 27:
                        break
                    side = preview if division <= 5 else next_tick
                    ticks[index].update(valid=True, band=side.band_index,
                                        name=BANDS[side.band_index].name if division in (5, 6) else None)
                if not 0 <= index + direction < 27:
                    break
            index += direction
            ticks[index].update(hz=next_tick.frequency, step=next_tick.steps[next_tick.band_index],
                                band=next_tick.band_index, valid=True, endpoint=transition)
            preview = next_tick
    return ticks


def render(band_name: str, frequency: int, step: int, output: Path) -> None:
    band_index = INDEX[band_name]
    if step <= 0:
        raise ValueError("step must be positive")
    if not BANDS[band_index].low <= frequency <= BANDS[band_index].high:
        raise ValueError(f"{band_name} frequency must be between "
                         f"{BANDS[band_index].low} and {BANDS[band_index].high} Hz")

    ticks = make_ticks(band_index, frequency, step)
    current_short = short_frequency(frequency)
    current_number = current_short[:-1]
    current_label = current_number + ("MHz" if frequency >= 1_000_000 else "kHz")
    # LovyanGFX's built-in 6x8 font reports a six-pixel cell per character.
    current_width = 6 * len(current_label)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="640" height="104" '
        f'viewBox="0 128 {WIDTH} {HEIGHT}" role="img">',
        '<rect x="0" y="128" width="320" height="52" fill="#000"/>',
        '<line x1="0" y1="140" x2="320" y2="140" stroke="#424242"/>',
    ]
    for i, tick in enumerate(ticks):
        if not tick["valid"]:
            continue
        major = tick["name"] or tick["endpoint"] or (tick["hz"] and (tick["hz"] // tick["step"]) % 5 == 0)
        adjacent = tick["band"] != band_index
        color = "#00ffff" if adjacent else "#d3d3d3" if major else "#505050"
        parts.append(f'<line x1="{tick["x"]}" y1="140" x2="{tick["x"]}" y2="{140 + (12 if major else 6)}" stroke="{color}"/>')
        if not major:
            continue
        label = tick["name"] or short_frequency(tick["hz"])
        width = 6 * len(label)
        right = tick["name"] and i < 26 and ticks[i+1]["name"]
        left = tick["name"] and i > 0 and ticks[i-1]["name"]
        label_left = tick["x"] - (width if right else 0 if left else width / 2)
        label_right = label_left + width
        if label_right >= CENTER-current_width/2-6 and label_left <= CENTER+current_width/2+6:
            continue
        if not tick["endpoint"] and not tick["name"] and any(
                other["valid"] and other["endpoint"] and
                label_right >= other["x"]-3*len(short_frequency(other["hz"]))-2 and
                label_left <= other["x"]+3*len(short_frequency(other["hz"]))+2 for other in ticks):
            continue
        anchor = "end" if right else "start" if left else "middle"
        color = "#00ffff" if adjacent else "#d3d3d3"
        parts.append(f'<text x="{tick["x"]}" y="159" fill="{color}" text-anchor="{anchor}" '
                     f'font-family="monospace" font-size="10" textLength="{width}" '
                     f'lengthAdjust="spacingAndGlyphs">{html.escape(label)}</text>')

    parts.extend([
        '<path d="M155 133 L165 133 L160 139 Z" fill="#ff0000"/>',
        '<line x1="160" y1="140" x2="160" y2="154" stroke="#ff0000"/>',
        f'<text x="160" y="159" fill="#ff0000" text-anchor="middle" '
        f'font-family="monospace" font-size="10" textLength="{current_width}" '
        f'lengthAdjust="spacingAndGlyphs">{html.escape(current_label)}</text>',
        '</svg>',
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--band", choices=INDEX, default="SW")
    parser.add_argument("--frequency", type=parse_frequency, default=parse_frequency("26.1MHz"),
                        help="center frequency, e.g. 25.9MHz or 25900000")
    parser.add_argument("--step", type=parse_step, default=parse_step("100k"),
                        help="current band's tuning step, e.g. 100k or 10k")
    parser.add_argument("--output", type=Path, default=Path("/tmp/ats-mini-ruler.svg"))
    args = parser.parse_args()
    render(args.band, args.frequency, args.step, args.output)
    print(f"Rendered {args.band} {short_frequency(args.frequency)}, "
          f"step {args.step} Hz -> {args.output}")


if __name__ == "__main__":
    main()
