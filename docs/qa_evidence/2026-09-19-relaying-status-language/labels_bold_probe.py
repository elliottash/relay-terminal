#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dump the label-family pixels inside the plain `bold` word's OCR box.

    python3 labels_bold_probe.py [scene]

For each matched pixel: offset within the box and its raw RGB, plus the same scan of the
neighbouring words on the line (`plain`, `word`) to tell a real tint apart from OCR-box
padding and antialiasing noise.
"""
import subprocess
import sys

from PIL import Image

DIST = 70
SCENE = sys.argv[1] if len(sys.argv) > 1 else "labels"
FAMILIES = {
    "green": [(0x7D, 0xD3, 0x99), (0x98, 0xE5, 0xB0)],
    "amber": [(0xEC, 0xC4, 0x76), (0xF8, 0xD5, 0x8E)],
    "red":   [(0xF2, 0x77, 0x7A), (0xFF, 0x8C, 0x8F)],
} if SCENE == "labels" else {
    "green": [(0x18, 0x60, 0x2F), (0x12, 0x52, 0x2A)],
    "amber": [(0x7A, 0x54, 0x00), (0x5E, 0x42, 0x00)],
    "red":   [(0xA8, 0x20, 0x1A), (0x8A, 0x14, 0x0F)],
}


def family_of(rgb):
    r, g, b = rgb
    for name, rgbs in FAMILIES.items():
        if any(abs(r - cr) + abs(g - cg) + abs(b - cb) <= DIST for cr, cg, cb in rgbs):
            return name
    return None


def main():
    image = Image.open(f"implementer-{SCENE}-x.png").convert("RGB").crop((0, 96, 1440, 460))
    image.save("/tmp/4e13-probe.png")
    tsv = subprocess.run(["tesseract", "/tmp/4e13-probe.png", "-", "--psm", "6", "tsv"],
                         capture_output=True, text=True).stdout.splitlines()
    for row in tsv[1:]:
        fields = row.split("\t")
        if len(fields) < 12 or fields[11].strip() not in ("plain", "bold", "word", "stays"):
            continue
        left, top, width, height = (int(fields[i]) for i in range(6, 10))
        print(f"{fields[11]}: box x={left} y={top} w={width} h={height}")
        for x in range(left, min(left + width, image.width)):
            for y in range(top, min(top + height, image.height)):
                name = family_of(image.getpixel((x, y)))
                if name:
                    print(f"  ({x - left:>3},{y - top:>3}) {name} rgb={image.getpixel((x, y))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
