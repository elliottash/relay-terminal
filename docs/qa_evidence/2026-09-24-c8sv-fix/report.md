# C8SV: the Keep-link SIGSEGV, reproduced and fixed

## Cause

`RelayWindow::refreshVisibleGlobals()` (called from `Pane::handleMemorySuggestionEvent` through
`onMemorySuggestionDecided` after every Keep or No) found the Globals panes with
`top->findChildren<relay::globals::GlobalsPane *>()`. `GlobalsPane` has no `Q_OBJECT`, so under
Qt 5 that call matches on `QWidget`'s meta-object and returns **every** widget in every window
cast to `GlobalsPane *`. For each visible one it called `refresh()`, which calls `onRequest`: a
`std::function` read from whatever lay at that offset in an unrelated widget. The call jumps to
data. The live crash (relay.log, 2026-09-23 16:44:18.932 UTC) matches: SIGSEGV `code=2`
(SEGV_ACCERR) with the PC `0xb1f51ab28a00` in the heap, 1 ms after `globals_suggestion_accepted`
on terminal pane `2e7406f6`. It needed no Globals pane on screen at all.

The earlier probe (`docs/qa_evidence/2026-09-23-c8sv/`) passed because it called
`globals.refresh()` directly instead of the window's `findChildren` loop.

## Reproduction (before the fix)

An AddressSanitizer build of `relay-consolemode-tests` in `/tmp/c8sv-asan` (checkout at
`4e4b7783` plus the working tree), with the probe case in [`live_probe.patch`](live_probe.patch).
The probe case does the following:

- seeds a real pending suggestion with `memory_suggestions.suggest` in a scratch HOME;
- opens a **terminal** pane (real bash, real Python worker) and waits for its prompt;
- prints the suggestion line;
- finds the `relay://memory/<pane>/keep/<id>` cell on screen and Ctrl+clicks it with real mouse
  events;
- lets the worker's own `globals_suggestion_accepted` reply arrive over its stdout;
- refreshes Globals from `onMemorySuggestionDecided` with the same loop the window used.

Variant 0 (no Globals pane shown) crashed: `probe-before-variant0.log`. gdb's backtrace:

```
#0  QListData::shared_null () from libQt5Core.so.5        <- PC in data: SIGILL here, SIGSEGV live
#1  std::function<void(QJsonObject const&)>::operator() (this=0x5040000338c0)
#2  relay::globals::GlobalsPane::request (this=0x504000033890)  GlobalsPane.cpp:176
#3  relay::globals::GlobalsPane::refresh (this=0x504000033890)  GlobalsPane.cpp:243
#4  (onMemorySuggestionDecided's loop)
#9  Pane::handleMemorySuggestionEvent  Pane.h:9640
#10 Pane::handle                       PaneEvents.cpp:66
#11 Pane::connectWorker()::<lambda>    PaneRuntime.cpp:502  (worker stdout)
```

`this=0x504000033890` is a small heap object, not the probe's `GlobalsPane`, which was never shown.
Variant 1 (Globals visible) crashed the same way.

## Fix

`GlobalsPane::refreshVisible()` (`src/GlobalsPane.cpp`) walks `findChildren<QWidget *>()` and
tests each widget with `dynamic_cast<GlobalsPane *>`. `applySingleClickSetting()` and
`chromeOf()` already avoid the same trap this way. `refreshVisibleGlobals()` now calls it. An audit
of every `findChild`/`findChildren`/`qobject_cast` on a non-`Q_OBJECT` class in `src/` and
`engine/` found no other live use; the three other matches are comments recording the same trap.

## After the fix

The same ASan probe, calling `GlobalsPane::refreshVisible()`, exited 0 with no ASan report in all
four variants:

| variant | case | result |
|---|---|---|
| 0 | Keep, Globals closed | `✦ Kept: … · Globals › User memory`; no Globals request |
| 1 | Keep, Globals visible | Kept line; exactly `globals_suggestions` + `globals_list` sent by that pane |
| 2 | pane closed and deleted right after the click, before the reply | no crash |
| 3 | No, Globals visible | `✦ Rejected — won't be suggested again: …`; the same two requests |

Regression test: `tests/globalspane_test.cpp`
`refreshVisibleCallsOnlyTheGlobalsPanesOnScreen` puts a label, a line edit and two `GlobalsPane`s
(one hidden) in a shown window, then calls `refreshVisible()`. With the old loop it failed with
`Received signal 11`. With the fix, all 13 globalspane cases pass.
