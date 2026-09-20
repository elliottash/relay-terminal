# #PF4K — terminal engine and paint path

Profiling of `engine/` (emulator core, session, pty, `TerminalView`) in the real `relay` app,
2026-09-20. Everything below is measured; nothing was changed.

## Machines and state

| | spark | sphinxpad |
|---|---|---|
| CPU | aarch64, 20 cores | x86_64 i7-1365U, 12 threads |
| OS / Qt | Ubuntu 24.04, Qt 5.15.13 | Ubuntu 26.04, Qt 5 **and** Qt 6 (both built) |
| Build | `/tmp/claude-1000/pf4k/build`, RelWithDebInfo `-O2 -g -fno-omit-frame-pointer` | `~/relay-perf/build` (Qt5), `~/relay-perf/build-qt6` (Qt6) |
| Power / governor | mains | AC=1, governor `performance` |
| Load average during runs | 1.1 – 4.5 (four other #PF4K agents) | 0.3 – 1.2 |
| Display | Xvfb :71 1400x900 (and :72 2600x1500 for the large-grid runs) | Xvfb :71 1400x900 |

Source: clean export of `main` at ccb31a8e (`/tmp/claude-1000/pf4k/src`); all `file:line` below are
that export. Terminal pane grid **132x30** in the 1400x900 window, **280x71** in the 2600x1500 one.

**Only the libvterm core builds on either machine** (`RELAY_ENGINE_WITH_GHOSTTY=OFF` in both CMake
caches — no Zig, no libghostty-vt prefix). So libvterm is what ships in these builds and every
number here describes libvterm. The 755 MiB/s ghostty figures in `docs/ENGINE-PERF.md` do not apply
to anything the owner can run on spark or sphinxpad today.

**Build flags, priced so the 2026-09-17 comparison is honest.** The #PF4K build is RelWithDebInfo
with frame pointers; the 2026-09-17 baseline was Release `-O3`. Same source, same machine, same
50 MB input, headless `relay-engine-bench`: **37.0 MiB/s** (this build) vs **46.9 MiB/s** (Release
`-O3`), i.e. the profiling flags cost 21 %. The Release number matches the 2026-09-17 baseline
(43.9–45.6 MiB/s), so **the core has not regressed since then**; read every app-level number below
as roughly 20 % pessimistic against a shipped Release build.

Raw numbers: `RAW-NUMBERS.txt`. Harness: `run-scenario.sh` (types a command into a live pane, times
it from inside the pane's own shell, splits CPU per thread from `/proc/<pid>/task/*/stat`),
`scroll.sh`, `sphinx-run.sh`. Profiles: `perf-flood-*.txt`, `perf-statx-*.txt`, `strace-flood.txt`,
`strace-hover.txt`.

---

## 1. The view repaints ~150 times a second during output. The flood cap can never engage.

**What the user feels.** Nothing on a desktop — but a whole core of a laptop's battery is spent
drawing frames no display shows. On sphinxpad, 50 MB of output costs 0.93 s of GUI-thread CPU on top
of the 1.9 s the parser needs, for frames a 60 Hz panel throws away.

**Measurement** (uprobes on `relay::TerminalView::paintEvent` and `::pullFrame`, `perf stat`, 4 s
window in the middle of a `cat` of 200 MB):

| grid | paintEvent | pullFrame | effective fps | GUI CPU / 50 MB |
|---|---|---|---|---|
| 132x30 (spark) | 590 | 589 | **147** | 1.01 s |
| 280x71 (spark) | 240 | 240 | 60 | 2.1 s |
| 132x30 (sphinxpad Qt5) | see "what blocked me" | — | — | 0.93 s |

**Reproduce**
```sh
sudo perf probe -x build/relay --add 'paint=0x<addr of TerminalView::paintEvent>'
# start a cat of a few hundred MB in a pane, then:
sudo perf stat -e probe_relay:paint -p <relay pid> -- sleep 4
```

**Root cause.** `engine/view/TerminalView.cpp:448-460`:

```cpp
const quint64 bytes = m_session->bytesReceived();
const bool flooding = bytes - m_bytesAtFrame > 512 * 1024;
const int wanted = flooding ? 33 : 4;
```

`m_bytesAtFrame` is reset in `pullFrame` (`:466`), so `flooding` asks whether more than 512 KiB
arrived *since the last frame*. At a 4 ms interval that needs **over 128 MiB/s**. libvterm delivers
37 MiB/s on spark and 26 MiB/s on sphinxpad, so 4 ms never grows to 33 ms and the "~30 fps while
flooding" behaviour described in `docs/ENGINE-PERF.md` never happens with the core that ships. It is
a feedback loop that latches at the fast end: the more often you frame, the less each frame
accumulated, the more often you frame. (The ghostty core at 755 MiB/s *would* trip it — the
threshold was tuned for a core that is not built here.)

**Fix.** Pace on time, not bytes. Keep 4 ms for the first frame after an idle gap so echo latency is
untouched, and clamp to one display frame while output keeps arriving:

```cpp
const bool streaming = bytes != m_bytesAtFrame;          // anything at all arrived
const int wanted = streaming ? 16 : 4;
```

`m_frameTimer` already restarts with the `interval() <= wanted` guard, so no other change is needed.

**Expected gain.** 147 → ~60 frames/s at 132x30. Measured per-frame cost is 2.1 s / 590 = **3.6 ms**
(pull + paint), so GUI CPU during a flood falls from ~1.0 s to ~0.42 s per 50 MB on spark and ~0.93 s
to ~0.40 s on sphinxpad. Wall time is unchanged on a multi-core box (the parser is the bottleneck)
but the laptop gets ~0.5 s of a core back per 50 MB of output.

**Risk.** Low. The only visible difference is an intermediate frame no display could have shown.
Keystroke echo (10.6 ms p50, below) comes from the *first* frame after idle, which still uses 4 ms.

---

## 2. `restLinkColumns` does a `readlink`+`stat` of `/proc/<fgpid>/cwd`, and takes the core mutex, once per painted row per frame.

**What the user feels.** 20 % of the terminal's paint CPU, and a lock the parser thread has to yield
for thousands of times a second during output.

**Measurement.**

- `perf record -e syscalls:sys_enter_statx -g` during a flood: **90.75 % of every `statx` the process
  makes** comes from `paintEvent → paintRow → restLinkColumns → currentDirectory →
  QFileInfo::symLinkTarget()` (`perf-statx-flood.txt`). The rest is the pane-status pollers.
- `strace` of a 1.8 s `cat` of 50 MB: 4 611 `readlinkat("/proc/N/cwd")` + 4 611 `statx` of the same
  (`strace-flood.txt`) — one pair per painted row.
- A/B with Options › Terminal › "colour links" off, spark, 3 runs each:
  GUI CPU per 50 MB **1.01 s → 0.81 s (−20 %)**, wall 1.37 s → 1.29 s, peak RSS 183 → 173 MB.

**Reproduce**
```sh
# add  [terminal]\ncolour_links=false  to the pane's relay.conf, restart, re-run:
run-scenario.sh cat50 <pid> <win> "cat big.b64"
```

**Root cause.** `engine/view/TerminalView.cpp:723-724` calls `restLinkColumns()` for every painted
row; `:1858` calls `currentDirectory()` inside it, per row. `currentDirectory()`
(`engine/view/TerminalView.cpp:1624-1647`) first calls `TerminalSession::currentDirectory()`, which
takes `GuiLock` — the *same* mutex the pty thread holds while feeding each 64 KiB chunk
(`engine/session/TerminalSession.cpp:327-331`, `:164-175`) — and then, when the shell has published
no OSC 7 cwd, resolves `/proc/<pid>/cwd` with `QFileInfo::symLinkTarget()` (readlink + stat). When
OSC 7 *has* published one it still costs a `statx`, from the `QFileInfo(dir).isDir()` check at
`:1634`. Either way: one filesystem round trip and one core-mutex acquisition per row per frame —
about 4 400 of each per second at 147 fps. The pty thread's profile shows the consequence: 5.2 %
`__schedule`, 1.2 % `__sched_yield` (`perf-flood-132x30.txt`).

`restLinkColumns` also builds a fresh cache-key `QString` (mode + cwd + the whole logical line) per
row per frame, and `logicalRowAt` (`:1697-1717`) rebuilds the joined logical line cell by cell with
`out->text += text` before the cache is even consulted — `logicalRowAt` is 0.56 % of GUI samples and
`QString::resize` another 0.66 %.

**Fix.**
1. Resolve the cwd **once per frame**: read it in `pullFrame` (`:462`) into a member and have
   `restLinkColumns` and `paintRow` use that. It cannot change within one frame. This removes both
   the syscall and the mutex acquisition from the per-row path.
2. Key `m_restLinks` on row identity rather than row text: the frame already carries per-row dirty
   flags (`m_frame.dirty`), so an unchanged row keeps its span vector and `logicalRowAt` need not run.

**Expected gain.** ~0.2 s of the 1.0 s GUI CPU per 50 MB from (1) alone — measured directly by the
colour-links A/B, which removes the whole call. With finding 1 as well, GUI CPU per 50 MB should fall
from 1.0 s to about 0.35 s on spark. The 4 400 lock acquisitions/s disappearing should give the
parser a percent or two back too.

**Risk.** Low for (1) — a cwd that changes mid-frame takes effect on the next frame, 4–16 ms later.
Medium for (2), because the fold layer can move rows; gate it on `!foldsVisible()`.

---

## 3. Every scrolled-out line is converted and stored at the full terminal width.

**What the user feels.** A maximised window costs a third of the terminal's throughput and 2–3x the
scrollback memory of a narrow one, for nothing.

**Measurement.**

| | 132x30 | 280x71 |
|---|---|---|
| wall, `cat` of 4 x 50 MB (spark) | 5.43 s (**36.8 MiB/s**) | 7.81 / 7.86 s (**25.6 MiB/s**) |
| pty-thread CPU | 5.24 s | 7.63 / 7.64 s |
| peak RSS | 176 MB | 230 MB |
| `convertCell` share of pty thread | 13.3 % | **18.4 %** |
| `sb_pushline_from_row` | 8.2 % | 11.4 % |
| `vector<Cell>::_M_default_append` | 3.0 % | 5.2 % |

(`perf-flood-132x30.txt`, `perf-flood-280x71.txt`. Both floods are the same 99-character lines, so
the extra work at 280 columns is entirely blank cells.)

Memory, spark, `smaps_rollup`: an empty pane is Pss 38.6 MB; after 10 000 lines of ~80 characters it
is 66.4 MB (**+27.7 MB = 2.8 kB per line**). 132 columns x 20 B per `Cell` = 2 640 B, plus the `Line`
object — the stored line costs the *grid* width, not its own. Pushing another 200 000 lines leaves
Pss at 66.6 MB, so the 10 000-line cap itself is sound.

**Reproduce.** `cat` the same file in a 1400x900 and a 2600x1500 window; compare wall time and
`VmHWM`. `perf record -p <pid>`, read the pty thread's flat profile.

**Root cause.** `engine/core/LibVtermCore.cpp:299-319`:

```cpp
l->cells.resize(size_t(cols));
for (int i = 0; i < cols; ++i)
    d->convertCell(cells[i], l, &l->cells[size_t(i)]);
while (!l->cells.empty() && l->cells.back().isBlank() && l->cells.back().attrs == 0)
    l->cells.pop_back();
```

Every one of the 280 cells is converted (colour conversion, attribute unpacking, cluster handling)
and only then are trailing blanks popped — and `pop_back` does not release capacity, so the vector
keeps its grid-width allocation for as long as the line is in scrollback. `readScreenRow`
(`:226-240`) has the same shape for the live viewport, once per visible row per frame.

**Fix.** Scan backwards over the incoming `VTermScreenCell` array first — blank is
`chars[0] == 0 && width <= 1 && VTERM_COLOR_IS_DEFAULT_*(&fg/&bg) && attrs == 0` — resize to that
length, convert only those cells, and give the vector its exact size (`shrink_to_fit()`, or build
into a right-sized vector and move it in). `wrapColumns` must keep recording the full `cols`
(`:312`), since reflow and `Line::text()` depend on it.

**Expected gain.** For 60–100-character lines in a 200–300-column window the conversion loop shrinks
2–3x. The width-proportional share of the pty thread at 280 columns is ~35 % (`convertCell` +
`sb_pushline_from_row` + the vector resize); removing roughly two thirds of it puts throughput at
280x71 back near 33–35 MiB/s from the measured 25.6 — **a maximised window stops costing a third of
the terminal's speed**. Scrollback for 10 000 lines of 80-character output drops from 2.8 kB to about
1.7 kB per line at 132 columns, and from ~5.6 kB to ~1.7 kB at 280.

**Risk.** Low–medium. The blank test must match `Cell::isBlank()`'s meaning exactly (a cell with a
non-default background or an underline is not blank), and `readScreenRow` must keep resizing to the
grid width, because the viewport's `Line` is indexed by column.

---

## 4. One fold anchor on screen turns every frame into a repaint of every content row.

**What the user feels.** Agent panes — which always carry tool-call fold anchors — repaint the whole
viewport for every frame of streaming output, where a terminal pane repaints one row.

**Measurement** (uprobes on `paintEvent` and `paintRow`, 20 single-character writes over 6 s):

| pane state | paintEvent | paintRow | rows per frame |
|---|---|---|---|
| plain terminal, no folds | 30 | 30 | **1.0** |
| one `relay://call/` OSC 8 anchor on screen | 16 | 77 | **4.8** (= every row that had content) |

That pane had six content rows; on a full 30-row screen the same code paints 30 rows instead of one.
The measured cost of a full 132x30 row set is 3.6 ms against ~0.12 ms for a single row.

**Reproduce**
```sh
printf '\033]8;;relay://call/abc\033\\ tool line \033]8;;\033\\\n' > fold.txt   # cat this in a pane
sudo perf stat -e probe_relay:paint,probe_relay:row -p <pid> -- sleep 6         # while one char/150ms prints
```

**Root cause.** `engine/view/TerminalView.cpp:493`:

```cpp
if (m_frame.full || force || foldsVisible()) {
    update();                      // whole widget
} else { ...build a QRegion from m_frame.dirty... }
```

`foldsVisible()` (`TerminalView.h:284`) is `m_folds.active() && !altScreen`, and `active()` is
`!m_anchors.empty()` (`FoldLayer.h:152`) — *any* anchor, open or shut. `src/Pane.h:9098` sets the fold
prefix on every pane that supports folds, so an agent pane is permanently in this state.

**Fix.** A *collapsed* anchor does not move any row: the visual-row mapping is the identity and the
existing dirty-row region is already correct. Gate the full repaint on there actually being an open
fold — `m_folds.expandedCount() > 0` rather than `foldsVisible()` — keeping the full repaint for the
frame in which a fold opens or closes (`m_forceFull` / `m_foldAnchorsDirty` already cover that) and
for `paintFoldRow`'s own rows.

**Expected gain.** Agent-pane frames go from ~30 painted rows to 1–2 while nothing is expanded:
3.6 ms → ~0.2 ms of GUI CPU per frame. With finding 1 this is the difference between an agent pane
costing most of a core while streaming and costing a few percent.

**Risk.** Medium — the anchor chevron overpaint at `:819-833` and `m_folds.foldAtAnchorStart()` must
still land on the rows that need it. Worth a `ViewTest` that opens a fold and checks the row mapping.

---

## 5. (Cross-area, `src/Pane.h`) Every key event costs about 590 filesystem syscalls.

Not engine code; found while profiling keystroke latency, handed over rather than fixed.

**Measurement** (`perf stat` on the `statx`/`faccessat` tracepoints): 30 keystrokes in the prompt box
produced **12 970 `statx` and 5 244 `faccessat`** against an idle baseline of 460/144 over the same
4 s — **417 `statx` + 170 `faccessat` per key event**, about 1.2 ms of syscall time on the GUI thread
per keypress. The same tax applies to keys typed straight into the terminal (Ctrl+H mode): 10 868
`statx` for 20 keystrokes.

**Root cause** (`perf-statx-typing.txt`, full call graph): an application-wide event filter runs on
every `KeyPress` *and* `KeyRelease` — `src/Pane.h:3485-3489`:

```cpp
const QString hold = voiceHoldKey();
const bool isVoiceKey = voiceEnabled() && relay::voice::isHoldKey(hold, ...);
```

`voiceHoldKey()` (`src/Pane.h:5845`) constructs a `QSettings` *and* opens `/etc/default/keyboard`;
`voiceEnabled()` (`:5834`) constructs another. Qt `stat`s all seven candidate config paths on every
`QSettings` construction. The prompt box's `updateGhost()` (`:497`, on every
`cursorPositionChanged`) makes a third. The filter is per pane, so the cost multiplies by the number
of open panes.

**Fix.** Cache `voice/enabled` and `voice/hold_key` (and the `/etc/default/keyboard` layout string)
in members, refreshed from the existing `applySettings()` path, instead of re-reading per key event.

---

## 6. Card #9MYY re-checked by measurement

The card asserted three hot paths from code reading. Measured:

1. **`linkAt()` per hover cell — real, but LOW, not MEDIUM.** Sweeping the pointer one cell at a time
   across a screen full of paths costs **0.141 ms of GUI CPU per hovered cell** (423 steps, 0.060 s)
   and **4–5 filesystem syscalls per cell** — `readlink`+`stat` of the cwd, then `access`+`stat` of
   the candidate (`strace-hover.txt`). At any human pointer speed that is well under 1 % of a core.
   The card's fix is still worth doing, but the code it names matters because `restLinkColumns` calls
   the *same* `logicalRowAt` + `links::scan` **per painted row** (finding 2), not because of hover.
2. **`colorsFor` twice per cell — real, but below the noise floor.** `colorsFor` is 1.91 % of GUI
   samples during a flood and `resolve()` 1.44 % (3.40 % / 2.60 % at 280 columns). With a live search
   holding **15 695 matches** (about 10 highlight ranges per visible row), Shift+PgUp through a
   10 000-line scrollback costs 3.00–4.00 ms per page against 3.25–4.00 ms with no search at all — no
   difference above run-to-run noise. Keep it as a readability cleanup; it will not show up as a
   performance win.
3. **`TerminalAccessible::allText()` — not on any hot path here.** Reachable only from `QAccessible`
   queries, and in none of the profiles (no AT client was running). It remains a real problem *for a
   screen-reader user*: `characterRect()` (`:129-141`) and `offsetAtPoint()` (`:142-152`) each rebuild
   the whole viewport string **and re-split it** per character query — O(viewport) per call. Measuring
   that needs an AT client, which was out of reach here; the fix (cache the joined text and the split,
   keyed on the frame version) is cheap and should just be done.

---

## Measured and fine — do not re-profile these

- **Dirty-row repaint in a plain terminal pane.** A one-character echo paints exactly one row
  (30 `paintEvent` / 30 `paintRow` over 20 keystrokes). The region code at `:498-513` works.
- **Keystroke-to-echo latency: p50 10.6 ms** (min 8.5, p90 11.6, max 12.2, 20 samples, uprobe on
  `paintEvent` against `CLOCK_MONOTONIC`). That covers X delivery, `Pty::write`, the kernel tty echo,
  the parse on the pty thread, the cross-thread delivery, the 4 ms coalescing timer and the paint.
  Inside one 60 Hz frame. The path is clean: key presses write to the pty from the GUI thread without
  waiting for the parser, and the parser hands the GUI thread a `ViewportFrame` snapshot of dirty rows
  only.
- **Resize / reflow with a 10 000-line scrollback: 20–30 ms of CPU per resize**, 60 ms for 20 rapid
  drag steps. The Relay-patched libvterm does not rewrap history, so this does not grow with
  scrollback depth; `m_geometryTimer` coalesces the drag.
- **Scrolling back through 10 000 lines:** 3.0–4.0 ms of GUI CPU per Shift+PgUp (30 rows), 2.0 ms per
  wheel step. The `m_restLinks` cache absorbs the link scan on the second pass.
- **The scrollback cap.** Pushing 200 000 lines past a 10 000-line limit leaves Pss flat at 66.6 MB.
- **Glyph cache.** `glyphFor` is 1.9–2.0 % of GUI samples, correctly keyed on (font variant,
  codepoint); `QRawFont::glyphIndexesForString` shows up only through it.
- **Escape-heavy output.** 20 MB of per-cell SGR runs at ~135 MiB/s on spark and ~130 MiB/s on
  sphinxpad — the escape parser is not a bottleneck. Wide unicode (CJK + emoji) runs at ~53 MiB/s on
  spark, ~44 MiB/s on sphinxpad; the emoji path takes the `QPainter::drawText` fallback per cell,
  which is why it is slower than ASCII and is the right trade for how rare it is.
- **The core has not regressed since 2026-09-17** — 46.9 MiB/s headless in a Release build against the
  baseline's 43.9–45.6.

---

## spark vs sphinxpad, and Qt5 vs Qt6 on the laptop

Same 1400x900 window, same grid, same inputs, libvterm core, three runs each.

| scenario | spark (aarch64, -O2+fp) | sphinxpad Qt5 | sphinxpad Qt6 |
|---|---|---|---|
| `cat` 50 MB base64 — wall | **1.37 s** (36.6 MiB/s) | 1.92 s (26.0 MiB/s) | **1.79 s** (28.0 MiB/s) |
| — pty-thread CPU | 1.33 s | 1.88 s | 1.75 s |
| — GUI-thread CPU | 1.01 s | 0.93 s | **0.78 s** |
| `cat` 20 MB SGR torture | 0.14 s | 0.154 s | 0.160 s |
| `cat` 20 MB CJK + emoji | 0.37 s | 0.457 s | 0.441 s |
| `seq 1 2000000` (2 M lines) | 2.07 s | 3.73 s | 3.74 s |
| `ls --color -R /usr` (warm) | 1.46 s | 2.01 s | 2.03 s |
| peak RSS at startup | 155 MB | 119 MB | **80 MB** |
| Pss after all scenarios | 67 MB | 90 MB | 75 MB |

- spark is 1.4x the laptop on bulk output and 1.8x on line-feed-bound output (`seq`), despite carrying
  the 21 % profiling-flag penalty and a load average of 2–4 from four other agents. The laptop's
  number is what a user on the shipped .deb gets.
- **Qt6 is not slower than Qt5 anywhere measured**, is 7 % faster on the bulk-output headline, uses
  16 % less GUI CPU for it, and starts with **33 % less RSS**. Nothing here argues against the .deb's
  Qt6.
- Per-line cost dominates line-heavy output: `seq 1 2000000` is 2 M scroll operations at
  **1.03 µs/line** on spark and **1.87 µs/line** on the laptop, with the GUI thread nearly idle
  (0.5 s) — that time is all `sb_pushline_from_row` + `convertCell` + `erase_internal`, which is what
  finding 3 attacks.

## What blocked me

- **Paint counts on sphinxpad.** The uprobe counting behind finding 1's headline number could not be
  repeated on the laptop: driving a *fresh* pane there with `xdotool` proved unreliable (first-run
  banners steal focus, and a mistyped command makes Relay hand the line to the agent — on sphinxpad
  the Relay Free preset auto-configures, so one such turn reached the hosted service before I added a
  warm-up guard that aborts rather than retype). I stopped rather than keep firing agent turns the
  brief forbids. The laptop's GUI-thread CPU per 50 MB (0.93 s Qt5 / 0.78 s Qt6) is measured and is
  what finding 1's estimate applies to; only the frames-per-second figure is spark-only.
- **Agent-pane fold behaviour (finding 4) was measured with a hand-printed OSC 8 anchor**, not a real
  agent transcript, because no provider key may be used and standing up the stub-provider harness was
  outside the time this left. The 1.0 → 4.8 rows-per-frame result is decisive about the mechanism; the
  absolute cost in a real agent pane (30 rows instead of 1 on a full screen) is arithmetic from the
  measured 3.6 ms per full 132x30 row set. `src/Pane.h` belongs to the transcript agent in any case.
