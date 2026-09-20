---
id: 057J
type: work
status: done
labels: [bug, performance]
assignee: claude-code
rank: m3
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [e73e7bae], evidence: [docs/qa_evidence/2026-09-20-perf-fixes/idle/, docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K], github: null}
---
# An idle pane wakes 13 times a second; QSettings is constructed on hot paths

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Detail: [docs/qa_evidence/2026-09-20-perf-profile/startup/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/startup/FINDINGS.md) finding 1, [docs/qa_evidence/2026-09-20-perf-profile/engine/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/engine/FINDINGS.md) finding 5, [docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md) finding 4.

Idle, one pane: 0.53 % CPU / 20.9 wakeups/s (spark), 0.48 % / 18.5 (sphinxpad Qt5), 0.52 % / 20.7 (Qt6). Four panes: 59.8 / 47.0 / 48.0 wakeups/s. About 470 syscalls/s per idle pane (145 `statx`, 37 `openat`, 35 `getdents64`). **Minimising changes nothing**: 44.0 → 44.8 wakeups/s on the laptop.

1. `Pane::pollGuestEvents()` (`src/Pane.h:1781`, from `pollShell()` every 80 ms) lists a directory that is empty unless a guest agent runs: 23.9 % of sampled idle CPU. `QDir::exists` + `entryList` 13.3 µs against a `stat` at 0.45 µs.
2. A `QSettings` is constructed (7–8 config-path stats each) in: `metersEnabled()` (`src/PaneUsage.cpp:457`; 75 statx/s for one boolean, called from `src/RelayWindow.h:7255` and per pane at `src/Pane.h:702`); `voiceHoldKey()` / `voiceEnabled()` in the app event filter (`src/Pane.h:3485-3489`) and `updateGhost()` (`:497`) — **417 statx + 170 faccessat, ~1.2 ms, on every key press and release**, including an open of `/etc/default/keyboard`; `Pane::showToolOutput()` (`src/Pane.h:4407`) and `relay::log::level()` (`src/Logging.cpp:76`) — 9.6 % of GUI cycles in a tool-heavy turn.
3. `tunePoll()` (`src/Pane.h:14893`) tests only `isVisible()`, which stays true when the window is iconified.
4. `RichEditor`'s caret blink (`src/RichEditor.cpp:208-222`) has no focus handlers and never stops.
5. `relay::usage::walkTrees` walks `/proc` for every pane in every tab at 2.5 Hz (`src/RelayWindow.h:7263-7275`): 3.7 % of idle samples.

## Plan
1. Mtime-gate `pollGuestEvents()` exactly as `pollShell()` already gates `state.json`.
2. Cache each of those settings reads and invalidate on the existing settings-change notification; pass the bool already computed at `RelayWindow.h:7255` down to the panes.
3. Add `window()->isMinimized()` / application-inactive to `tunePoll()`: the existing 80 → 400 ms slowdown then applies (~50 → ~14 wakeups/s minimised).
4. Stop the caret timer on focus-out, as `engine/view/TerminalView.cpp:2713` does.
5. Walk only the visible tab's panes.
Re-measure with `startup/harness/` on sphinxpad, on battery if possible.

## Done
All five landed in `e73e7bae`; evidence and the full tables in
[docs/qa_evidence/2026-09-20-perf-fixes/idle/](../../../docs/qa_evidence/2026-09-20-perf-fixes/idle/README.md).

1. `pollGuestEvents()` lists the spool only when its directory's mtime or inode moved
   (`relay::runtimedirs::DirStamp`, the gate `pollShell()` keeps on `state.json`).
2. `relay::settings` (`src/SettingsCache.h`) is the one way to read a setting on a hot path: one
   `QSettings` per key, dropped by `SettingsWatch::notify()`, which every control in Options
   already calls after it writes. `metersEnabled()`, `relay::log::level()`, `showToolOutput()`,
   `voiceEnabled()`, `voiceHoldKey()` (with the `/etc/default/keyboard` read behind the same
   generation) and `updateGhost()`'s history suggestions go through it.
3. `tunePoll()` also asks `window()->isMinimized()` and `QGuiApplication::applicationState()`, and
   is re-run from `QEvent::WindowStateChange` and `applicationStateChanged` rather than waiting for
   the next quiet tick.
4. `RichEditor` starts its caret blink in `focusInEvent` and stops it in `focusOutEvent` /
   `hideEvent`, as `engine/view/TerminalView.cpp` does.
5. `refreshPaneStatus()` walks `/proc` for the tab in front on every poll and for the tabs behind
   it on the tab label's own 5 s clock, which is the only thing that reads those numbers.

Measured on spark, Xvfb, fresh profile, against the clean export of `ccb31a8e`:

| | before | after |
|---|---|---|
| 1 idle pane, CPU | 0.48 % | 0.33 % |
| 1 idle pane, syscalls/s | 325 (145 `statx`, 35 `getdents64`, 36 `faccessat`) | 162 (58, 10, 1) |
| 4 idle panes, CPU / wakeups | 1.35 % / 47.3 wk/s | 0.68 % / 33.7 wk/s |
| per key press | 417.5 `statx` + 170.2 `faccessat` | 2.5 + 2.2 |
| 4 panes minimised | 31.1 wk/s / 0.87 % (no change from mapped) | 7.3 wk/s / 0.20 % |

Tests: `settingscache` (new — a value read 1 000 times builds one `QSettings`),
`settings::aSettingChangedInOptionsIsInEffectAtOnce` (a control in Options is in effect on the next
read), `runtimedirs::aDirectoryStampReportsOnlyRealChanges`,
`editor::theCaretBlinksOnlyForTheFocusedBox`, and the existing `logging`, `paneusage`, `voice`,
`panes`, `panestatus`, `titles`, `guestbridge` and `appcommands` suites.
