#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the subagent-badge screenshots (card #YMSR) and say what is actually in them.

    python3 analyze.py            # writes analyze-output.txt beside the frames

For each scene it reports:

  * the OCR of the pane header row (the 300 % crop, y 36..76), where the state word and the badge
    are;
  * where the badge's chip is: the pixels in that row that are the chip's own fill, i.e. the
    agent's violet mixed 16 % into the theme background, and their bounding box;
  * how much of the agent's violet ink is inside that box (the star and the number's strokes) and
    the pixels that differ from the `none` scene — the badge and the state word are the only things
    that may differ, so the difference proves the badge was painted, not just OCR'd.

Nothing here is eyeballed: every claim in the card's evidence section is a number this prints.
The theme is read from the file the run used (relay-dark), so the two colours are the run's own.
"""
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

SCENES = ["none", "one", "two", "busy", "endedlive", "ended"]
ROW = (0, 36, 1440, 76)      # the pane header row, as the first run's tesseract -tsv showed
THEME = Path("../../../data/theme/themes/relay-dark.toml")


def token(name):
    text = THEME.read_text()
    match = re.search(r'^%s = "#([0-9a-fA-F]{6})"' % name, text, re.M)
    return tuple(int(match.group(1)[i:i + 2], 16) for i in (0, 2, 4))


AGENT = token("agent")                       # the badge's violet
BACKGROUND = token("background")
FILL = tuple(round(AGENT[i] * 0.16 + BACKGROUND[i] * 0.84) for i in range(3))   # the chip's ground
INK = AGENT                                  # the star and the number, at 5.7:1 on that fill


def near(a, b):
    return sum(abs(a[i] - b[i]) for i in range(3))


def row(scene):
    return Image.open("implementer-%s.png" % scene).convert("RGB").crop(ROW)


def ocr(img):
    img.save("/tmp/ymsr-ocr.png")
    return subprocess.run(["tesseract", "/tmp/ymsr-ocr.png", "-", "--psm", "7"],
                          capture_output=True, text=True).stdout.strip()


def chip(img, tolerance=24):
    """The badge's box: the widest run of columns in the header row that carry the chip's own fill
    colour. A chip is a solid 18 px-high block, so its columns are full of the fill while the
    background, the title and the state word are not; the longest such run is the badge."""
    counts = [sum(1 for y in range(img.height) if near(img.getpixel((x, y)), FILL) <= tolerance)
              for x in range(img.width)]
    runs, start = [], None
    for x, n in enumerate(counts + [0]):
        if n >= 4 and start is None:
            start = x
        elif n < 4 and start is not None:
            runs.append((start, x)); start = None
    if not runs:
        return None
    x0, x1 = max(runs, key=lambda r: sum(counts[r[0]:r[1]]))
    if x1 - x0 < 12:
        return None
    ys = [y for y in range(img.height) for x in range(x0, x1)
          if near(img.getpixel((x, y)), FILL) <= tolerance]
    return (x0, min(ys), x1, max(ys) + 1), sum(counts[x0:x1])


def ink_in(img, box, tolerance=60):
    return sum(1 for y in range(box[1], box[3]) for x in range(box[0], box[2])
               if near(img.getpixel((x, y)), INK) <= tolerance)


def main():
    lines = ["theme: relay-dark agent=#%02x%02x%02x background=#%02x%02x%02x -> chip fill=#%02x%02x%02x"
             % (*AGENT, *BACKGROUND, *FILL), ""]
    base = row("none")
    for scene in SCENES:
        img = row(scene)
        found = chip(img)
        box, fill_px = found if found else (None, 0)
        diff = ImageChops.difference(base, img)
        diff_box, diff_px = diff.getbbox(), (sum(1 for p in diff.getdata() if p != (0, 0, 0)) if diff.getbbox() else 0)
        lines.append("%-5s badge: %-24s fill=%4dpx  ink=%3dpx   diff vs none: %-22s %5dpx"
                     % (scene,
                        "x %d..%d, y %d..%d" % (box[0], box[2], box[1], box[3]) if box else "none",
                        fill_px, ink_in(img, box) if box else 0,
                        "%s" % (diff_box,) if diff_box else "identical", diff_px))
        lines.append("      header OCR: %s" % ocr(img))
    text = "\n".join(lines) + "\n"
    Path("analyze-output.txt").write_text(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    main()
