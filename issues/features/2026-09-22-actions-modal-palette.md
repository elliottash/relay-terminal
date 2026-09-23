---
id: MAGP
type: work
status: needs-verification
labels: [feature, keyboard, actions]
implemented_by: openai/gpt-6-sol via codex
rank: n
created: '2026-09-22'
source: Owner in a Relay pane, 2026-09-22; keyboard-system discussion
links: {plans: [], commits: [7ff14f76, f040b696, cd7dcfcb, c00a2859, 3857b5fc, 878b0ca452ea7a4c4caa876da1c8d58c39ac8006, 5df774b491e452798b998e27c7ac75e2f4403e90, 134186308f20d143d070b086a40236ee7ab82b13], evidence: [docs/qa_evidence/2026-09-22-keyboard-set/, docs/qa_evidence/2026-09-23-sessions-projects-keys/], related: [QWAS, ACDG, A9QR, A7SC, S3JH, XAME, ANX9, KYPR], github: null}
---
# Actions modal palette: search, groups, and Ctrl+? help

## Issue
what about actions? i think we need something like ctrl+shift+p for warp. what about ctrl+shift+1 or ctrl+shift+r? i think it should open a magic action modal with text search and some buttons that show dropdowns that give you quick intuitive access to everything.

you can give that a first proposal and let me know if you need input

## Planning notes
First proposal, 2026-09-22.

**Key.** Ctrl+Shift+P. It is the palette chord in VS Code and Sublime and the one Warp users reach for; it is a two-handed chord (left hand on Ctrl+Shift, right index finger on P), so it has none of the one-handed stretch of Ctrl+Shift+1 or Ctrl+Shift+R; and P is free once Projects joins Sessions (#SPSG). Plain Ctrl+P is never bound (previous history in readline and vim). Ctrl+? (`help.shortcuts`) opens the same palette; F1 is dropped (#KYPR). The chord again, Esc or a click outside closes it. If P is refused: Ctrl+Shift+R, free once restart loses its key (#QWAS), or Ctrl+Shift+1, which would move notifications.

**What it is.** A modal, not a pane: centred over the window, about 640 px wide, gone the moment something is chosen. The Actions pane (`SettingsPane` in actions mode, opened by `toggleSettingsPane` in `src/RelayWindow.h`) stops being the destination; its list survives as the shortcut reference under Options › Keyboard. The panes-not-overlays rule is for surfaces that stay open; a palette is transient.

**Layout, top to bottom.**
1. One search box, placeholder "Search actions, options and slash commands…". Typing filters at once with `relayFuzzyScore()` across every registered action, every Options section (#S3JH) and every slash command (#A7SC); the `/name` spelling matches too.
2. A row of group buttons, each a dropdown of that group's commands with its live shortcut on the right: Agent · Panes & tabs · Sessions & Projects · Board · Files · Models · Options · Remote · Help. The groups are #A9QR's task groups, not the `Keymap` categories. Tab moves from the search box into the row, Left and Right walk it, Enter or Down opens the dropdown.
3. The result list. With an empty query: **Recent** (the last 12 distinct choices already stored beside `src/RelayWindow.h:2134`), then **For this pane** (what applies to the focused pane's state: restart when the shell stopped, stop when the agent is running, take control when a program has the keyboard, back to the prompt otherwise), then **Browse all**, which is the button row. Each row shows name, the shortcut as currently bound, `/slash` when it has one, and the target when it is ambiguous (which pane).
4. Enter runs the highlighted row and closes; Ctrl+Enter runs it and keeps the palette open; Up and Down move; right-click on a row opens Options › Keyboard at that action to change its binding.

**Everything means the registries, not a hand list.** `rootItems()` in `src/RelayWindow.h` assembles the catalog by hand and misses registered actions (#ACDG). The palette reads `Keymap::instance()` for actions and keys, `settingsSections()` for Options and `Pane::slashCommands()` for slash commands, so a new action appears without editing three lists. Contextual restrictions and equivalents are represented once, as #ACDG asks. Cards and saved sessions are not in this version: they are one key away (Ctrl+Shift+A, Ctrl+Shift+S) and have their own search.

**Reopen.** #XAME's finding, a menu that would not reopen after close, applies: the modal opens cleanly every time.

## Done means
- Ctrl+? opens the modal Actions/help palette with the search box focused; Esc, a click outside, or the same key closes it. Ctrl+Shift+P opens Projects.
- Search finds registered actions, Options and slash commands; group buttons show live shortcuts and choosing an item runs it.
- The empty palette shows Recent and For this pane; Change shortcut remains available on a result row.
- The built-in presets have no Ctrl+Shift+P or other separate shortcut for `palette.open`, with no key conflicts.

## Plan
**Goal.** Replace the Actions pane as the command finder with a modal palette that reads the registries.

**Findings.** `palette.open` routes to `toggleSettingsPane(true)` at `src/RelayWindow.h:1300`; the catalog is `rootItems()` (`src/RelayWindow.h:4291`) and `searchableActions()` (`src/RelayWindow.h:1647`), consumed by `relay::SettingsPane` (`src/SettingsPane.cpp`); the recent-choices store is beside `src/RelayWindow.h:2134`; hints that print the key are in `src/Pane.h` (help rows near line 10682, idle tip near 12689) and `src/RelayWindow.h:4144`.

**Steps.**
1. Land #ACDG first, so the catalog comes from the registries (actions, options sections, slash commands) with contextual rules represented once.
2. `src/ActionPalette.{h,cpp}`: a QDialog-based modal (search box, group button row with QMenu dropdowns, result list) that takes the catalog and the recent store; its own library target so `tests/` can drive it headless; added to the `relay` target in `CMakeLists.txt`.
3. Route `palette.open` and `help.shortcuts` to it; keep the Actions list reachable under Options › Keyboard.
4. Rebind `palette.open` to Ctrl+Shift+P in the Relay preset and put the preset tables' palette rows in order (#QWAS carries the full map).
5. Update every printed hint and `docs/ARCHITECTURE.md`.

**Risks.** The owner's rule "panes, not overlays": this card asks for a modal explicitly; if the owner prefers a pane, step 2 becomes a splitter pane and the rest stands. Group membership is a judgement; reuse #A9QR's groups. Focus return after close on X11 and Wayland.

**Verify.** `ctest -R settingspane`, a new `ctest -R actionpalette` (catalog completeness, fuzzy search, group menus, recent list, contextual rows), `tests/test_keybindings.py`, and an isolated GUI drive: open, type, choose, reopen, with composer focus and with a full-screen program focused.

## Tests
`QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^actionpalette$' --output-on-failure`
`QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(conversations|keymap)$' --output-on-failure`
`PYTHONPATH=backend python3 -m unittest tests.test_keybindings tests.test_action_catalog`
manual: `docs/qa_evidence/2026-09-22-keyboard-set/` (original palette UI)
manual: `docs/qa_evidence/2026-09-23-sessions-projects-keys/` (revised navigation)

## Execution Summary
New relay::ActionPalette (src/ActionPalette.{h,cpp}, own library): a modal overlay with the search box, a row of group buttons with dropdowns showing live shortcuts, and a result list that starts with Recent then For this pane. Ctrl+Shift+P and Ctrl+? open it; Esc, the chord, a click elsewhere or focus moving on closes it. Right-click or the Menu key on a row offers Change shortcut…, which opens a key-capture dialog that writes keybindings.json (Keymap::setBinding) and moves a key off its old action. The old Actions list stays as the Shortcut list row. Deviation from the plan: the reference list is reached from the palette's Shortcut list row rather than from Options › Keyboard. Commits: 7ff14f76, f040b696, cd7dcfcb, c00a2859, 3857b5fc (live-drive fixes). Evidence: docs/qa_evidence/2026-09-22-keyboard-set/.
2026-09-23 revision: Ctrl+? is the Actions/help key; Ctrl+Shift+P now selects Projects. `palette.open` remains registered without a default key. All three alternate Ctrl+? spellings remain for Qt keyboard layouts. Action hints now use `help.shortcuts`. Commits `878b0ca4`, `5df774b4`, `13418630`.

## Decisions
Owner: "i like ctrl ? for actions / help , thats enough." Ctrl+Shift+P belongs to Projects; the palette remains available through Ctrl+? and its keyboard-layout spellings.
