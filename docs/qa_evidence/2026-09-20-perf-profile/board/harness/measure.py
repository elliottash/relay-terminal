#!/usr/bin/env python3
"""Measure what one UI action costs Relay's GUI process.

    measure.py <pid> [--settle MS] [--timeout S] -- <xdotool args...>

Samples /proc/<pid>/stat (whole process) and /proc/<pid>/task/<pid>/stat (the GUI thread)
every 5 ms, waits until the process has been idle for `--settle` ms, sends the action, then
waits for idle again.  Prints the wall time from the action to quiescence and the CPU time
(GUI thread and whole process) burnt in between, which is the number that does not depend on
how loaded the machine is.  Also reports the peak over any 100 ms window, so a long stall
shows up even when the total is modest.
"""
import argparse
import os
import subprocess
import sys
import time

HZ = os.sysconf('SC_CLK_TCK')


def cpu(path):
    with open(path, 'rb') as fh:
        fields = fh.read().rsplit(b')', 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / HZ   # utime + stime


def read(pid):
    return cpu(f'/proc/{pid}/stat'), cpu(f'/proc/{pid}/task/{pid}/stat')


def wait_idle(pid, settle, timeout, mark=None):
    """Return once the process has burnt <1 ms of CPU for `settle` seconds."""
    quiet_since = None
    last = read(pid)
    end = time.monotonic() + timeout
    peak, window = 0.0, []
    while time.monotonic() < end:
        time.sleep(0.005)
        now = read(pid)
        delta = now[0] - last[0]
        window.append((time.monotonic(), delta))
        while window and window[0][0] < time.monotonic() - 0.1:
            window.pop(0)
        peak = max(peak, sum(d for _, d in window))
        last = now
        if delta <= 0.0:
            quiet_since = quiet_since or time.monotonic()
            if time.monotonic() - quiet_since >= settle:
                return True, peak
        else:
            quiet_since = None
    return False, peak


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--settle', type=float, default=0.4)
    ap.add_argument('--timeout', type=float, default=60.0)
    ap.add_argument('--label', default='action')
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('cmd', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == '--' else args.cmd

    settled, _ = wait_idle(args.pid, args.settle, 30)
    if not settled:
        print('warning: process never went idle before the action', file=sys.stderr)
    before = read(args.pid)
    t0 = time.monotonic()
    subprocess.run(cmd, check=False)
    ok, peak = wait_idle(args.pid, args.settle, args.timeout)
    t1 = time.monotonic()
    after = read(args.pid)
    wall = (t1 - t0) - args.settle
    print(f"{args.label}\twall={wall*1000:.0f}ms\tcpu_proc={(after[0]-before[0])*1000:.0f}ms"
          f"\tcpu_gui={(after[1]-before[1])*1000:.0f}ms\tpeak100ms={peak*1000:.0f}ms"
          f"\t{'ok' if ok else 'TIMEOUT'}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
