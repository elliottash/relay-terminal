#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Per-word colour check for the #4E13 labelled bolds.

    python3 labels_words.py

Finds the words Done:, Need:, Problem: and the plain bold word in the labels captures
(tesseract TSV word boxes), then classifies each word's own pixels against the theme's
label palette (green/amber/red). The labelled words must land in their family; the plain
bold word must land in none (plain foreground text).

Two distances, because white-on-dark text subpixel-fringes yellow/cyan at its edges and
those pale edge pixels land near the pale label colours on every word, bold or not (see
labels_bold_probe.py): FRINGE is reported for transparency, but the verdict uses CORE —
the label's own solid colour — with a MIN_CORE pixel count that fringe never reaches.
"""
import subprocess
import sys

from PIL import Image

FRINGE = 70
CORE = 25
MIN_CORE = 20

PALETTES = {
    "labels": ("relay-dark", {  # ANSI palette colours the transcript resolves to
        "green": [(0x7D, 0xD3, 0x99), (0x98, 0xE5, 0xB0)],
        "amber": [(0xEC, 0xC4, 0x76), (0xF8, 0xD5, 0x8E)],
        "red":   [(0xF2, 0x77, 0x7A), (0xFF, 0x8C, 0x8F)],
    }),
    "labels-beige": ("ibm-beige", {
        "green": [(0x18, 0x60, 0x2F), (0x12, 0x52, 0x2A)],
        "amber": [(0x7A, 0x54, 0x00), (0x5E, 0x42, 0x00)],
        "red":   [(0xA8, 0x20, 0x1A), (0x8A, 0x14, 0x0F)],
    }),
}

EXPECTED = {"Done:": "green", "Need:": "amber", "Problem:": "red", "bold": "plain"}


def word_boxes(image):
    image.save("/tmp/4e13-words.png")
    tsv = subprocess.run(["tesseract", "/tmp/4e13-words.png", "-", "--psm", "6", "tsv"],
                         capture_output=True, text=True).stdout.splitlines()
    boxes = {}
    for row in tsv[1:]:
        fields = row.split("\t")
        if len(fields) < 12 or not fields[11].strip():
            continue
        boxes.setdefault(fields[11].strip(), []).append(
            (int(fields[6]), int(fields[7]), int(fields[8]), int(fields[9])))
    return boxes


def classify(image, box, families):
    fringe = {name: 0 for name in families}
    core = {name: 0 for name in families}
    for x in range(box[0], min(box[0] + box[2], image.width)):
        for y in range(box[1], min(box[1] + box[3], image.height)):
            r, g, b = image.getpixel((x, y))
            for name, rgbs in families.items():
                if any(abs(r - cr) + abs(g - cg) + abs(b - cb) <= FRINGE for cr, cg, cb in rgbs):
                    fringe[name] += 1
                if any(abs(r - cr) + abs(g - cg) + abs(b - cb) <= CORE for cr, cg, cb in rgbs):
                    core[name] += 1
    return fringe, core


def main():
    ok = True
    for scene, (theme, families) in PALETTES.items():
        image = Image.open(f"implementer-{scene}-x.png").convert("RGB").crop((0, 96, 1440, 460))
        boxes = word_boxes(image)
        print(f"=== {scene} ({theme}) ===")
        for word, expected in EXPECTED.items():
            for left, top, width, height in boxes.get(word, []):
                fringe, core = classify(image, (left, top, width, height), families)
                verdict = (max(core, key=core.get)
                           if max(core.values()) >= MIN_CORE else "plain")
                print(f"  {word:<9} at x={left:<4} y={top:<3} core={core} fringe={fringe}"
                      f"  => {verdict}")
                ok &= verdict == expected
    print("labels colours:", "OK" if ok else "MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
