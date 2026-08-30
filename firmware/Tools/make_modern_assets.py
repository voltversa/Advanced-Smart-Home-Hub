#!/usr/bin/env python3
"""Generate QSPI-resident modern bitmap fonts and dashboard icons.

The generated blob contains three proportional DejaVu Sans font sizes and
seven 20x20 monochrome icons. It is only linked into the one-time installer;
normal firmware reads the blob directly from memory-mapped QSPI.
"""

from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


TOOLS_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = TOOLS_DIR.parent
SOURCE_C = FIRMWARE_DIR / "Core" / "Src" / "gui_modern_assets.c"
HEADER_H = FIRMWARE_DIR / "Core" / "Inc" / "gui_modern_assets.h"
PREVIEW = FIRMWARE_DIR / "Assets" / "modern_font_icons_preview.png"

FIRST_CHARACTER = 32
LAST_CHARACTER = 90
GLYPH_COUNT = LAST_CHARACTER - FIRST_CHARACTER + 1
FONT_SPECS = [
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11),
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15),
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 23),
]

ICON_NAMES = [
    "temperature",
    "humidity",
    "pressure",
    "co2",
    "air_quality",
    "wifi",
    "cloud",
]
ICON_WIDTH = 20
ICON_HEIGHT = 20
ICON_ROW_BYTES = (ICON_WIDTH + 7) // 8

MODERN_MAGIC = 0x544E464D  # "MFNT" in little-endian memory
MODERN_VERSION = 1
HEADER_BYTES = 64
DESCRIPTOR_BYTES = 12


def align4(value: int) -> int:
    return (value + 3) & ~3


def pack_mask(image: Image.Image, threshold: int = 80) -> bytes:
    pixels = image.load()
    row_bytes = (image.width + 7) // 8
    output = bytearray(row_bytes * image.height)
    for y in range(image.height):
        for x in range(image.width):
            if pixels[x, y] >= threshold:
                output[y * row_bytes + (x // 8)] |= 0x80 >> (x & 7)
    return bytes(output)


def build_font(path: str, size: int):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    descriptors = []
    bitmap = bytearray()

    for code in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
        character = chr(code)
        left, top, right, bottom = font.getbbox(character)
        width = max(0, right - left)
        height = max(0, bottom - top)
        advance = max(1, round(font.getlength(character)))
        row_bytes = (width + 7) // 8 if width else 0
        data_offset = len(bitmap)

        if width and height:
            glyph_image = Image.new("L", (width, height), 0)
            draw = ImageDraw.Draw(glyph_image)
            draw.text((-left, -top), character, font=font, fill=255)
            bitmap.extend(pack_mask(glyph_image))

        descriptors.append(
            (left, top, width, height, advance, row_bytes, data_offset)
        )

    return {
        "size": size,
        "line_height": line_height,
        "ascent": ascent,
        "descriptors": descriptors,
        "bitmap": bytes(bitmap),
        "font": font,
    }


def make_icon(index: int) -> Image.Image:
    image = Image.new("L", (ICON_WIDTH, ICON_HEIGHT), 0)
    draw = ImageDraw.Draw(image)
    white = 255

    if index == 0:  # thermometer
        draw.rounded_rectangle((8, 2, 12, 14), radius=2, outline=white, width=2)
        draw.ellipse((6, 11, 14, 19), outline=white, width=2)
        draw.line((10, 6, 10, 15), fill=white, width=2)
        draw.ellipse((8, 13, 12, 17), fill=white)
    elif index == 1:  # water droplet
        draw.polygon([(10, 1), (4, 10), (4, 14), (7, 18),
                      (13, 18), (16, 14), (16, 10)], outline=white)
        draw.line((10, 2, 5, 11, 5, 14, 8, 17), fill=white, width=2)
        draw.line((10, 2, 15, 11, 15, 14, 12, 17), fill=white, width=2)
    elif index == 2:  # pressure gauge
        draw.arc((2, 3, 18, 19), 185, 355, fill=white, width=2)
        draw.line((10, 12, 15, 7), fill=white, width=2)
        draw.ellipse((8, 10, 12, 14), fill=white)
        draw.line((4, 16, 16, 16), fill=white, width=2)
    elif index == 3:  # CO2 molecule
        draw.line((5, 10, 15, 10), fill=white, width=2)
        draw.ellipse((1, 6, 8, 13), outline=white, width=2)
        draw.ellipse((7, 5, 13, 11), outline=white, width=2)
        draw.ellipse((12, 6, 19, 13), outline=white, width=2)
        draw.text((5, 12), "CO2", font=ImageFont.truetype(FONT_SPECS[0][0], 7),
                  fill=white)
    elif index == 4:  # leaf / air quality
        draw.ellipse((3, 2, 17, 16), outline=white, width=2)
        draw.line((5, 16, 15, 5), fill=white, width=2)
        draw.line((9, 11, 6, 8), fill=white)
        draw.line((11, 9, 14, 9), fill=white)
        draw.line((4, 18, 10, 12), fill=white, width=2)
    elif index == 5:  # Wi-Fi
        draw.arc((1, 2, 19, 18), 215, 325, fill=white, width=2)
        draw.arc((5, 6, 15, 16), 215, 325, fill=white, width=2)
        draw.ellipse((8, 15, 12, 19), fill=white)
    elif index == 6:  # cloud
        draw.ellipse((2, 8, 11, 17), outline=white, width=2)
        draw.ellipse((6, 4, 16, 16), outline=white, width=2)
        draw.ellipse((12, 8, 19, 17), outline=white, width=2)
        draw.line((6, 17, 16, 17), fill=white, width=2)

    return image


def build_blob(fonts, icons):
    descriptor_offsets = []
    bitmap_offsets = []
    cursor = HEADER_BYTES

    for _font in fonts:
        descriptor_offsets.append(cursor)
        cursor += GLYPH_COUNT * DESCRIPTOR_BYTES
    cursor = align4(cursor)

    for font in fonts:
        bitmap_offsets.append(cursor)
        cursor += len(font["bitmap"])
        cursor = align4(cursor)

    icons_offset = cursor
    icon_data = b"".join(pack_mask(icon) for icon in icons)
    cursor += len(icon_data)
    total_size = align4(cursor)

    blob = bytearray([0xFF] * total_size)
    struct.pack_into("<IHH", blob, 0,
                     MODERN_MAGIC, MODERN_VERSION, GLYPH_COUNT)
    struct.pack_into("<BBBBBB", blob, 8,
                     FIRST_CHARACTER, LAST_CHARACTER, len(fonts), len(icons),
                     ICON_WIDTH, ICON_HEIGHT)
    for index, font in enumerate(fonts):
        blob[16 + index] = font["line_height"]
        blob[19 + index] = font["ascent"]
        struct.pack_into("<I", blob, 24 + index * 4,
                         descriptor_offsets[index])
        struct.pack_into("<I", blob, 36 + index * 4,
                         bitmap_offsets[index])
    struct.pack_into("<II", blob, 48, icons_offset, total_size)

    for font_index, font in enumerate(fonts):
        descriptor_cursor = descriptor_offsets[font_index]
        for descriptor in font["descriptors"]:
            left, top, width, height, advance, row_bytes, data_offset = descriptor
            struct.pack_into(
                "<bBBBBBHI",
                blob,
                descriptor_cursor,
                left,
                top,
                width,
                height,
                advance,
                row_bytes,
                0,
                data_offset,
            )
            descriptor_cursor += DESCRIPTOR_BYTES

        start = bitmap_offsets[font_index]
        blob[start:start + len(font["bitmap"])] = font["bitmap"]

    blob[icons_offset:icons_offset + len(icon_data)] = icon_data
    return bytes(blob)


def write_sources(blob: bytes) -> None:
    HEADER_H.write_text(
        f"""#ifndef GUI_MODERN_ASSETS_H
#define GUI_MODERN_ASSETS_H

#include <stdint.h>

#define GUI_MODERN_ASSET_BYTES  {len(blob)}UL

extern const uint8_t gui_modern_assets[GUI_MODERN_ASSET_BYTES];

#endif /* GUI_MODERN_ASSETS_H */
""",
        encoding="utf-8",
    )

    with SOURCE_C.open("w", encoding="utf-8", newline="\n") as output:
        output.write('#include "gui_modern_assets.h"\n\n')
        output.write("/* Rasterized DejaVu Sans glyphs and custom line icons. */\n")
        output.write("__attribute__((aligned(32)))\n")
        output.write(
            "const uint8_t gui_modern_assets[GUI_MODERN_ASSET_BYTES] =\n{\n"
        )
        for start in range(0, len(blob), 20):
            chunk = blob[start:start + 20]
            output.write("    ")
            output.write(", ".join(f"0x{value:02X}U" for value in chunk))
            if start + len(chunk) < len(blob):
                output.write(",")
            output.write("\n")
        output.write("};\n")


def write_preview(fonts, icons) -> None:
    preview = Image.new("RGB", (620, 190), (5, 8, 14))
    draw = ImageDraw.Draw(preview)
    colours = [(72, 215, 255), (79, 227, 164), (255, 255, 255)]
    labels = ["MODERN SMALL  24.6 C", "ROOM AIR QUALITY  GOOD",
              "CO2  800 PPM"]
    y = 12
    for font, label, colour in zip(fonts, labels, colours):
        draw.text((18, y), label, font=font["font"], fill=colour)
        y += font["line_height"] + 10

    x = 18
    for icon, name in zip(icons, ICON_NAMES):
        coloured = Image.new("RGB", icon.size, (5, 8, 14))
        tint = Image.new("RGB", icon.size, (72, 215, 255))
        coloured.paste(tint, mask=icon)
        preview.paste(coloured, (x, 118))
        draw.text((x - 2, 145), name[:5].upper(),
                  font=fonts[0]["font"], fill=(140, 166, 183))
        x += 84

    preview.save(PREVIEW, optimize=True)


def main() -> None:
    fonts = [build_font(path, size) for path, size in FONT_SPECS]
    icons = [make_icon(index) for index in range(len(ICON_NAMES))]
    blob = build_blob(fonts, icons)
    write_sources(blob)
    write_preview(fonts, icons)
    print(f"Generated {len(blob)} bytes, {GLYPH_COUNT} glyphs x 3, "
          f"{len(icons)} icons")


if __name__ == "__main__":
    main()
