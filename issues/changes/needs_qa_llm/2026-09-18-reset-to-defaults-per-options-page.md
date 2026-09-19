---
id: OPRS
type: work
status: needs-qa-llm
labels: [feature, ux]
component: [gui]
milestone: beta
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18: "also add a reset to defaults button on options pages"'
links: {plans: [], commits: [], evidence: [2026-09-18-reset-to-defaults], related: [P2WD], github: null}
---
# Every Options page can be put back to how Relay ships

## Issue

> also add a reset to defaults button on options pages

## Behaviour as implemented

The last row of each Options page is **Reset to defaults**, with a Reset… button and a detail line
that counts what it covers: "Puts the 8 options on this page back to what Relay ships with, at once.
No other page changes, and your API keys, saved servers and custom shortcuts are left alone." It is
an ordinary row, so it takes focus, Enter presses it, and the pane's search finds it (factory,
restore, revert, original are aliases).

It asks first — a dialog named after the page ("Reset the General options to what Relay ships
with?" / "Only this page changes. It cannot be undone."), with Cancel as both the default and the
escape button, because Enter is what opened it. On confirm the page's rows go back, a notice says
how many changed, and every open Options pane redraws.

A page resets *itself*: `relay::resetRow(section, …)` is built from that section's own rows and can
reach nothing else. A row declares its default by carrying a `reset` on `relay::SettingRow`; the
row helpers in the settings catalog set it automatically, so a row written tomorrow is covered the
day it is written, and `choiceRow` now requires its default to be named. A row with no single
default — a button, a saved local model server, an Info line — carries none and is left alone, and a
page where nothing carries one gets no button at all (Local models).

Resetting a value **removes** its key rather than writing the shipped value back, which is what a
fresh install looks like, and then re-runs whatever the row does live, so a theme, the log level,
the keymap preset, desktop notifications, shortcut hints, pane colours and single-click opening
change on screen rather than at the next start. The voice key is the one derived default: its reset
clears `voice/hold_key` so the keyboard layout decides it again.

Pages with a reset row: General (8), Appearance (2), Models (3), Terminal (7), Agent (10), Voice
(5), Privacy (3), Keyboard (2). Shortcut-hint *counts* are not touched — that is already Actions ›
Reset shortcut hints — and neither are keys, saved servers or keybindings.json.

## Why

Options are one-way today: a setting changed months ago is a value you have to remember, and several
of them (theme, log level, keymap preset) are applied live by their own writers rather than read
from QSettings at the next start, so "delete the key" is not a recovery a person can do by hand.

## Checks

- [ ] Every Options page except Local models ends with "Reset to defaults"; the count in the detail
      line matches the options above it.
- [ ] The row takes focus with ↑/↓ and Enter opens the dialog; Esc and Enter both cancel it.
- [ ] Cancel changes nothing and says nothing.
- [ ] Confirm on General: the toggles and Log detail go back, the pane redraws at once, and a notice
      names the page and the count.
- [ ] Options changed on Appearance, Terminal and Agent survive a reset of General.
- [ ] Reset Appearance with a non-default theme: the app restyles immediately, not at the next start.
- [ ] Voice: reset with a hold key chosen by hand, and the key goes back to the one the layout
      implies rather than to a fixed value.
- [ ] API keys, saved local model servers and keybindings.json are untouched by any of it.
- [ ] `ctest -R settingspane` passes; the whole `ctest` passes.
