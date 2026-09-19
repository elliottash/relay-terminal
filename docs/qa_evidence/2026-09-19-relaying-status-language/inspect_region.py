#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Inspect one region of one capture: quantized colour clusters + upscaled OCR.

    python3 inspect_region.py <image> <x> <y> <w> <h> [scale]

Prints, for the crop, the colour clusters (quantized to 8 per channel) with pixel
counts and bounding boxes, then an OCR pass on the crop upscaled and contrasted.
Used to identify what the analyse.py families actually matched.
"""
import subprocess
import sys
import tempfile

from PIL import Image, ImageOps


def main() -> None:
    path, x, y, w, h = sys.argv[1], *map(int, sys.argv[2:6])
    scale = int(sys.argv[6]) if len(sys.argv) > 6 else 3
    image = Image.open(path).convert("RGB").crop((x, y, x + w, y + h))

    clusters: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    for py in range(image.height):
        for px in range(image.width):
            r, g, b = image.getpixel((px, py))
            key = (r >> 4, g >> 4, b >> 4)
            clusters.setdefault(key, []).append((px, py))

    print(f"{path} region ({x},{y})+{w}x{h}:")
    for key, points in sorted(clusters.items(), key=lambda kv: -len(kv[1]))[:12]:
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        colour = tuple(c << 4 for c in key)
        print(f"  #{colour[0]:02x}{colour[1]:02x}{colour[2]:02x}  {len(points):6d} px  "
              f"bbox=({min(xs)},{min(ys)})-({max(xs)},{max(ys)})")

    big = image.resize((w * scale, h * scale), Image.LANCZOS)
    big = ImageOps.autocontrast(big.convert("L"))
    with tempfile.NamedTemporaryFile(suffix=".png") as scratch:
        big.save(scratch.name)
        text = subprocess.run(["tesseract", scratch.name, "-", "--psm", "6"],
                              capture_output=True, text=True).stdout
    print(f"  OCR (x{scale}, autocontrast): {text.strip()!r}")


if __name__ == "__main__":
    main()
