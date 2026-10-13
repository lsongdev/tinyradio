#!/usr/bin/env python3
"""Render the firmware's 320x170 frequency ruler as an SVG preview.

Example:
  python3 tools/render_ruler.py --band SW --frequency 25.5MHz --step 100k \
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
    tuning = Tuning(band_index, frequency, step)
    ticks = []
    for i in range(27):
        offset = i - CENTER_INDEX
        preview = Tuning(tuning.band_index, tuning.frequency, step)
        preview.steps = tuning.steps.copy()
        preview.turn(offset)
        valid = True
        if offset:
            closer = Tuning(tuning.band_index, tuning.frequency, step)
            closer.steps = tuning.steps.copy()
            closer.turn(offset - 1 if offset > 0 else offset + 1)
            valid = (closer.band_index != preview.band_index or
                     closer.frequency != preview.frequency)
        ticks.append({
            "hz": preview.frequency,
            "step": preview.steps[preview.band_index],
            "band": preview.band_index,
            "x": CENTER + (i - CENTER_INDEX) * TICK_GAP,
            "valid": valid,
        })
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
    def is_edge(index):
        return ((index > 0 and ticks[index - 1]["valid"] and
                 ticks[index - 1]["band"] != ticks[index]["band"]) or
                (index < 26 and ticks[index + 1]["valid"] and
                 ticks[index + 1]["band"] != ticks[index]["band"]))

    def edge_label_x(index):
        tick = ticks[index]
        if tick["band"] == 2 and index < 26 and ticks[index + 1]["band"] != tick["band"]:
            for j in range(index - 1, -1, -1):
                if (not ticks[j]["valid"] or ticks[j]["band"] != tick["band"] or
                        is_edge(j)):
                    continue
                if (ticks[j]["hz"] // ticks[j]["step"]) % 5 == 0:
                    return ticks[j]["x"] + 5 * TICK_GAP
        if tick["band"] == 3 and index > 0 and ticks[index - 1]["band"] == 2:
            sw_label_x = ticks[index - 1]["x"]
            for j in range(index - 2, -1, -1):
                if (not ticks[j]["valid"] or ticks[j]["band"] != 2 or
                        is_edge(j)):
                    continue
                if (ticks[j]["hz"] // ticks[j]["step"]) % 5 == 0:
                    sw_label_x = ticks[j]["x"] + 5 * TICK_GAP
                    break
            return sw_label_x + 21
        previous_band = index > 0 and ticks[index - 1]["band"] != tick["band"]
        return tick["x"] + (6 if previous_band else -6)

    for i, tick in enumerate(ticks):
        if not tick["valid"] or not 0 <= tick["x"] < WIDTH:
            continue
        edge = is_edge(i)
        major = edge or (tick["hz"] // tick["step"]) % 5 == 0
        adjacent = tick["band"] != band_index
        tick_color = "#00ffff" if adjacent else "#d3d3d3" if major else "#505050"
        parts.append(f'<line x1="{tick["x"]}" y1="140" x2="{tick["x"]}" '
                     f'y2="{140 + (12 if major else 6)}" stroke="{tick_color}"/>')

        if not major:
            continue
        if edge:
            label = BANDS[tick["band"]].name
            label_x = edge_label_x(i)
        else:
            label = short_frequency(tick["hz"])
            label_x = tick["x"]
        label_width = 6 * len(label)
        collides_with_band_name = False
        if not edge:
            for j, other in enumerate(ticks):
                if not other["valid"] or not is_edge(j):
                    continue
                other_width = 6 * len(BANDS[other["band"]].name)
                if abs(label_x - edge_label_x(j)) * 2 < label_width + other_width + 2:
                    collides_with_band_name = True
                    break
        if collides_with_band_name:
            continue
        if not (label_x + label_width / 2 < CENTER - current_width / 2 - 6 or
                label_x - label_width / 2 > CENTER + current_width / 2 + 6):
            continue
        color = "#00ffff" if adjacent else "#d3d3d3"
        text = html.escape(label)
        parts.append(f'<text x="{label_x}" y="159" fill="{color}" text-anchor="middle" '
                     f'font-family="monospace" font-size="10" textLength="{label_width}" '
                     f'lengthAdjust="spacingAndGlyphs">{text}</text>')

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
    parser.add_argument("--frequency", type=parse_frequency, default=parse_frequency("25.5MHz"),
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
