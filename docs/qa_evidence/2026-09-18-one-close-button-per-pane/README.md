# Implementer evidence — one close button per pane

Issue: [`issues/changes/needs_qa_llm/2026-09-18-one-close-button-per-pane.md`](../../../issues/changes/needs_qa_llm/2026-09-18-one-close-button-per-pane.md)
Implemented by Claude Opus 5 (Claude Code subagent), 2026-09-18. These are implementer captures,
**not** a QA verdict.

| File | What it shows |
|---|---|
| `implementer-before.png` | HEAD `4bf8fc6`, Ctrl+Shift+A. The pane chrome's row (⬓+ ◫+ ⇱ ×) sits on top of the Settings search box, and the Settings pane's own ✕ is drawn right beside the chrome's ×: two crosses in the corner. |
| `implementer-after.png` | The same steps with the change: one ×, the chrome's, and the search box ends before the chrome row. |
| `implementer-after-closed.png` | After clicking that ×: the Settings pane is gone, the terminal pane has the window again and the prompt box has focus, as with Esc. |

## Reproducing

Both builds were HEAD `4bf8fc6` exported to a scratch directory, with and without the change's patch
(the shared checkout's own `build/` could not link at the time: another session's unfinished
`src/BoardPane.cpp` edit). Run under Xvfb with `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
`XDG_STATE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` all pointed at a fresh directory, a 1400x850
screen, the first-run instructions dialog dismissed with "Not now", then `xdotool key ctrl+shift+a`
and `import -window root`. The close check clicks the × at (1121, 60).

Not captured: the subagent transcript cases (a pane of its own, and the overlay over a narrow pane),
which need a running subagent. See the card for what changed there and why.
