#!/usr/bin/env python3
"""Join plug in the header, 2026-09-20 (#20XA): right of the gear, hairlines around it.

Drives the built Relay under Xvfb with an isolated profile and checks, from screenshots:

  o  the header's right row is the decided order — bell, hairline, Actions, Sessions,
     Switchboard, gear, hairline, plug, hairline, minimize, maximize, close — by clustering
     the ink columns of the header strip: 12 clusters, the hairlines the 1px ones at
     positions 2, 7 and 9, each painted in the theme's border token
  m  the button between the second and third hairline is the plug: clicking it opens the
     menu with "Join with a code…" and "Open a pane your other desktop shares…"
  b  the bell still opens the notification list
  l  the same row under the light theme: same clusters, hairline colour the light border
  n  under window/native_frame the row ends at the plug: bell, hairline, tool buttons,
     hairline, plug — no trailing hairline, no window buttons, and the last button is the
     plug (its menu opens there too)

No provider account and no network turn: the profile only says onboarded=true so no setup
pane blocks the window, and no prompt is ever submitted. Needs Xvfb, xdotool, ImageMagick,
Pillow, tesseract.

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

from PIL import Image, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build')
RUN = '/tmp/claude-1000/join-plug-header'


def theme_ui_token(theme_id, key):
    """A theme's [ui] token as an RGB tuple, from the toml the app ships (data/theme/themes)."""
    import re
    path = os.path.join(ROOT, 'data', 'theme', 'themes', theme_id + '.toml')
    text = open(path, encoding='utf-8').read()
    match = re.search(rf'^{key} = "#([0-9a-fA-F]{{6}})"', text, re.M)
    return tuple(int(match.group(1)[i:i + 2], 16) for i in (0, 2, 4))


DARK_UI = {'id': 'dark-copper',    # the default theme (Theme.cpp: defaultThemeId)
           'border': theme_ui_token('dark-copper', 'border'),
           'surface': theme_ui_token('dark-copper', 'surface')}
LIGHT_UI = {'id': 'relay-light',
            'border': theme_ui_token('relay-light', 'border'),
            'surface': theme_ui_token('relay-light', 'surface')}

fails = []


def check(name, ok, detail=''):
    print(('PASS ' if ok else 'FAIL ') + name + ((' — ' + detail) if detail else ''), flush=True)
    if not ok:
        fails.append(name)


def run(cmd):
    return subprocess.run(cmd, check=False, capture_output=True, text=True)


def xdo(*args):
    return run(['xdotool', *args])


def geometry(win):
    out = xdo('getwindowgeometry', '--shell', str(win)).stdout
    values = dict(line.split('=', 1) for line in out.splitlines() if '=' in line)
    return int(values['X']), int(values['Y']), int(values['WIDTH']), int(values['HEIGHT'])


def shot(name, win):
    path = os.path.join(HERE, 'implementer-%s.png' % name)
    run(['import', '-window', str(win), path])
    return path


def root_shot(name):
    path = os.path.join(HERE, 'implementer-%s.png' % name)
    run(['import', '-window', 'root', path])
    return path


def zoom(png, name, x, y, w, h):
    path = os.path.join(HERE, 'implementer-%s.png' % name)
    run(['convert', png, '-crop', f'{w}x{h}+{x}+{y}', '+repage', '-resize', '300%', path])
    return path


def clusters(png, win_w):
    """Ink-column clusters of the header strip: the right 300 px of the window, below the top border.

    Returns [(left, right_inclusive, width, centre_x, ink_colour), ...] left to right. A
    hairline is a 1 px cluster; every glyph (bell, gear, plug, ...) is wider than 3 px.
    """
    im = Image.open(png).convert('RGB')
    px = im.load()
    x0, x1 = max(0, win_w - 300), win_w - 3
    y0, y1 = 3, 34
    crop = [px[x, y] for x in range(x0, x1) for y in range(y0, y1)]
    bg = Counter(crop).most_common(1)[0][0]

    def far(c):
        return sum(abs(c[i] - bg[i]) for i in range(3)) > 40

    ink_cols = {}
    for x in range(x0, x1):
        # Two hits, not three: the minimize glyph is a single thin line (1-2 px of coverage),
        # and it must be counted like any other button.
        hits = [y for y in range(y0, y1) if far(px[x, y])]
        if len(hits) >= 2:
            ink_cols[x] = Counter(px[x, y] for y in hits).most_common(1)[0][0]
    groups = []
    for x in sorted(ink_cols):
        if groups and x - groups[-1][-1] <= 2:
            groups[-1].append(x)
        else:
            groups.append([x])
    return [(g[0], g[-1], g[-1] - g[0] + 1, (g[0] + g[-1]) / 2, ink_cols[(g[0] + g[-1]) // 2])
            for g in groups], bg


def popup_box(png, y0=28, y1=280, min_w=150, max_w=460):
    """A pop-up's box (QMenu, the notification list) in a root shot: its 1px theme border.

    Scans for rows holding a wide contiguous run of border-token pixels and unions the runs;
    the header's own bottom edge is wider than max_w, the glyphs are narrower than min_w.
    Returns None if no such box sits below the header.
    """
    im = Image.open(png).convert('RGB')
    px = im.load()
    W, H = im.size
    border = DARK_UI['border']
    near = lambda c: sum(abs(c[i] - border[i]) for i in range(3)) <= 24
    boxes = []
    for y in range(y0, min(y1, H)):
        start = None
        for x in range(W):
            if near(px[x, y]):
                if start is None:
                    start = x
            elif start is not None:
                if min_w <= x - start <= max_w:
                    boxes.append((start, y, x - 1, y))
                start = None
    if not boxes:
        return None
    left = min(b[0] for b in boxes)
    top = min(b[1] for b in boxes)
    right = max(b[2] for b in boxes)
    bottom = max(b[3] for b in boxes)
    return left, top, right, bottom


def ocr_box(png, box):
    crop = os.path.join(HERE, 'ocr-crop.png')
    im = Image.open(png).convert('RGB')
    left, top, right, bottom = box
    crop_im = im.crop((max(0, left - 6), max(0, top - 6), min(im.size[0], right + 7), min(im.size[1], bottom + 7)))
    crop_im = ImageOps.autocontrast(crop_im.convert('L'))
    crop_im = crop_im.resize((crop_im.width * 4, crop_im.height * 4))
    crop_im.save(crop)
    text = run(['tesseract', crop, '-', '--psm', '6']).stdout
    os.path.exists(crop) and os.remove(crop)
    return text


def isolate(theme=None, native_frame=False):
    subprocess.run(['rm', '-rf', RUN], check=True)
    for name in ('home', 'config/RelayTerminal', 'data', 'run', 'tmp', 'workspace'):
        os.makedirs(os.path.join(RUN, name), exist_ok=True)
    with open(os.path.join(RUN, 'config/RelayTerminal/relay.conf'), 'w', encoding='utf-8') as f:
        f.write('[instructions]\nonboarded=true\n\n[hints]\nenabled=false\n\n[isolation]\nenabled=false\n')
        if theme:
            f.write(f'\n[theme]\nname={theme}\n')
        if native_frame:
            f.write('\n[window]\nnative_frame=true\n')
    env = dict(os.environ)
    env.update({'HOME': os.path.join(RUN, 'home'), 'XDG_CONFIG_HOME': os.path.join(RUN, 'config'),
                'XDG_DATA_HOME': os.path.join(RUN, 'data'), 'XDG_RUNTIME_DIR': os.path.join(RUN, 'run'),
                'TMPDIR': os.path.join(RUN, 'tmp'), 'RELAY_KEYRING': 'off',
                'RELAY_DATA_DIR': os.path.join(RUN, 'data')})
    for name in ('RELAY_ENGINE_CORE', 'PYTHONPATH'):
        env.pop(name, None)
    return env


def launch(env):
    relay = subprocess.Popen([os.path.join(BUILD, 'relay'), '--fresh', '--clean-shell',
                              '-w', os.path.join(RUN, 'workspace')], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(f'[drive] relay pid {relay.pid}', flush=True)
    win = None
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not win:
        ids = [i for i in xdo('search', '--name', 'Relay').stdout.split() if i.strip()]
        win = ids[-1] if ids else None
        time.sleep(0.5)
    time.sleep(8)   # the shell's first prompt, the worker's ready
    return relay, win


def stop(relay):
    relay.terminate()
    try:
        relay.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        relay.kill()
    print('[drive] relay stopped', flush=True)


def park(win):
    x, y, w, h = geometry(win)
    xdo('mousemove', '--sync', str(x + w // 2), str(y + h // 2))


def click_cluster(png, win, cs, index_from_right, settle=1.5):
    """Click the cluster that far from the row's right edge; returns its screen point."""
    x, y, w, h = geometry(win)
    c = cs[-1 - index_from_right]
    sx, sy = int(x + c[3]), y + 19
    xdo('mousemove', '--sync', str(sx), str(sy), 'click', '1')
    time.sleep(settle)
    return sx, sy


def near(c, want, tol=25):
    return sum(abs(c[i] - want[i]) for i in range(3)) <= tol


def main():
    # --- run 1: the default dark theme, Relay's own frame -----------------------------------------
    relay, win = launch(isolate())
    try:
        x, y, w, h = geometry(win)
        print(f'[drive] window {win} at {x},{y} {w}x{h}', flush=True)
        park(win)
        base = shot('01-header-dark', win)
        cs, bg = clusters(base, w)
        print('[drive] clusters ' + json.dumps([[c[2], c[4]] for c in cs]), flush=True)
        zoom(base, '01b-header-dark-zoom', w - 300, 0, 297, 36)
        row = cs[-12:] if len(cs) >= 12 else cs
        widths = [c[2] for c in row]
        check('o-twelve-clusters', len(cs) == 12, f'{len(cs)} clusters, widths {widths}')
        if len(row) == 12:
            hairlines = [i for i, c in enumerate(row) if c[2] == 1]
            check('o-hairlines-at-2-7-9', hairlines == [1, 6, 8], f'1px clusters at {hairlines}')
            check('o-all-others-wide', all(c[2] > 3 for i, c in enumerate(row) if i not in (1, 6, 8)),
                  f'widths {widths}')
            check('o-hairline-is-border-token',
                  all(near(row[i][4], DARK_UI['border']) for i in (1, 6, 8)),
                  f"colours {[row[i][4] for i in (1, 6, 8)]} vs {DARK_UI['border']} "
                  f"({DARK_UI['id']} border)")

        # The plug is the wide cluster between hairlines 2 and 3 (7th of 12).
        if len(row) == 12:
            sx, sy = click_cluster(base, win, row, 4)   # 12 clusters -> index 7 is 4 from the right
            menu = root_shot('02-plug-menu')
            box = popup_box(menu)
            text = ocr_box(menu, box).lower() if box else ''
            print('[drive] menu OCR: ' + ' / '.join(text.splitlines()), flush=True)
            check('m-join-with-a-code', 'join with a code' in text, repr(text))
            check('m-other-desktop', 'other desktop' in text, repr(text))
            xdo('key', 'Escape')
            time.sleep(0.8)

            # The bell is the row's first cluster.
            sx, sy = click_cluster(base, win, row, 11)
            popup = root_shot('03-bell-popup')
            box = popup_box(popup)
            if box:
                text = ocr_box(popup, box).lower()
                print('[drive] bell popup OCR: ' + ' / '.join(text.splitlines()), flush=True)
                check('b-bell-opens-list',
                      'notifications' in text and 'nothing yet' in text, repr(text))
            else:
                check('b-bell-opens-list', False, 'no popup box found under the bell')
            xdo('key', 'Escape')
            park(win)
    finally:
        stop(relay)

    # --- run 2: the light theme — same row, hairline the light border ------------------------------
    relay, win = launch(isolate(theme='relay-light'))
    try:
        park(win)
        light = shot('04-header-light', win)
        cs, _ = clusters(light, geometry(win)[2])
        print('[drive] light clusters ' + json.dumps([[c[2], c[4]] for c in cs]), flush=True)
        zoom(light, '04b-header-light-zoom', geometry(win)[2] - 300, 0, 297, 36)
        check('l-twelve-clusters', len(cs) == 12, f'{len(cs)} clusters')
        if len(cs) == 12:
            hairlines = [i for i, c in enumerate(cs) if c[2] == 1]
            check('l-hairlines-at-2-7-9', hairlines == [1, 6, 8], f'1px clusters at {hairlines}')
            check('l-hairline-is-light-border',
                  all(near(cs[i][4], LIGHT_UI['border']) for i in (1, 6, 8)),
                  f"colours {[cs[i][4] for i in (1, 6, 8)]} vs {LIGHT_UI['border']} "
                  f"({LIGHT_UI['id']} border)")
    finally:
        stop(relay)

    # --- run 3: window/native_frame — the row ends at the plug ------------------------------------
    relay, win = launch(isolate(native_frame=True))
    try:
        # Without a WM, Qt may place the native-frame window off-centre; pin it on-screen so
        # the plug's menu (clamped to the screen edge) opens where the drive can see it.
        xdo('windowmove', win, '0', '0')
        xdo('windowsize', win, '1000', '700')
        time.sleep(1.0)
        park(win)
        native = shot('05-header-native', win)
        ww = geometry(win)[2]
        print(f'[drive] native window at {geometry(win)}', flush=True)
        cs, _ = clusters(native, ww)
        print('[drive] native clusters ' + json.dumps([[c[2], c[4]] for c in cs]), flush=True)
        zoom(native, '05b-header-native-zoom', ww - 300, 0, 297, 36)
        row = cs[-8:] if len(cs) >= 8 else cs
        check('n-eight-clusters', len(cs) == 8, f'{len(cs)} clusters, widths {[c[2] for c in cs]}')
        if len(row) == 8:
            hairlines = [i for i, c in enumerate(row) if c[2] == 1]
            check('n-two-hairlines', hairlines == [1, 6], f'1px clusters at {hairlines}')
            check('n-row-ends-wide', row[-1][2] > 3, f'last cluster {row[-1][2]}px')
        if row:
            sx, sy = click_cluster(native, win, row, 0)   # the rightmost button should be the plug
            menu = root_shot('06-native-plug-menu')
            box = popup_box(menu)
            text = ocr_box(menu, box).lower() if box else ''
            print('[drive] native menu OCR: ' + ' / '.join(text.splitlines()), flush=True)
            check('n-last-button-is-plug', 'join with a code' in text, repr(text))
            xdo('key', 'Escape')
    finally:
        stop(relay)

    print('[drive] RESULT ' + json.dumps({'fails': fails}), flush=True)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
