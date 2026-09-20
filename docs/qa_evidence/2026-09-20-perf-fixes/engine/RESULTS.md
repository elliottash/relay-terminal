# #6W0Z (and #PPR4 item 2) — before and after, measured on spark

The fix is `b8e91fe3`. Everything below is **the same tree with and without that commit's hunks**:
`/tmp/claude-1000/pf4k/fix/engine/src` is `git archive b8e91fe3`, `…/src-before` is the same export
with the ten engine files put back to `b8e91fe3^`. Both configured identically to the #PF4K
profiler's build —

```
cmake -S <src> -B <build> -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer
```

— Ubuntu 24.04, Qt 5.15.13, aarch64, 20 cores, libvterm core (`RELAY_ENGINE_WITH_GHOSTTY=OFF`: the
ghostty core builds on neither machine, so libvterm is what ships here). Xvfb, a fresh isolated
profile per run, `--clean-shell --fresh`, `RELAY_KEYRING=off`. The load average was 3–6 throughout
(four other agents were working), so read every absolute number as pessimistic and the pairs as
like-for-like: before and after runs were interleaved within minutes of each other.

Grid **148x37** in the 1400x900 window and **281x72** in the 2600x1500 one, both read back with
`tput cols`/`tput lines` inside the pane. (The #PF4K profiler's own runs were 132x30 and 280x71 for
the same pixel sizes — a different font size, the same shape of comparison.)

Harness: `spark-run.sh` (bulk output; wall time is taken by the shell *inside* the pane, CPU is
split per thread from `/proc/<pid>/task/*/stat`), `mem-run.sh` (scrollback memory on a fresh pane),
`probe-run.sh` (`perf` uprobe on `TerminalView::paintEvent`, and the GUI thread's syscall
tracepoints), `lat-run.sh` (byte → first paint). All four are beside this file.

## Headline

| | before | after | |
|---|---|---|---|
| `cat` 50 MB base64 — wall | 1.365 / 1.381 / 1.388 s | 1.347 / 1.330 / 1.317 s | 36.3 → 37.6 MiB/s |
| — GUI-thread CPU | 1.15 / 1.23 / 1.15 s | 0.64 / 0.67 / 0.64 s | **−45 %** |
| — pty-thread CPU | 1.31 / 1.34 / 1.33 s | 1.30 / 1.30 / 1.28 s | −3 % |
| — peak RSS | 187.9 MB | 178.3 MB | −5 % |
| paints per second during a flood | **167** (670 in 4 s) | **60** (241 in 4 s) | one display frame |
| GUI-thread `statx` per second | **6 597** | **494** | −93 % |
| GUI-thread `readlinkat` per second | **6 161** | **65** | −99 % |
| scrollback, 10 000 lines of 79 chars | 30.1 MB Pss = **3.01 kB/line** | 16.7 MB Pss = **1.67 kB/line** | −45 % |
| `cat` 50 MB at 281x72 — wall | 1.923 / 1.873 / 1.879 s | 1.562 / 1.525 / 1.494 s | 26.4 → **32.7 MiB/s** |
| — GUI CPU / pty CPU | 1.35 s / 1.84 s | 0.97 s / 1.48 s | −28 % / −20 % |
| — peak RSS | 230.3 MB | 194.8 MB | −15 % |
| `seq 1 2000000` (2 M scrolled lines) — wall | 2.364 s | 1.212 s | **−49 %** |
| — pty-thread CPU | 2.36 s | 1.20 s | −49 % |
| byte → first paint, 25 samples | p50 **5.2** (min 4.8, p90 6.0, max 6.3) ms | p50 **4.8** (4.4 / 5.3 / 5.4) ms | unchanged |
| GUI CPU per agent turn, turns 1–50 | 61.8 ms | 62.4 ms | unchanged — see below |
| GUI CPU per agent turn, turns 201–250 | 78.2 ms | 78.6 ms | unchanged — see below |

A maximised window no longer costs a third of the terminal's throughput: at 281 columns it now runs
at 32.7 MiB/s against 37.6 at 148 columns, where it was 26.4 against 36.3.

## Where each number comes from

**Paints and syscalls** (`probe-run.sh`). A `cat` of 750 MB is left running; then, 3 s in,
`perf stat -e probe_relay:paint -p <pid> -- sleep 4` and
`perf stat -e syscalls:sys_enter_statx,syscalls:sys_enter_readlinkat -t <gui tid> -- sleep 4`.

```
before   670 probe_relay:paint / 4.003 s     26 387 statx   24 642 readlinkat   (GUI thread, 4 s)
after    241 probe_relay:paint / 4.001 s      1 976 statx        258 readlinkat
```

167 → 60 frames a second is the whole of the frame-pacing fix: the old flood cap needed 128 MiB/s at
a 4 ms interval and libvterm delivers 37. The `statx`/`readlinkat` collapse is the per-row
`/proc/<pid>/cwd` lookup becoming one per frame — 494 `statx` a second is what the pane-status
pollers make on their own.

**Scrollback memory** (`mem-run.sh`, `Pss` from `smaps_rollup` on a *fresh* pane):

```
before  empty 48 433 kB   after 10 000 lines 78 548 kB   (+30 115 kB = 3.01 kB/line)
after   empty 48 710 kB   after 10 000 lines 65 407 kB   (+16 697 kB = 1.67 kB/line)
```

and the 10 000-line cap still holds: another 200 000 lines add 0.4 MB (before) and 0.2 MB (after).

**Byte → first paint** (`lat-run.sh`). A single byte is written to the pane's own tty from outside,
25 times, 400 ms apart, with `perf record -k CLOCK_MONOTONIC` on the paint uprobe; each write is
matched to the first paint after it. This is the path the frame timer is on — X delivery and the
kernel's echo, which the change does not touch, sit in front of it and add the rest of the 10.6 ms
the #PF4K profile measured end to end. Deliberately *not* driven by typing into the pane: a fresh
profile auto-configures Relay Free, and no run here may put a character where it could become a
hosted turn.

```
before  4.8 4.9 4.9 4.9 4.9 4.9 4.9 5.0 5.0 5.1 5.1 5.1 5.2 5.2 5.2 5.3 5.4 5.6 5.7 5.7 5.8 5.9 6.0 6.2 6.3
after   4.4 4.4 4.4 4.5 4.5 4.5 4.6 4.7 4.7 4.7 4.7 4.7 4.8 4.8 4.9 4.9 4.9 5.1 5.1 5.1 5.2 5.3 5.3 5.3 5.4
```

Every one of those is a first frame after a quiet moment, which still goes out on the 4 ms timer.
A byte that arrives while output is already streaming waits up to 16 ms instead of 4 — by design:
that is the frame the display would not have shown anyway.

## #PPR4 item 2 — GUI cost per turn stops growing with the conversation

`turns.sh 250` (the #PF4K transcript harness, stub provider on loopback, pane seeded into agent
mode, 250 turns typed back to back, each answered `ok.`), reduced by `turncost.py`: GUI-thread CPU
between the first and the last prompt of each group of 50 turns.

```
turns        before          after
   1-50      61.8 ms/turn    62.4 ms/turn
  51-100     64.5            61.2
 101-150     68.6            67.8
 151-200     74.7            72.2
 201-250     78.2            78.6
```

The before column reproduces the #PF4K curve (its own 300-turn run was 65.4 ms at turns 1–50 and
86.2 ms at 201–250). **The curve did not flatten**, and that is the honest result: the two walks
finding 3 blamed are now provably cheap — `FoldLayer::retainAnchored` is **0.07 %** and
`LibVtermCore::Impl::walkLinkRows` **0.11 %** of GUI-thread cycles in a 12 s `perf` of the after
build taken 200 turns into the same conversation (`late.perf-symbols.txt`), and the unit tests pin
the operation counts (one layout rebuild per resolve whatever the fold count; a second
`hyperlinkRuns` walk visits `rows()` rows, not the scrollback) — but they were never the dominant
term in a turn. Something else on the GUI side grows with the conversation. In that same profile
the top of the GUI thread is `qHash(QString)` 2.6 %, `operator==(QString)` 2.0 %,
`QListWidgetItem::~QListWidgetItem` 1.6 %, `statx` 1.8 %, `malloc`/`_int_free` 3.3 % — per-turn
bookkeeping in `src/`, not in `engine/`. That is recorded on #PPR4's thread for whoever holds its
item 1, since `src/Pane.h` is theirs.

## Nothing on screen changed

`engine/scripts/gui/folds.sh` drives the fold GUI (open a run fold, open a coloured diff fold,
drag a selection across a fold, open a 300-line fold, scroll inside it, search while folds are
open, search for a needle only a fold's inside has) and screenshots each state from the live
widget with `import -window root`. Run against both builds (`folds-gui.sh`), all eight screenshots
are **pixel-identical** (`compare -metric AE` = 0 for every one), and the eight `Ctrl+Shift+D`
dumps of the fold layer's own state are identical too:

```
01-folded 0   02-unfolded-run 0   03-unfolded-diff 0   04-selection-across-the-fold 0
05-unfolded-300-lines 0   06-scrolled-inside-the-long-fold 0
07-search-while-folds-are-open 0   08-search-hit-inside-a-fold 0
```

These are real screen captures, so they exercise the partial-repaint path the change touches: the
same pixels come out, with 4 % fewer paints.
