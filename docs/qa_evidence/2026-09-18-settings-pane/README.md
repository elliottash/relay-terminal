# Settings as a full pane (card SP4N) — implementer evidence

Implementer: Claude Fable 5.1 (Claude Code session), 2026-09-18. These are implementer
screenshots, not a QA verdict. `drive.sh` reproduces them under Xvfb with an isolated profile and
the loopback stub provider (no key, no network).

| File | What it shows |
|---|---|
| `implementer-a-general.png` | Ctrl+Shift+A from the prompt box: the Settings pane opens beside the terminal (the tab reads "project; Settings · 2"), search focused, General tab with its Diagnostics heading and the new rows (Show tool output, Desktop notifications, Log detail, Log folder) |
| `implementer-b-search.png` | "think" typed: one list, best match first — the Show thinking toggle (highlighted, Enter flips it), the Reasoning effort › entries from the Actions catalog, the stall timeout spin box, actions with their key caps (Switchboard, Fast agent) — each row naming its section |
| `implementer-c-actions.png` | ← from General wraps to the Actions tab: the shortcut settings rows, then AGENT with the Model and Input mode submenus opened inline under engraved headers, keys as caps, ✓ on the current choice |
| `implementer-d-agent.png` | The Agent tab: Instructions and skills / Turn limits headings, combo, buttons, text fields, spin boxes (Stop a silent model after is new here) and the audit toggle |
| `implementer-e-closed.png` | Esc: the pane is gone, the terminal has its full width back and the typed line landed in the prompt box, so focus returned where it was |
| `implementer-f-light.png` | The same pane in Relay Light |

Checked by hand from the shots: every row of the old dialog is present; the search reaches submenu
entries; toggles, combos, spin boxes and buttons render as real controls; the footer names the keys.
`tests/settingspane_test.cpp` (12 tests) and the full ctest suite pass. The `relay-stderr-*.log` files
are empty: no Qt warnings while the pane was built, searched, switched and closed.

Not covered here (needs a desktop or a longer script): vim in native input keeping focus across
open/close (checklist item 3), the Theme combo popup (item 5), Ctrl+? naming the pressed key (item 7).
