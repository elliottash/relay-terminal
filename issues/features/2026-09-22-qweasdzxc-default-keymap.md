---
id: QWAS
type: work
status: discussing
labels: [feature, keyboard]
waiting_on: owner
rank: m
created: '2026-09-22'
source: 'Owner in a Relay pane, 2026-09-22; keyboard-system discussion continued from #ACDG, #KYPR and #SPSG'
links: {plans: [], commits: [], evidence: [], related: [SPSG, MAGP, CPRQ, KYPR, ACDG, DKEW, D60R, RBVK], github: null}
---
# Adopt the QWEASDZXC default keymap: A Board, S Sessions & Projects, D explorer, P Actions

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
- With the Relay preset, Ctrl+Shift+A opens, focuses or closes the Board; Ctrl+Shift+S the Sessions & Projects pane; Ctrl+Shift+D the explorer; Ctrl+Shift+P the Actions palette; Ctrl+Shift+Q clears the prompt box; Ctrl+Shift+Z restores the last closed. Failure shows as a chord doing something else or nothing, or as a row in Options › Keyboard's conflict list.
- Ctrl+A, Ctrl+S, Ctrl+Z, Ctrl+X and Ctrl+C in an editor, and Ctrl+D, Ctrl+P and Ctrl+Q in a terminal program, reach the editor or the program unchanged.
- No Relay command differs between Ctrl+key and Ctrl+Shift+key, H included once the decision on this card is in.
- Restart after a stop and stop-all-subagents stay reachable without keys: banner, subagents UI, palette.
- All four presets pass the conflict check, and every printed hint and both docs show the new keys.

## Tests
Planned: `tests/test_keybindings.py` (defaults, the four presets, no duplicate chords, and the Ctrl / Ctrl+Shift pairing rule as a test), `ctest -R panetabnavigation`, `ctest -R filepanes`, `ctest -R settingspane`, and an isolated GUI check with composer focus, file-editor focus and a full-screen program in focus.
