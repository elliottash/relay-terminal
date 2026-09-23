# BGRN composer placement and shortcut

`xvfb-run -a bash docs/qa_evidence/2026-09-23-bgrn-shortcut/drive.sh` started
Relay with a disposable profile and an explicit private socket. `panes.json`
shows the single disposable pane.

`composer.png` shows the Run in background button immediately left of the
**auto** mode picker. The script typed a draft and pressed Ctrl+Alt+Return;
`shortcut.png` shows Relay's **Choose an agent before running in background**
guard while the draft remains in the composer. This proves the key reached the
same action without starting an unconfigured agent.

The local `relay` build passed, and the `keymap` test passed with assertions that
Ctrl+Alt+Enter and Ctrl+Alt+Return map to `pane.runInBackground` in every preset,
while Ctrl+Enter remains `agent.interrupt`.
