---
id: X55K
type: work
status: needs-verification
labels: [feature, shortcuts]
assignee: agent
implemented_by: glm/glm-5.3
session: 266dc3f5-dd39-459d-8e92-ffd53d17ff8a
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [person], human: none, criteria: relay-keymap-tests green (incl. altHomeEndScrollActsInsidePrograms) and the engine suite green on the landed commit; person presses Alt+Home / Alt+End in a console with scrollback., sign_off: none, effort: low}
source: pane 1, 2026-09-26
links: {plans: [], commits: [3a7cb28ff5a5], evidence: [docs/qa_evidence/2026-09-25-x55k-alt-home-end-scroll/], related: [], github: null}
---
# Alt+Home / Alt+End scroll the console to the top / bottom of its scrollback

## Issue
Alt+Home and Alt+End should scroll a pane's console scrollback to the very top / very bottom, as remappable shortcuts.

> make alt+home and alt+end as shortcuts that make the console content scroll back the very top or the very bottom. make sense?
> — elliott · [session:a5a62f4021a74a7c89025844b1a6540c](relay://session/a5a62f4021a74a7c89025844b1a6540c) · 2026-09-25

## Done means
- Alt+Home scrolls the focused pane's console to the very top of its scrollback; Alt+End scrolls back to the newest output.
- Both are real Keymap actions (`terminal.scrollTop` / `terminal.scrollBottom`): visible and remappable in Settings › Shortcuts, terminal group.
- They work from the composer, from the terminal, and while a program runs (viewer keys: the engine's scrollback moves, nothing is sent to the program). Plain Alt+arrows remain the program's (#VD2M), and `program_keys: none` still hands these keys to the program.

## Tests
- `tests/keymap_test.cpp` — new `altHomeEndScrollActsInsidePrograms()`: Alt+Home/Alt+End resolve to the two new actions, act inside programs under `shift-only`, and fall to the program under `none`. `ctest --test-dir build -R '^keymap$'` passed; full `relay-keymap-tests` binary green.
- `./build/engine/relay-engine-tests` — 77 passed, 0 failed (the view `scrollToTop`/`scrollToBottom` primitives the new backend method forwards to).
- `RELAY_SESSION=scrlhome scripts/relay-build` — `[100%] Built target relay`.
- Evidence: docs/qa_evidence/2026-09-25-x55k-alt-home-end-scroll/evidence.md

## Execution Summary
- `src/Keymap.h`: `terminal.scrollTop` (Alt+Home) and `terminal.scrollBottom` (Alt+End) actions; both allowed to act inside programs with a comment distinguishing them from #VD2M's plain Alt+arrows.
- `src/RelayWindowCore.cpp`: dispatch in `runActionNow` → `Pane::scrollTerminalExtreme(top)`; toasts "This engine cannot scroll." if the engine lacks scroll control.
- `src/Pane.h`: public `scrollTerminalExtreme(bool top)` beside `scrollTerminalPage`, gated on `ScrollControl`.
- `engine/TerminalBackend.h` + `engine/backend/VTermBackend.{h,cpp}`: new `scrollToTop()` primitive next to `scrollToBottom()`, forwarding to the view.
- Landed via scripts/land.py as 3a7cb28ff5a5: `src/Pane.h` was contested by four live sessions, so only this card's hunk was selected (`--only-hunk`, re-derived per attempt against the moving tree); the other sessions' 18 Pane.h hunks stayed uncommitted in the working tree.
