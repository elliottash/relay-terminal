#!/usr/bin/env python3
"""Pane header, 2026-09-20: a two-line title and the share button under the close one.

Drives the built Relay under Xvfb with an isolated profile and checks, from screenshots:

  L  the pane chrome is the sideways L the owner drew: one row of four buttons
     (i, new pane, move to tab, close) and the share button alone beneath the last
     of them — one button's width given back to the title
  2  a long title in a narrow pane crosses two lines: the header grows downward by a
     line, line 2 sits under line 1, and the tail of the title survives the wrap
  w  widening the pane puts the title back on one line
  s  the button under the close one is the share button: clicking it opens the
     "Share this pane" dialog

No provider account and no network turn: the profile only says onboarded=true so
no setup pane blocks the window, and no prompt is ever submitted. Needs Xvfb,
xdotool, ImageMagick, Pillow; tesseract if you want the OCR read-outs.

  Xvfb :58 -screen 0 1600x1000x24 &
  DISPLAY=:58 python3 drive.py [build-dir]

Writes implementer-*.png next to itself and prints PASS/FAIL lines; exit 1 on any FAIL.
"""
import json
import os
import subprocess
import sys
import time
from collections import Counter

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build')
RUN = '/tmp/claude-1000/pane-header-2line'
LONG_TITLE = 'Pane header: long titles cross two lines instead of eliding'
TAIL_WORD = 'eliding'

fails = []


def check(name, ok, detail=''):
    print(('PASS ' if ok else 'FAIL ') + name + ((' — ' + detail) if detail else ''), flush=True)
    if not ok:
        fails.append(name)


def run(cmd, env=None):
    return subprocess.run(cmd, check=False, env=env, text=True, capture_output=True)


def xdo(*args):
    return run(['xdotool', *args])


def shot(name, win):
    path = os.path.join(HERE, 'implementer-%s.png' % name)
    run(['import', '-window', str(win), path])
    return path


def geometry(win):
    out = xdo('getwindowgeometry', '--shell', str(win)).stdout
    values = dict(line.split('=', 1) for line in out.splitlines() if '=' in line)
    return int(values['X']), int(values['Y']), int(values['WIDTH']), int(values['HEIGHT'])


def pixels(png):
    im = Image.open(png).convert('RGB')
    return im.load(), im.size


def find_chrome(png, win_x, win_y, win_w):
    """The chrome tile in the pane's top-right: the bounding box and its ink clusters."""
    px, _ = pixels(png)
    x0, y0 = win_x + win_w - 240, win_y + 26
    crop = [(x, y) for x in range(x0, x0 + 240) for y in range(y0, y0 + 130)]
    bg = Counter(px[x, y] for x, y in crop).most_common(1)[0][0]
    close = lambda a, b: sum(abs(a[i] - b[i]) for i in range(3)) < 30
    # Rows that hold anything that is not the pane background, then the tile's bounds. The
    # crop's right edge is inside the pane (10 px of it), so stray terminal ink to the left of
    # the tile is cut by taking the columns the tile's own raised background covers.
    rows = [y for y in range(y0, y0 + 130) if sum(0 if close(px[x, y], bg) else 1 for x in range(x0, x0 + 240, 2)) > 6]
    if not rows:
        return None
    top, bottom = min(rows), max(rows)
    mid = (top + bottom) // 2
    tile_bg = Counter(px[x, y] for x in range(x0, x0 + 240) for y in range(top, top + 6)).most_common(1)[0][0]
    cols = [x for x in range(x0, x0 + 240) if sum(0 if close(px[x, y], tile_bg) else 1 for y in range(top, bottom + 1)) > 2]
    left, right = min(cols), max(cols)

    def clusters(y_from, y_to):
        cols_ink = [x for x in range(left, right + 1)
                    if sum(0 if close(px[x, y], tile_bg) else 1 for y in range(y_from, y_to + 1)) > 0]
        groups = []
        for x in cols_ink:
            if groups and x - groups[-1][-1] <= 3:
                groups[-1].append(x)
            else:
                groups.append([x])
        return [sum(g) / len(g) for g in groups]

    return {'top': top, 'bottom': bottom, 'left': left, 'right': right,
            'row1': clusters(top + 2, mid - 2), 'row2': clusters(mid + 3, bottom - 2)}


def header_bands(png, win_x, win_y, win_w, chrome, limit=90):
    """Vertical bands of bright title ink left of the chrome, top of the pane."""
    px, _ = pixels(png)
    x_from, x_to = win_x + 12, min(chrome['left'] - 12 if chrome else win_x + win_w // 2, win_x + win_w // 2)
    y_from, y_to = (chrome['top'] - 10 if chrome else win_y + 26), chrome['top'] + limit
    rows = []
    for y in range(y_from, y_to):
        bright = sum(1 for x in range(x_from, x_to, 2) if sum(px[x, y]) > 420)
        rows.append(bright > 2)
    bands = []
    for y, on in zip(range(y_from, y_to), rows):
        if on and (not bands or not bands[-1][1]):
            bands.append([y, y])
        elif on:
            bands[-1][1] = y
    return [tuple(b) for b in bands if b[1] - b[0] >= 4]


def ocr(png, box):
    crop = os.path.join(HERE, 'ocr-crop.png')
    x, y, w, h = box
    run(['convert', png, '-crop', f'{w}x{h}+{x}+{y}', '+repage', '-colorspace', 'gray', '-resize', '200%', crop])
    text = run(['tesseract', crop, '-', '--psm', '6']).stdout
    os.path.exists(crop) and os.remove(crop)
    return text


def isolate():
    subprocess.run(['rm', '-rf', RUN], check=True)
    for name in ('home', 'config/RelayTerminal', 'data', 'run', 'tmp', 'workspace'):
        os.makedirs(os.path.join(RUN, name), exist_ok=True)
    with open(os.path.join(RUN, 'config/RelayTerminal/relay.conf'), 'w', encoding='utf-8') as f:
        f.write('[instructions]\nonboarded=true\n\n[hints]\nenabled=false\n\n[isolation]\nenabled=false\n')
    env = dict(os.environ)
    env.update({'HOME': os.path.join(RUN, 'home'), 'XDG_CONFIG_HOME': os.path.join(RUN, 'config'),
                'XDG_DATA_HOME': os.path.join(RUN, 'data'), 'XDG_RUNTIME_DIR': os.path.join(RUN, 'run'),
                'TMPDIR': os.path.join(RUN, 'tmp'), 'RELAY_KEYRING': 'off',
                'RELAY_DATA_DIR': os.path.join(RUN, 'data')})
    for name in ('RELAY_ENGINE_CORE', 'PYTHONPATH'):
        env.pop(name, None)
    return env


def main():
    env = isolate()
    relay = subprocess.Popen([os.path.join(BUILD, 'relay'), '--fresh', '--clean-shell',
                              '-w', os.path.join(RUN, 'workspace')], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(f'[drive] relay pid {relay.pid}', flush=True)
    try:
        win = None
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and not win:
            ids = [i for i in xdo('search', '--name', 'Relay').stdout.split() if i.strip()]
            win = ids[-1] if ids else None
            time.sleep(0.5)
        time.sleep(8)   # the shell's first prompt, the worker's ready
        x, y, w, h = geometry(win)
        print(f'[drive] window {win} at {x},{y} {w}x{h}', flush=True)

        # L: four buttons in the top row, the share button alone beneath the last one.
        base = shot('01-chrome-L', win)
        chrome = find_chrome(base, x, y, w)
        if not chrome:
            check('L-chrome-found', False, 'no chrome tile in the top-right crop')
        else:
            zoom = os.path.join(HERE, 'implementer-01b-chrome-zoom.png')
            run(['convert', base, '-crop', f"240x{chrome['bottom'] - chrome['top'] + 16}"
                                         f"+{chrome['left'] - 10}+{chrome['top'] - 8}", '+repage',
                 '-resize', '300%', zoom])
            row1, row2 = len(chrome['row1']), len(chrome['row2'])
            check('L-row1-four-buttons', row1 == 4, f'row 1 has {row1} clusters')
            check('L-row2-one-under-right', row2 == 1 and chrome['row2']
                  and chrome['right'] - chrome['row2'][0] < 26,
                  f"row 2 clusters {chrome['row2']} vs right edge {chrome['right']}")
            check('L-taller-than-one-row', chrome['bottom'] - chrome['top'] > 30,
                  f"tile {chrome['bottom'] - chrome['top']}px tall")

        # Two-line title in a narrow pane.
        xdo('windowsize', win, '620', '820')
        xdo('windowmove', win, '0', '0')
        time.sleep(1.2)
        x, y, w, h = geometry(win)
        xdo('mousemove', '--sync', str(x + w // 2), str(y + h // 2), 'click', '1')
        time.sleep(0.5)
        xdo('type', '--delay', '25', '/rename ' + LONG_TITLE)
        xdo('key', 'Return')
        time.sleep(1.2)
        narrow = shot('02-two-line-title', win)
        chrome = find_chrome(narrow, x, y, w)
        bands = header_bands(narrow, x, y, w, chrome)
        gap = bands[1][0] - bands[0][1] if len(bands) >= 2 else 999
        check('2-two-bands-close', len(bands) >= 2 and gap <= 14, f'bands {bands} (gap {gap})')
        text = ocr(narrow, (x + 8, chrome['top'] - 10, chrome['left'] - x - 20, 78)) if chrome else ''
        print('[drive] header OCR: ' + ' / '.join(text.splitlines()), flush=True)
        check('2-tail-word-survives', TAIL_WORD in text.lower(), f'OCR of the header: {text!r}')

        # Wide again: one line.
        xdo('windowsize', win, '1240', '820')
        time.sleep(1.2)
        wide = shot('03-wide-one-line', win)
        chrome = find_chrome(wide, x, y, w)
        bands = header_bands(wide, x, y, w, chrome)
        gap = bands[1][0] - bands[0][1] if len(bands) >= 2 else 999
        check('w-one-band-when-wide', not (len(bands) >= 2 and gap <= 14), f'bands {bands}')
        check('L-still-l-when-wide', chrome and len(chrome['row1']) == 4 and len(chrome['row2']) == 1,
              f"row1 {len(chrome['row1']) if chrome else '-'} row2 {len(chrome['row2']) if chrome else '-'}")

        # The button under the close one shares the pane.
        if chrome and chrome['row2']:
            # The row-2 cluster's centre, a half-row down from the tile's vertical middle.
            sx, sy = int(chrome['row2'][0]), (chrome['top'] + chrome['bottom']) // 2 + 11
            xdo('mousemove', '--sync', str(sx), str(sy), 'click', '1')
            time.sleep(1.5)
            dialog = [i for i in xdo('search', '--name', 'Share this pane').stdout.split() if i.strip()]
            shared = shot('04-share-clicked', win)
            check('s-share-opens-dialog', bool(dialog), f'windows named "Share this pane": {dialog}')
            xdo('key', '--window', dialog[0], 'Escape') if dialog else None
            time.sleep(0.8)
        else:
            check('s-share-opens-dialog', False, 'no row-2 cluster to click')

        print('[drive] RESULT ' + json.dumps({'fails': fails}), flush=True)
    finally:
        relay.terminate()
        try:
            relay.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            relay.kill()
        print('[drive] relay stopped', flush=True)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
