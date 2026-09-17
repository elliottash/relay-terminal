# Terminal engine performance (2026-09-17)

Measurements for the Relay terminal engine ([ENGINE.md](ENGINE.md)) and the emulator cores it can
use. Machine: aarch64, 20 cores, Ubuntu 24.04, Qt 5.15.13, Release builds (gcc 13.3 `-O3`;
libghostty-vt `ReleaseFast` with Zig 0.16.0). Other agents were using the machine (load average
5-6), so expect a few percent of noise; every number below is from three runs unless noted.
Evidence: [qa_evidence/2026-09-17-engine-phase1/](qa_evidence/2026-09-17-engine-phase1/).

## Inputs

| File | Content | Notes |
|---|---|---|
| `big200.txt` | 209 715 200 bytes of base64, 99 characters per line + `\n` | The spike's worst case. Fed **raw** (no pty), `\n` without `\r` makes every line wrap once: 4.17 M scrolled lines. Through a real pty (ONLCR) it is 2.1 M lines |
| `nonl50.txt` | 51 975 000 bytes, the same data with newlines removed | Autowrap only |

Terminal 100x30, 10 000 lines of scrollback, input fed in 64 KiB chunks.

## Headless core throughput (no pty, no painting)

`relay-engine-bench --core NAME FILE` (feeds through `relay::VtCore`, including Relay's adapter
and scrollback code). MiB/s; peak RSS minus the RSS right after loading the input.

| Core | big200 MiB/s | nonl50 MiB/s | Core memory (big200) | Source |
|---|---|---|---|---|
| libvterm 0.3.3, distro, unpatched (raw C bench, host ring of `VTermScreenCell`) | 21.5 / 21.6 / 21.5 | 33.9 / 34.1 / 33.9 | 31 MB | `cores/vterm-bench` |
| libvterm 0.3.3, Relay-patched (`LibVtermCore`) | 43.9 / 45.1 / 45.6 | 57.6 / 60.4 / 60.8 | 21 MB | `relay-engine-bench` |
| Contour v0.7.0 `vtbackend` (MockTerm, `writeToScreen`) | 97.7 / 91.6 / 91.0 (earlier set: 116 / 86 / 118) | 155 / 159 / 160 (earlier: 199 / 176 / 164) | 63 MB | separate driver, one gcc-13 patch (`std::ranges::to`) |
| **libghostty-vt** (`GhosttyCore`) | **755.6 / 756.7 / 750.2** | **894.6 / 912.0 / 889.1** | 11 MB | `relay-engine-bench` |
| libghostty-vt + a `ViewportFrame` extracted after every 64 KiB chunk | 556 (one run) | - | - | `--frames` (upper bound of render-state cost) |

libghostty-vt scrollback limit matters: with `SCROLLBACK_MAX_LINES=10000` it prunes line by line and
drops to **83 MiB/s** (raw C bench, 9 619 rows kept); with the equivalent byte limit
(`SCROLLBACK_MAX_BYTES`, about 10 bytes per cell for plain text) it keeps 8 700-11 500 rows at
700+ MiB/s. `GhosttyCore` therefore translates the line setting into a byte budget.

### Why libvterm was slow and what the patch changed

`perf` on the unpatched library (40 MB of `big200`): **52 % `memcpy`** and **13 %
`vterm_screen_get_cell`**. Every line feed at the bottom `memmove`d the whole 30x100 cell
buffer (60 bytes per cell after the Relay patch, 40 before) and rebuilt the scrolled-out row
cell by cell for `sb_pushline`. Relay's vendored copy (see
`engine/third_party/libvterm/README.relay.md`):

1. keeps a row-pointer table per screen buffer and rotates pointers on full-width scrolls
   (resize linearizes first, so the reflow code is untouched);
2. converts a scrolled-out row in one pass (`row_to_external`) instead of per-cell `get_cell`;
3. uses `VTERM_DAMAGE_SCROLL` with a cheap `moverect` callback in the adapter.

Result: 21.5 -> 45 MiB/s on `big200`, 34 -> 60 MiB/s on `nonl50`. The remaining time is libvterm's
per-glyph path (UTF-8 decode, width lookup, `putglyph` callback per character), which would need a
batched text path to go further. That is not worth doing now that libghostty-vt is 16x faster.

## GUI: `cat` of the 200 MB file in a terminal window

`engine/scripts/gui/perf-cat.sh` under Xvfb 1400x900 (DISPLAY :78), terminal 100x30. Wall is
measured by the shell inside the terminal from `cat` start to exit (includes pty back-pressure);
terminal CPU is the terminal process over the same window + 1 s settle.

| Terminal | Wall s (3 runs) | Terminal CPU s | Peak RSS |
|---|---|---|---|
| Spike (2026-09-17 morning, libvterm on the GUI thread) | 7.6-7.7 | 7.3-7.6 | 158 MB |
| **relay-vterm-spike, ghostty core** | **1.17 / 1.16 / 1.34** | 1.74 / 1.72 / 1.99 | 137 MB |
| relay-vterm-spike, libvterm core | 4.04 / 4.02 / 4.04 | 5.32 / 5.37 / 5.35 | 146 MB |
| Konsole 23.08.5 | 3.66 / 3.57 / 3.71 | 3.43 / 3.40 / 3.49 | 142 MB |
| xterm 390 | 2.91 / 2.94 / 2.91 | 2.73 / 2.75 / 2.72 | 13 MB |

Target was "at most Konsole's time" (and within 1.2x of Konsole's 3.7 s for the core decision): the
ghostty core takes **about a third of Konsole's time**. The libvterm core is 1.1x Konsole: better than
the spike's 2x, and within the 1.2x fallback budget. Terminal CPU above wall time is the second
thread (parse on the pty thread, paint on the GUI thread).

What made the GUI path fast, independent of the core:

- **Parsing off the GUI thread.** `relay::Pty` reads on its own thread and
  `TerminalSession::onPtyOutput` feeds the core there under one mutex. The GUI thread only takes a
  `ViewportFrame` snapshot (dirty rows only) under the lock and paints from the snapshot.
- **Lock hand-off.** The pty thread yields while the GUI thread waits for the lock, so a paint or a
  key press never waits for more than one 64 KiB chunk.
- **Coalesced repaint.** Core events are delivered once per event-loop pass; the view repaints
  4 ms after a change normally and at ~30 fps while more than 512 KiB arrived since the last frame.
- **Glyph runs.** Text is drawn with `QRawFont` glyph indexes at exact cell positions
  (`drawGlyphRun`), one run per colour/font per row; box drawing and blocks are filled rectangles.

## Ctrl+C latency during a flood

`engine/scripts/gui/interrupt.sh`: `cat` of eight copies of `big200.txt`, xdotool Ctrl+C 1.5 s later,
time until the `cat` process is gone (polling every 5 ms; xdotool itself adds a few ms).

| Terminal | Ctrl+C -> cat exit (3 runs) |
|---|---|
| **relay-vterm-spike, ghostty core** | **32 / 31 / 31 ms** |
| relay-vterm-spike, libvterm core | 32 / 31 / 32 ms |
| Konsole 23.08.5 | 31 / 33 / 32 ms |

Target < 50 ms: met. Key presses write to the pty directly from the GUI thread (`Pty::write` tries
a non-blocking write before queueing), so input never waits for the parser.
`relay-engine-tests` also checks Ctrl+C through the view (`ViewTest::ctrlCInterruptsForegroundJob`,
< 500 ms on the offscreen platform) and that the GUI thread keeps running timers during a
20 000-line flood (`SessionTest::floodIsParsedOffTheGuiThread`).

## Reproducing

```sh
# libghostty-vt (optional core; needs Zig 0.16)
engine/scripts/build-libghostty-vt.sh ~/opt/ghostty-vt
cmake -S . -B build-engine -G Ninja -DRELAY_QT_MAJOR=5 -DRELAY_BUILD_APP=OFF \
  -DRELAY_BUILD_ENGINE=ON -DRELAY_ENGINE_WITH_GHOSTTY=ON -DRELAY_GHOSTTY_VT_PREFIX=$HOME/opt/ghostty-vt \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-engine
build-engine/engine/relay-engine-bench --core ghostty big200.txt
Xvfb :78 -screen 0 1400x900x24 &
DISPLAY=:78 engine/scripts/gui/perf-cat.sh ghostty "$PWD/big200.txt" /tmp/perf \
  build-engine/engine/relay-vterm-spike --core ghostty --size 100x30 -e
DISPLAY=:78 engine/scripts/gui/interrupt.sh ghostty "$PWD/big200.txt" /tmp/perf \
  build-engine/engine/relay-vterm-spike --core ghostty --size 100x30 -e
```

The raw C benches (`vterm-bench`, `ghostty-bench`) and the Contour driver were one-off programs in
the session scratch directory; their method is described above and the numbers are reproducible with
`relay-engine-bench` for the two cores Relay builds.
