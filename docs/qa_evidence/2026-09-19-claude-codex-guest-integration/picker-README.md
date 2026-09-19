# The picker route: Claude Code from the model box (GT7X, protocol 26.9)

Implementer evidence, 2026-09-19. Pictures are prefixed `implementer-` and are not QA verdicts.
Driver: `picker-drive.py` (run log `picker-drive-run.log`, Relay's own output `picker-run.log`).

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 picker-drive.py .

The run is fully isolated (fresh HOME, XDG dirs, TMPDIR under `/tmp/claude-1000`, `RELAY_KEYRING=off`,
no provider key). The real `claude` and `codex` are kept off the pane's PATH; a **stand-in
`claude`** (a bash script that re-execs itself under the process name `claude`, prints its argv and
the two bridge variables, echoes every line it is given, and exits on `/exit`) stands where the CLI
would. Everything the launch has to get right is therefore visible on the screen and in a file,
without spending a Claude session. The real CLI's flags are exercised separately in
`launch-flags-README.md`.

What the isolated home carries before the run, so the launch has something to keep and something
to clean: a user-written `statusLine` in `~/.claude/settings.json`, and the retired installer's
`--relay-guest` entries in the workspace's `.claude/settings.local.json` beside one entry of the
user's own.

| # | Picture | What it shows | Checked by |
|---|---|---|---|
| 01 | `implementer-picker-01-before.png` | The pane at its prompt, the model box on "No stored keys" (no key in this home) | OCR: no stand-in output yet |
| 02 | `implementer-picker-02-launched.png` | `/model claude` typed the launch line into the pane's own shell: `CLAUDE_CODE_SSE_PORT=<port> ENABLE_IDE_INTEGRATION=true claude --settings <runtime>/guest/claude-settings.json --dangerously-skip-permissions`; the stand-in echoes the same argv and both variables; the model box now reads **Claude Code**; the strip says "claude is waiting for input" | the stand-in's argv/env files; the settings file read back (four hook entries, **no** `statusLine`, because the user has their own); the workspace's local file with the marked entries gone and the user's own permission kept; the user's global statusline untouched; OCR of the terminal and the strip |
| 03 | `implementer-picker-03-terminal-line.png` | `echo relay-terminal-line` submitted with Ctrl+Shift+Enter (terminal mode) arrives in the guest as `!echo relay-terminal-line` | the stand-in's received-lines file |
| 04 | `implementer-picker-04-prompt.png` | `summarise this repository` submitted with Ctrl+Enter (agent mode) arrives as itself | received-lines file, OCR |
| 05 | (in 06) | a typed `!pwd` in auto mode arrives as `!pwd` | received-lines file |
| 06 | `implementer-picker-06-already.png` | `/model claude` again relaunches nothing (the argv file is still the one launch) | argv file unchanged |
| 07 | `implementer-picker-07-left.png` | `/exit` typed through the composer ends the stand-in; the shell is back at its prompt ("Shell ready · exit 0") and the model box is back on "No stored keys" | OCR |

Result of the recorded run: **all checks passed** (`picker-drive-run.log`, last line).

Things the run also shows, worth knowing:

- The sidecar started with this launch: the port in the launch line is a live one from
  `guest_bridge.py serve` (Relay's log in `picker-run.log` has `guest_bridge_ready`). No shell in
  the run carries the two variables; only the launched command line does.
- The launch line is visible in the terminal on purpose (26.9): it is exactly what the user could
  have typed.
- The stand-in reads its lines with `read`, so the `Esc` the composer sends after a `/command`
  (to close the real TUI's popup) is stripped in the stand-in before the comparison.
- "The agent worker exited" is on the banner throughout: the isolated home has no provider and the
  worker leaves. The guest route does not need the worker, which is the point of the "no tier, no
  key" row; with no router the auto mode delivers a line as a prompt, so the `!pwd` step types the
  bang explicitly.

Not covered here and covered elsewhere or not at all:

- The real CLIs and their flags: `launch-flags-README.md` (Claude Code 2.1.278, Codex 0.155.1).
- Codex through the picker: the same `launchGuest` path with `codex -c … --dangerously-bypass-approvals-and-sandbox`;
  the command line is unit-tested (`tests/test_guest_launch.py`) and its flags verified against the
  real CLI, but no stand-in run was made for it.
- A preset picked while the guest runs (the `/exit`-then-switch path) needs a stored key to switch
  to, which this isolated home has none of; the code path is `Pane::leaveGuest` and the refusal
  while busy is a status line.
