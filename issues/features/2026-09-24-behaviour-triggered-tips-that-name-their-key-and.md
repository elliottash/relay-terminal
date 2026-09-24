---
id: X3SM
type: work
status: planned
labels: [feature, gui, hints, onboarding]
component: [gui, worker]
milestone: beta
rank: zzzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe, person], human: optional, criteria: 'each trigger in the table fires its tip once in a scripted drive, no tip appears while a turn runs or within two minutes of a recipe''s first Enter, a tip stops after its key is used, and ''rm *.log'' prints the files it would remove before running', sign_off: none, effort: medium, stakes: nuisance, blast: capability}
source: 'Claude Fable session in Relay, 2026-09-24, delivery card 3 of 3 from #9HS0'
links: {plans: [], commits: [], evidence: [], related: [9HS0, K2FV, 6VMF, CWVF], github: null}
---
# Behaviour-triggered tips that name their key and retire on use, and a preview of what a destructive wildcard hits

## Issue
all recs approved, go ahead and file the three delivery cards linked to #9HS0.

## Done means
- Each row of the trigger table in `reports/Beginner UX and onboarding for Relay.md` §3.5 is a hint id: a failed shell line ("command not found") tips Ctrl+Shift+Enter; a request routed to the shell by mistake tips `!`; the same command three times tips ↑ and →; the agent's first file edit tips Esc Esc and /rewind-code; the agent's first `rm`, `mv` or `git push` under allow-everything tips Options › Security, once ever, as information; `cd` into a folder holding `.board/` or `TODO.md` tips the Board key; `ssh` tips that the agent keeps working on the host; a long paste tips `@`; the Relay Free quota at 20% and the fifth free turn each tip once.
- Every tip is one sentence naming a key or a command; it shows at most three times with the existing cooldowns; it never appears while a turn is running, within two minutes of a recipe's first Enter, or after five behaviour-triggered tips in one session; it is retired the first time the key or command it names is used (`ShortcutHints` gains a use count beside its show count).
- Esc on three tips in a row, or the hints toggle off, stops all of them; the Options row "Reset shortcut hints" brings them back.
- Before the shell runs a line that pairs a wildcard with `rm`, `mv`, `chmod -R` or `chown -R`, the pane prints the expansion under the box ("rm would remove 14 files: a.log, b.log, …"), then runs it. No confirmation dialog; the preview is the safety.
- Failure looks like: a tip during a running turn; a tip that keeps showing after its key was used; two tips inside the 20-second gap; the wildcard preview missing on `rm *.log` or appearing on `ls *.log`.

## Plan
### Goal
Tips fire on what the person just did, name their key, and retire once the key is used; a destructive wildcard shows what it will hit before it runs. Design: #9HS0, `reports/Beginner UX and onboarding for Relay.md` §3.5 and §3.7; decisions 11 and 12 on #9HS0 apply (cap of five per session, two quiet minutes after a recipe's first Enter, the Security tip once ever as information).

### Findings
- `relay::ShortcutHints` (`src/Hints.h`, `src/Hints.cpp`): `mayShow(id, limit=3, cooldown=600)`, `recordShown`, `shouldShow`, `shownCount`, `resetAll`, `nextTime`, `nextIdleTip` (limit 3, cooldown 1800), `kGlobalGapSeconds = 20`; counts persist under `hints/`.
- Idle tips: `Pane::showIdleTip` (`src/Pane.h:10182-10210`), toasts via `enqueueToast`; hint call sites: `Pane::hint` / `RelayWindow::hint` (`rg "hint(QStringLiteral" src/`), the registry `docs/ARCHITECTURE.md` "Shortcut hints".
- Triggers already observable: shell exit and "command not found" (#EB4A), the prefix chips, history recall, tool events for `write_file`/`edit_file`/`run_command` (the approvals classifier `backend/relay_core/approvals.py` names `delete_or_move` and `network`), `cd` and the project probe (`backend/relay_core/project_probe.py`), the router's `ssh` handling (#S5SH), the paste handler, `hosted_quota` (protocol 13.9).
- Wildcard expansion: the router sees the line before the shell does (`docs/ARCHITECTURE.md` §5); `security.segments()` and `_programs` (#2Y96) already split a line into programs and arguments.

### Steps
1. `ShortcutHints`: add `recordUsed(id)` and a use count; `mayShow` returns false once used; a per-session counter with a cap of five for behaviour-triggered ids; a quiet-until timestamp set by the Start pane's first Enter (two minutes). Unit-test in a new ctest case beside the existing hints tests (or add one: `tests/hints_test.cpp`).
2. Wire the triggers from the table as hint ids, each at the place the event is already seen (above), each with the key from `Keymap::instance().shortcutText`. The Security tip (`tip.security.ask`) has limit 1. Register every id in the registry `docs/ARCHITECTURE.md` keeps.
3. Retire-on-use: the action handlers for the named keys (`agent.fixCommand`, prefix `!`, history accept, `pane.splitRight`, `/rewind-code`, `board.open`, `@` attach) call `recordUsed`.
4. Wildcard preview: in the router's shell path, when a segment's program is `rm`, `mv`, `chmod -R` or `chown -R` and an argument holds `*`, `?` or `[`, expand it against the pane's cwd (worker side, `glob`) and emit a `preview` line the pane prints under the box ("rm would remove 14 files: …", capped at ten names) before the command runs. No dialog. Test in `tests/test_router.py` (or the file the router's tests live in).
5. Docs: the hint table in `docs/ARCHITECTURE.md`; README key table unchanged.

### Risks
- Tips are toasts and share the queue with notices; the cap and the quiet period keep the first task uninterrupted, and a tip must never pre-empt an approval card (#K2FV).
- The expansion runs on the worker with the pane's cwd; a slow filesystem must not delay Enter: cap the glob at 200 entries and skip the preview past that with "… and more".

### Verify
- `ctest --test-dir build -R hints`
- `PYTHONPATH=backend python3 -m unittest tests.test_router -v` (the preview cases)
- A scripted Xvfb drive that produces each trigger once and screenshots the toast, into `docs/qa_evidence/<date>-targeted-tips/`.
