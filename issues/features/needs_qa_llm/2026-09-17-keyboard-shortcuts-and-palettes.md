# Configurable shortcuts, program pass-through, palettes, and per-pane controls

- **Status**: needs-qa-llm
- **Component**: gui, agent
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (Claude Code session) and a backend subagent, 2026-09-17
- **Source**: `issues/feature_intake.txt` items: Ctrl+C/X/V, "make changing hotkeys easy and allow the agent to change the hotkeys", Ctrl+Shift+A and Ctrl+Shift+T palettes, Esc to close sidebars, move mode and model selectors beneath the terminal, remove the F12 labels, replace Interrupt shell with a cancel icon

## Behavior as implemented

- **Registry:** every window shortcut is a named action; overrides in `~/.config/RelayTerminal/relay/keybindings.json` reload live; conflicts go to the status bar.
- **Agent:** the `set_keybinding` tool rewrites one binding in that file.
- **Programs:** while vim, nano or less owns the focused terminal, only Ctrl+Shift shortcuts and F-keys act (`program_keys: shift-only`). Provisional default; see open decisions.
- **Clipboard in the terminal:** Ctrl+C copies a selection or interrupts; Ctrl+V pastes at a prompt and passes through inside programs; Ctrl+X passes through.
- **Palettes (superseded by the update below):** Ctrl+Shift+A agent options and Ctrl+Shift+T terminal options float over the right edge, filter as you type, show shortcuts, mark current values, and return focus where it was.
- **Per-pane row:** input mode picker, model picker and an interrupt icon; toolbar has Agent options, Terminal options, New chat, Stop agent, Provider.
- **F12:** still toggles native input, but its labels are removed.

## Implementer check (not a QA verdict)

2026-09-17 under Xvfb, isolated `XDG_CONFIG_HOME`:
- Ctrl+W then `v` inside vim split vim, not the Relay pane.
- Ctrl+Shift+A opened over vim without resizing it; Esc returned focus to vim and `ihello` + `:wq` saved `w.txt` containing `hello`.
- Filtering "deep" found the DeepSeek model; "split ri" + Enter split the pane.
- Triple-click selection + Ctrl+C put `copy-me-token-123` on the clipboard; Ctrl+C without a selection interrupted `sleep 30` (exit 130).
- Kimi K3 called `set_keybinding`; the file became `{"bindings":{"agent.newChat":["Ctrl+Shift+N"]},"version":1}`.

Evidence: `docs/qa_evidence/2026-09-17-keyboard-and-palettes/`. Backend: `tests/test_keybindings.py`, 104 tests passing.

## QA checklist

1. Edit a binding in `keybindings.json`; the new key works without restarting.
2. A duplicate binding shows a conflict message.
3. Inside nano: Ctrl+W searches, Ctrl+O saves; Ctrl+Shift+T still opens the palette.
4. Ctrl+V pastes at a prompt; inside vim it starts visual block mode.
5. Palettes: arrow keys, Ctrl+N/P, Enter, Esc clears then closes; focus returns to the previous widget.
6. Ask the agent to rebind something; the change applies live.

## Open decisions (owner deferred to end-of-work discussion)

1. Ctrl+I: toggle input mode, or move focus between composer and terminal.
2. `program_keys` default: `shift-only`, `all` or `none`.
3. Palette research (`docs/PALETTE-RESEARCH.md`): one palette engine with two entry points vs one global palette; Ctrl+Shift+T muscle memory (Konsole new tab, browser reopen tab); submenus for model and mode; an approval or autonomy toggle; "set model as default for new panes".

## Update (2026-09-17): one palette and shortcut presets

Owner decisions: keep a single Ctrl+Shift+A actions palette (Ctrl+Shift+T is now unbound) and
add Warp, VS Code and Konsole shortcut presets.

- Palette: Recent, then Agent / Terminal / Panes and tabs / Shortcuts, ordered by focus; submenus
  for Model, Input mode, Shortcut preset and Shortcuts inside programs; search reaches submenus.
- Presets from `docs/KEYBINDING-PRESETS.md` (sources cited there); overrides stay on top.
- The agent's `set_keybinding` tool now accepts all ASCII symbols except `+`, and Esc/Del/Ins aliases.

Implementer check under Xvfb: sections and shortcuts render; "deep" finds Model › DeepSeek;
choosing VS Code wrote `"preset":"vscode"`; Ctrl+\ split and Ctrl+Shift+P opened the palette;
switching the file to `konsole` made Ctrl+Shift+( split. Evidence:
`docs/qa_evidence/2026-09-17-keyboard-and-palettes/implementer-palette-and-presets.png`.

Additional QA checks: each preset's pane, tab and window keys on a real desktop; Warp's
Ctrl+Alt+arrows and Ctrl+Alt+T are usually grabbed by GNOME/KDE.
