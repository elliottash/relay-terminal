#!/usr/bin/env python3
# Diagnose and classify the #4E13 screenshots: what does the busy line above the prompt say
# (OCR), is it the work's own colour (pixel families: blue shell / violet agent), does the
# relay mark move (frame diffs, see implementer-notes.txt), and do the labelled bolds render
# green / amber / red while a plain bold stays uncoloured — under both themes.
#
#   python3 analyze.py
import subprocess
import sys
from PIL import Image, ImageChops

# relay-dark: ui tokens for the painted chrome (shell, agent), and the terminal ANSI palette
# (indices 2/3/1 and their brights 10/11/9) for the transcript's labelled bolds.
# ibm-beige: same slots from its own files. DIST covers antialiasing and atLeast() lifts.
DIST = 70
FAMILIES = {
    "relay-dark": {
        "blue(shell/running)": [(0x3E, 0xC5, 0xF0)],
        "violet(agent/working)": [(0xB4, 0x8E, 0xF7)],
        "green(Done)": [(0x7D, 0xD3, 0x99), (0x98, 0xE5, 0xB0)],
        "amber(Need)": [(0xEC, 0xC4, 0x76), (0xF8, 0xD5, 0x8E)],
        "red(Problem)": [(0xF2, 0x77, 0x7A), (0xFF, 0x8C, 0x8F)],
    },
    "ibm-beige": {
        "blue(shell/running)": [(0x00, 0x49, 0xA9)],
        "violet(agent/working)": [(0x75, 0x00, 0xC3)],
        "green(Done)": [(0x18, 0x60, 0x2F), (0x12, 0x52, 0x2A)],
        "amber(Need)": [(0x7A, 0x54, 0x00), (0x5E, 0x42, 0x00)],
        "red(Problem)": [(0xA8, 0x20, 0x1A), (0x8A, 0x14, 0x0F)],
    },
}


def load(name):
    return Image.open(name).convert("RGB")


def diffcount(a, b):
    return sum(1 for p in ImageChops.difference(a, b).getdata() if p != (0, 0, 0))


def families(img, theme):
    out = {}
    for label, rgbs in FAMILIES[theme].items():
        hits = []
        for x in range(img.width):
            for y in range(img.height):
                pr, pg, pb = img.getpixel((x, y))
                for r, g, b in rgbs:
                    if abs(pr - r) + abs(pg - g) + abs(pb - b) <= DIST:
                        hits.append((x, y))
                        break
        if hits:
            xs = [p[0] for p in hits]
            ys = [p[1] for p in hits]
            out[label] = (len(hits), (min(xs), min(ys), max(xs), max(ys)))
        else:
            out[label] = (0, None)
    return out


def ocr(img, label):
    img.save(f"/tmp/4e13-ocr-{label}.png")
    text = subprocess.run(["tesseract", f"/tmp/4e13-ocr-{label}.png", "-", "--psm", "6"],
                          capture_output=True, text=True).stdout
    return " ".join(text.split())


def report(img, where, theme):
    for label, (count, bbox) in families(img, theme).items():
        print(f"  {where} {label}: {count} px  bbox={bbox}")


def main():
    print("=== idle (relay-dark) ===")
    comp = load("implementer-idle-x-comp.png")
    print(f"  composer OCR: {ocr(comp, 'idle-comp')!r}")
    report(comp, "comp", "relay-dark")

    for scene in ("running", "relaying", "spawn"):
        print(f"=== {scene} (relay-dark) ===")
        comp_a, comp_b = load(f"implementer-{scene}-a-comp.png"), load(f"implementer-{scene}-b-comp.png")
        head_a = load(f"implementer-{scene}-a-head.png")
        print(f"  composer a-vs-b differ: {diffcount(comp_a, comp_b)} px (state holds still)")
        print(f"  composer OCR: {ocr(comp_a, scene + '-comp')!r}")
        print(f"  header   OCR: {ocr(head_a, scene + '-head')!r}")
        report(comp_a, "comp", "relay-dark")
        report(head_a, "head", "relay-dark")

    for scene, theme in (("labels", "relay-dark"), ("labels-beige", "ibm-beige")):
        print(f"=== {scene} ({theme}) ===")
        full = load(f"implementer-{scene}-x.png")
        # The readiness probe ends with `clear`, so the conversation block starts right under
        # the pane header (~y 96): the echoed prompt, the turn marker, then the reply.
        term = full.crop((0, 96, full.width, 460))
        print(f"  transcript OCR: {ocr(term, scene + '-term')!r}")
        report(term, "term", theme)

    return 0


if __name__ == "__main__":
    sys.exit(main())
