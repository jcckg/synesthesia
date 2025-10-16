#!/usr/bin/env python3
"""Generate the ReSyne display LUT mapping physical channels to preview RGB."""

from __future__ import annotations

import colorsys
import math
from pathlib import Path

DB_MIN = -120.0
DB_MAX = 20.0
DB_RANGE = DB_MAX - DB_MIN
MIN_FREQ = 20.0
MAX_FREQ = 20000.0
LOG_FREQ_MIN = math.log2(MIN_FREQ)
LOG_FREQ_RANGE = math.log2(MAX_FREQ) - LOG_FREQ_MIN
PHASE_OFFSET = 0.5
PHASE_SCALE = 0.5
MAX_AMPLITUDE = math.pow(10.0, DB_MAX / 20.0)
LUT_SIZE = 33


def denormalise_magnitude(value: float) -> float:
    value = min(max(value, 0.0), 1.0)
    db = value * DB_RANGE + DB_MIN
    return math.pow(10.0, db / 20.0)


def denormalise_frequency(value: float) -> float:
    value = min(max(value, 0.0), 1.0)
    log_freq = LOG_FREQ_MIN + value * LOG_FREQ_RANGE
    return math.pow(2.0, log_freq)


def decode_cosine_component(encoded: float) -> float:
    return (min(max(encoded, 0.0), 1.0) - PHASE_OFFSET) / PHASE_SCALE


def map_to_preview_rgb(magnitude_norm: float, frequency_norm: float, phase_cos_norm: float) -> tuple[float, float, float]:
    magnitude = denormalise_magnitude(magnitude_norm)
    _ = denormalise_frequency(frequency_norm)
    cos_phase = decode_cosine_component(phase_cos_norm)

    amplitude_ratio = min(max(magnitude / MAX_AMPLITUDE, 0.0), 1.0)
    hue_degrees = (1.0 - frequency_norm) * 280.0 + 20.0
    hue = (hue_degrees % 360.0) / 360.0

    saturation = min(max(0.25 + amplitude_ratio * 0.6, 0.0), 1.0)
    phase_weight = 0.55 + 0.45 * cos_phase
    value = min(max(math.sqrt(amplitude_ratio) * phase_weight, 0.0), 1.0)

    rgb = colorsys.hsv_to_rgb(hue, saturation, value)
    return tuple(min(max(colour, 0.0), 1.0) for colour in rgb)


def generate_lut(size: int = LUT_SIZE) -> list[tuple[float, float, float]]:
    samples: list[tuple[float, float, float]] = []
    for b in range(size):
        phase_cos_norm = b / (size - 1)
        for g in range(size):
            frequency_norm = g / (size - 1)
            for r in range(size):
                magnitude_norm = r / (size - 1)
                samples.append(map_to_preview_rgb(magnitude_norm, frequency_norm, phase_cos_norm))
    return samples


def write_cube_file(path: Path, size: int = LUT_SIZE) -> None:
    samples = generate_lut(size)
    with path.open("w", encoding="utf-8") as stream:
        stream.write('TITLE "ReSyne Display (Magnitude/LogFreq/PhaseCos)"\n')
        stream.write(f"LUT_3D_SIZE {size}\n")
        stream.write("DOMAIN_MIN 0.0 0.0 0.0\n")
        stream.write("DOMAIN_MAX 1.0 1.0 1.0\n")
        for r, g, b in samples:
            stream.write(f"{r:.6f} {g:.6f} {b:.6f}\n")


def main() -> None:
    output = Path(__file__).resolve().parent.parent / "assets" / "luts" / "ReSyne_Display_v1.cube"
    output.parent.mkdir(parents=True, exist_ok=True)
    write_cube_file(output)


if __name__ == "__main__":
    main()
