# Implementer evidence: #P7SJ and #Y2MP

Implemented with three subagents (Projects, Globals UI, HQ backend) and parent window integration.
Integration: `be42269896bc3e0beecff69b28da487e40cfee63`; movement followup: `f24b12a728eed750c8bf7031fb7655f162bf29da`.

## Build and targeted tests

- `scripts/relay-build --target relay relay-projectspane-tests relay-globalspane-tests relay-conversations-tests` passed.
- `ctest --test-dir build -R '^(conversations|projectspane|globalspane)$' --output-on-failure`: all three passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_keybindings.py`: 25 passed.
- Backend agent's final targeted runs: `test_globals_protocol.py` 10, `test_memories.py` 6,
  `test_aliases.py` 34, `test_system_prompt.py` 18, `test_remote_wire.py` 48,
  `test_agent_context.py` 23. Earlier prompt profile tests: 17 passed.
- `land.py` built the exact proposed integration tree's `relay` target before advancing main.

## Live GUI

`python3 docs/qa_evidence/2026-09-21-projects-sessions-globals/drive.py`
ran the built application under Xvfb at 1400×1000 with isolated HOME, XDG directories and TMPDIR.
The final run used the exact-tree gate binary for `f24b12a7`.
No real user's projects or global knowledge were changed.

Observed and captured:

1. Ctrl+Shift+P opens Projects, showing the fixture project and a No project group with the live terminal.
2. Ctrl+Shift+Y selects Sessions in the same pane, keeping its existing filters/search.
3. Ctrl+Shift+G selects Globals in that pane and lists real instruction source locations.
4. New memory → edit Markdown → Save creates one global memory file and selects the saved record.
5. Append an unsaved draft, switch to Projects and back to Globals: the draft remains in the editor,
   is marked unsaved and is absent from the file on disk.
6. The runtime memory loader reads the actual GUI-created memory and includes its text in context.
7. Close the original terminal; Projects and Globals still open by shortcut, and Globals saves the draft.

The final driver prints: `PASS: memory persisted and loaded into runtime context; draft survived tab switches; ownerless Globals saved.`
An initial ad-hoc driver used xdotool multiline typing, which drops Return in this Qt editor;
validation correctly refused malformed front matter. The checked-in driver sends Return explicitly,
and the recorded final run passes.

## Coverage and limits

Widget tests cover explicit project actions, stable selection, No project filtering, declined-project
undo, Globals create/save/retire, source editing, draft preservation, stale replies and save errors.
Protocol tests cover optimistic conflicts, case-normalized duplicate names, alias editor interoperability,
retirement and scope overrides. Runtime tests cover bounded loading and scoped supersession.
The helper context follows the selected tab; no paid/provider-backed agent turn was used in GUI QA.
Global instructions continue using their existing files and loading settings. Team memories remain inactive.
The earlier HQ suggestion of routing loose work cards to a global inbox was not adopted.

## Tracker verification

Backend tests were also recorded through `TestsCommands.run_and_wait`: 181 pass, zero fail/skip,
no opened signals. Targeted Qt tracker run `20260921T222701Z-fca3`: 3 pass, zero fail/skip.
Both cards' `tests_check` now has no failure findings. Remaining discovery warnings call conditional
skip decorators “skipped-forever” for remote/alias tests although those actual recorded runs passed;
Conversations has a 2.91-second slow-test notice. The board format checker reports unrelated existing
errors and two pre-existing section-heading warnings on #Y2MP; #P7SJ's task markers were corrected.

The manager movement followup resolves its current window/tab for project actions and global worker
requests. With no terminal left, Projects/Globals remain usable; Sessions restores a terminal
resume destination when needed, including mouse entry into that tab.

## Ctrl+Shift+S followup

Commit `188b5931c9f7f60d3f96487c7cebfcd9a279ae28` removes the legacy picker host.
Ctrl+Shift+S opens Switchboard directly, using its existing project or the current directory.
The helper receives the uninitialized board state, so the empty view fully loads.
Project selection and pending `/card` text use the shared Projects tab.

`switchboard-direct.py`, run against land.py's exact-tree binary, passed in isolated Xvfb.
Screenshot `07-switchboard-direct.png` shows Switchboard's “No cards yet” state in a loose
Downloads folder. No board/git files were created, and the worker log has no protocol error.
An earlier live check caught the missing uninitialized-state block; it was fixed before this final run.
11 targeted `test_board_protocol.InitTests` passed, recorded in runs
`20260921T233453Z-1582` and `20260921T233455Z-6953`. The exact-tree application build passed.

## Sessions loading and Recently closed followup

Commit `56d671cf5a914099f0a3d28c424e396e46ba681c` refreshes the existing query on Sessions tab
activation and moves Recently closed behind a button inside Sessions, with a Back to sessions button.
The regression was reproduced before the fix: open Projects, click Sessions with the mouse, and
the pane had no rows or result count even though a saved session fixture existed.
The earlier key-driven empty-profile check did not exercise this mouse path.

Targeted Conversations tests pass, including mouse activation with preserved query/filters and
nested Recently closed navigation. Tracker run `20260921T234801Z-943e` passed with no opened signals.
A clean export including the commit built successfully with scripts/relay-build. The first shared
checkout build was interrupted by another session's in-progress ModelPicker header/source changes;
those files were not modified by this task.

Live verification with `sessions-loading.py` against the clean application build passed.
`08-sessions-loaded.png` shows one saved session and its populated preview immediately after a mouse
click from Projects to Sessions, without an intervening query change or keyboard refresh.
`09-closed-button.png` shows the nested Recently closed page with Back to sessions and exactly
three top-level tabs. The fixture remains in the index after navigation.
