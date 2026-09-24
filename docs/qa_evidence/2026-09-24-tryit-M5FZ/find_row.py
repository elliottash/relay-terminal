#!/usr/bin/env python3
"""Find a phrase on a screenshot with tesseract and print the centre of its bounding box: the
click target for a control whose only address is its text (the subagent strip row, the first-run
dialog's button). Words are matched alphanumerically and may be merged by OCR ("Not now" comes
back as one word), and the image's negative is tried too because the theme is dark."""
import subprocess
import sys
from pathlib import Path

def norm(text):
    return "".join(ch for ch in text.lower() if ch.isalnum())

def words(path):
    out = subprocess.run(["tesseract", path, "stdout", "tsv"], capture_output=True, text=True).stdout
    rows = [line.split("\t") for line in out.splitlines()[1:]]
    return [(norm(r[-1]), int(r[6]), int(r[7]), int(r[8]), int(r[9]))
            for r in rows if len(r) == 12 and r[-1].strip()]

def locate(path, target):
    found = words(path)
    run = []
    for i, (text, x, y, w, h) in enumerate(found):
        run = [(text, x, y, w, h)]
        joined = text
        j = i
        while len(joined) < len(target) and j + 1 < len(found):
            j += 1
            run.append(found[j])
            joined += found[j][0]
        if joined == target:
            x0 = min(r[1] for r in run); x1 = max(r[1] + r[3] for r in run)
            y0 = min(r[2] for r in run); y1 = max(r[2] + r[4] for r in run)
            return (x0 + x1) // 2, (y0 + y1) // 2
    return None

def locate_phrase(image, phrase):
    target = norm(phrase)
    for candidate in (image,):
        point = locate(candidate, target)
        if point:
            return point
    Path("/tmp/m5fz-ocr.png").write_bytes(Path(image).read_bytes())
    subprocess.run(["convert", image, "-negate", "/tmp/m5fz-ocr.png"], check=True)
    return locate("/tmp/m5fz-ocr.png", target)

if __name__ == "__main__":
    point = locate_phrase(sys.argv[1], " ".join(sys.argv[2:]))
    if point is None:
        sys.exit(f"could not find '{' '.join(sys.argv[2:])}' on {sys.argv[1]}")
    print(point[0], point[1])
