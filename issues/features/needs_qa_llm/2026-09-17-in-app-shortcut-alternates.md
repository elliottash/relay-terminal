---
id: 5FY5
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzy
created: '2026-09-17'
acceptance: every core action has a Ctrl+Shift alternate that still works while a full-screen program owns the keyboard; the shortcuts overlay lists both
source: '`issues/feature_intake.txt`, 2026-09-17: "for important keyboard shortcutes, eg ctrl+t, ctrl+p, ctrl+n, make ctrl+shift+[ ] do the same thing, so that it will work when you are in an app. give me a list of those you would propose."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+Shift alternates for the core shortcuts

## Why

A terminal cannot transmit Ctrl+Shift+letter to a program, so Relay can always keep those keys, even while a
program owns the keyboard (native mode / take control). Plain Ctrl+letter keys must be given to the program:
vim uses Ctrl+W, Ctrl+P, Ctrl+N and Ctrl+T, and Ctrl+I *is* Tab, Ctrl+H *is* Backspace.

## Proposed pairs

| Action | Today | Proposed alternate |
|---|---|---|
| New window | Ctrl+N | Ctrl+Shift+N |
| New tab | Ctrl+T | Ctrl+Shift+T |
| New pane | Ctrl+P | Ctrl+Shift+P (freed by card #78BN, which drops the separate "split down" key) |
| Close pane / tab / window | Ctrl+W | Ctrl+Shift+W |
| Restore last closed | Ctrl+Shift+W today | **moves to Ctrl+Shift+Z** |
| Toggle terminal / agent input | Ctrl+I (= Tab in a terminal) | Ctrl+Shift+I |
| Find in view | Ctrl+F | Ctrl+Shift+F |
| Focus pane left/right/up/down | Alt+arrows | Ctrl+Shift+arrows |
| Take control / back to the prompt | Ctrl+H / Ctrl+Shift+H | already a pair; Ctrl+Shift+H works inside programs |
| Conversations, Tasks, Palette, Stop subagents | Ctrl+Shift+O / K / A / X | already Ctrl+Shift |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab | unchanged: programs never see Ctrl+Tab |
| Move pane | Ctrl+Alt+arrows | unchanged |

Both keys appear in the Ctrl+? overlay, and presets (Warp, VS Code, Konsole) keep their own bindings.

## Open questions
1. Confirm restore-last-closed moving from Ctrl+Shift+W to Ctrl+Shift+Z.
2. Should the alternates be active always (recommended) or only while a program owns the keyboard?

## Partial fix already shipped (2026-09-17)

Owner report: "i am running a claude terminal in relay, and it seems like i cant change over to the other
pane". Inside a program only F-keys and Ctrl+Shift+… reached Relay, and pane focus is Alt+arrows, so a
full-screen TUI trapped the keyboard in its pane. `Keymap::actsInsidePrograms` now also keeps **Alt+arrows**,
so pane focus works while a program runs. The `program_keys` setting (`all` / `none`) still overrides.

Note for the alternates above: Ctrl+Shift+Left/Right also mean "select the previous/next word" in the prompt
box, so binding them to pane focus needs the owner's call (open question 2).

## Decisions and first implementation (owner, 2026-09-17)

- Pane focus stays on **Alt+arrows** (they already reach Relay inside full-screen programs); Ctrl+Shift+arrows
  keep their word-selection meaning in the prompt box.
- Restore last closed moves to **Ctrl+Shift+Z**; close takes **Ctrl+Shift+W** as its twin.
- The first time anything is closed in a run, a one-off hint names the restore key.

Landed in the Relay preset: `window.new` Ctrl+N / Ctrl+Shift+N · `tab.new` Ctrl+T / Ctrl+Shift+T ·
`pane.close` Ctrl+W / Ctrl+Shift+W · `closed.restore` Ctrl+Shift+Z · `input.toggle` Ctrl+I / Ctrl+Shift+I ·
`find.inView` Ctrl+F / Ctrl+Shift+F. Still to do: the new-pane key pair, which waits on card #78BN
(one key plus an arrow) so Ctrl+Shift+P is free.
