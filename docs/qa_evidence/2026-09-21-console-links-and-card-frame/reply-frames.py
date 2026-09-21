#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""How many frames are drawn around a card page's reply box, read off the pixels.

    reply-frames.py <shot.png> <left> <right> <top> <bottom>

The three finishing items of card #AGNT include "the reply box still shows a faint second
border" and "Plan shows a bright outline at rest", and the two earlier readings of that area
were taken from words on the screen. Neither of these is a word. A vertical rule is a
one-pixel column lighter than the pixel on either side of it; a **frame's** rule runs the whole
height of the band and a **button's** runs only the height of the button, so counting how many
rows of the band each column is a rule in tells the two apart with no OCR and with no
dependence on what the theme's `@border` happens to be.

It prints:

    FRAMES <n> at=<x>,<x>…              rules that run the band: the frames around the box
    BUTTONS row=<y> <x>:<hex> …         rules that run a button's height, with their colour

`FRAMES 2` is one frame around the prompt box — its left edge and its right. `FRAMES 4` is the
border inside a border the owner reported: the console's own `QWidget#pane` frame outside the
composer's. The first `BUTTONS` cell is the first action's left edge, and its colour is the
whole of item 3: the quiet `@border` of a plain card-shaped button, or the bright `@text` ring
that was there instead.
"""
import sys

from PIL import Image


def rules(row):
    """The indices of every one-pixel column lighter than both its neighbours."""
    return [i for i in range(1, len(row) - 1)
            if sum(row[i]) > sum(row[i - 1]) + 24 and sum(row[i]) > sum(row[i + 1]) + 24]


def main():
    shot, left, right, top, bottom = sys.argv[1], *(int(v) for v in sys.argv[2:6])
    image = Image.open(shot).convert("RGB")
    bands = {y: [image.getpixel((x, y)) for x in range(left, right)] for y in range(top, bottom)}
    height = len(bands)

    hits = {}
    for y, row in bands.items():
        for x in rules(row):
            hits.setdefault(x, []).append(y)

    frames = sorted(x for x, ys in hits.items() if len(ys) >= 0.6 * height)
    print("FRAMES %d at=%s" % (len(frames), ",".join(str(left + x) for x in frames) or "-"))

    # A button's edge: tall enough to be a rule and not tall enough to be a frame. Its colour is
    # read at the middle of its own run, so a rounded corner is never what is sampled.
    buttons = sorted(x for x, ys in hits.items() if 0.12 * height <= len(ys) < 0.6 * height)
    if not buttons:
        print("BUTTONS row=- (no action row in the band)")
        return
    ys = sorted(hits[buttons[0]])
    row = ys[len(ys) // 2]
    cells = " ".join("%d:%02x%02x%02x" % (left + x, *bands[row][x])
                     for x in buttons if row in hits[x])
    print("BUTTONS row=%d %s" % (row, cells))


main()
