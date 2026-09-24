---
id: C8SV
type: work
status: needs-verification
labels: [bug, gui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: c2417ccd-583d-416a-9839-667cbac6a24e
rank: m
created: '2026-09-23'
source: 'Crash log investigation during #N6R8, 2026-09-23'
links: {plans: [], commits: [5fd243681de4ca76129971c53aeda26587203209, a4cd23f5], evidence: [docs/qa_evidence/2026-09-23-c8sv/report.md, docs/qa_evidence/2026-09-24-c8sv-fix/report.md], related: [N6R8, ADNM], github: null}
---
# SIGSEGV immediately after a Globals suggestion acceptance event

## Issue
The live Relay GUI, build 2026-09-23.11H.02, logged `gui_crash signal=11` at 2026-09-23 16:44:18.932 UTC, PID 637025. The preceding event at 16:44:18.931 UTC was `globals_suggestion_accepted` on pane `2e7406f6`. The three crash frames are the signal handler, `__kernel_rt_sigreturn`, and an unmapped program counter (`0xb1f51ab28a00`); they do not identify the failing application function. Investigate the crash with a reproducible acceptance sequence or a debug run. The build that crashed has since been replaced, and there is no core file.

The owner identified the action immediately before the crash: "the crash happened when i clicked a link to 'keep' a memory". The link is the transcript's `Keep` link, which sends `globals_suggestion_accept` from `Pane::decideMemorySuggestion`. The logged accepted reply narrows the failure to GUI processing after the backend saved the memory. `Pane::handleMemorySuggestionEvent` writes the outcome line and refreshes any visible Globals panes; the available stack does not identify which operation failed.

## Tests
- `ctest:globalspane` — `refreshVisibleCallsOnlyTheGlobalsPanesOnScreen` (new): a shown window with a label, a line edit, a visible and a hidden `GlobalsPane`; `refreshVisible()` must send exactly two requests from the visible pane and none from the hidden one. With the old `findChildren<GlobalsPane *>` loop it failed with `Received signal 11`; with the fix, 13/13 pass. land.py ran it on the exact committed tree of `a4cd23f5`.
- `manual: docs/qa_evidence/2026-09-24-c8sv-fix/report.md` — ASan probe (`live_probe.patch`): a real terminal pane with a real bash and Python worker, a real pending suggestion, and a Ctrl+click on the on-screen Keep/No link. The worker's own reply arrives over stdout. Before the fix, variant 0 crashed with the gdb stack `GlobalsPane::refresh` → `request` → `onRequest` → PC in data. After the fix, variants 0 (Keep, Globals closed), 1 (Keep, Globals visible), 2 (pane closed before the reply) and 3 (No, Globals visible) all exit 0 with no ASan report and show the Kept/Rejected line.
- `tests/test_globals_protocol.py` — backend acceptance; unchanged by this fix.
- `ctest:consolemode` — unchanged code path (`Pane` not edited); passed after #VZ8C.

## Planning notes
Priority: high. The 2026-09-23 16:44:18.932 UTC SIGSEGV followed `globals_suggestion_accepted` by about 1 ms on live build 2026-09-23.11H.02. The owner confirms clicking the transcript Keep link. Backend acceptance and `globalspane` unit flows pass, but they do not exercise the live transcript click, pane callback, and visible Globals refresh together. The stack is not diagnostic; the temporal link is evidence to reproduce, not proof of the cause. The later worker exit with code 15 was on another pane and should not be joined to this crash.

## Done means
Clicking Keep or No on a transcript memory suggestion completes once without crashing, with the expected saved/rejected state and visible outcome in the pane. The same flow remains safe with Globals open, after the originating pane closes, and when a reply arrives during a UI refresh. A reproducible failing case identifies the actual fault before a code change is accepted; an isolated GUI regression and relevant unit tests pass on the fixed revision.

## Plan
**Goal:** reproduce and remove the crash on transcript memory acceptance.

**Findings:** The click reaches `Pane::decideMemorySuggestion` (`src/Pane.h`); the accepted reply reaches `Pane::handleMemorySuggestionEvent`, which writes a transcript outcome and calls `onMemorySuggestionDecided` to refresh Globals. `tests/consolemode_test.cpp` covers a synthetic accepted event; `tests/globalspane_test.cpp` covers the Globals pane separately. Neither proves the combined live flow.

**Steps:**
1. Reproduce in an isolated profile with a real transcript Keep link and both closed/open Globals panes. Record exact build and a symbolized backtrace using `scripts/relay-debug` or a sanitizer build; also test pane closure before the reply.
2. Trace the callback and object lifetimes across `printInline`, Globals refresh, and `closeInline`. Fix only the demonstrated invalid access or reentrancy path.
3. Add a focused GUI regression that clicks the transcript link and delivers the real reply shape. Keep backend acceptance coverage; avoid a test that merely calls the handler directly.

**Risks:** The 2026-09-23 stack lacks an application frame and the current checkout contains concurrent GUI edits. Do not attribute the crash to a specific callback until the reproducer or symbolized stack does.

**Verify:** Targeted memory/Globals tests, focused `consolemode` memory case and `globalspane` CTest, then the isolated live Keep/No flow with a visible Globals pane on the fixed build. Use `scripts/relay-build` per `docs/BUILDING.md` if a build is needed.

## Execution Summary
**Cause (reproduced):** `RelayWindow::refreshVisibleGlobals()`, run by `onMemorySuggestionDecided` after every Keep/No, used `findChildren<relay::globals::GlobalsPane *>()`. `GlobalsPane` has no `Q_OBJECT`, so Qt 5 matched every `QWidget` in every window. For each visible one it called `refresh()` → `onRequest`, a `std::function` read from an unrelated object, and jumped into data. That matches the live `SEGV_ACCERR` with the PC in the heap 1 ms after `globals_suggestion_accepted`. It needs no Globals pane open. The earlier probe missed it because it called `globals.refresh()` directly.

**Fix (a4cd23f5):** `GlobalsPane::refreshVisible()` tests each child widget with `dynamic_cast`, the pattern `applySingleClickSetting()`/`chromeOf()` already use for non-`Q_OBJECT` classes. `refreshVisibleGlobals()` calls it. An audit of every `findChild`/`findChildren`/`qobject_cast` on a non-`Q_OBJECT` class in `src/` and `engine/` found no other live use.

**Not covered:** Relay was not relaunched with this build for a hand click. The probe drives the real `Pane`, `TerminalView` click, worker and callback, but not a full `RelayWindow`.
