#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The pictures the #1MGS drive shows: each is distinct at a glance, so a screenshot says which
protocol drew what. Writes into the directory given (default: here)."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
out.mkdir(parents=True, exist_ok=True)

def card(name, colour, label, size=(360, 180)):
    img = Image.new("RGB", size, colour)
    d = ImageDraw.Draw(img)
    d.rectangle([6, 6, size[0] - 7, size[1] - 7], outline="white", width=4)
    d.line([0, size[1], size[0], 0], fill="white", width=3)
    d.text((18, 18), label, fill="white")
    img.save(out / name)

card("kitty.png", (200, 60, 60), "kitty graphics (PNG, direct)")
card("iterm.png", (40, 120, 200), "iTerm2 OSC 1337 File=")
card("sixel.png", (40, 160, 90), "sixel")
card("reply.png", (150, 80, 180), "agent reply: ![chart](reply.png)", (480, 240))
card("attached.png", (220, 150, 30), "attached to the prompt", (300, 300))
