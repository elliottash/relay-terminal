# Tier A live: Claude Code as the pane's agent, one real turn (GT7X, protocol 29)

Implementer evidence, 2026-09-19. Pictures are prefixed `implementer-` and are not QA verdicts.
Driver: `harness-drive.py` (run log `harness-drive-run.log`, Relay's own output `harness-run.log`).

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 harness-drive.py .

Unlike every other harness test, this run is **not** a replay: it uses the real `claude`
(2.1.278, logged in) and spends one Claude Code turn, so it keeps the real HOME and isolates only
Relay's XDG directories, TMPDIR and keyring. Per-pane systemd isolation is turned off for the run
(`[isolation] enabled=false`), because a transient scope cannot be started from an isolated
`XDG_RUNTIME_DIR` (no user bus there) and the worker then exits at once — an artefact of the
harness environment, not of the feature; the first attempt of this run showed exactly that, fell
back to the Tier B TUI launch, and stopped on Claude Code's workspace-trust dialog.

| # | Picture | What it shows | Checked by |
|---|---|---|---|
| 01 | `implementer-harness-01-before.png` | The pane at its prompt; with no key in this home the worker configured Relay Free and printed its disclosure line | — |
| 02 | `implementer-harness-02-picked.png` | `/model claude`: the worker reports `guest:claude` as `harness: true`, so the pane sends `set_model {preset: "guest:claude", guest: {permissions: "bypass"}}` and the model box reads **Claude Code**; no TUI is launched (no `--settings` line in the terminal) | OCR |
| 03 | `implementer-harness-03-answered.png` | "Reply with the single word ok." sent with Ctrl+Enter goes through the ordinary `ask`; the harness runs one `claude -p` turn and Relay's own transcript prints the prompt band, the model line (`claude-fable-5-1`, the model Claude Code reported) and the answer `ok`; the context chip reads from the guest's usage | OCR: prompt and answer, no "Relaying" left |
| 04 | `implementer-harness-04-shell.png` | `echo relay-shell-still-mine` with Ctrl+Shift+Enter runs in the pane's own shell: there is no TUI to pipe it into | OCR |

Result of the recorded run: **all checks passed** (`harness-drive-run.log`, last line). One Claude
Code turn spent. The transcript of that turn is filed by claude under
`~/.claude/projects/<slug of the temp workspace>/`, as any headless session's would be.

Not covered here: Codex through the same path (the adapter is replay-tested in
`tests/test_guest_harness_codex.py` against transcripts recorded from the real `codex app-server`;
`harness-codex-README.md` says what those cost), a tool-using turn in a live pane (the recorded
fixtures hold one for each guest), and a guest approval reaching the question card (bypass is the
default posture, so none is raised).
