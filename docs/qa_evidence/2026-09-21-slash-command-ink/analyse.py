#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""#SQ3D: read the inks of the echoed prompt row straight out of drive.sh's screenshots.

    python3 analyse.py            # prints the table below and writes notes.txt

The claim to check is not "the command looks different" but the rule the code implements: the
pane writes that span as a *palette index*, and the view paints it in
`legibleOn(<the theme's bright cyan>, <the row's band>)` — the written hue, moved only as far as
the band demands (engine/view/FaintInk.h). So this recomputes that colour from the theme file's
own numbers and looks for it in the pixels of the command, while the rest of the row must still
be the role's ink. Both themes are checked: Relay Dark's agent band is light, IBM Beige's is dark.
"""
from collections import Counter
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent
FLOOR = 4.5
# From data/theme/themes/*.toml: [ui] agent (the band the row wears) and palette[14], the bright
# cyan SGR 96 names — the entry the composer's `[syntax] token` colour is drawn from.
THEMES = {
    "dark.png": ("relay-dark", (0xb4, 0x8e, 0xf7), (0x78, 0xdd, 0xea)),
    "light.png": ("ibm-beige", (0x75, 0x00, 0xc3), (0x0a, 0x4a, 0x46)),
}
# The row the prompt is echoed on, and the columns the `✦ /deliver` mark and command occupy, in
# the 1200x760 window drive.sh takes: everything here is that one row of one pane.
ROW = range(251, 267)
COMMAND = range(28, 136)
REST = range(140, 620)


def luminance(c):
    def channel(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * channel(c[0]) + 0.7152 * channel(c[1]) + 0.0722 * channel(c[2])


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def legible_on(fg, bg):
    """engine/view/FaintInk.h, legibleOn(), in Python."""
    if contrast(fg, bg) >= FLOOR:
        return fg
    pole = (0, 0, 0) if luminance(bg) > 0.179 else (255, 255, 255)
    keep, push = 0.0, 1.0
    for _ in range(12):
        mid = (keep + push) / 2
        if contrast(mix(fg, pole, mid), bg) >= FLOOR:
            push = mid
        else:
            keep = mid
    return mix(fg, pole, push)


def inks(img, columns, band):
    out = Counter()
    for y in ROW:
        for x in columns:
            c = img.getpixel((x, y))[:3]
            if max(abs(c[i] - band[i]) for i in range(3)) > 55:
                out[c] += 1
    return out


def report():
    lines, ok = [], True
    for name, (theme, band, written) in THEMES.items():
        img = Image.open(OUT / name).convert("RGB")
        wanted = legible_on(written, band)
        command = inks(img, COMMAND, band)
        rest = inks(img, REST, band)
        role = rest.most_common(1)[0][0]
        painted = command.get(wanted, 0)
        lines += [
            f"--- {name} ({theme})",
            "  band          #%02x%02x%02x" % band,
            "  written       #%02x%02x%02x   (palette[14], SGR 96)  %.2f:1 on the band" % (*written, contrast(written, band)),
            "  legibleOn()   #%02x%02x%02x   %.2f:1 on the band" % (*wanted, contrast(wanted, band)),
            "  painted       %d px of it in the command, %d in the rest of the row" % (painted, rest.get(wanted, 0)),
            "  role ink      #%02x%02x%02x   %.2f:1 on the band, %d px" % (*role, contrast(role, band), rest[role]),
        ]
        for label, test in (
            ("the command is painted in legibleOn(written, band)", painted > 0),
            ("it clears the 4.5:1 floor on the band", contrast(wanted, band) >= FLOOR - 0.01),
            ("the written colour itself is not painted", command.get(written, 0) == 0),
            ("the command's ink is not the role's", wanted != role),
            ("the rest of the row is still the role's ink", rest[role] > painted and rest.get(wanted, 0) == 0),
        ):
            lines.append(("  PASS  " if test else "  FAIL  ") + label)
            ok = ok and test
    lines.append("PASS" if ok else "FAIL")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    text = report()
    (OUT / "notes.txt").write_text(text)
    print(text, end="")
    raise SystemExit(0 if text.rstrip().endswith("PASS") else 1)
