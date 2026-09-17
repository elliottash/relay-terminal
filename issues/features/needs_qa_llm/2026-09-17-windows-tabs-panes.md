---
id: 80FZ
type: work
status: needs-qa-llm
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: zf
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop session with a window manager runs the checklist and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` ("allow new tabs and panes", the window, tab and pane hotkey list, "up from the top line scans history (alt not needed)") and the owner''s request of 2026-09-17'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Windows, tabs and panes with Chrome-style shortcuts

## Behavior as implemented

| Shortcut | Action |
|---|---|
| Ctrl+N | New window in the focused pane's directory |
| Alt+Tab / Alt+Shift+Tab | Next / previous Relay window |
| Ctrl+T | New tab in the focused pane's directory |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab, wrapping |
| Ctrl+P / Ctrl+Shift+P | New pane right / below |
| Alt+arrows | Focus the neighboring pane |
| Ctrl+W | Close pane, then tab, then window with a warning dialog |
| Ctrl+Shift+W | Restore the last closed pane, tab or window |

- Each pane has its own shell, composer, agent worker and conversation; the toolbar follows the focused pane.
- Restore recreates layout and directories with new shells; scrollback and running programs are not restored.
- `exit` in a shell closes its pane. Tab titles show the directory and pane count.
- Composer history moved from Alt+Up/Down to Up on the first line and Down on the last line.
- Narrow panes hide the key hints and shorten the path line.

Design notes: `docs/ARCHITECTURE.md`, "Windows, tabs and panes".

## Implementer check (not a QA verdict)

2026-09-17 under Xvfb with xdotool, no window manager. Verified: Ctrl+P, Ctrl+Shift+P,
Alt+Left, Ctrl+T, Ctrl+Tab wrap, Ctrl+Shift+Tab, Ctrl+W on a pane and on a single-pane tab,
Ctrl+Shift+W for both, Ctrl+N, `exit` closing a pane, the last-pane warning dialog, and
split, new-tab and restored-tab shells starting in `/tmp`. Screenshots in
`docs/qa_evidence/2026-09-17-windows-tabs-panes/`.

Not verified: Alt+Tab. Xvfb has no window manager, so window activation could not be
observed. On KDE and GNOME the window manager normally captures Alt+Tab before Relay.

## QA checklist

1. Every shortcut in the table works from the composer and from native mode (F12).
2. After Ctrl+P, Ctrl+Shift+P, Ctrl+T and Ctrl+N, the new shell's `pwd` matches the focused pane's.
3. Alt+arrows reach every pane in a 2×2 layout.
4. Ctrl+W then Ctrl+Shift+W restores a pane in its original position; the same for a tab and a window.
5. Ctrl+W on the last pane shows the warning; Cancel keeps the window.
6. An agent turn in one pane does not affect another pane's agent or shell.
7. Alt+Tab on a real KDE session: record whether Relay receives it.

Independence: the implementer is Claude; QA must be a different model family and record both identities.
