#!/usr/bin/env python3
"""Build eight shared-palette L8 weather backgrounds for the STM32 GUI.

The source artwork is kept at full resolution.  The C installer contains only
240x136 indexed images and expands each pixel 2x while programming QSPI.  This
keeps the temporary installer firmware small enough for the STM32F746 flash,
while QSPI still stores native 480x272 frames for fast runtime theme changes.
"""

from pathlib import Path
from PIL import Image, ImageEnhance, ImageFilter, ImageOps


TOOLS_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = TOOLS_DIR.parent
SOURCE_DIR = FIRMWARE_DIR / "Assets" / "weather_sources"
OUTPUT_DIR = FIRMWARE_DIR / "Assets" / "weather_generated"
HEADER_H = FIRMWARE_DIR / "Core" / "Inc" / "weather_backgrounds.h"
SOURCE_C = FIRMWARE_DIR / "Core" / "Src" / "weather_backgrounds.c"

FULL_SIZE = (480, 272)
LOW_SIZE = (240, 136)

THEMES = [
    ("CLEAR_DAY", "clear_day_source.png", 0.68),
    ("DAWN", "dawn_source.png", 0.62),
    ("SUNSET", "sunset_source.png", 0.58),
    ("NIGHT", "night_source.png", 0.80),
    ("CLOUDY", "cloudy_source.png", 0.52),
    ("RAIN", "rain_source.png", 0.70),
    ("STORM", "storm_source.png", 0.75),
    ("SNOW", "snow_source.png", 0.50),
]

# Indices 0..15 are deliberately stable because the firmware uses them for
# text, warnings, and alarm pages.  Background quantization shares the same
# palette, so LTDC never needs a palette reload during a theme change.
RESERVED = [
    (0, 0, 0),       # 0 unused/black
    (0, 0, 0),       # 1 LCD_BLACK
    (255, 255, 255), # 2 LCD_WHITE
    (255, 72, 72),   # 3 LCD_RED
    (70, 235, 135),  # 4 LCD_GREEN
    (80, 145, 255),  # 5 LCD_BLUE
    (80, 225, 255),  # 6 LCD_CYAN
    (255, 220, 70),  # 7 LCD_YELLOW
    (3, 18, 45),     # 8 LCD_DARK_BLUE
    (15, 42, 67),    # 9 LCD_CARD
    (20, 53, 79),    # 10 LCD_CARD_ALT
    (185, 205, 220), # 11 LCD_MUTED
    (255, 145, 45),  # 12 LCD_ORANGE
    (45, 215, 190),  # 13 LCD_TEAL
    (85, 45, 5),     # 14 LCD_WARNING_BG
    (82, 8, 18),     # 15 LCD_DANGER_BG
]


def fit_background(path: Path, brightness: float) -> Image.Image:
    image = Image.open(path).convert("RGB")
    image = ImageOps.fit(image, FULL_SIZE, Image.Resampling.LANCZOS)
    image = image.filter(ImageFilter.GaussianBlur(radius=0.7))
    image = ImageEnhance.Brightness(image).enhance(brightness)

    # A soft central/lower scrim gives white values dependable contrast without
    # creating a visible rectangle or card.
    overlay = Image.new("RGBA", FULL_SIZE, (0, 0, 0, 0))
    alpha = Image.new("L", FULL_SIZE)
    pixels = alpha.load()
    width, height = FULL_SIZE
    for y in range(height):
        for x in range(width):
            dx = abs(x - width / 2) / (width / 2)
            dy = abs(y - height * 0.56) / (height * 0.56)
            centre = max(0.0, 1.0 - 0.58 * dx - 0.42 * dy)
            pixels[x, y] = int(38 * centre)
    overlay.putalpha(alpha)
    return Image.alpha_composite(image.convert("RGBA"), overlay).convert("RGB")


def make_master_palette(images: list[Image.Image]) -> list[tuple[int, int, int]]:
    montage = Image.new("RGB", (FULL_SIZE[0], FULL_SIZE[1] * len(images)))
    for index, image in enumerate(images):
        montage.paste(image, (0, index * FULL_SIZE[1]))

    adaptive = montage.quantize(
        colors=240,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    raw = adaptive.getpalette()[: 240 * 3]
    learned = [tuple(raw[i : i + 3]) for i in range(0, len(raw), 3)]
    palette = RESERVED + learned
    return (palette + [(0, 0, 0)] * 256)[:256]


def palette_image(palette: list[tuple[int, int, int]]) -> Image.Image:
    result = Image.new("P", (1, 1))
    flat = [channel for rgb in palette for channel in rgb]
    result.putpalette(flat)
    return result


def format_u8_array(data: bytes, indent: str = "        ") -> str:
    lines = []
    for start in range(0, len(data), 24):
        chunk = data[start : start + 24]
        lines.append(indent + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(lines)


def write_header() -> None:
    text = """#ifndef WEATHER_BACKGROUNDS_H
#define WEATHER_BACKGROUNDS_H

#include <stdint.h>

#define WEATHER_BACKGROUND_COUNT       8U
#define WEATHER_BACKGROUND_LOW_WIDTH   240U
#define WEATHER_BACKGROUND_LOW_HEIGHT  136U
#define WEATHER_BACKGROUND_LOW_BYTES   \\
    (WEATHER_BACKGROUND_LOW_WIDTH * WEATHER_BACKGROUND_LOW_HEIGHT)

extern const uint8_t weather_backgrounds_l8
    [WEATHER_BACKGROUND_COUNT][WEATHER_BACKGROUND_LOW_BYTES];
extern const uint32_t weather_background_clut[256];

#endif /* WEATHER_BACKGROUNDS_H */
"""
    HEADER_H.write_text(text, encoding="utf-8")


def write_source(indexed: list[Image.Image], palette: list[tuple[int, int, int]]) -> None:
    parts = [
        '#include "weather_backgrounds.h"',
        "",
        "const uint8_t weather_backgrounds_l8",
        "    [WEATHER_BACKGROUND_COUNT][WEATHER_BACKGROUND_LOW_BYTES] =",
        "{",
    ]
    for (name, _, _), image in zip(THEMES, indexed):
        parts.append(f"    /* {name} */")
        parts.append("    {")
        parts.append(format_u8_array(image.tobytes()))
        parts.append("    },")
    parts.extend(["};", "", "const uint32_t weather_background_clut[256] =", "{"])
    for start in range(0, 256, 8):
        values = []
        for red, green, blue in palette[start : start + 8]:
            values.append(f"0x{red:02X}{green:02X}{blue:02X}UL")
        parts.append("    " + ", ".join(values) + ",")
    parts.extend(["};", ""])
    SOURCE_C.write_text("\n".join(parts), encoding="utf-8")


def main() -> None:
    OUTPUT_DIR.mkdir(exist_ok=True)
    full_images = [
        fit_background(SOURCE_DIR / filename, brightness)
        for _, filename, brightness in THEMES
    ]
    palette = make_master_palette(full_images)
    master = palette_image(palette)
    indexed = []

    for (name, _, _), image in zip(THEMES, full_images):
        low = image.resize(LOW_SIZE, Image.Resampling.LANCZOS)
        # No error-diffusion dots: the source art is already softly blurred,
        # and clean palette bands look better on this small RGB panel.
        quantized = low.quantize(palette=master, dither=Image.Dither.NONE)
        indexed.append(quantized)

        # This preview is the exact 2x-expanded image the TFT will display.
        preview = quantized.resize(FULL_SIZE, Image.Resampling.NEAREST)
        preview.putpalette([channel for rgb in palette for channel in rgb])
        preview.convert("RGB").save(OUTPUT_DIR / f"{name.lower()}_480x272.png")

    write_header()
    write_source(indexed, palette)
    print(f"Generated {len(indexed)} backgrounds, {LOW_SIZE[0]}x{LOW_SIZE[1]} each")


if __name__ == "__main__":
    main()
