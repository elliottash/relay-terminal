"""Measure terminal row pitch, and paragraph gap as a multiple of it, from screenshots (#T7AH).

The pane shows only a handful of rows, so each case is captured twice:
  pitch mode: shell printed `clear; seq 1 9`  -> the bottom rows sit at the row pitch
  gap mode:   shell printed alternating text/blank lines -> bottom rows sit 2 pitches
              apart (one full blank grid row between paragraphs)

Light text on a dark theme: ink = pixels above 120. Columns lit on >50% of strip rows
are pane borders/scrollbars and are masked out before banding.
"""
import statistics
import sys

from PIL import Image

X0, X1 = 0, 700
Y0, Y1 = 60, 810
INK = 120
MIN_INK = 2


def bands(path):
    img = Image.open(path).convert("L")
    px = img.load()
    x1, y1 = min(X1, img.width), min(Y1, img.height)
    inked = [[y, x] for y in range(Y0, y1) for x in range(X0, x1) if px[x, y] > INK]
    col_hits = {}
    for _, x in inked:
        col_hits[x] = col_hits.get(x, 0) + 1
    structural = {x for x, n in col_hits.items() if n > (y1 - Y0) * 0.5}
    per_row = {}
    for y, x in inked:
        if x not in structural:
            per_row[y] = per_row.get(y, 0) + 1
    out, cur = [], None
    for y in range(Y0, y1):
        if per_row.get(y, 0) >= MIN_INK:
            if cur is None:
                cur = [y, y]
            else:
                cur[1] = y
        elif cur is not None and y - cur[1] > 2:
            out.append(sum(cur) / 2)
            cur = None
    if cur is not None:
        out.append(sum(cur) / 2)
    return out


def longest_constant_run(centres, need):
    best = []
    for i in range(len(centres)):
        for j in range(i + need, len(centres) + 1):
            run = centres[i:j]
            diffs = [b - a for a, b in zip(run, run[1:])]
            if max(diffs) - min(diffs) > 3:
                continue
            if len(run) > len(best):
                best = run
    return best


def main():
    path, tag, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    centres = bands(path)
    print(f"{tag} ({mode}): {len(centres)} bands, centres {[round(c, 1) for c in centres]}")
    if mode == "pitch":
        run = longest_constant_run(centres, 4)
        if len(run) < 4:
            print(f"{tag}: FAIL — no constant-pitch run of 4+ rows")
            sys.exit(3)
        pitch = statistics.median([b - a for a, b in zip(run, run[1:])])
        print(f"{tag}: row pitch {pitch:.1f}px (run of {len(run)})")
        sys.exit(0)
    if mode == "gap":
        pitch = float(sys.argv[4])
        run = longest_constant_run(centres, 3)
        if len(run) < 3:
            print(f"{tag}: FAIL — no constant run of 3+ rows")
            sys.exit(3)
        gap = statistics.median([b - a for a, b in zip(run, run[1:])])
        print(f"{tag}: paragraph-to-paragraph distance {gap:.1f}px = {gap / pitch:.2f} row pitches")
        if not 1.6 < gap / pitch < 2.4:
            print(f"{tag}: FAIL — expected one full blank row (2.00 pitches) between paragraphs")
            sys.exit(4)
        print(f"{tag}: paragraph gap intact: one full blank grid row")
        sys.exit(0)
    print("mode must be pitch or gap")
    sys.exit(2)


if __name__ == "__main__":
    main()
