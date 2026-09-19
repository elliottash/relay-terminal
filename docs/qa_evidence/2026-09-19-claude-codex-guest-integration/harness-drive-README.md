# Tier A live: Claude Code and Codex as the pane's agent, one real turn each (GT7X, protocol 29)

Implementer evidence, 2026-09-19. Pictures are prefixed `implementer-` and are not QA verdicts.
Driver: `harness-drive.py` (run log `harness-drive-run.log`, Relay's own output `harness-run.log`).

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 harness-drive.py . [claude|codex]

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

## Codex, the same route (2026-09-19)

Run against a clean export of `main` at 6e4e49e, the same four steps, pictures prefixed
`implementer-harness-codex-`; Relay's own output is `harness-run-codex.log`. **All checks passed**,
one Codex turn spent on the owner's ChatGPT plan.

| # | What it shows |
|---|---|
| 01 | The pane at its prompt |
| 02 | `/model codex` puts the box on **Codex** with no TUI in the terminal: no `-c notify=` launch line, because this is the app-server route (§29.2), not the Tier B launch |
| 03 | The prompt answered in Relay's own transcript, under the model line `gpt-5.6-sol` that Codex reported; the context chip reads "89% left" from the guest's own token usage |
| 04 | A terminal-mode line still runs in the pane's own shell |

This is the run that had never been made: until now Codex's half of Tier A was covered only by
transcripts replayed through a fake process. The two guests take the same code path, and it
behaves the same on both. Codex files its own rollout under `~/.codex/sessions/YYYY/MM/DD/`, which
is where the Sessions list reads it from.

Not covered here: a tool-using turn in a live pane (the recorded fixtures hold one for each
guest), and a guest approval reaching the question card (bypass is the default posture, so none is
raised).
