#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The card page's prompt box, out of the RELAY_QA_RECTS dump: `x y w h`.

    rect_composer.py <rects.json> <min-x>

Object names are not unique — every prompt box in Relay is a `composerEditor`, and the dump
numbers the second and later ones — so the card's is named by where it is: the lowest one whose
centre is past `min-x`, which on this drive's window is the right half. The terminal pane's is
the other one on screen. Exits 1 when there is none, which is itself an answer.
"""
import json
import re
import sys

try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(1)
minx, best = int(sys.argv[2]), None
for key, row in rows.items():
    if key != "composerEditor" and not re.fullmatch(r"composerEditor#\d+", key):
        continue
    if row["x"] + row["w"] // 2 < minx:
        continue
    if best is None or row["y"] > best["y"]:
        best = row
if best is None:
    sys.exit(1)
print(best["x"], best["y"], best["w"], best["h"])
