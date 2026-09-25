---
id: S976
type: work
status: planned
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: zzs9
created: '2026-09-17'
acceptance: typing into a running program from the prompt box works, with completion for at least one program's commands, and Terminal mode never starts an agent turn on a typo
source: '`issues/feature_intake.txt`, 2026-09-17: "in terminal mode, is it better to be able to type non-commands and they will just go through? ... it would be great if you could type commands and they will just go into the claude command box. or should we have an alternative input mode for that? i guess that would be best, so you could for example have autocomplete for claude / codex commands."'
verify: {artifact: code, primary: script, also: [ai-visual], human: required, criteria: 'with python3, psql and sqlite3 at their prompts in a Relay pane, typing in the prompt box in Program mode reaches the program (no Take control), Tab offers that program''s completions, the chip reads PROGRAM; a typo in Terminal mode starts no agent turn', sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A program input mode: type into the running program, with its own completions

Owner: in terminal mode a non-command should not start an agent turn; and when a program like Claude Code or
Codex owns the terminal, typing in Relay's prompt box should go into that program's input, ideally with
completion for that program's own commands.

Proposal to work through:
- A fourth destination, **program**, offered automatically while a program is reading input (the take-over
  banner gains "type into it from here"), and selectable from the mode chip.
- In program mode the line goes to the program's stdin, with no routing and no agent turn; history is the
  program's, not the shell's.
- Completion: per-program tables (claude, codex, gh, psql, python) for their slash commands and arguments,
  starting with the ones Relay can detect.
- In Terminal mode proper, a line that is not a runnable command should offer a fix ("did you mean …?")
  rather than starting an agent turn, per the owner's preference.

## Done means
- With a line-editing program at its prompt in a pane (`python3`, `psql`, `sqlite3`, `node`), a line typed in the prompt box in Program mode reaches that program's input without Take control, is not kept in shell history, and never starts an agent turn.
- Tab in Program mode completes that program's own commands for at least `psql`/`sqlite3` backslash/dot commands, and the chip reads PROGRAM (naming the program) while it applies.
- In Terminal mode a line that is not a runnable command starts no agent turn: it prints a ✗ line with "did you mean …" when a close command exists, and keeps the text.
- Failure looks like: the line queues until the program exits, a "Take control" prompt appears, or a `⟳ … asking the agent to fix it` line prints for a typo.

## Plan
**Goal.** Finish what is left of this request after GT7X and #33G0: a generic **Program** destination for the composer that types into any line-editing program the pane is running (not only claude/codex, and not only canonical-mode `[Y/n]` prompts), with per-program completion, and stop Terminal mode from turning a typo into an agent turn.

**Findings** (re-checked 2026-09-25 at `8163b4ed`; `src/main.cpp` split, so the card's original references are gone).
- *Already done, not to be redone:*
  - **Claude Code / Codex** — the owner's headline case. Since GT7X (`d9075774` "Route the composer through guest CLIs") the prompt box *is* the guest's input line: `Pane::dispatch` (`src/PaneRuntime.cpp:1186`) sends every routed line through `Pane::submitGuest` (`src/Pane.h:12992`) → `typeIntoGuest` (`src/Pane.h:12965`). Their slash commands are completed from a scanned catalog: `publishGuestSlashCatalog` (`src/Pane.h:807`, backend `backend/relay_core/guest_slash.py`, `tests/test_guest_slash.py`), received in `handleGuestEvent` `slash` (`src/Pane.h:3151`) and merged into the `/` popup (`src/PaneRuntime.cpp:505-515`). Protocol `docs/AGENT-SESSIONS-PROTOCOL.md` §26.8.
  - **Canonical-mode prompts** (`apt`'s `[Y/n]`, `read -p`, `input()`): `relay::input::targetFor` returns `LineTarget::Program` (`src/InputPolicy.cpp:63-69`), written by `Pane::sendLineToProgram` (`src/Pane.h:10331`), since `952fe042`.
  - **Terminal mode + a clear request** already runs nothing (#94V5 wrong-mode hints, `src/PaneRuntime.cpp:1210-1220`).
  - **Python/IPython/Stata classifier** exists in the worker: `backend/relay_core/lang_router.py` (`detect_repl` :644, `classify_line`, destination `program`), called from `workspace_plugins.route` (:681) via `backend/worker.py:263` (protocol 36), `tests/test_lang_router.py` — card **#33G0** (executing), tasks `t:v0`/`t:xj` still open.
- *Still missing:*
  - A **raw-mode line editor** (python3/psql/sqlite3/node REPLs all use readline/libedit, so `TerminalMode::Raw`, not alt-screen) fails `lineRequested` (`src/InputPolicy.cpp:17-21`), so `targetFor` says `Shell`; `submitTerminal` (`src/Pane.h:12939`) then queues the line behind the running program (`shellIdleForQueue`, `src/Pane.h:12957`). The only way in is Take control (`src/RelayWindow.h:2104`, action `control.human`).
  - The GUI's `route` request (`src/PaneRuntime.cpp:1153`) sends no `foreground_program`, and `Pane::dispatch` has no `route == "program"` branch, so #33G0's worker-side `program` verdict can never arrive or be acted on.
  - The mode set is three-way only: `toggleInputMode` (`src/Pane.h:2615-2633`, auto → shell → agent), `setMode` (`src/Pane.h:931`), `m_modeValue` validated at `src/Pane.h:1262` and `defaultInputMode` (:2610), Keymap actions `input.modeAuto/modeTerminal/modeAgent/toggle` (`src/Keymap.h:470-473`), highlighter `InputHighlighter::Destination {Auto, Shell, Agent}` (`src/ShellHighlighter.h:15`), colour in `applyDestinationColor`/`refreshDestinationColor` (`src/Pane.h:11050`, `:11077`).
  - Completion is shell-only: `relay::completeAt` (`src/Completion.h`, lib `relay-completion`, test `completion` / `tests/completion_test.cpp`) knows paths and command names; there is no per-program table.
  - **Terminal mode + a typo** (invalid, no request signal) still starts an agent turn: `src/PaneRuntime.cpp:1224` `startFix(text, …, 1)` → `Pane::startFix` (`src/Pane.h:12016`) prints `⟳ … asking the agent to fix it`. `relay::slash::closest` (`src/SlashCommands.h`) already has the edit-distance ranking a "did you mean" needs.

**Steps.**
1. *Policy (pure, `relay-input`).* In `src/InputPolicy.{h,cpp}` add `bool lineEditorWaiting(const State&)` — program running, not alt-screen, `TerminalMode::Raw`, and `programReading` — and make `targetFor(state, "program")` return `LineTarget::Program` whenever a program is running and not alt-screen; in `auto`/`shell` keep today's rule unless the owner answers Q2 "automatic". Add the cases to `tests/inputpolicy_test.cpp`. *(independent)*
2. *Delivery.* Generalise `Pane::sendLineToProgram` (`src/Pane.h:10331`) to raw-mode programs: Ctrl+U-free, bracketed paste when the program advertised it, a multi-line draft allowed only when the program is a REPL (paste then one `\r`), `retainable()` still false; status "Sent to psql". Reuse `typeIntoGuest`'s paste path rather than a second writer. *(after 1)*
3. *The fourth mode.* Accept `"program"` in `setMode`/`m_modeValue` validation (`src/Pane.h:1262`, `:931`), add `input.modeProgram` to `src/Keymap.h` (unbound by default) and to `tests/keymap_test.cpp`; `toggleInputMode` includes `program` only while `lineEditorWaiting`/`lineRequested` (Q1); add `Destination::Program` to `src/ShellHighlighter.h` with its own ink; chip text `PROGRAM · psql`. When the program exits, the pane drops back to the mode it was in before and toasts it. Never persisted as `input/default`. *(after 1; parallel with 4)*
4. *Offer it.* Where the pane currently offers Take control for a running program (`src/RelayWindow.h:2104`, the busy action in `src/Pane.h:16149-16160`), add "Type into it from here" (→ `setMode("program")`) for non-full-screen programs; the "waiting for input" hint (#TP3W) names the same action. *(parallel with 3)*
5. *#33G0 hook.* Add `foreground_program` (from `/proc/<tpgid>/cmdline`, as #W011 does for `ask`) to the `route` request at `src/PaneRuntime.cpp:1153`, and a `route == "program"` branch in `Pane::dispatch` that calls step 2's writer, and `incomplete` that keeps the draft. This is the GUI half of #33G0 `t:v0`/`t:xj`: agree ownership first (Q5). *(after 2)*
6. *Completion tables.* New pure unit `src/ProgramCompletion.{h,cpp}` (library `relay-programcompletion`, QtCore only, added to `CMakeLists.txt` beside `relay-completion`): `completeProgram(program, line, cursor)` over static tables for `psql` (`\d`, `\dt`, `\x`, `\q`, …), `sqlite3` (`.tables`, `.schema`, `.mode`, …), `python`/`ipython` (keywords, builtins, `%` magics), `node` (`.help`, `.exit`, …); claude/codex keep the guest catalog. Wire Tab in Program mode to it instead of `completeAt`. Test `tests/programcompletion_test.cpp`, `add_test(NAME programcompletion …)`. *(independent; parallel with 1-4)*
7. *Terminal-mode typos.* At `src/PaneRuntime.cpp:1224`, replace `startFix` for an invalid Terminal-mode line with a ✗ line: the router's `invalid_reason` plus `did you mean <closest known command>` (`relay::slash::closest` over `m_knownCommands`), text kept in the box, no agent turn. The fix loop for a command that *ran and failed* (`src/PaneRuntime.cpp:2220`, `src/Pane.h:15217`) is untouched unless the owner says otherwise (Q4). A pure helper for the message goes in `InputPolicy` with a test. *(independent)*
8. Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` (the route request's new field; a `program` input mode), the Keymap reference, and the input-mode wording in help (`src/Pane.h:9979` "switch terminal / agent").

**Risks / owner questions.**
1. *How is Program mode entered and left?* Recommendation: offered automatically — a banner/busy-action "Type into psql from here" and Ctrl+I cycling includes PROGRAM only while a program is at its prompt; left automatically when the program exits. Alternative: a manual fourth chip state always in the cycle.
2. *Should Auto send to a REPL by itself?* Recommendation: only where a language router can tell code from prose (Python/Stata via #33G0); for psql/sqlite3/node, Auto keeps today's behaviour and only explicit Program mode types into them — a prose line must never execute in a database shell.
3. *Which programs get completion first?* Recommendation: psql and sqlite3 (small, stable command sets), then python/ipython keywords and magics; gh has no interactive prompt worth completing. Tables are static, not scraped from the running program.
4. *Terminal-mode typo:* card says "did you mean", owner quote asked whether non-commands should "just go through". Recommendation: ✗ + "did you mean", nothing runs, no agent turn; Ctrl+Shift+Enter does not force it into the shell. Keep the existing fix loop for commands that ran and failed?
5. *Ownership with #33G0:* recommendation — this card owns the GUI Program destination (steps 1-5); #33G0 `t:xj` then only adds block/interrupt handling on top of it. Needs agreement with that card's session before step 5 so both don't wire `dispatch`.
6. Security: an explicit Program-mode line is typed verbatim into a program that may be a DB shell or a remote host's REPL; history and logs never keep it (`retainable`), and a masked prompt still goes through the existing Secret path only.

**Verify.**
- `scripts/relay-build --target relay-input-tests && ctest --test-dir build -R '^input$'` (new `lineEditorWaiting` / Program-mode `targetFor` cases).
- `scripts/relay-build --target relay-programcompletion-tests && ctest --test-dir build -R programcompletion`.
- `ctest --test-dir build -R 'keymap|completion|slashcommands'`; `PYTHONPATH=backend python3 -m unittest tests.test_lang_router tests.test_router`.
- `scripts/relay-build --check "PROGRAM ·"` then in the app: start `python3`, `psql` (or `sqlite3 :memory:`) and `node` in a pane; press Ctrl+I to PROGRAM, type `print(1)` / `\dt` / `.tables`, Enter — output appears, no Take control, nothing in shell history; Tab on `\d` lists psql commands. Exit the program: chip returns to the previous mode. Terminal mode, `gti status` Enter: ✗ line "did you mean git", no `⟳ asking the agent` line, text kept. Screenshots to `docs/qa_evidence/<date>-program-input-mode/`.
