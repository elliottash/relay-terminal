# #057J — an idle pane wakes 13 times a second; QSettings on hot paths

Card: `issues/changes/done/2026-09-20-idle-pane-wakes-13-times-a-second-qsettings-on-hot-paths.md`.
Landed in `e73e7bae`. Profile this answers:
`docs/qa_evidence/2026-09-20-perf-profile/startup/FINDINGS.md` finding 1,
`engine/FINDINGS.md` finding 5, `transcript/FINDINGS.md` finding 4.

## What changed

| # | The finding | The fix |
|---|---|---|
| 1a | `Pane::pollGuestEvents()` lists an empty directory 12.5×/s per pane | `relay::runtimedirs::DirStamp`: list only when the spool directory's mtime or inode moved — the gate `pollShell()` already keeps on `state.json` |
| 1b | a `QSettings` per read of `appearance/pane_usage`, `voice/*`, `agent/show_tool_output`, `logging/level`, `composer/history_suggestions` | `relay::settings` (`src/SettingsCache.h`): one cached read per key, dropped by `SettingsWatch::notify()` — which every control in Options already calls after it writes |
| 1c | `tunePoll()` asks only `isVisible()`, which stays true when the window is iconified | also `window()->isMinimized()` and `QGuiApplication::applicationState()`, re-tuned from `QEvent::WindowStateChange` and `applicationStateChanged` so coming back is instant |
| 1d | `RichEditor`'s caret blinks whether or not the box has focus | `focusInEvent` / `focusOutEvent` / `hideEvent` / `showEvent`, as `engine/view/TerminalView.cpp:2713` does |
| 5 | `relay::usage::walkTrees` walks `/proc` for every pane in every tab at 2.5 Hz | the tab in front keeps 2.5 Hz; a tab behind it is sampled on the tab label's own 5 s clock, which is the only thing that reads it |

## Tests

| Test | What it proves |
|---|---|
| `ctest -R settingscache` (`tests/settingscache_test.cpp`, new) | a value read 1 000 times builds **one** `QSettings` (`relay::settings::reads()` is the counter); an unset key caches its fallback too; `invalidate()` puts the file's value back in front of the reader; `metersEnabled()` and `relay::log::level()` each read once however often they are asked |
| `ctest -R "^settings$"` — `aSettingChangedInOptionsIsInEffectAtOnce` | end to end: a control in Options writes, and the cached value is the new one on the **next** read, with no event loop turn in between |
| `ctest -R runtimedirs` — `aDirectoryStampReportsOnlyRealChanges` | the guest-event gate: first look, an event written, an event removed, the directory replaced (new inode) — and no listing in between |
| `ctest -R "^editor$"` — `theCaretBlinksOnlyForTheFocusedBox` | the blink timer is stopped without focus, stopped when hidden, running again when both come back |
| `ctest -R "^logging$" "^paneusage$" "^voice$" "^panes$" "^panestatus$" "^titles$" "^guestbridge$" "^appcommands$"` | unchanged behaviour on everything downstream |

## Numbers

spark (aarch64, 20 cores), Xvfb `:241`, fresh isolated profile per run, `RELAY_KEYRING=off`,
`--clean-shell --fresh`. **before** = the clean export of `ccb31a8e` at
`/tmp/claude-1000/pf4k/build/relay`, **after** = this checkout built with the fix. The harness is
the profiler's own (`docs/qa_evidence/2026-09-20-perf-profile/startup/harness/`), copied to
`/tmp/claude-1000/pf4k/fix/idle/harness` so nothing is written into the repo; `syscalls.py`,
`states.py` and `statx.sh` beside it are new and quoted below.

### Idle: CPU, wakeups, syscalls (`idle-wakeups.txt`, `syscalls-idle.txt`)

`idle.py <tag> <panes> 1 60` and `syscalls.py <tag> <bin> <panes> 20`
(`perf stat` on the syscall tracepoints, nobody touching anything):

| 60 s idle | before | after |
|---|---|---|
| 1 pane, CPU | 0.48 % | **0.33 %** |
| 1 pane, wakeups/s | 20.2 | 20.2 |
| 4 panes, CPU | 1.35 % | **0.68 %** |
| 4 panes, wakeups/s | 47.3 | **33.7** |
| marginal, per extra pane | 9.0 wk/s | **4.5 wk/s** |

| syscalls/s at idle | before (1 pane) | after (1 pane) | before (4) | after (4) |
|---|---|---|---|---|
| `statx` | 145.1 | **57.5** | 467.6 | **230.2** |
| `openat` | 37.0 | **24.5** | 148.1 | **98.1** |
| `getdents64` | 35.0 | **10.0** | 140.2 | **40.0** |
| `faccessat` | 36.0 | **1.0** | 91.5 | **4.0** |
| all six counted | 325.1 | **162.5** | 1109.8 | **647.4** |

The before column reproduces the profile's bpftrace counts exactly (145 `statx`, 37 `openat`,
35 `getdents64` per idle pane), which is what says the two measurements are of the same thing.

### Per key press (`syscalls-per-key.txt`)

The engine profiler's method: `perf stat` on the tracepoints over a 4 s idle window, then over a
4 s window in which xdotool types 30 characters; the difference over 30 is the per-key cost.
`statx.sh` types a **warm-up** 30 characters first and throws that window away: the shell
highlighter scans `PATH` once, the first time a word is typed in Terminal mode
(`pathCommands()`, `src/ShellHighlighter.cpp`), and that one scan is ~8 600 `access()` calls —
it would swamp a per-key figure taken in the same window. The second half of the file is the
first, un-warmed run, kept because it shows that contamination; the commit message's per-key
figures are from it and are superseded by these.

| per key press (press **and** release) | before | after |
|---|---|---|
| `statx` | 417.5 | **2.5** |
| `faccessat` | 170.2 | **2.2** |
| `openat` | 11.0 | **0.0** |

417.5 and 170.2 are the profile's own figures (417 + 170) to the decimal, from a different
harness — so this is the same cost, now gone. `/etc/default/keyboard` is no longer opened at all
while typing.

### The window's own state, 4 idle panes (`window-states.txt`)

`states.py <bin> 15` — focused, then another application in front, then unmapped (what minimising
does), then remapped and refocused with **no settling time at all**:

| 4 panes | before | after |
|---|---|---|
| mapped, in front | 34.2 wk/s · 1.20 % | 21.5 wk/s · 0.47 % |
| mapped, another app in front | 35.0 wk/s · 1.33 % | **7.3 wk/s · 0.27 %** |
| unmapped (minimised) | 31.1 wk/s · 0.87 % | **7.3 wk/s · 0.20 %** |
| first 2 s after being restored | — | **24.5 wk/s · 0.50 %** |
| restored, settled | 36.9 wk/s · 1.33 % | 21.4 wk/s · 0.47 % |

Before, the window's state changed nothing at all — which is the finding. After, minimised is a
third of visible here and a sixth in a run at higher load (`window-states.txt`, first block:
45.3 → 7.3), and the first two seconds back are already at the full rate: the re-tune is on the
state-change event, not on the next 400 ms tick.

The harness runs under a bare X server with no window manager, which never focuses anything on its
own; `Pane::applicationIsInFront()` therefore treats "the platform has never once reported this
application active" as "in front", so a session where nobody can be told who is in front is never
slowed. That is why `idle.py`'s runs above (which focus the window) show the fast rate.

## Not fixed here

- The one-off `PATH` scan above (~8 600 `access()` calls at the first word typed in Terminal mode)
  is a single scan for the life of the process, cached in a function static. It is a hitch of a
  few milliseconds once, not a per-key cost, so it is left alone.
- `relay::log::write()` calls `directory()` — `QDir::exists` + `mkpath` + `setPermissions` — for
  every line it actually writes. That is on the writing path, not the level check, and it belongs
  to whoever takes the logging card next.
