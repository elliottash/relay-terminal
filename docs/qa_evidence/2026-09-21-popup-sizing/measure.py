#!/usr/bin/env python3
"""Measure a FilterPopup in the shots and geometry the drive takes of it (card #MDL1).

The popup is an override-redirect X window with no title, but it is mapped and it carries the
application's WM_CLASS, so the drive picks it out as the one visible `relay` window that is not
the main one and writes its geometry beside the shot. That is where the rectangle comes from.
Reading it out of the pixels does not work: over the composer strip the popup's ground *is* the
strip's ground, so neither a diff of the closed and open shots nor a hunt for the border can say
where one ends and the other begins, and both of them silently measure the wrong box.

Inside that rectangle the rows are bands of ink — glyphs, or the highlighted row's filled band. A
row scrolled off the top is a band that is not there; a row clipped by the frame is a band that is
not there either, or one with its descenders shaved off. So the count of bands against the count
of rows the list holds is the test, and the clearances say how close it came.

    measure.py open.png popup.txt "four levels" 4
"""
import re
import sys
from PIL import Image

GEOM = re.compile(r"(\S+)\s+(\d+)x(\d+)\+(-?\d+)\+(-?\d+)")


def popup_rect(path):
    """The popup's rectangle, from the one line the drive wrote for this step."""
    lines = [m for m in (GEOM.search(line) for line in open(path)) if m]
    if len(lines) != 1:
        return None, f"{len(lines)} visible popup-sized windows, expected exactly one"
    wid, w, h, x, y = lines[0].group(1), *(int(g) for g in lines[0].groups()[1:])
    return (x, y, x + w - 1, y + h - 1), wid


def bands(img, box, ink_at_least=2):
    """The y-bands inside `box` that hold ink, and where the filter line's rule is."""
    left, top, right, bottom = box
    pix = img.load()
    ground = {}
    for y in range(top + 2, bottom - 1):
        for x in range(left + 2, right - 1):
            ground[pix[x, y]] = ground.get(pix[x, y], 0) + 1
    base = max(ground, key=ground.get)
    width = right - left - 4
    lines = []
    for y in range(top + 2, bottom - 1):
        ink = full = 0
        for x in range(left + 3, right - 2):
            p = pix[x, y]
            d = abs(p[0] - base[0]) + abs(p[1] - base[1]) + abs(p[2] - base[2])
            if d > 28:
                ink += 1
            if d > 10:
                full += 1
        lines.append((y, ink, full))
    # The filter line's own bottom border is a near-full-width rule, and it is the *first* one: the
    # highlighted row's fill is nearly as wide, so taking the last would put the rule halfway down
    # the list and swallow every row above the highlight.
    rule = None
    for y, ink, full in lines:
        if full > width * 0.92:
            rule = y
            break
    out, run = [], None
    for y, ink, full in lines:
        if rule is not None and y <= rule + 1:
            continue
        if ink >= ink_at_least:
            run = (run[0], y) if run else (y, y)
        elif run:
            out.append(run)
            run = None
    if run:
        out.append(run)
    return base, rule, out


def main():
    shot, geometry, label, expected = sys.argv[1:5]
    expected = int(expected)
    print(f"== {label} ==")
    box, who = popup_rect(geometry)
    if box is None:
        print(f"no popup: {who}")
        return 1
    img = Image.open(shot).convert("RGB")
    left, top, right, bottom = box
    base, rule, rows = bands(img, box)
    # Ascenders and descenders of one row are one band: merge anything closer than 3 px.
    merged = []
    for band in rows:
        if merged and band[0] - merged[-1][1] <= 3:
            merged[-1] = (merged[-1][0], band[1])
        else:
            merged.append(band)
    print(f"popup {who}: {right - left + 1}x{bottom - top + 1} at ({left},{top}); ground {base}")
    print(f"filter line rule at y={rule}, {rule - top} px down the popup" if rule
          else "no filter rule found")
    print(f"row bands: {len(merged)}, expecting {expected}")
    last = None
    for i, (y0, y1) in enumerate(merged):
        pitch = f", pitch {y0 - last}" if last is not None else ""
        print(f"  band {i}: y {y0}..{y1}, {y1 - y0 + 1} px tall{pitch}")
        last = y0
    if not merged:
        print("VERDICT: NO ROWS DRAWN AT ALL")
        return 2
    print(f"clear above the first band: {merged[0][0] - (rule if rule is not None else top)} px; "
          f"under the last: {bottom - merged[-1][1]} px")
    ok = len(merged) == expected
    print("VERDICT:", "every row drawn" if ok else "ROWS MISSING OR CLIPPED")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
