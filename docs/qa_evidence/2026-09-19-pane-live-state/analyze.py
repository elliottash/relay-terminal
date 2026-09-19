#!/usr/bin/env python3
# Diagnose the relaying scene's 0-pixel pulse (card #V8KT evidence): what state was actually
# on screen when the two frames were shot? Compares each scene's crops against the idle
# baseline, classifies pixels by theme ink family (blue shell / violet agent / red error),
# locates where the a/b frames differ, and OCRs the header word and the terminal transcript.
#
#   python3 analyze.py
import subprocess
import sys
from PIL import Image, ImageChops

# Theme tokens (src/Theme.h, relay-dark): shell blue, agent violet, error red.
FAMILIES = {
    "blue(shell/running)": (0x3E, 0xC5, 0xF0),
    "violet(agent/working)": (0xB4, 0x8E, 0xF7),
    "red(error/failed)": (0xE0, 0x6C, 0x75),
}
DIST = 70  # antialiasing + atLeast() lifts stay near the token


def load(name):
    return Image.open(name).convert("RGB")


def diffcount(a, b):
    return sum(1 for p in ImageChops.difference(a, b).getdata() if p != (0, 0, 0))


def diffbbox(a, b):
    return ImageChops.difference(a, b).getbbox()


def families(img):
    out = {}
    for label, (r, g, b) in FAMILIES.items():
        hits = []
        for x in range(img.width):
            for y in range(img.height):
                pr, pg, pb = img.getpixel((x, y))
                if abs(pr - r) + abs(pg - g) + abs(pb - b) <= DIST:
                    hits.append((x, y))
        if hits:
            xs = [p[0] for p in hits]
            ys = [p[1] for p in hits]
            out[label] = (len(hits), (min(xs), min(ys), max(xs), max(ys)))
        else:
            out[label] = (0, None)
    return out


def ocr(img, label):
    img.save(f"/tmp/v8kt-ocr-{label}.png")
    text = subprocess.run(["tesseract", f"/tmp/v8kt-ocr-{label}.png", "-", "--psm", "6"],
                          capture_output=True, text=True).stdout
    return " ".join(text.split())


def main():
    scenes = ["idle", "running", "relaying", "mix"]
    idle_bar, idle_head = load("implementer-idle-bar.png"), load("implementer-idle-head.png")
    for scene in scenes:
        print(f"=== {scene} ===")
        stem = scene if scene == "idle" else f"{scene}-a"
        bar_a, head_a = load(f"implementer-{stem}-bar.png"), load(f"implementer-{stem}-head.png")
        bar_b = load(f"implementer-{scene}-b-bar.png") if scene != "idle" else None
        head_b = load(f"implementer-{scene}-b-head.png") if scene != "idle" else None
        if bar_b is not None:
            print(f"  bar  a-vs-b differ: {diffcount(bar_a, bar_b)} px   head a-vs-b differ: {diffcount(head_a, head_b)} px")
            print(f"  full a-vs-b bbox: {diffbbox(load(f'implementer-{scene}-a.png'), load(f'implementer-{scene}-b.png'))}")
        print(f"  bar  a-vs-idle differ: {diffcount(bar_a, idle_bar)} px   head a-vs-idle differ: {diffcount(head_a, idle_head)} px")
        for label, (count, bbox) in families(bar_a).items():
            print(f"  bar  {label}: {count} px  bbox={bbox}")
        for label, (count, bbox) in families(head_a).items():
            print(f"  head {label}: {count} px  bbox={bbox}")
        print(f"  head OCR: {ocr(head_a, scene + '-head')!r}")
        # Terminal transcript: below the header row, left half, top slice where the turn prints.
        if scene != "idle":
            full = load(f"implementer-{scene}-a.png")
            term = full.crop((0, 96, full.width, 420))
            print(f"  terminal OCR: {ocr(term, scene + '-term')!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
