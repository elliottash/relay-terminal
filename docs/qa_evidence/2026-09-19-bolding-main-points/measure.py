#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads the label colours back out of a screenshot (card #CVHT).

    measure.py implementer-dark.png done=#c692e9,#d8aaf5 need=... problem=... plain=...

For each named colour it prints how many pixels of the image are exactly that colour (and, in
brackets, how many are within a small distance of it, for antialiased glyph edges), the rows and
columns they occupy, and whether the colour is present at all. The three role colours must be
present, each in its own narrow band of rows, so the notes can show the colour stops at the label
while the rest of the line — including a plain **Bold** — stays in the terminal foreground.
"""
import sys
from collections import defaultdict

from PIL import Image


def parse(value):
    name, _, colours = value.partition("=")
    return name, [tuple(int(hexcode[i:i + 2], 16) for i in (1, 3, 5)) for hexcode in colours.split(",")]


def near(a, b, tolerance=24):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def main():
    path = sys.argv[1]
    targets = [parse(v) for v in sys.argv[2:]]
    image = Image.open(path).convert("RGB")
    width, height = image.size
    pixels = image.load()

    counts = {colour: count for count, colour in (image.getcolors(width * height) or [])}
    wanted = {colour: name for name, colours in targets for colour in colours}
    exact = defaultdict(list)
    for y in range(height):
        for x in range(width):
            pixel = pixels[x, y]
            if pixel in wanted:
                exact[pixel].append((x, y))

    print("image %s (%dx%d)" % (path.rsplit("/", 1)[-1], width, height))
    plain_points = [p for name, colours in targets if name == "plain" for c in colours for p in exact[c]]
    for name, colours in targets:
        found = [(c, exact[c]) for c in colours if exact[c]]
        count = sum(len(points) for _, points in found)
        near_count = sum(n for c, n in counts.items() if any(near(c, t) for t in colours))
        if not found:
            print("  %-8s ABSENT (no pixel of %s)" % (name, ", ".join("#%02x%02x%02x" % c for c in colours)))
            continue
        points = [p for _, pts in found for p in pts]
        rows = sorted({y for _, y in points})
        columns = sorted({x for x, _ in points})
        bands, start = [], rows[0]
        for a, b in zip(rows, rows[1:]):
            if b - a > 4:
                bands.append((start, a))
                start = b
        bands.append((start, rows[-1]))
        print("  %-8s %6d exact pixels (%d within 24), rows %d..%d, %d row band(s) %s, columns %d..%d"
              % (name, count, near_count, rows[0], rows[-1], len(bands), bands, columns[0], columns[-1]))
        for colour, pts in found:
            print("           #%02x%02x%02x: %d pixels" % (colour + (len(pts),)))
        # On the same rows, where does the terminal's own foreground run to? A label colour that
        # stops before it is a colour that covers the label only (the reply's plain words, and the
        # unlabelled **Bold**, are not swept up with it).
        if name != "plain" and plain_points:
            band_rows = {y for _, y in points}
            here = [x for x, y in plain_points if y in band_rows]
            if here:
                print("           plain foreground on these rows: columns %d..%d" % (min(here), max(here)))


if __name__ == "__main__":
    main()
