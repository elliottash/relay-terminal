#!/usr/bin/env python3
"""Time one UI action by when the window actually stops repainting.

    act.py --pid P --win W --label L [--region WxH+X+Y] [--settle MS] [--timeout S]
           -- <xdotool args...>

CPU-time alone cannot time the Switchboard: the board pane waits on an out-of-process Python
worker, so the GUI is idle while nothing is on screen yet.  This samples the window's pixels
(`xwd` on a region, hashed) every ~40 ms as well as the CPU of the GUI process and of every
child it has, and reports:

  first   ms from the keypress to the first pixel change
  last    ms to the last pixel change  (= when the user sees the finished result)
  cpu_gui/cpu_kids  CPU time burnt by the GUI process and by its children in that window
  frames  how many distinct images the window went through
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import time

HZ = os.sysconf('SC_CLK_TCK')

if os.path.exists('/usr/bin/xwd'):
    _GRAB, _GRAB_TAIL = ['xwd', '-silent', '-id'], []
else:
    _GRAB, _GRAB_TAIL = ['import', '-silent', '-window'], ['xwd:-']


def _cpu(path):
    try:
        with open(path, 'rb') as fh:
            f = fh.read().rsplit(b')', 1)[1].split()
        return (int(f[11]) + int(f[12])) / HZ
    except OSError:
        return 0.0


def children(pid):
    try:
        with open(f'/proc/{pid}/task/{pid}/children') as fh:
            kids = [int(x) for x in fh.read().split()]
    except OSError:
        return []
    out = list(kids)
    for k in kids:
        out.extend(children(k))
    return out


def cpu_all(pid):
    return _cpu(f'/proc/{pid}/stat'), sum(_cpu(f'/proc/{k}/stat') for k in children(pid))


def snap(win, region, display):
    """Hash the window's pixels, or just `region` = (x, y, w, h) of them.

    A region is needed because the terminal pane's cursor blinks: a whole-window hash
    changes twice a second whatever the pane under test is doing.  The XWD header is a run
    of big-endian 32-bit words; words 2, 4, 5 and 11 are the header size, width, height and
    bytes-per-line, which is all that is needed to slice rows out of the raw image.
    """
    env = dict(os.environ, DISPLAY=display)
    # `xwd` where it exists; ImageMagick's `import` writes the same format and is what the
    # laptop has instead.
    p = subprocess.run(_GRAB + [str(win)] + _GRAB_TAIL, capture_output=True, env=env)
    if p.returncode != 0 or len(p.stdout) < 100:
        return None
    buf = p.stdout
    if region is None:
        return hashlib.blake2b(buf, digest_size=8).digest()
    w = struct.unpack('>25I', buf[:100])
    # XWDFileHeader, in order: header_size, file_version, pixmap_format, pixmap_depth,
    # pixmap_width, pixmap_height, xoffset, byte_order, bitmap_unit, bitmap_bit_order,
    # bitmap_pad, bits_per_pixel, bytes_per_line, ... and a colormap of ncolors (word 19)
    # 12-byte XColor entries sits between the header and the pixels.
    width, height, bpl, ncolors = w[4], w[5], w[12], w[19]
    hdr = w[0] + ncolors * 12
    depth_bytes = max(1, w[11] // 8)
    x, y, w, h = region
    x, y = max(0, x), max(0, y)
    w, h = min(w, width - x), min(h, height - y)
    digest = hashlib.blake2b(digest_size=8)
    for row in range(y, y + h):
        start = hdr + row * bpl + x * depth_bytes
        digest.update(buf[start:start + w * depth_bytes])
    return digest.digest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--win', required=True)
    ap.add_argument('--label', default='action')
    ap.add_argument('--settle', type=float, default=0.6)
    ap.add_argument('--timeout', type=float, default=120.0)
    ap.add_argument('--display', default=os.environ.get('DISPLAY', ':23'))
    ap.add_argument('--region', default=None, help='WxH+X+Y of the window to watch')
    ap.add_argument('cmd', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == '--' else a.cmd
    region = None
    if a.region:
        wh, xy = a.region.split('+', 1)
        w, h = (int(v) for v in wh.split('x'))
        x, y = (int(v) for v in xy.split('+'))
        region = (x, y, w, h)

    base = snap(a.win, region, a.display)
    c0 = cpu_all(a.pid)
    t0 = time.monotonic()
    if cmd:
        subprocess.run(cmd, check=False)
    first = last = None
    frames = 0
    prev = base
    while time.monotonic() - t0 < a.timeout:
        h = snap(a.win, region, a.display)
        now = time.monotonic()
        if h is not None and h != prev:
            frames += 1
            first = first if first is not None else now - t0
            last = now - t0
            prev = h
        elif last is not None and now - t0 - last > a.settle:
            break
        time.sleep(0.01)
    c1 = cpu_all(a.pid)
    f = '-' if first is None else f"{first*1000:.0f}"
    l = '-' if last is None else f"{last*1000:.0f}"
    print(f"{a.label}\tfirst={f}ms\tlast={l}ms\tcpu_gui={(c1[0]-c0[0])*1000:.0f}ms"
          f"\tcpu_kids={(c1[1]-c0[1])*1000:.0f}ms\tframes={frames}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
