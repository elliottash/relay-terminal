# Tool output on the wire — #PPR4, measured on spark, 2026-09-20

Findings 2 and 5 of [the transcript profile](../../2026-09-20-perf-profile/transcript/FINDINGS.md):
the GUI parses about 66 KB of tool output per tool call and throws all of it away, and one
event-loop iteration of 65–81 ms at the start of every turn that the profile could measure but not
place. The fixes are protocol 23.10 (`stream_tool_output`) and the model catalog's curated list.

**Before** is `/tmp/claude-1000/pf4k/build/relay` — the clean export of `main` the profile itself
was measured on. **After** is a clean export of the landed fix, configured the same way
(`RelWithDebInfo`, `-fno-omit-frame-pointer`, Qt5, libvterm). Both ran the profile's own harness
(`docs/qa_evidence/2026-09-20-perf-profile/transcript/`: `stub.py`, `drive.sh`, `an.py`) under Xvfb
on a private display, with a fresh `HOME`/`XDG_*`/`TMPDIR`, `RELAY_KEYRING=off`, `--clean-shell`,
no provider key and the loopback stub as the model. Five other #PF4K agents were profiling and
building on the box throughout — load average 3–5 — which is why the tool-heavy scenario was run as
**three alternating before/after rounds** rather than once.

One difference from the profile's own setup: its `big.txt` was not kept, so this one is
regenerated at the same size (2 000 lines, 156 000 bytes). The before numbers reproduce the
published ones to within 2 %, which is the check that it is the same scenario.

## 1. The channel: bytes per message

`IPC=1 ./drive.sh … 'please stream zprose20000x20r200 now' 'please run ztools20x5 now'` — one
20 000-character prose turn and one turn of 20 `run_command` calls, 2 000 lines of output each.
Full tables in `ipc-before.txt` / `ipc-after.txt`; the reducer is `reduce.py`.

| worker → GUI | before | after |
| --- | --- | --- |
| the whole channel, both turns | 1 642 498 B | **316 587 B** — −80.7 % |
| `tool_output` × 20 | 664 500 B (40.5 %) | **1 780 B** (0.6 %) |
| `tool_result` × 20 | 672 082 B (40.9 %) | **9 242 B** (2.9 %) |
| the two, per tool call | 66 829 B | **551 B** — −99.2 % |
| `delta` × 1 009 | 51 768 B | 51 768 B — unchanged |

The published before figures are 1 642 216 B, 664 500 B and 672 082 B. What is left at the top of
the after table is `presets` (223 KB, 70.6 % of what remains) and `delta`; neither is this card's.

## 2. Scenario (d): 200 `run_command` calls, 2 000 lines each

GUI-thread CPU seconds for the whole run (`an.py` over `drive.sh`'s 200 ms per-thread samples),
three rounds, before and after alternating so the other agents' load falls on both sides
(`cpu-ab.txt`, `ab.sh`):

| round | before | after |
| --- | --- | --- |
| 1 | 6.89 s | **5.84 s** |
| 2 | 6.80 s | **5.74 s** |
| 3 | 7.15 s | **5.49 s** |
| mean | **6.95 s** (34.7 ms/call) | **5.69 s** (28.4 ms/call) |

**−1.26 s of 6.95 s = −18.1 %**, and every round is the same way round. The card asked for "of the
order of 1 s of the 6.8 s"; the published before figure for this scenario is 6.81 s, and the three
before runs here average 6.95 s, so the harness is reproducing it.

## 3. Where the cycles went

`perf record -g --call-graph fp -F 499` for 25 s of the same scenario, before and after
(`perf-before.txt`, `perf-after.txt`). Cycles counted on the whole process over the window:
**9.10 G before, 5.68 G after.**

The share `QJsonDocument::fromJson` holds could **not** be re-resolved the way finding 2 reported
it (7.1 %, inclusive, GUI thread). At 499 Hz over a 25 s window that contains roughly 15 s of work
these runs collected 1 445 and 937 samples, and Qt's JSON parser is inlined into unnamed addresses
in the distribution's stripped `libQt5Core`, so neither flat profile names a JSON symbol above
0.4 %. The two numbers that do settle it are the bytes in §1 and the CPU in §2; this section is
here to say plainly that the third one was not reproduced rather than to leave it looking measured.

## 4. Item 3: the 65–81 ms hitch at the start of a turn

`stall.sh`, and `stall.txt` for what it caught. Finding 5 measured the gap between two `ppoll()`s,
so the stack it could take was always the event loop and the stall was already over. This probe
arms a timestamp at `sys_exit_ppoll` and lets a 997 Hz profile probe take a user stack *while* the
thread has been busy for more than 10 ms, so every stack is from inside a long iteration.

Four turns, 60 samples in long iterations, **75 % of them in the model catalog** and 67 % in one
call chain:

```
QSettings::QSettings(...)                 ← QFileInfo::absoluteFilePath → statx / access / fstatat
relay::models::curation::shownKeys()
relay::models::curation::isShown(Entry)   ← once per catalog entry
relay::models::shown(Catalog)
Pane::refreshPickers()
Pane::changed()
Pane::pumpQueue()
Pane::handle(QJsonObject)
```

`isShown()` builds a `QSettings`, which re-stats the whole XDG search path — eight `statx` per
construction, finding 4 — and `shown()` asked it once per entry. `refreshPickers()` runs from
`changed()`, which a turn start calls, so on an OpenRouter catalog that is a few hundred `QSettings`
constructions inside one event-loop iteration. The innermost frames agree: of 49 distinct stacks,
13 are in `statx`, 4 in `__access` and 3 in `fstatat`.

Fixed by reading the curated list once for the whole walk (`src/ModelCatalog.cpp`, `shown()`;
`curation::isShown(entry, keys)` is the overload that takes it). The remaining 8 samples are
`FoldLayer::layout` under `setProseBlock`, which is finding 3's area and what #6W0Z rewrote in
`b8e91fe3`.

## Files

`ipc-before.txt`, `ipc-after.txt` and `reduce.py` (the channel tables and the reducer); `ab.sh` and
`cpu-ab.txt` (the six scenario-(d) runs); `perf-before.txt`, `perf-after.txt` (flat profiles);
`stall.sh` and `stall.txt` (the turn-start probe and what it caught). No `perf.data` is kept; the
reports made from them are.
