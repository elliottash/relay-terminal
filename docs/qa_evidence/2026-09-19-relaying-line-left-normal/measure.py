#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measure the #HQ2B captures: is the "Relaying…" line on the prompt text's left edge, in every
state, in the normal weight, and does it elide from the middle in a narrow pane?

    python3 measure.py

For each scene's composer crop (100 %, window-width, bottom 240 px):
  * the busy row — the topmost band of state-coloured pixels (violet agent / blue terminal /
    amber needs-you, relay-dark tokens) — its left edge x_busy and its OCR;
  * the prompt text's left edge x_text: the first column under the busy row whose ink is
    neither background nor a frame hairline (a hairline is a column inked for most of the
    band; text columns are inked for a few px only);
  * x_frame: the composer frame's left border (a near-full-height inked column), so the
    report can also say how far the line sits inside the frame;
  * |x_text - x_busy| <= 2 px is the acceptance, and the reason 2 is enough: the two rows'
    first glyphs differ (the line's capital R, the placeholder's own first letter), and a
    glyph's leftmost antialiased pixel sits its left side bearing — up to 2 px at this size —
    right of the shared pen origin. The pen origin itself is pinned by the caret: in the
    captures where the prompt's blink is on, a 2 px-wide, ~18 px-tall column (the caret) stands
    at the prompt's origin, and the busy row's pen origin — its ink x less the R's bearing —
    is the same column. The old right-aligned line put x_busy near the right edge instead.

The weight verdict (scene `relaying`): the captured first word's ink ratio — ink pixels over
the word's bounding box — against the same word rendered from the fc-matched UI font at normal
and at demibold weight, scaled to the same ink height. Whichever it sits nearer is the verdict;
the numbers are printed so the call is auditable.

`idle` must report no coloured pixels at all (the control).
"""
import subprocess
import sys
from PIL import Image

DIST = 70
FAMILIES = {
    "violet(agent/working)": (0xB4, 0x8E, 0xF7),
    "blue(shell/running)": (0x3E, 0xC5, 0xF0),
    "amber(needs you)": (0xEC, 0xC4, 0x76),
}
SCENES = ("idle", "relaying", "running", "spawn", "question", "narrow")


def coloured(p):
    for r, g, b in FAMILIES.values():
        if abs(p[0] - r) + abs(p[1] - g) + abs(p[2] - b) <= DIST:
            return True
    return False


def bands(img):
    """Y-bands of coloured pixels: (top, bottom, leftmost_x, rightmost_x, count, family)."""
    rows = {}
    for y in range(img.height):
        xs = [x for x in range(img.width) if coloured(img.getpixel((x, y)))]
        if xs:
            rows[y] = xs
    out = []
    ys = sorted(rows)
    i = 0
    while i < len(ys):
        j = i
        while j + 1 < len(ys) and ys[j + 1] - ys[j] <= 4:
            j += 1
        block = [y for y in ys[i:j + 1]]
        xs = [x for y in block for x in rows[y]]
        fam = {}
        for y in block:
            for x in rows[y]:
                p = img.getpixel((x, y))
                for name, (r, g, b) in FAMILIES.items():
                    if abs(p[0] - r) + abs(p[1] - g) + abs(p[2] - b) <= DIST:
                        fam[name] = fam.get(name, 0) + 1
        out.append((block[0], block[-1], min(xs), max(xs), sum(len(rows[y]) for y in block), fam))
        i = j + 1
    return out


def ocr(img, tag):
    img.save(f"/tmp/hq2b-ocr-{tag}.png")
    text = subprocess.run(["tesseract", f"/tmp/hq2b-ocr-{tag}.png", "-", "--psm", "7"],
                          capture_output=True, text=True).stdout
    return " ".join(text.split())


def column_ink(img, x, y0, y1, surface, thresh=55):
    return sum(1 for y in range(y0, y1)
               if sum(abs(a - b) for a, b in zip(img.getpixel((x, y)), surface)) > thresh)


def surface_of(img, y0, y1):
    counts = {}
    for y in range(y0, min(y1, img.height)):
        for x in range(0, img.width):
            counts[img.getpixel((x, y))] = counts.get(img.getpixel((x, y)), 0) + 1
    return max(counts, key=counts.get) if counts else (0, 0, 0)


def measure(scene):
    img = Image.open(f"implementer-{scene}-x-comp.png").convert("RGB")
    print(f"=== {scene} ===")
    bs = bands(img)
    # The busy line is the band that *says* "Relaying": terminal ANSI text above the composer
    # and the strip chips below the prompt both carry state colours without being the line.
    busy = None
    for band in bs:
        top, bottom = band[0], band[1]
        row = img.crop((0, max(0, top - 4), img.width, min(img.height, bottom + 5))).resize(
            (img.width * 3, (bottom + 5 - max(0, top - 4)) * 3), Image.LANCZOS)
        text = ocr(row, f"{scene}-{top}")
        if "relaying" in text.lower():
            busy = (band, text)
            break
    if not busy:
        detail = "idle control: no \"Relaying\" row — pass" if scene == "idle" else "NO BUSY LINE FOUND"
        print(f"  {detail} ({len(bs)} coloured band(s) in the composer, none of them the line)")
        return scene == "idle"
    (top, bottom, x_busy, x_right, count, fam), text = busy
    fam_name = max(fam, key=fam.get)
    print(f"  busy row y {top}..{bottom}, x {x_busy}..{x_right}, {count} px, {fam_name}")
    print(f"  busy row OCR: {text!r}")

    # The prompt text: the first column under the busy row that is inked like text (a few px),
    # not like the frame's hairline (most of the band). The caret, when its blink is on, sits
    # at the same x as the text, so it measures the same edge either way.
    y0, y1 = bottom + 4, min(bottom + 46, img.height)
    surface = surface_of(img, y0, y1)
    band_h = y1 - y0
    x_text = None
    text_ink = 0
    for x in range(2, img.width - 2):
        ink = column_ink(img, x, y0, y1, surface)
        if 0 < ink <= band_h * 0.5:
            x_text, text_ink = x, ink
            break
    # The composer frame's left border: a column inked for most of the crop's height.
    full_surface = surface_of(img, 0, img.height)
    x_frame = None
    for x in range(0, img.width):
        if column_ink(img, x, 0, img.height, full_surface) > img.height * 0.7:
            x_frame = x
            break
    if x_text is None:
        print("  PROMPT TEXT NOT FOUND under the busy row")
        return False
    delta = x_text - x_busy
    verdict = "PASS" if abs(delta) <= 2 else "FAIL"
    print(f"  busy left x={x_busy}, prompt text left x={x_text} (frame border x={x_frame}, "
          f"line sits {x_busy - x_frame} px inside it)")
    print(f"  |prompt_x - busy_x| = {abs(delta)} px -> {verdict} "
          f"(acceptance: same pen origin, ±2 px of glyph bearing)")
    if text_ink >= 14:
        print(f"  (the first prompt column is {text_ink} px tall — the caret, i.e. the prompt's "
              f"own pen origin, not a glyph edge)")
    if "…" not in text and "..." not in text and scene == "narrow":
        print("  narrow scene: no ellipsis in the OCR — check the elision by eye")
        return False
    return abs(delta) <= 2


def font_files():
    def fc(pattern):
        return subprocess.run(["fc-match", "-f", "%{file}", pattern],
                              capture_output=True, text=True).stdout.strip()
    return fc("sans"), fc("sans:weight=demibold")


def weight_verdict():
    """Compare the captured busy word's ink ratio with normal- and demibold-weight renders."""
    from PIL import ImageDraw, ImageFont
    img = Image.open("implementer-relaying-x-comp.png").convert("RGB")
    top = bottom = x_busy = None
    for band in bands(img):
        row = img.crop((0, max(0, band[0] - 4), img.width, min(img.height, band[1] + 5)))
        if "relaying" in ocr(row, f"weight-{band[0]}").lower():
            top, bottom, x_busy = band[0], band[1], band[2]
            break
    if x_busy is None:
        print("weight: no Relaying row found")
        return False
    word = img.crop((x_busy, top, x_busy + 90, bottom + 1))   # "Relaying" is ~90 px at 11 pt
    gray = word.convert("L")
    wbox = gray.point(lambda v: 255 if v > 110 else 0).getbbox()   # bright ink on dark ground
    if not wbox:
        print("weight: no ink found in the busy word crop")
        return
    word = word.crop(wbox)
    pixels = list(word.convert("L").getdata())
    thresh = (max(pixels) + min(pixels)) // 2
    ink = sum(1 for v in pixels if v > thresh)
    captured = ink / (word.width * word.height)
    cap_h = word.height

    normal_file, demi_file = font_files()
    results = {}
    for name, path in (("normal", normal_file), ("demibold", demi_file)):
        best = None
        for size in range(10, 40):
            font = ImageFont.truetype(path, size)
            box = font.getbbox("Relaying")
            h = box[3] - box[1]
            if h == cap_h or best is None:
                best = (size, h)
            if h >= cap_h:
                break
        font = ImageFont.truetype(path, best[0])
        render = Image.new("L", (200, 80), 0)
        draw = ImageDraw.Draw(render)
        draw.text((10, 10), "Relaying", fill=255, font=font, anchor="ls")
        bbox = render.getbbox()
        render = render.crop(bbox)
        rp = list(render.getdata())
        rink = sum(1 for v in rp if v > 127)
        results[name] = rink / (render.width * render.height)
    nearer = min(results, key=lambda n: abs(results[n] - captured))
    print("=== weight (scene relaying, the word 'Relaying') ===")
    print(f"  captured ink ratio {captured:.3f}; renders: normal {results['normal']:.3f}, "
          f"demibold {results['demibold']:.3f}")
    print(f"  -> the captured line is nearest {nearer} "
          f"({'PASS' if nearer == 'normal' else 'FAIL'}; acceptance: normal weight)")
    return nearer == "normal"


def main():
    ok = [measure(scene) for scene in SCENES]
    weight = weight_verdict()
    ok_all = all(ok) and weight
    print("=== overall:", "PASS" if ok_all else "FAIL", "===")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
