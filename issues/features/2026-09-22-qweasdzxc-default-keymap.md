---
id: QWAS
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 83f552c6-372f-4f64-9b01-2beb4e493f9c
rank: m
created: '2026-09-22'
source: 'Owner in a Relay pane, 2026-09-22; keyboard-system discussion continued from #ACDG, #KYPR and #SPSG'
links: {plans: [], commits: [e914d65c, cd7dcfcb, 3c66dd01, cbdd1166, c00a2859, 3e37c528, 878b0ca452ea7a4c4caa876da1c8d58c39ac8006, 5df774b491e452798b998e27c7ac75e2f4403e90, 134186308f20d143d070b086a40236ee7ab82b13], evidence: [docs/qa_evidence/2026-09-22-keyboard-set/, docs/qa_evidence/2026-09-23-sessions-projects-keys/], related: [SPSG, MAGP, CPRQ, KYPR, ACDG, DKEW, D60R, RBVK], github: null}
---
# Adopt the QWEASDZXC default keymap: Board, Sessions, Projects, explorer, Actions/help

## Issue
we could also put the board on A and sessions on S? the most accessible keys are 1 2 3 q w e a s d z x c. lets think if that helps the decision. i am thinking that the board 
could really be a central piece of relay, so ctrl shift a would be worth it. i want to hear your view though. 

i dont want a ctrl and ctrl shift to have different funcs

nobody has used relay yet so we can think fresh.

i think ctrl shift 1 2 3 is actually not that easy to press actually, similar to ctrl shift v f r. so qweasdzxc are really the sweet spot for accessibility

what if ctrl shift d is the file explorer. ctrl z is undo text edit; ctrl shift z is undo close. x and c reserved for cut and copy. 

ready to put this on cards?

## Decisions
Owner: "i dont want a ctrl and ctrl shift to have different funcs".
Owner: "nobody has used relay yet so we can think fresh."
Owner: "qweasdzxc are really the sweet spot for accessibility".
Owner: "ctrl z is undo text edit; ctrl shift z is undo close. x and c reserved for cut and copy."
Owner: "ctrl shift g can still be globals". Ctrl+Shift+G opens Globals; plain Ctrl+G remains unbound.
Owner: "ctrl shift p on projects" and "ctrl shift s on sessions". Owner: "i like ctrl ? for actions / help , thats enough." The 2026-09-23 revision gives Projects P, Sessions S and Actions/help Ctrl+?.

## Planning notes
The proposed Relay preset. The other three presets get an explicit row for every action whose Relay default moves.

| Key | Ctrl+Shift+key (Relay's layer; acts inside programs too) | Ctrl+key |
|---|---|---|
| Q | Clear the prompt box, undoable (#CPRQ) | the same in the prompt box; reaches the program when a terminal program has the keyboard |
| W | Close pane, then tab, then window (unchanged) | the same (unchanged) |
| E | New pane (unchanged) | the same (unchanged) |
| A | Board (was Ctrl+Shift+S) | select all: the editor's |
| S | Sessions & Projects: one pane for sessions, projects, recently closed and globals (#SPSG; was Y, P and G) | save: the file editor's |
| D | File explorer, open or close (was Ctrl+B and Ctrl+Shift+B) | never bound: end-of-input in every shell |
| Z | Restore the last closed pane, tab or window (unchanged) | undo: the editor's |
| X | reserved for cut; `agent.stopAllSubagents` loses its default key | cut |
| C | reserved for copy (the terminal's copy) | copy, and interrupt in the terminal |
| P | Actions palette (#MAGP; was Projects) | never bound: previous history in readline and vim |

Freed: Ctrl+Shift+Y, G, B, R and X, and Ctrl+B. `pane.restartShell` (was Ctrl+Shift+R) keeps the stopped-shell banner in `src/Pane.h` (`showBanner` with "Restart shell") and its palette row; `agent.stopAllSubagents` keeps the subagents UI and its palette row. Ctrl+? (`help.shortcuts`) opens the palette; F1 is dropped per #KYPR.

Unchanged: Ctrl+N and Ctrl+T for windows and tabs, Ctrl+Shift+O and Ctrl+, for Options, Ctrl+Shift+M Models, Ctrl+Shift+K Tasks, Ctrl+Shift+1 notifications, Ctrl+Shift+L links, Ctrl+Shift+J delegate, Ctrl+F find, Ctrl+I input toggle, the Alt letters and Alt arrows for panes and models, F12 native input.

The modifier rule, as the owner's line on Z settles it: Ctrl+Shift+key is Relay's. Plain Ctrl+key is bound to the same Relay command only where the key has no editing or terminal meaning (W, E, N, T, and Q inside the prompt box); where it has one (A, S, Z, X, C, D, P) Relay leaves it to the editor or the program. No Relay command ever differs between Ctrl+key and Ctrl+Shift+key. The one letter pair that differs today is H: Ctrl+H takes control of the terminal and Ctrl+Shift+H gives it back (`control.human`, `control.prompt`); the proposal makes both one toggle. Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+Enter / Ctrl+Shift+Enter are conventional direction pairs and stay.

Defaults live in `src/Keymap.h` (the `Keymap()` constructor; presets in `presetJson()`). The Warp and VS Code preset tables put `palette.open` on Ctrl+Shift+A, which collides with `board.open` there once the Relay default moves, so each preset table needs its own `board.open` row. Hints that print keys: the help rows and idle tips in `src/Pane.h`, banner text in `src/RelayWindow.h`, `docs/ARCHITECTURE.md`, `docs/KEYBINDING-PRESETS.md`.

Sequence: #SPSG (one `sessions.open` action) and #CPRQ first, #MAGP beside them; then this card flips the defaults in one commit so the map never sits half-moved.

## Done means
- In the Relay preset, Ctrl+Shift+A opens Board; Ctrl+Shift+S selects Sessions; Ctrl+Shift+P selects Projects; Ctrl+Shift+G selects Globals; Ctrl+Shift+D opens explorer; Ctrl+? opens Actions/help; Ctrl+Shift+Q clears the prompt box; Ctrl+Shift+Z restores closed.
- Plain Ctrl+A/S/Z/X/C/D/G/P keep their editing or terminal meaning. No Relay action differs between Ctrl+letter and Ctrl+Shift+letter.
- All four built-in presets have no shortcut conflict, and the printed hints and keybinding docs reflect the keys.

## Tests
`scripts/relay-build --target relay`
`QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(conversations|keymap)$' --output-on-failure`
`PYTHONPATH=backend python3 -m unittest tests.test_keybindings tests.test_action_catalog`
manual: `docs/qa_evidence/2026-09-22-keyboard-set/`
manual: `docs/qa_evidence/2026-09-23-sessions-projects-keys/`

## Execution Summary
Relay preset now: Ctrl+Shift+A Board, S Sessions & Projects, D explorer, P Actions palette, Q clear prompt (plain Ctrl+Q in the prompt box), Z restore closed; Ctrl+H and Ctrl+Shift+H are one take-control toggle; restart and stop-all-subagents keyless (banner, subagents UI, palette); F1 unbound; Ctrl+Shift+Y G B R X and Ctrl+B freed. Warp, VS Code and Konsole presets conflict-free (docs/KEYBINDING-PRESETS.md). Commits: e914d65c keymap and presets, cd7dcfcb window dispatch and docs/ARCHITECTURE.md, 3c66dd01 and cbdd1166 comments, c00a2859 relay-keymap-tests with the pairing-rule test. Evidence: docs/qa_evidence/2026-09-22-keyboard-set/.
Owner restored Ctrl+Shift+G as a direct Globals shortcut; plain Ctrl+G remains unbound. Verified Relay-keymap and worker tests pass. Commit `3e37c528`; `src/Keymap.h`, `tests/keymap_test.cpp`, `tests/test_keybindings.py`, `docs/KEYBINDING-PRESETS.md`, and `docs/ARCHITECTURE.md`.
2026-09-23 revision: Ctrl+Shift+P restores Projects, Ctrl+Shift+S selects Sessions, Ctrl+? alone opens Actions/help; Ctrl+Shift+G remains Globals. Updated all built-in presets, keyboard hints, keymap assertions and docs. Commits `878b0ca4`, `5df774b4`, `13418630`. The Xvfb capture of the revised Sessions layout is in `docs/qa_evidence/2026-09-23-sessions-projects-keys/`.
