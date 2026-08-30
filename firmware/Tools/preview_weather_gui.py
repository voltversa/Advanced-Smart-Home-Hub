#!/usr/bin/env python3
"""Render a desktop preview of the card-free 480x272 weather dashboard."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


TOOLS_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = TOOLS_DIR.parent
BACKGROUND_DIR = FIRMWARE_DIR / "Assets" / "weather_generated"
OUTPUT_DIR = FIRMWARE_DIR / "Assets" / "previews"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def font(size: int, bold: bool = False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def centered(draw, y, text, face, fill, shadow=(0, 0, 0)):
    box = draw.textbbox((0, 0), text, font=face)
    x = (480 - (box[2] - box[0])) // 2
    draw.text((x + 1, y + 1), text, font=face, fill=shadow)
    draw.text((x, y), text, font=face, fill=fill)


def render(theme: str, city: str, condition: str, outside: int):
    image = Image.open(
        BACKGROUND_DIR / f"{theme}_480x272.png"
    ).convert("RGB")
    draw = ImageDraw.Draw(image)
    white = (255, 255, 255)
    cyan = (80, 225, 255)
    green = (70, 235, 135)
    muted = (185, 205, 220)

    draw.text((11, 5), "14:42", font=font(11), fill=(0, 0, 0))
    draw.text((10, 4), "14:42", font=font(11), fill=white)
    date = "FRI 14 AUG 2026"
    width = draw.textbbox((0, 0), date, font=font(11))[2]
    draw.text((471 - width, 5), date, font=font(11), fill=(0, 0, 0))
    draw.text((470 - width, 4), date, font=font(11), fill=white)

    centered(draw, 23, "MY LOCATION", font(11), white)
    centered(draw, 36, city, font(23, True), white)
    centered(draw, 57, f"{outside} C", font(46), white)
    centered(draw, 110, condition, font(15), white)
    centered(draw, 132, "H:24 C  L:15 C  FEELS:20 C", font(11), white)

    centered(draw, 151, "ROOM ONE", font(15), cyan)
    centered(
        draw,
        174,
        "TEMP 23.4 C   HUM 45.2 %   CO2 650 PPM",
        font(11),
        white,
    )
    centered(draw, 191, "PRESSURE 1004.4 HPA", font(11), white)
    centered(draw, 209, "GOOD", font(15, True), green)
    centered(draw, 232, "CLEAN AIR - CO2 BELOW 1000 PPM", font(11), white)
    centered(draw, 255, "WIFI ON   CLOUD ON", font(11), green)
    return image


def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    samples = [
        ("clear_day", "BRUSSELS", "SUNNY", 21),
        ("sunset", "BRUSSELS", "CLEAR", 18),
        ("night", "BRUSSELS", "CLEAR NIGHT", 15),
        ("rain", "BRUSSELS", "LIGHT RAIN", 13),
    ]

    previews = []
    for theme, city, condition, outside in samples:
        preview = render(theme, city, condition, outside)
        preview.save(OUTPUT_DIR / f"weather_gui_{theme}_preview.png")
        previews.append(preview)

    sheet = Image.new("RGB", (960, 544), (15, 15, 15))
    for index, preview in enumerate(previews):
        sheet.paste(preview, ((index % 2) * 480, (index // 2) * 272))
    sheet.save(OUTPUT_DIR / "weather_gui_preview_sheet.png")


if __name__ == "__main__":
    main()
