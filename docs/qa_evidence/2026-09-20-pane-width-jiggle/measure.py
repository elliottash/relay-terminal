#!/usr/bin/env python3
"""#SDXE, pane widths jiggle in response to content: the measurement.

Runs the built Relay under Xvfb with an isolated profile and RELAY_LAYOUT_LOG set, so every
pane prints one line a second saying how wide it is, what its minimum width is and what each
widget in its header row contributes to that minimum.

Then it makes three panes side by side, starts a CPU burner in two of them (which brings the
header's usage meter up and keeps its reading moving) and renames a pane the way a working
agent renames it, and watches for sixty seconds.

  Xvfb :59 -screen 0 1600x1000x24 &
  DISPLAY=:59 python3 measure.py [build-dir] > before.txt

What it prints per pane: every distinct width it was seen at, and every header widget whose
contribution to the minimum moved. A pane whose width has ONE value and whose minimum has one
value did not jiggle. Exit 1 when any pane's width moved after the panes were laid out.
"""
import os
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build')
RUN = '/tmp/claude-1000/sdxe-jiggle'
LINE = re.compile(r'pane "(?P<name>[^"]*)" w=(?P<w>\d+) min=(?P<min>\d+) header=(?P<header>\d+) \| (?P<parts>.*)')
PHASES = ('idle', 'busy', 'busy+rename', 'busy+short-title', 'quiet-again')
LONG_TITLE = 'Reading the request ledger and the roles file to find the stale tier'


def run(cmd, env=None):
    return subprocess.run(cmd, check=False, env=env, text=True, capture_output=True)


def xdo(*args):
    return run(['xdotool', *args])


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
                'RELAY_LAYOUT_LOG': '1', 'RELAY_DATA_DIR': os.path.join(RUN, 'data')})
    for name in ('RELAY_ENGINE_CORE', 'PYTHONPATH'):
        env.pop(name, None)
    return env


def main():
    env = isolate()
    relay = subprocess.Popen([os.path.join(BUILD, 'relay'), '--fresh', '--clean-shell',
                              '-w', os.path.join(RUN, 'workspace')], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    samples = []          # (phase, name, width, minimum, header, {widget: px})
    phase = ['start']
    raw = open(os.path.join(HERE, 'layout-log.txt'), 'w', encoding='utf-8')

    def reader():
        for line in relay.stdout:
            raw.write(line)
            m = LINE.search(line)
            if m:
                parts = dict((p.split('=')[0], int(p.split('=')[1])) for p in m.group('parts').split() if '=' in p)
                samples.append((phase[0], m.group('name'), int(m.group('w')), int(m.group('min')),
                                int(m.group('header')), parts))
    threading.Thread(target=reader, daemon=True).start()

    try:
        win, deadline = None, time.monotonic() + 60
        while time.monotonic() < deadline and not win:
            ids = [i for i in xdo('search', '--name', 'Relay').stdout.split() if i.strip()]
            win = ids[-1] if ids else None
            time.sleep(0.5)
        if not win:
            print('FAIL no Relay window')
            return 1
        xdo('windowsize', win, '1200', '850')
        xdo('windowmove', win, '0', '0')
        time.sleep(7)

        def click(x, y):
            xdo('mousemove', '--sync', str(x), str(y), 'click', '1')
            time.sleep(0.6)

        def type_line(text):
            xdo('type', '--delay', '20', text)
            time.sleep(0.4)
            xdo('key', 'Return')
            time.sleep(1.0)

        # A third pane beside the two the window opens with: the terminal on the left and the
        # Switchboard on the right. The click puts the focus in the left terminal's prompt box
        # first, so the new pane is split off that one and not off the board.
        click(150, 790)
        xdo('key', 'ctrl+e')
        time.sleep(4)
        print('[measure] three panes; settling', flush=True)
        phase[0] = 'idle'
        time.sleep(10)

        # Content, and nothing else: a CPU burner (which brings the usage meter up and keeps its
        # reading moving) and the long name a working agent writes over its pane's title.
        phase[0] = 'busy'
        click(120, 790)
        type_line('yes > /dev/null')
        time.sleep(14)
        phase[0] = 'busy+rename'
        type_line('/rename ' + LONG_TITLE)
        time.sleep(12)
        phase[0] = 'busy+short-title'
        type_line('/rename shell')
        time.sleep(12)
        phase[0] = 'quiet-again'
        xdo('key', 'ctrl+c')
        time.sleep(14)
    finally:
        relay.terminate()
        try:
            relay.wait(timeout=10)
        except subprocess.TimeoutExpired:
            relay.kill()
        raw.close()

    # Per pane: the widths it was seen at after the layout settled, and the header widgets whose
    # contribution moved.
    measured = [s for s in samples if s[0] != 'start']
    print('[measure] panes seen: ' + ', '.join(sorted({s[1] for s in measured})))
    panes = {}
    for ph, name, w, mn, header, parts in measured:
        key = name.split(' ')[0] + ' ' + name.split(' ')[-1]   # "#2 header": the pane and the row
        panes.setdefault(key, []).append((ph, name, w, mn, header, parts))
    bad = []
    for key in sorted(panes):
        rows = panes[key]
        widths = sorted({r[2] for r in rows})
        mins = sorted({r[3] for r in rows})
        print(f'pane {key}: {len(rows)} samples, widths {widths}, minimums {mins}')
        movers = {}
        for _, _, _, _, _, parts in rows:
            for widget, px in parts.items():
                movers.setdefault(widget, set()).add(px)
        for widget in sorted(movers):
            values = sorted(movers[widget])
            if len(values) > 1:
                print(f'    {widget} moved: {values}')
        if len(widths) > 1:
            bad.append(key)
            for ph in PHASES:
                seen = sorted({r[2] for r in rows if r[0] == ph})
                if seen:
                    print(f'    width during {ph}: {seen}')
    print(('FAIL ' if bad else 'PASS ') + 'pane widths: ' + (
        'moved in ' + ', '.join(bad) if bad else 'every pane kept one width'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
